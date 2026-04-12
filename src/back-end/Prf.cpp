#include "include/Prf.h"
#include <cstring>

void Prf::reset() {
	std::memset(value_, 0, sizeof(value_));
	std::memset(value_n_, 0, sizeof(value_n_));
}

void Prf::comb_begin() { std::memcpy(value_n_, value_, sizeof(value_)); }

void Prf::seq() { std::memcpy(value_, value_n_, sizeof(value_)); }

uint64_t Prf::read(wire<PRF_IDX_WIDTH> preg) const {
	const uint32_t idx = static_cast<uint32_t>(preg) % PRF_NUM;
	if (idx == 0u) {
		return 0;
	}
	return value_[idx];
}

void Prf::write(wire<PRF_IDX_WIDTH> preg, uint64_t value) {
	const uint32_t idx = static_cast<uint32_t>(preg) % PRF_NUM;
	if (idx == 0u) {
		return;
	}
	value_n_[idx] = value;
}

