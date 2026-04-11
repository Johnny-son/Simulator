#pragma once

#include "../../../include/IO.h"

struct ExuIn {
	IssExeIO *iss2exe = nullptr;
	RobBroadcastIO *rob_bcast = nullptr;
};

struct ExuOut {
	ExeWbIO *exe2wb = nullptr;
};

class Exu {
public:
	void comb_begin();
	void comb_exec();
	void seq();

	ExuIn in;
	ExuOut out;
};

