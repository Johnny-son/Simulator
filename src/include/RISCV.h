#pragma once

#include "Uop.h"
#include <string>

// Reused naming from the reference simulator (/simulator/include/RISCV.h),
// so both projects can share opcode/encoding vocabulary.
#define INST_EBREAK 0x00100073
#define INST_ECALL 0x00000073
#define INST_MRET 0x30200073
#define INST_SRET 0x10200073
#define INST_WFI 0x10500073
#define INST_NOP 0x00000013

enum enum_number_opcode {
	number_0_opcode_lui = 0b0110111,
	number_1_opcode_auipc = 0b0010111,
	number_2_opcode_jal = 0b1101111,
	number_3_opcode_jalr = 0b1100111,
	number_4_opcode_beq = 0b1100011,
	number_5_opcode_lb = 0b0000011,
	number_6_opcode_sb = 0b0100011,
	number_7_opcode_addi = 0b0010011,
	number_8_opcode_add = 0b0110011,
	number_9_opcode_fence = 0b0001111,
	number_10_opcode_ecall = 0b1110011,
	number_11_opcode_lrw = 0b0101111,
	number_12_opcode_float = 0b1010011,
	number_13_opcode_fmadd = 0b1000011,
	number_14_opcode_fmsub = 0b1000111,
	number_15_opcode_fnmsub = 0b1001011,
	number_16_opcode_fnmadd = 0b1001111,
};

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
