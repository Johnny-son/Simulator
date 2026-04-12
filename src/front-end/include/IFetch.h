#pragma once

#include "../../include/IO.h"
#include "../../MemSubSystem/include/cache.h"
#include <deque>

class IFetch {
public:
	void comb_begin();
	void comb(const PIFetchIFetchIO &pi2if, const DecFrontIO &dec2front,
			  const RobBroadcastIO &rob_bcast, FrontDecIO &front2dec);
	void seq();
	void bind_icache(memsys::ICacheSimple *icache) { icache_ = icache; }

private:
	void clear_bundle_n();

	memsys::ICacheSimple *icache_ = nullptr;

	struct PendingBundle {
		uint64_t base = 0;
		uint8_t mask = 0;
	};
	std::deque<PendingBundle> queue_;

	bool bundle_active_q_ = false;
	bool bundle_active_n_ = false;
	uint64_t bundle_base_q_ = 0;
	uint64_t bundle_base_n_ = 0;
	uint8_t bundle_mask_q_ = 0;
	uint8_t bundle_mask_n_ = 0;

	bool lane_need_q_[FETCH_WIDTH] = {};
	bool lane_need_n_[FETCH_WIDTH] = {};
	bool lane_req_q_[FETCH_WIDTH] = {};
	bool lane_req_n_[FETCH_WIDTH] = {};
	bool lane_done_q_[FETCH_WIDTH] = {};
	bool lane_done_n_[FETCH_WIDTH] = {};
	uint32_t lane_inst_q_[FETCH_WIDTH] = {};
	uint32_t lane_inst_n_[FETCH_WIDTH] = {};
};
