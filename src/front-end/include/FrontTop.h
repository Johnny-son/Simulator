#pragma once

#include "Decode.h"
#include "FrontEnqueue.h"
#include "IFetch.h"
#include "PIFetch.h"
#include "Predecode.h"

struct FrontTopIn {
	wire<32> inst[FETCH_WIDTH];
	wire<64> pc[FETCH_WIDTH];
	wire<1> valid[FETCH_WIDTH];
	wire<1> predict_dir[FETCH_WIDTH];
	wire<1> limit_en;
	wire<64> limit_pc;

	FrontTopIn() { std::memset(this, 0, sizeof(FrontTopIn)); }
};

struct FrontTopOut {
	EnqRenIO enq2buf;

	FrontTopOut() { std::memset(this, 0, sizeof(FrontTopOut)); }
};

class FrontTop {
public:
	FrontTopIn in;
	FrontTopOut out;
	EnqDecIO enq2dec;
	RobBroadcastIO rob_bcast;

	void bind_icache(memsys::ICacheSimple *icache) { ifetch_.bind_icache(icache); }
	void comb_begin();
	void comb();
	void seq();

private:
	PIFetch pifetch_;
	IFetch ifetch_;
	Predecode predecode_;
	Decode decode_;
	FrontEnqueue front_enqueue_;

	PIFetchIFetchIO pifetch_to_ifetch_;
	DecFrontIO dec2front_;
	FrontDecIO ifetch_to_predecode_;
	FrontDecIO predecode_to_decode_;
	DecEnqIO decode_to_enq_;
};
