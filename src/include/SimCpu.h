#pragma once

#include "IO.h"
#include "RISCV.h"
#include "SimContext.h"
#include "../back-end/include/BackTop.h"
#include "../front-end/include/FrontTop.h"

class SimCpu {
public:
	explicit SimCpu(SimContext *ctx) : ctx_(ctx) { back_top_.init(); }

	FrontTop front_top_;
	BackTop back_top_;

	void reset() {
		front_top_ = {};
		back_top_ = {};
		back_top_.init();
	}

	void tick() {
		// 1) connect boundary IO (front -> back, back -> front)
		back_top_.in.dec2ren = front_top_.out.dec2ren;
		front_top_.ren2dec = back_top_.out.ren2dec;
		front_top_.rob_bcast = back_top_.out.rob_bcast;

		// 2) comb phase
		front_top_.comb_begin();
		back_top_.comb_begin();
		front_top_.comb();
		back_top_.comb();

		// 3) seq phase
		front_top_.seq();
		back_top_.seq();

		if (back_top_.out.rob_commit.commit_entry[0].valid && ctx_ != nullptr) {
			ctx_->on_commit();
		}

		if (ctx_ != nullptr) {
			ctx_->on_cycle();
		}
	}

	// RISC-V 优先：当前阶段只要求 DecodeRISCV 走通。
	RiscvDecodedInst decode_riscv(uint32_t inst, uint64_t pc) {
		return decode_riscv_basic(inst, pc);
	}

private:
	SimContext *ctx_ = nullptr;
};

