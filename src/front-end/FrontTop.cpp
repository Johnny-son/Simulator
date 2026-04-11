#include "include/FrontTop.h"

void FrontTop::comb_begin() {
	out = {};
	pifetch_to_ifetch_ = {};
	ifetch_to_predecode_ = {};
	predecode_to_decode_ = {};
	decode_to_enq_ = {};
}

void FrontTop::comb() {
	pifetch_.comb(in, rob_bcast, pifetch_to_ifetch_);
	ifetch_.comb(pifetch_to_ifetch_, out.dec2front, rob_bcast, ifetch_to_predecode_);
	predecode_.comb(ifetch_to_predecode_, predecode_to_decode_);
	decode_.comb(predecode_to_decode_, ren2dec, decode_to_enq_, out.dec2front);
	front_enqueue_.comb(decode_to_enq_, ren2dec, out.dec2ren);
}

void FrontTop::seq() {
	// 当前阶段前端各级为无状态组合骨架。
}

