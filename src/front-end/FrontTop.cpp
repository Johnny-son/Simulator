#include "include/FrontTop.h"

void FrontTop::comb_begin() {
	out = {};
	dec2front_ = {};
	pifetch_to_ifetch_ = {};
	ifetch_to_predecode_ = {};
	predecode_to_decode_ = {};
	decode_to_enq_ = {};

	// ready comes from UopBuffer and is consumed by IFetch in the same comb pass.
	dec2front_.ready = enq2dec.ready;
	pifetch_.comb_begin();
	ifetch_.comb_begin();
}

void FrontTop::comb() {
	pifetch_.comb(in, dec2front_, rob_bcast, pifetch_to_ifetch_);
	ifetch_.comb(pifetch_to_ifetch_, dec2front_, rob_bcast, ifetch_to_predecode_);
	predecode_.comb(ifetch_to_predecode_, predecode_to_decode_);
	decode_.comb(predecode_to_decode_, enq2dec, decode_to_enq_, dec2front_);
	front_enqueue_.comb(decode_to_enq_, enq2dec, out.enq2buf);
}

void FrontTop::seq() {
	pifetch_.seq();
	ifetch_.seq();
}
