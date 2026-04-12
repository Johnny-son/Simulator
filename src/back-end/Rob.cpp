#include "include/Rob.h"

void Rob::init() {
	std::memset(entry_, 0, sizeof(entry_));
	std::memset(entry_n_, 0, sizeof(entry_n_));
	enq_ptr_ = {};
	deq_ptr_ = {};
	enq_ptr_n_ = {};
	deq_ptr_n_ = {};
	rob_bcast_n_ = {};
}

bool Rob::is_empty(const RingPtr<ROB_SIZE> &enq, const RingPtr<ROB_SIZE> &deq) {
	return (enq.idx == deq.idx) && (enq.epoch == deq.epoch);
}

bool Rob::is_full(const RingPtr<ROB_SIZE> &enq, const RingPtr<ROB_SIZE> &deq) {
	return (enq.idx == deq.idx) && (enq.epoch != deq.epoch);
}

uint32_t Rob::free_slots(const RingPtr<ROB_SIZE> &enq,
				 const RingPtr<ROB_SIZE> &deq) {
	const uint32_t enq_idx = static_cast<uint32_t>(enq.idx);
	const uint32_t deq_idx = static_cast<uint32_t>(deq.idx);
	const uint32_t enq_ext = enq_idx + (static_cast<uint32_t>(enq.epoch) * ROB_SIZE);
	const uint32_t deq_ext = deq_idx + (static_cast<uint32_t>(deq.epoch) * ROB_SIZE);

	int32_t used = static_cast<int32_t>(enq_ext) - static_cast<int32_t>(deq_ext);
	if (used < 0) {
		used += static_cast<int32_t>(2 * ROB_SIZE);
	}
	if (used > ROB_SIZE) {
		used = ROB_SIZE;
	}
	return ROB_SIZE - static_cast<uint32_t>(used);
}

void Rob::advance_ptr(RingPtr<ROB_SIZE> &ptr) {
	const uint32_t next = (static_cast<uint32_t>(ptr.idx) + 1u) % ROB_SIZE;
	if (next == 0u) {
		ptr.epoch = !static_cast<bool>(ptr.epoch);
	}
	ptr.idx = next;
}

void Rob::comb_begin() {
	std::memcpy(entry_n_, entry_, sizeof(entry_));
	enq_ptr_n_ = enq_ptr_;
	deq_ptr_n_ = deq_ptr_;
	rob_bcast_n_ = {};

	if (out.rob_commit != nullptr) {
		*(out.rob_commit) = {};
	}
	if (out.rob2dis != nullptr) {
		*(out.rob2dis) = {};
	}
	if (out.rob_bcast != nullptr) {
		*(out.rob_bcast) = {};
	}
}

void Rob::comb_ready() {
	if (out.rob2dis == nullptr) {
		return;
	}

	const uint32_t slots = free_slots(enq_ptr_n_, deq_ptr_n_);
	out.rob2dis->ready_num = (slots > 0xFFu) ? static_cast<wire<8>>(0xFFu)
					   : static_cast<wire<8>>(slots);
}

void Rob::comb_alloc() {
	if (in.dis2rob == nullptr) {
		return;
	}

	for (int i = 0; i < RENAME_WIDTH; ++i) {
		if (!in.dis2rob->valid[i] || is_full(enq_ptr_n_, deq_ptr_n_)) {
			continue;
		}

		auto &slot = entry_n_[static_cast<uint32_t>(enq_ptr_n_.idx)];
		slot.valid = 1;
		slot.uop = in.dis2rob->req[i];
		slot.uop.rob_idx = enq_ptr_n_.idx;
		slot.uop.rob_epoch = enq_ptr_n_.epoch;
		slot.uop.cplt_mask = 0;
		advance_ptr(enq_ptr_n_);
	}
}

void Rob::comb_complete() {
	if (in.wb2rob == nullptr) {
		return;
	}

	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		if (!in.wb2rob->valid[i]) {
			continue;
		}

		const ExecUop &wb_uop = in.wb2rob->wb[i];
		for (int j = 0; j < ROB_SIZE; ++j) {
			auto &slot = entry_n_[j];
			if (!slot.valid || slot.uop.front.seq_id != wb_uop.front.seq_id) {
				continue;
			}

			slot.uop.cplt_mask |= wb_uop.cplt_mask;
			if (wb_uop.front.except_valid) {
				slot.uop.front.except_valid = 1;
				slot.uop.front.except_code = wb_uop.front.except_code;
			}
			slot.uop.replay =
				static_cast<bool>(slot.uop.replay) ||
				static_cast<bool>(wb_uop.replay);
			break;
		}
	}
}

void Rob::comb_commit() {
	if (out.rob_commit == nullptr || is_empty(enq_ptr_n_, deq_ptr_n_)) {
		return;
	}

	auto &slot = entry_n_[static_cast<uint32_t>(deq_ptr_n_.idx)];
	if (!slot.valid) {
		return;
	}

	const wire<8> expected = slot.uop.expect_mask;
	const wire<8> done = slot.uop.cplt_mask;
	if ((done & expected) != expected) {
		return;
	}

	CommitEntry ce;
	ce.valid = 1;
	ce.seq_id = slot.uop.front.seq_id;
	ce.pc = slot.uop.front.pc;
	ce.dst_en = slot.uop.front.dst_en;
	ce.dst_areg = slot.uop.front.dst_areg;
	ce.dst_value = slot.uop.result_value;
	ce.except_valid = slot.uop.front.except_valid;
	ce.except_code = slot.uop.front.except_code;
	ce.is_store = slot.uop.front.is_store;
	ce.store_addr = slot.uop.mem_addr;
	ce.store_data = slot.uop.mem_wdata;
	ce.store_mask = slot.uop.mem_wmask;
	ce.mispred = 0;
	ce.flush_pipe = slot.uop.front.except_valid;
	ce.dbg = slot.uop.front.dbg;
	ce.tma = slot.uop.front.tma;

	out.rob_commit->commit_entry[0] = ce;

	slot.valid = 0;
	advance_ptr(deq_ptr_n_);
}

void Rob::comb_flush() {
	if (in.commit_bcast == nullptr) {
		return;
	}

	if (in.commit_bcast->flush) {
		std::memset(entry_n_, 0, sizeof(entry_n_));
		enq_ptr_n_ = {};
		deq_ptr_n_ = {};
		rob_bcast_n_ = *(in.commit_bcast);
	}

	if (out.rob_bcast != nullptr) {
		*(out.rob_bcast) = rob_bcast_n_;
	}
}

void Rob::seq() {
	std::memcpy(entry_, entry_n_, sizeof(entry_));
	enq_ptr_ = enq_ptr_n_;
	deq_ptr_ = deq_ptr_n_;
}
