#pragma once

#include "../../include/IO.h"

struct FrontTopIn;

class PIFetch {
public:
	void comb(const FrontTopIn &in, const RobBroadcastIO &rob_bcast,
			  PIFetchIFetchIO &pi2if) const;
};
