#pragma once

#include "../../../include/IO.h"

struct LsuIn {
	IssLsuIO *iss2lsu = nullptr;
	RobBroadcastIO *rob_bcast = nullptr;
};

struct LsuOut {
	LsuWbIO *lsu2wb = nullptr;
};

class Lsu {
public:
	void comb_begin();
	void comb_exec();
	void seq();

	LsuIn in;
	LsuOut out;
};

