#pragma once

#include "../../include/IO.h"
#include "Prf.h"

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
	void bind_prf(Prf *prf) { prf_ = prf; }

	WbIn in;
	WbOut out;

private:
	Prf *prf_ = nullptr;
};
