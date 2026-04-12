#include "include/Lsu.h"
#include <cstdint>

namespace {

uint64_t signext8(uint8_t v) {
	return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(v)));
}
uint64_t signext16(uint16_t v) {
	return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(v)));
}

} // namespace

void Lsu::comb_begin() {
	if (out.lsu2wb != nullptr) {
		*(out.lsu2wb) = {};
	}
	for (int d = 0; d < kPipeDepth; ++d) {
		pipe_n_[d] = pipe_q_[d];
	}
	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		tail_req_sent_n_[i] = tail_req_sent_q_[i];
	}
}

void Lsu::comb_exec() {
	if (in.iss2lsu == nullptr || out.lsu2wb == nullptr || in.rob_bcast == nullptr) {
		return;
	}

	if (in.rob_bcast->flush) {
		for (int d = 0; d < kPipeDepth; ++d) {
			pipe_n_[d] = {};
		}
		for (int i = 0; i < ISSUE_WIDTH; ++i) {
			tail_req_sent_n_[i] = false;
			ingress_q_[i].clear();
		}
		return;
	}

	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		out.lsu2wb->valid[i] = 0;
		out.lsu2wb->wb[i] = {};
		if (in.iss2lsu->valid[i]) {
			if (ingress_q_[i].size() < kIngressDepth) {
				ingress_q_[i].push_back(in.iss2lsu->req[i]);
			}
		}
	}

	const int tail = kPipeDepth - 1;
	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		if (!pipe_q_[tail].valid[i]) {
			continue;
		}

		ExecUop mem = pipe_q_[tail].req[i];
		const uint64_t base = (prf_ != nullptr) ? prf_->read(mem.src_preg[0]) : 0;
		const uint64_t addr = base + static_cast<uint64_t>(mem.front.imm);
		mem.mem_addr = addr;

		bool done = false;
		if (mem.front.is_load) {
			if (!tail_req_sent_q_[i] && dcache_ != nullptr) {
				if (dcache_->request_load32(addr, static_cast<uint8_t>(i))) {
					tail_req_sent_n_[i] = true;
				}
			}

			uint32_t data = 0;
			if (dcache_ != nullptr &&
				dcache_->poll_load32(static_cast<uint8_t>(i), data)) {
				switch (static_cast<RiscvSubOp>(
					static_cast<uint16_t>(mem.front.sub_op))) {
				case RiscvSubOp::LB:
					mem.result_value = signext8(static_cast<uint8_t>(data & 0xFFu));
					break;
				case RiscvSubOp::LBU:
					mem.result_value = static_cast<uint64_t>(data & 0xFFu);
					break;
				case RiscvSubOp::LH:
					mem.result_value =
						signext16(static_cast<uint16_t>(data & 0xFFFFu));
					break;
				case RiscvSubOp::LHU:
					mem.result_value = static_cast<uint64_t>(data & 0xFFFFu);
					break;
				case RiscvSubOp::LW:
				default:
					mem.result_value = static_cast<uint64_t>(
						static_cast<int64_t>(static_cast<int32_t>(data)));
					break;
				}
				done = true;
			}
		} else if (mem.front.is_store) {
			const uint64_t src =
				(prf_ != nullptr) ? prf_->read(mem.src_preg[1]) : 0;
			uint32_t data = 0;
			uint8_t mask = 0xFu;
			switch (static_cast<RiscvSubOp>(
				static_cast<uint16_t>(mem.front.sub_op))) {
			case RiscvSubOp::SB:
				data = static_cast<uint32_t>(src & 0xFFu);
				mask = 0x1u;
				break;
			case RiscvSubOp::SH:
				data = static_cast<uint32_t>(src & 0xFFFFu);
				mask = 0x3u;
				break;
			case RiscvSubOp::SW:
			default:
				data = static_cast<uint32_t>(src & 0xFFFFFFFFu);
				mask = 0xFu;
				break;
			}
			mem.mem_wdata = static_cast<uint64_t>(data);
			mem.mem_wmask = mask;

			if (!tail_req_sent_q_[i] && dcache_ != nullptr) {
				if (dcache_->request_store32(addr, data, mask,
								 static_cast<uint8_t>(i))) {
					tail_req_sent_n_[i] = true;
				}
			}
			if (dcache_ != nullptr &&
				dcache_->poll_store32(static_cast<uint8_t>(i))) {
				done = true;
			}
		} else {
			done = true;
		}

		if (done) {
			mem.cplt_mask |= static_cast<wire<8>>(0x2);
			out.lsu2wb->valid[i] = 1;
			out.lsu2wb->wb[i] = mem;
			pipe_n_[tail].valid[i] = 0;
			pipe_n_[tail].req[i] = {};
			tail_req_sent_n_[i] = false;
		} else {
			pipe_n_[tail].valid[i] = 1;
			pipe_n_[tail].req[i] = mem;
		}
	}

	for (int d = kPipeDepth - 1; d > 0; --d) {
		for (int i = 0; i < ISSUE_WIDTH; ++i) {
			if (pipe_n_[d].valid[i]) {
				continue;
			}
			if (!pipe_q_[d - 1].valid[i]) {
				continue;
			}
			pipe_n_[d].valid[i] = 1;
			pipe_n_[d].req[i] = pipe_q_[d - 1].req[i];
			pipe_n_[d - 1].valid[i] = 0;
			pipe_n_[d - 1].req[i] = {};
			if (d == tail) {
				tail_req_sent_n_[i] = false;
			}
		}
	}

	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		if (pipe_n_[0].valid[i]) {
			continue;
		}
		if (ingress_q_[i].empty()) {
			continue;
		}
		pipe_n_[0].valid[i] = 1;
		pipe_n_[0].req[i] = ingress_q_[i].front();
		ingress_q_[i].pop_front();
	}
}

void Lsu::seq() {
	for (int d = 0; d < kPipeDepth; ++d) {
		pipe_q_[d] = pipe_n_[d];
	}
	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		tail_req_sent_q_[i] = tail_req_sent_n_[i];
	}
}
