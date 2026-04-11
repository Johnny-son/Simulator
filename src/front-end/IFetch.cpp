#include "include/FrontTop.h"

void IFetch::comb(const PIFetchIFetchIO &pi2if, const DecFrontIO &dec2front,
				 const RobBroadcastIO &rob_bcast,
				 FrontDecIO &front2dec) const {
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		front2dec.valid[i] = 0;
		front2dec.inst[i] = 0;
		front2dec.pc[i] = 0;
		front2dec.predict_dir[i] = 0;

		if (!dec2front.ready || rob_bcast.flush) {
			continue;
		}

		front2dec.valid[i] = pi2if.valid[i];
		front2dec.inst[i] = pi2if.inst[i];
		front2dec.pc[i] = pi2if.pc[i];
		front2dec.predict_dir[i] = pi2if.predict_dir[i];
	}
}

