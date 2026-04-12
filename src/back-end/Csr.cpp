#include "include/Csr.h"

void CsrFile::reset() {
	mstatus_ = 0;
	mtvec_ = 0;
	mepc_ = 0;
	mcause_ = 0;
	mscratch_ = 0;
	mstatus_n_ = 0;
	mtvec_n_ = 0;
	mepc_n_ = 0;
	mcause_n_ = 0;
	mscratch_n_ = 0;
}

void CsrFile::comb_begin() {
	mstatus_n_ = mstatus_;
	mtvec_n_ = mtvec_;
	mepc_n_ = mepc_;
	mcause_n_ = mcause_;
	mscratch_n_ = mscratch_;
}

void CsrFile::seq() {
	mstatus_ = mstatus_n_;
	mtvec_ = mtvec_n_;
	mepc_ = mepc_n_;
	mcause_ = mcause_n_;
	mscratch_ = mscratch_n_;
}

uint64_t CsrFile::read(uint16_t addr) const {
	switch (addr) {
	case 0x300: return mstatus_;
	case 0x305: return mtvec_;
	case 0x340: return mscratch_;
	case 0x341: return mepc_;
	case 0x342: return mcause_;
	default: return 0;
	}
}

void CsrFile::write(uint16_t addr, uint64_t value) {
	switch (addr) {
	case 0x300: mstatus_n_ = value; break;
	case 0x305: mtvec_n_ = value; break;
	case 0x340: mscratch_n_ = value; break;
	case 0x341: mepc_n_ = value; break;
	case 0x342: mcause_n_ = value; break;
	default: break;
	}
}

