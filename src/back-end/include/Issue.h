#pragma once

#include "../../include/IO.h"

struct IssIn {
	DisIssIO *dis2iss = nullptr;
	RobBroadcastIO *rob_bcast = nullptr;
};

struct IssOut {
	IssDisIO *iss2dis = nullptr;
	IssExeIO *iss2exe = nullptr;
	IssLsuIO *iss2lsu = nullptr;
};

class Issue {
public:
	void comb_begin();
	void comb_ready();
	void comb_issue();
	void seq();

	IssIn in;
	IssOut out;
};

