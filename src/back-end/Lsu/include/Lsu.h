#pragma once

#include "../../../include/IO.h"
#include "../../../MemSubSystem/include/cache.h"
#include "../../include/Prf.h"
#include <deque>

struct LsuIn {
	IssLsuIO *iss2lsu = nullptr;
	RobBroadcastIO *rob_bcast = nullptr;
};

struct LsuOut {
	LsuWbIO *lsu2wb = nullptr;
};

class Lsu {
public:
	void comb_begin();
	void comb_exec();
	void seq();
	void bind_dcache(memsys::DCacheSimple *dcache) { dcache_ = dcache; }
	void bind_prf(Prf *prf) { prf_ = prf; }

	LsuIn in;
	LsuOut out;

private:
	static constexpr int kPipeDepth = 2;
	static constexpr std::size_t kIngressDepth = 64;
	IssLsuIO pipe_q_[kPipeDepth];
	IssLsuIO pipe_n_[kPipeDepth];
	bool tail_req_sent_q_[ISSUE_WIDTH] = {};
	bool tail_req_sent_n_[ISSUE_WIDTH] = {};
	std::deque<ExecUop> ingress_q_[ISSUE_WIDTH];
	memsys::DCacheSimple *dcache_ = nullptr;
	Prf *prf_ = nullptr;
};
