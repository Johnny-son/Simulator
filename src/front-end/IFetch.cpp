#include "include/FrontTop.h"

void IFetch::clear_bundle_n() {
	bundle_active_n_ = false;
	bundle_base_n_ = 0;
	bundle_mask_n_ = 0;
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		lane_need_n_[i] = false;
		lane_req_n_[i] = false;
		lane_done_n_[i] = false;
		lane_inst_n_[i] = 0;
	}
}

void IFetch::comb_begin() {
	bundle_active_n_ = bundle_active_q_;
	bundle_base_n_ = bundle_base_q_;
	bundle_mask_n_ = bundle_mask_q_;
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		lane_need_n_[i] = lane_need_q_[i];
		lane_req_n_[i] = lane_req_q_[i];
		lane_done_n_[i] = lane_done_q_[i];
		lane_inst_n_[i] = lane_inst_q_[i];
	}
}

void IFetch::comb(const PIFetchIFetchIO &pi2if, const DecFrontIO &dec2front,
				  const RobBroadcastIO &rob_bcast,
				  FrontDecIO &front2dec) {
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		front2dec.valid[i] = 0;
		front2dec.inst[i] = 0;
		front2dec.pc[i] = 0;
		front2dec.predict_dir[i] = 0;
	}

	if (rob_bcast.flush) {
		queue_.clear();
		clear_bundle_n();
		return;
	}

	bool any_valid = false;
	int first_valid = -1;
	uint8_t mask = 0;
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		if (!pi2if.valid[i]) {
			continue;
		}
		any_valid = true;
		if (first_valid < 0) {
			first_valid = i;
		}
		mask = static_cast<uint8_t>(mask | (1u << i));
	}

	if (any_valid) {
		const uint64_t base =
			static_cast<uint64_t>(pi2if.pc[first_valid]) -
			static_cast<uint64_t>(first_valid * 4ull);
		const bool is_active_same = bundle_active_q_ && (bundle_base_q_ == base);
		const bool queued_same = !queue_.empty() && queue_.back().base == base;
		if (!is_active_same && !queued_same) {
			queue_.push_back(PendingBundle{base, mask});
		}
	}

	if (!bundle_active_n_ && !queue_.empty()) {
		const PendingBundle next = queue_.front();
		queue_.pop_front();
		bundle_active_n_ = true;
		bundle_base_n_ = next.base;
		bundle_mask_n_ = next.mask;
		for (int i = 0; i < FETCH_WIDTH; ++i) {
			const bool need = ((next.mask >> i) & 0x1u) != 0u;
			lane_need_n_[i] = need;
			lane_req_n_[i] = false;
			lane_done_n_[i] = false;
			lane_inst_n_[i] = 0;
		}
	}

	if (!bundle_active_n_) {
		return;
	}

	for (int i = 0; i < FETCH_WIDTH; ++i) {
		if (!lane_need_n_[i] || lane_done_n_[i]) {
			continue;
		}
		const uint64_t lane_pc = bundle_base_n_ + static_cast<uint64_t>(i * 4ull);
		if (!lane_req_n_[i]) {
			if (icache_ != nullptr &&
				icache_->request_fetch32(lane_pc, static_cast<uint8_t>(i))) {
				lane_req_n_[i] = true;
			}
		}
		uint32_t inst = 0;
		if (icache_ != nullptr &&
			icache_->poll_fetch32(static_cast<uint8_t>(i), inst)) {
			lane_done_n_[i] = true;
			lane_inst_n_[i] = inst;
		}
	}

	bool all_done = true;
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		if (lane_need_n_[i] && !lane_done_n_[i]) {
			all_done = false;
			break;
		}
	}

	if (!all_done) {
		return;
	}

	for (int i = 0; i < FETCH_WIDTH; ++i) {
		if (!lane_need_n_[i]) {
			continue;
		}
		front2dec.valid[i] = 1;
		front2dec.inst[i] = lane_inst_n_[i];
		front2dec.pc[i] = bundle_base_n_ + static_cast<uint64_t>(i * 4ull);
		front2dec.predict_dir[i] = 0;
	}

	if (dec2front.ready) {
		clear_bundle_n();
	}
}

void IFetch::seq() {
	bundle_active_q_ = bundle_active_n_;
	bundle_base_q_ = bundle_base_n_;
	bundle_mask_q_ = bundle_mask_n_;
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		lane_need_q_[i] = lane_need_n_[i];
		lane_req_q_[i] = lane_req_n_[i];
		lane_done_q_[i] = lane_done_n_[i];
		lane_inst_q_[i] = lane_inst_n_[i];
	}
}
