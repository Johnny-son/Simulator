#pragma once

#include <cstdint>

class CsrFile {
public:
	void reset();
	void comb_begin();
	void seq();

	uint64_t read(uint16_t addr) const;
	void write(uint16_t addr, uint64_t value);

private:
	uint64_t mstatus_ = 0;
	uint64_t mtvec_ = 0;
	uint64_t mepc_ = 0;
	uint64_t mcause_ = 0;
	uint64_t mscratch_ = 0;

	uint64_t mstatus_n_ = 0;
	uint64_t mtvec_n_ = 0;
	uint64_t mepc_n_ = 0;
	uint64_t mcause_n_ = 0;
	uint64_t mscratch_n_ = 0;
};

