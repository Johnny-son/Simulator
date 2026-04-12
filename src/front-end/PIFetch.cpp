#include "include/FrontTop.h"

void PIFetch::comb_begin() {
	pc_n_ = pc_q_;
	started_n_ = started_q_;
}

void PIFetch::comb(const FrontTopIn &in, const DecFrontIO &dec2front,
				   const RobBroadcastIO &rob_bcast, PIFetchIFetchIO &pi2if) {
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		pi2if.valid[i] = 0;
		pi2if.inst[i] = 0;
		pi2if.pc[i] = 0;
		pi2if.predict_dir[i] = 0;
	}

	if (!started_q_ && static_cast<bool>(in.valid[0])) {
		started_n_ = 1;
		pc_n_ = in.pc[0];
	}

	if (rob_bcast.flush) {
		pc_n_ = rob_bcast.redirect_pc;
	}

	if (!started_n_ || rob_bcast.flush) {
		return;
	}

	const uint64_t base =
		static_cast<uint64_t>(started_q_ ? pc_q_ : pc_n_);
	bool has_valid_lane = false;
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		const uint64_t lane_pc = base + static_cast<uint64_t>(4 * i);
		if (static_cast<bool>(in.limit_en) && lane_pc >= static_cast<uint64_t>(in.limit_pc)) {
			pi2if.valid[i] = 0;
			pi2if.pc[i] = lane_pc;
			continue;
		}
		pi2if.valid[i] = 1;
		pi2if.pc[i] = lane_pc;
		pi2if.predict_dir[i] = 0;
		has_valid_lane = true;
	}

	if (has_valid_lane && static_cast<bool>(dec2front.ready)) {
		pc_n_ = base + static_cast<uint64_t>(4 * FETCH_WIDTH);
	}
}

void PIFetch::seq() {
	pc_q_ = pc_n_;
	started_q_ = started_n_;
}
