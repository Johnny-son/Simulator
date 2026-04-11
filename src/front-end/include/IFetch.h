#pragma once

#include "../../include/IO.h"

class IFetch {
public:
	void comb(const PIFetchIFetchIO &pi2if, const DecFrontIO &dec2front,
			  const RobBroadcastIO &rob_bcast, FrontDecIO &front2dec) const;
};

