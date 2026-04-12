#pragma once

#include "base_types.h"
#include <cstdint>
#include <cstring>

constexpr int FETCH_WIDTH = 4;
constexpr int DECODE_WIDTH = 4;
constexpr int RENAME_WIDTH = 4;
constexpr int ISSUE_WIDTH = 4;
constexpr int COMMIT_WIDTH = 4;
constexpr int FE_BE_WIDTH = DECODE_WIDTH;
constexpr int UOP_BUFFER_DEPTH = 16;

constexpr int AREG_NUM = 32;
constexpr int PRF_NUM = 128;
constexpr int ROB_SIZE = 128;
constexpr int BR_TAG_NUM = 16;

constexpr int AREG_IDX_WIDTH = bit_width_for_count(AREG_NUM);
constexpr int PRF_IDX_WIDTH = bit_width_for_count(PRF_NUM);
constexpr int ROB_IDX_WIDTH = bit_width_for_count(ROB_SIZE);
constexpr int BR_TAG_WIDTH = bit_width_for_count(BR_TAG_NUM);
constexpr int BR_MASK_WIDTH = BR_TAG_NUM;

enum class IsaTag : uint8_t {
	RISCV = 0,
	X86 = 1,
};

enum class OpClass : uint8_t {
	INT = 0,
	BR = 1,
	MEM = 2,
	CSR = 3,
	MUL_DIV = 4,
	FP = 5,
	NOP = 6,
};

enum class FuClass : uint8_t {
	INT = 0,
	BR = 1,
	LSU = 2,
	CSR = 3,
	FP = 4,
};

enum class MemAttr : uint8_t {
	BaseImm = 0,
	BaseIndexShift = 1,
	PCRel = 2,
	PrePostUpdate = 3,
};

enum class FlushCause : uint8_t {
	Mispredict = 0,
	Exception = 1,
	Replay = 2,
	Fence = 3,
};

enum class RiscvSubOp : uint16_t {
	NOP = 0,
	LUI,
	AUIPC,
	JAL,
	JALR,
	BEQ,
	BNE,
	BLT,
	BGE,
	BLTU,
	BGEU,
	LB,
	LH,
	LW,
	LBU,
	LHU,
	SB,
	SH,
	SW,
	ADD,
	SUB,
	SLL,
	SLT,
	SLTU,
	XOR,
	SRL,
	SRA,
	OR,
	AND,
	ADDI,
	SLTI,
	SLTIU,
	XORI,
	ORI,
	ANDI,
	SLLI,
	SRLI,
	SRAI,
	MUL,
	MULH,
	MULHSU,
	MULHU,
	DIV,
	DIVU,
	REM,
	REMU,
	FENCE,
	FENCE_I,
	ECALL,
	EBREAK,
	MRET,
	SRET,
	SFENCE_VMA,
	CSRRW,
	CSRRS,
	CSRRC,
	CSRRWI,
	CSRRSI,
	CSRRCI,
};

struct DebugMeta {
	wire<32> raw_inst;
	wire<64> pc;
	wire<64> inst_seq;
	wire<1> difftest_skip;

	DebugMeta() { std::memset(this, 0, sizeof(DebugMeta)); }
};

struct TmaMeta {
	wire<1> is_cache_miss;
	wire<1> is_ret;
	wire<1> mem_commit_is_load;
	wire<1> mem_commit_is_store;

	TmaMeta() { std::memset(this, 0, sizeof(TmaMeta)); }
};

template <int Depth>
struct RingPtr {
	static_assert(Depth > 0, "Depth must be positive");
	static constexpr int kIdxWidth = bit_width_for_count(Depth);

	wire<kIdxWidth> idx;
	wire<1> epoch;

	RingPtr() {
		idx = 0;
		epoch = 0;
	}
};

struct FrontUop {
	wire<1> valid;

	wire<64> uop_id;
	wire<64> seq_id;
	wire<64> pc;
	wire<2> isa_tag;

	wire<4> op_class;
	wire<16> sub_op;
	wire<64> imm;

	wire<1> src_en[3];
	wire<AREG_IDX_WIDTH> src_areg[3];
	wire<1> dst_en;
	wire<AREG_IDX_WIDTH> dst_areg;

	wire<1> is_branch;
	wire<1> is_call;
	wire<1> is_ret;
	wire<1> pred_taken;
	wire<64> pred_target;

	wire<BR_TAG_WIDTH> br_id;
	wire<BR_MASK_WIDTH> br_mask;

	wire<1> except_valid;
	wire<16> except_code;

	wire<1> is_microop;
	wire<1> is_last_microop;
	wire<1> is_fusion;
	wire<1> is_mmio;
	wire<1> is_store;
	wire<1> is_load;
	wire<1> is_sc;

	wire<2> mem_attr;
	wire<2> fpr_writeback_mode;
	wire<8> macro_id;

	DebugMeta dbg;
	TmaMeta tma;

	FrontUop() { std::memset(this, 0, sizeof(FrontUop)); }
};

struct ExecUop {
	wire<1> valid;
	FrontUop front;

	wire<PRF_IDX_WIDTH> src_preg[3];
	wire<1> src_ready[3];
	wire<PRF_IDX_WIDTH> dst_preg;
	wire<PRF_IDX_WIDTH> old_dst_preg;

	wire<ROB_IDX_WIDTH> rob_idx;
	wire<1> rob_epoch;

	wire<8> expect_mask;
	wire<8> cplt_mask;

	wire<3> fu_class;
	wire<1> replay;
	wire<64> result_value;
	wire<64> mem_addr;
	wire<64> mem_wdata;
	wire<8> mem_wmask;

	ExecUop() { std::memset(this, 0, sizeof(ExecUop)); }
};

struct CommitEntry {
	wire<1> valid;
	wire<64> seq_id;
	wire<64> pc;

	wire<1> dst_en;
	wire<AREG_IDX_WIDTH> dst_areg;
	wire<64> dst_value;

	wire<1> except_valid;
	wire<16> except_code;

	wire<1> is_store;
	wire<64> store_addr;
	wire<64> store_data;
	wire<64> store_mask;

	wire<1> flush_pipe;
	wire<1> mispred;

	DebugMeta dbg;
	TmaMeta tma;

	CommitEntry() { std::memset(this, 0, sizeof(CommitEntry)); }
};

struct FlushEvent {
	wire<1> valid;
	wire<3> cause;
	wire<64> flush_seq_id;
	wire<64> redirect_pc;
	wire<ROB_IDX_WIDTH> rob_idx;
	wire<1> rob_epoch;

	FlushEvent() { std::memset(this, 0, sizeof(FlushEvent)); }
};

struct BranchBroadcast {
	wire<1> valid;
	wire<1> mispred;
	wire<BR_TAG_WIDTH> br_id;
	wire<BR_MASK_WIDTH> br_mask;
	wire<BR_MASK_WIDTH> clear_mask;
	wire<ROB_IDX_WIDTH> redirect_rob_idx;

	BranchBroadcast() { std::memset(this, 0, sizeof(BranchBroadcast)); }
};
