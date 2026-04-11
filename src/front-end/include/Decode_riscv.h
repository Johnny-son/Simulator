#pragma once

#include "IO.h"
#include "RISCV.h"

class DecodeRiscv {
public:
	FrontUop decode_lane(wire<32> inst, wire<64> pc, wire<1> pred_taken) const;
};

