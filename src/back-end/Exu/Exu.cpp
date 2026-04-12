#include "include/Exu.h"
#include <cstdint>

namespace {

uint64_t alu_compute(const ExecUop &ex, uint64_t s0, uint64_t s1) {
	const auto op = static_cast<RiscvSubOp>(static_cast<uint16_t>(ex.front.sub_op));
	const uint64_t imm = static_cast<uint64_t>(ex.front.imm);
	switch (op) {
	case RiscvSubOp::LUI: return imm;
	case RiscvSubOp::AUIPC: return static_cast<uint64_t>(ex.front.pc) + imm;
	case RiscvSubOp::JAL:
	case RiscvSubOp::JALR: return static_cast<uint64_t>(ex.front.pc) + 4ull;
	case RiscvSubOp::ADDI: return s0 + imm;
	case RiscvSubOp::SLTI:
		return (static_cast<int64_t>(s0) < static_cast<int64_t>(imm)) ? 1ull : 0ull;
	case RiscvSubOp::SLTIU: return (s0 < imm) ? 1ull : 0ull;
	case RiscvSubOp::XORI: return s0 ^ imm;
	case RiscvSubOp::ORI: return s0 | imm;
	case RiscvSubOp::ANDI: return s0 & imm;
	case RiscvSubOp::SLLI: return s0 << (imm & 0x3Fu);
	case RiscvSubOp::SRLI: return s0 >> (imm & 0x3Fu);
	case RiscvSubOp::SRAI:
		return static_cast<uint64_t>(static_cast<int64_t>(s0) >> (imm & 0x3Fu));
	case RiscvSubOp::ADD: return s0 + s1;
	case RiscvSubOp::SUB: return s0 - s1;
	case RiscvSubOp::SLL: return s0 << (s1 & 0x3Fu);
	case RiscvSubOp::SLT:
		return (static_cast<int64_t>(s0) < static_cast<int64_t>(s1)) ? 1ull : 0ull;
	case RiscvSubOp::SLTU: return (s0 < s1) ? 1ull : 0ull;
	case RiscvSubOp::XOR: return s0 ^ s1;
	case RiscvSubOp::SRL: return s0 >> (s1 & 0x3Fu);
	case RiscvSubOp::SRA:
		return static_cast<uint64_t>(static_cast<int64_t>(s0) >> (s1 & 0x3Fu));
	case RiscvSubOp::OR: return s0 | s1;
	case RiscvSubOp::AND: return s0 & s1;
	default: return 0;
	}
}

} // namespace

void Exu::comb_begin() {
	if (out.exe2wb != nullptr) {
		*(out.exe2wb) = {};
	}
	for (int d = 0; d < kPipeDepth; ++d) {
		pipe_n_[d] = pipe_q_[d];
	}
}

void Exu::comb_exec() {
	if (in.iss2exe == nullptr || out.exe2wb == nullptr || in.rob_bcast == nullptr) {
		return;
	}

	if (in.rob_bcast->flush) {
		for (int d = 0; d < kPipeDepth; ++d) {
			pipe_n_[d] = {};
		}
		return;
	}

	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		out.exe2wb->valid[i] = 0;
		out.exe2wb->wb[i] = {};

		if (!pipe_q_[kPipeDepth - 1].valid[i]) {
			continue;
		}

		ExecUop ex = pipe_q_[kPipeDepth - 1].req[i];
		const uint64_t s0 = (prf_ != nullptr) ? prf_->read(ex.src_preg[0]) : 0;
		const uint64_t s1 = (prf_ != nullptr) ? prf_->read(ex.src_preg[1]) : 0;

		if (static_cast<uint8_t>(ex.front.op_class) ==
			static_cast<uint8_t>(OpClass::CSR)) {
			const auto op = static_cast<RiscvSubOp>(static_cast<uint16_t>(ex.front.sub_op));
			const uint16_t csr_addr =
				static_cast<uint16_t>(static_cast<uint64_t>(ex.front.imm) & 0xFFFu);
			const uint64_t csr_old = (csr_ != nullptr) ? csr_->read(csr_addr) : 0;
			uint64_t csr_w = csr_old;

			switch (op) {
			case RiscvSubOp::ECALL:
				ex.front.except_valid = 1;
				ex.front.except_code = 11;
				break;
			case RiscvSubOp::EBREAK:
				ex.front.except_valid = 1;
				ex.front.except_code = 3;
				break;
			case RiscvSubOp::CSRRW: csr_w = s0; break;
			case RiscvSubOp::CSRRS: csr_w = csr_old | s0; break;
			case RiscvSubOp::CSRRC: csr_w = csr_old & (~s0); break;
			case RiscvSubOp::CSRRWI: {
				const uint64_t zimm = static_cast<uint64_t>(ex.front.src_areg[0]) & 0x1Fu;
				csr_w = zimm;
				break;
			}
			case RiscvSubOp::CSRRSI: {
				const uint64_t zimm = static_cast<uint64_t>(ex.front.src_areg[0]) & 0x1Fu;
				csr_w = csr_old | zimm;
				break;
			}
			case RiscvSubOp::CSRRCI: {
				const uint64_t zimm = static_cast<uint64_t>(ex.front.src_areg[0]) & 0x1Fu;
				csr_w = csr_old & (~zimm);
				break;
			}
			default:
				break;
			}

			if (csr_ != nullptr &&
				(op == RiscvSubOp::CSRRW || op == RiscvSubOp::CSRRS ||
				 op == RiscvSubOp::CSRRC || op == RiscvSubOp::CSRRWI ||
				 op == RiscvSubOp::CSRRSI || op == RiscvSubOp::CSRRCI)) {
				csr_->write(csr_addr, csr_w);
			}
			ex.result_value = csr_old;
		} else {
			ex.result_value = alu_compute(ex, s0, s1);
		}

		ex.cplt_mask |= static_cast<wire<8>>(0x1);
		out.exe2wb->valid[i] = 1;
		out.exe2wb->wb[i] = ex;
	}

	pipe_n_[0] = {};
	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		pipe_n_[0].valid[i] = in.iss2exe->valid[i];
		pipe_n_[0].req[i] = in.iss2exe->req[i];
	}
}

void Exu::seq() {
	for (int d = 0; d < kPipeDepth; ++d) {
		pipe_q_[d] = pipe_n_[d];
	}
}
