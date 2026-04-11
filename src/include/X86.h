#pragma once

#include "Uop.h"

struct X86DecodedInst {
	wire<1> valid;
	wire<64> pc;
	wire<1> unsupported;

	X86DecodedInst() : valid(0), pc(0), unsupported(1) {}
};

inline X86DecodedInst decode_x86_stub(uint64_t pc) {
	X86DecodedInst out;
	out.valid = 0;
	out.pc = pc;
	out.unsupported = 1;
	return out;
}

