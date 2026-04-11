#pragma once

#include "../../include/IO.h"

struct DisIn {
	RenDisIO *ren2dis = nullptr;
	IssDisIO *iss2dis = nullptr;
	RobBroadcastIO *rob_bcast = nullptr;
};

struct DisOut {
	DisRenIO *dis2ren = nullptr;
	DisIssIO *dis2iss = nullptr;
};

class Dispatch {
public:
	void comb_begin();
	void comb_ready();
	void comb_dispatch();
	void seq();

	DisIn in;
	DisOut out;
};

