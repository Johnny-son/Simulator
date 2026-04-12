#pragma once

#include "../../include/Uop.h"
#include <cstdint>

class Prf {
public:
	void reset();
	void comb_begin();
	void seq();

	uint64_t read(wire<PRF_IDX_WIDTH> preg) const;
	void write(wire<PRF_IDX_WIDTH> preg, uint64_t value);

private:
	uint64_t value_[PRF_NUM] = {};
	uint64_t value_n_[PRF_NUM] = {};
};

