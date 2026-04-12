#pragma once

#include "../../include/IO.h"
#include "UopBuffer.h"

struct RenIn {
	EnqRenIO *enq2buf = nullptr;
	DisRenIO *dis2ren = nullptr;
	RobBroadcastIO *rob_bcast = nullptr;
};

struct RenOut {
	EnqDecIO *enq2dec = nullptr;
	RenDecIO *ren2dec = nullptr;
	RenDisIO *ren2dis = nullptr;
};

class Rename {
public:
	void init();
	void comb_begin();
	void comb_alloc();
	void comb_rename();
	void seq();

	RenIn in;
	RenOut out;

private:
	reg<PRF_IDX_WIDTH> arch_rat_[AREG_NUM];
	DecRenIO buf2ren_;
	UopBuffer uop_buffer_;
};
