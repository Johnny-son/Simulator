#include "include/FrontTop.h"

void PIFetch::comb(const FrontTopIn &in, const RobBroadcastIO &rob_bcast,
				   PIFetchIFetchIO &pi2if) const {
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		pi2if.valid[i] = 0;
		pi2if.inst[i] = 0;
		pi2if.pc[i] = 0;
		pi2if.predict_dir[i] = 0;

		if (rob_bcast.flush) {
			continue;
		}

		pi2if.valid[i] = in.valid[i];
		pi2if.inst[i] = in.inst[i];
		pi2if.pc[i] = in.pc[i];
		pi2if.predict_dir[i] = in.predict_dir[i];
	}
}
