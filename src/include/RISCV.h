#pragma once

#include "Uop.h"

struct RiscvDecodedInst {
	wire<1> valid;
	wire<32> raw;
	wire<64> pc;

	wire<7> opcode;
	wire<3> funct3;
	wire<7> funct7;

	wire<5> rd;
	wire<5> rs1;
	wire<5> rs2;

	wire<64> imm_i;
	wire<64> imm_s;
	wire<64> imm_b;
	wire<64> imm_u;
	wire<64> imm_j;

	RiscvSubOp sub_op;
	OpClass op_class;

	wire<1> illegal;

	RiscvDecodedInst()
			: valid(0), raw(0), pc(0), opcode(0), funct3(0), funct7(0), rd(0),
				rs1(0), rs2(0), imm_i(0), imm_s(0), imm_b(0), imm_u(0), imm_j(0),
				sub_op(RiscvSubOp::NOP), op_class(OpClass::NOP), illegal(0) {}
};

constexpr uint32_t rv_get_opcode(uint32_t inst) { return inst & 0x7F; }
constexpr uint32_t rv_get_rd(uint32_t inst) { return (inst >> 7) & 0x1F; }
constexpr uint32_t rv_get_funct3(uint32_t inst) { return (inst >> 12) & 0x7; }
constexpr uint32_t rv_get_rs1(uint32_t inst) { return (inst >> 15) & 0x1F; }
constexpr uint32_t rv_get_rs2(uint32_t inst) { return (inst >> 20) & 0x1F; }
constexpr uint32_t rv_get_funct7(uint32_t inst) { return (inst >> 25) & 0x7F; }

inline int64_t rv_sign_extend(uint64_t value, int bits) {
	const uint64_t sign_bit = 1ULL << (bits - 1);
	const uint64_t mask = (bits == 64) ? ~0ULL : ((1ULL << bits) - 1ULL);
	const uint64_t v = value & mask;
	return (v ^ sign_bit) - sign_bit;
}

inline uint64_t rv_decode_imm_i(uint32_t inst) {
	return static_cast<uint64_t>(rv_sign_extend((inst >> 20) & 0xFFF, 12));
}

inline uint64_t rv_decode_imm_s(uint32_t inst) {
	const uint32_t imm = ((inst >> 25) << 5) | ((inst >> 7) & 0x1F);
	return static_cast<uint64_t>(rv_sign_extend(imm, 12));
}

inline uint64_t rv_decode_imm_b(uint32_t inst) {
	const uint32_t imm = (((inst >> 31) & 0x1) << 12) |
											 (((inst >> 7) & 0x1) << 11) |
											 (((inst >> 25) & 0x3F) << 5) |
											 (((inst >> 8) & 0xF) << 1);
	return static_cast<uint64_t>(rv_sign_extend(imm, 13));
}

inline uint64_t rv_decode_imm_u(uint32_t inst) {
	return static_cast<uint64_t>(static_cast<int64_t>(inst & 0xFFFFF000));
}

inline uint64_t rv_decode_imm_j(uint32_t inst) {
	const uint32_t imm = (((inst >> 31) & 0x1) << 20) |
											 (((inst >> 12) & 0xFF) << 12) |
											 (((inst >> 20) & 0x1) << 11) |
											 (((inst >> 21) & 0x3FF) << 1);
	return static_cast<uint64_t>(rv_sign_extend(imm, 21));
}

inline RiscvDecodedInst decode_riscv_basic(uint32_t inst, uint64_t pc) {
	RiscvDecodedInst out;
	out.valid = 1;
	out.raw = inst;
	out.pc = pc;
	out.opcode = rv_get_opcode(inst);
	out.funct3 = rv_get_funct3(inst);
	out.funct7 = rv_get_funct7(inst);
	out.rd = rv_get_rd(inst);
	out.rs1 = rv_get_rs1(inst);
	out.rs2 = rv_get_rs2(inst);
	out.imm_i = rv_decode_imm_i(inst);
	out.imm_s = rv_decode_imm_s(inst);
	out.imm_b = rv_decode_imm_b(inst);
	out.imm_u = rv_decode_imm_u(inst);
	out.imm_j = rv_decode_imm_j(inst);
	out.sub_op = RiscvSubOp::NOP;
	out.op_class = OpClass::NOP;
	out.illegal = 0;
	return out;
}

