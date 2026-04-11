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

	FrontTopIn() { std::memset(this, 0, sizeof(FrontTopIn)); }
};

struct FrontTopOut {
	DecRenIO dec2ren;
	DecFrontIO dec2front;

	FrontTopOut() { std::memset(this, 0, sizeof(FrontTopOut)); }
};

class FrontTop {
public:
	FrontTopIn in;
	FrontTopOut out;
	RenDecIO ren2dec;
	RobBroadcastIO rob_bcast;

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
	FrontDecIO ifetch_to_predecode_;
	FrontDecIO predecode_to_decode_;
	DecEnqIO decode_to_enq_;
};

