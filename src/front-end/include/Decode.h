#pragma once

#include "Decode_riscv.h"
#include "IO.h"

class Decode {
public:
	void comb(const FrontDecIO &front2dec, const RenDecIO &ren2dec,
			  DecEnqIO &dec2enq, DecFrontIO &dec2front) const;

private:
	DecodeRiscv riscv_decoder_;
};

