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

bool Rob::is_empty() const {
	return (enq_ptr_.idx == deq_ptr_.idx) && (enq_ptr_.epoch == deq_ptr_.epoch);
}

bool Rob::is_full() const {
	return (enq_ptr_.idx == deq_ptr_.idx) && (enq_ptr_.epoch != deq_ptr_.epoch);
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
	if (out.rob_bcast != nullptr) {
		*(out.rob_bcast) = {};
	}
}

void Rob::comb_complete() {
	if (in.wb2rob == nullptr) {
		return;
	}

	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		if (!in.wb2rob->valid[i] || is_full()) {
			continue;
		}

		auto &slot = entry_n_[static_cast<uint32_t>(enq_ptr_n_.idx)];
		slot.valid = 1;
		slot.uop = in.wb2rob->wb[i];
		advance_ptr(enq_ptr_n_);
	}
}

void Rob::comb_commit() {
	if (out.rob_commit == nullptr || is_empty()) {
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
	ce.except_valid = slot.uop.front.except_valid;
	ce.except_code = slot.uop.front.except_code;
	ce.is_store = slot.uop.front.is_store;
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

