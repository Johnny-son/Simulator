#pragma once

#include "../../include/IO.h"
#include "Commit.h"
#include "Dispatch.h"
#include "Issue.h"
#include "Rename.h"
#include "Rob.h"
#include "WriteBack.h"
#include "../Exu/include/Exu.h"
#include "../Lsu/include/Lsu.h"

struct BackTopIn {
	DecRenIO dec2ren;
};

struct BackTopOut {
	RenDecIO ren2dec;
	RobCommitIO rob_commit;
	RobBroadcastIO rob_bcast;
};

class BackTop {
public:
	BackTopIn in;
	BackTopOut out;

	void init();
	void comb_begin();
	void comb();
	void seq();

private:
	// stage-to-stage IO
	DisRenIO dis2ren_;
	RenDisIO ren2dis_;
	DisIssIO dis2iss_;
	IssDisIO iss2dis_;
	IssExeIO iss2exe_;
	IssLsuIO iss2lsu_;
	ExeWbIO exu2wb_;
	LsuWbIO lsu2wb_;
	WbRobIO wb2rob_;
	RobBroadcastIO commit_bcast_;

	Rename rename_;
	Dispatch dispatch_;
	Issue issue_;
	Exu exu_;
	Lsu lsu_;
	WriteBack writeback_;
	Rob rob_;
	Commit commit_;
};

