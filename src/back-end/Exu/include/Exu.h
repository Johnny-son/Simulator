#pragma once

#include "../../../include/IO.h"
#include "../../include/Csr.h"
#include "../../include/Prf.h"

struct ExuIn {
	IssExeIO *iss2exe = nullptr;
	RobBroadcastIO *rob_bcast = nullptr;
};

struct ExuOut {
	ExeWbIO *exe2wb = nullptr;
};

class Exu {
public:
	void comb_begin();
	void comb_exec();
	void seq();
	void bind_prf(Prf *prf) { prf_ = prf; }
	void bind_csr(CsrFile *csr) { csr_ = csr; }

	ExuIn in;
	ExuOut out;

private:
	static constexpr int kPipeDepth = 1;
	IssExeIO pipe_q_[kPipeDepth];
	IssExeIO pipe_n_[kPipeDepth];
	Prf *prf_ = nullptr;
	CsrFile *csr_ = nullptr;
};
