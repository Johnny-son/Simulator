#include "include/Predecode.h"
#include "../include/RISCV.h"

namespace {

// Reused from /simulator/front-end/predecode.cpp.
inline uint32_t sign_extend_u32(uint32_t value, int bits) {
	const uint32_t mask = 1u << (bits - 1);
	return (value ^ mask) - mask;
}

} // namespace

void Predecode::comb(const FrontDecIO &in, FrontDecIO &out) const {
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		out.valid[i] = in.valid[i];
		out.inst[i] = in.inst[i];
		out.pc[i] = in.pc[i];
		out.predict_dir[i] = in.predict_dir[i];

		if (!in.valid[i]) {
			continue;
		}

		const uint32_t inst = static_cast<uint32_t>(in.inst[i]);
		const uint32_t pc = static_cast<uint32_t>(in.pc[i]);
		const uint32_t opcode = inst & 0x7Fu;

		switch (opcode) {
		case number_2_opcode_jal:
			out.predict_dir[i] = 1;
			break;
		case number_4_opcode_beq: {
			const uint32_t imm12 = (inst >> 31) & 0x1;
			const uint32_t imm10_5 = (inst >> 25) & 0x3F;
			const uint32_t imm4_1 = (inst >> 8) & 0xF;
			const uint32_t imm11 = (inst >> 7) & 0x1;
			const uint32_t imm_raw =
				(imm12 << 12) | (imm11 << 11) | (imm10_5 << 5) | (imm4_1 << 1);
			const uint32_t target = pc + sign_extend_u32(imm_raw, 13);
			// Static BTFN hint: backward branch tends to be taken.
			out.predict_dir[i] = (target < pc) ? 1 : 0;
			break;
		}
		case number_3_opcode_jalr:
			out.predict_dir[i] = 1;
			break;
		default:
			break;
		}
	}
}
