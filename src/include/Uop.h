#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

enum class IsaTag : uint8_t {
	Unknown = 0,
	RISCV64 = 1,
	X86_64 = 2,
};

enum class UopKind : uint8_t {
	Invalid = 0,
	Alu,
	Branch,
	Load,
	Store,
	Atomic,
	CsrOrSys,
	Fp,
	Vector,
	Fence,
	Nop,
};

enum class FuClass : uint8_t {
	None = 0,
	Int,
	Mem,
	Br,
	Fp,
	Vec,
	Sys,
};

enum class MemAttr : uint8_t {
	None = 0,
	BaseImm,
	BaseIndexShift,
	PCRel,
	PrePostUpdate,
};

enum class ExceptHint : uint8_t {
	None = 0,
	IllegalInst,
	Ecall,
	Ebreak,
	PageFaultInst,
	PageFaultLoad,
	PageFaultStore,
	InterruptWindow,
};

struct CtrlFlag {
	bool is_branch = false;
	bool is_call = false;
	bool is_ret = false;
	bool is_indirect = false;
	bool predicted_taken = false;
	bool is_fusion = false;
	bool is_mmio = false;
	bool has_barrier = false;
};

struct RegOperand {
	uint16_t areg = 0;
	bool valid = false;
};

struct CommonUop {
	static constexpr size_t kMaxSrcRegs = 3;
	static constexpr size_t kMaxDstRegs = 2;

	bool valid = false;

	uint64_t inst_seq = 0;
	uint64_t pc = 0;
	uint8_t macro_id = 0;
	bool is_microop = false;
	bool is_last_microop = true;

	UopKind kind = UopKind::Invalid;
	FuClass fu_class = FuClass::None;
	uint8_t width = 0;
	bool is_signed = false;

	std::array<RegOperand, kMaxSrcRegs> src{};
	std::array<RegOperand, kMaxDstRegs> dst{};

	uint64_t imm = 0;
	MemAttr mem_attr = MemAttr::None;
	CtrlFlag ctrl{};
	ExceptHint except_hint = ExceptHint::None;

	IsaTag isa = IsaTag::Unknown;
	uint32_t isa_opcode = 0;
	uint64_t isa_aux0 = 0;
	uint64_t isa_aux1 = 0;
};
