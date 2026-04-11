#pragma once

#include "../../include/IO.h"

struct WbIn {
	ExeWbIO *exe2wb = nullptr;
	LsuWbIO *lsu2wb = nullptr;
	RobBroadcastIO *rob_bcast = nullptr;
};

struct WbOut {
	WbRobIO *wb2rob = nullptr;
};

class WriteBack {
public:
	void comb_begin();
	void comb_writeback();
	void seq();

	WbIn in;
	WbOut out;
};
