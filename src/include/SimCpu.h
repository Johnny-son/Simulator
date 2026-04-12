#pragma once

#include "IO.h"
#include "RISCV.h"
#include "SimContext.h"
#include "../MemSubSystem/include/cache.h"
#include "../back-end/include/BackTop.h"
#include "../front-end/include/FrontTop.h"
#include <vector>

class SimCpu {
public:
	explicit SimCpu(SimContext *ctx)
			: ctx_(ctx), axi_(), icache_(&axi_), dcache_(&axi_) {
		back_top_.init();
		back_top_.bind_dcache(&dcache_);
		front_top_.bind_icache(&icache_);
		mem_reset();
	}

	FrontTop front_top_;
	BackTop back_top_;

	void reset() {
		front_top_ = {};
		back_top_ = {};
		back_top_.init();
		back_top_.bind_dcache(&dcache_);
		front_top_.bind_icache(&icache_);
		icache_.reset();
		dcache_.reset();
	}

	void tick() {
		// 1) connect boundary IO (front -> back, back -> front)
		back_top_.in.enq2buf = front_top_.out.enq2buf;
		front_top_.enq2dec = back_top_.out.enq2dec;
		front_top_.rob_bcast = back_top_.out.rob_bcast;

		// 1.5) begin memory-fabric cycle (collect new req intents this cycle)
		axi_.cycle_begin();

		// 2) comb phase
		front_top_.comb_begin();
		back_top_.comb_begin();

		front_top_.comb();
		back_top_.comb();

		// 2.5) advance AXI fabric one cycle
		axi_.cycle_end();

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

	void mem_reset() {
		axi_.reset();
		icache_.reset();
		dcache_.reset();
	}

	void load_program(const std::vector<uint32_t> &program, uint64_t base_pc) {
		axi_.load_program(program, base_pc);
	}

	bool icache_fetch32(uint64_t pc, uint32_t &inst) {
		return icache_.fetch32(pc, inst);
	}

	bool dcache_load32(uint64_t addr, uint32_t &data) {
		return dcache_.load32(addr, data);
	}

	bool dcache_store32(uint64_t addr, uint32_t data, uint8_t wmask = 0xFu) {
		return dcache_.store32(addr, data, wmask);
	}

	uint64_t axi_read_req_count() const { return axi_.read_req_count(); }
	uint64_t axi_write_req_count() const { return axi_.write_req_count(); }
	uint64_t axi_icache_read_req_count() const {
		return axi_.read_req_count(memsys::ReadMasterId::ICache);
	}
	uint64_t axi_dcache_read_req_count() const {
		return axi_.read_req_count(memsys::ReadMasterId::DCache);
	}
	uint64_t axi_ptw_read_req_count() const {
		return axi_.read_req_count(memsys::ReadMasterId::Ptw);
	}

private:
	SimContext *ctx_ = nullptr;
	memsys::AxiLiteBus axi_;
	memsys::ICacheSimple icache_;
	memsys::DCacheSimple dcache_;
};
