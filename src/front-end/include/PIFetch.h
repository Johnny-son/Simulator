#pragma once

#include "../../include/IO.h"

struct FrontTopIn;

class PIFetch {
public:
	void comb_begin();
	void comb(const FrontTopIn &in, const DecFrontIO &dec2front,
			  const RobBroadcastIO &rob_bcast, PIFetchIFetchIO &pi2if);
	void seq();

private:
	wire<64> pc_q_ = 0;
	wire<64> pc_n_ = 0;
	wire<1> started_q_ = 0;
	wire<1> started_n_ = 0;
};
