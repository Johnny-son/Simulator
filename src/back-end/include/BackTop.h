#pragma once

#include "../../include/IO.h"
#include "../../MemSubSystem/include/cache.h"
#include "Csr.h"
#include "Commit.h"
#include "Dispatch.h"
#include "Issue.h"
#include "Prf.h"
#include "Rename.h"
#include "Rob.h"
#include "WriteBack.h"
#include "../Exu/include/Exu.h"
#include "../Lsu/include/Lsu.h"

struct BackTopIn {
	EnqRenIO enq2buf;
};

struct BackTopOut {
	EnqDecIO enq2dec;
	RobCommitIO rob_commit;
	RobBroadcastIO rob_bcast;
};

class BackTop {
public:
	BackTopIn in;
	BackTopOut out;

	void init();
	void bind_dcache(memsys::DCacheSimple *dcache) { lsu_.bind_dcache(dcache); }
	void comb_begin();
	void comb();
	void seq();

	int debug_ren2dis_valid_count() const {
		int n = 0;
		for (int i = 0; i < RENAME_WIDTH; ++i) {
			n += ren2dis_.valid[i] ? 1 : 0;
		}
		return n;
	}

	int debug_dis2rob_valid_count() const {
		int n = 0;
		for (int i = 0; i < RENAME_WIDTH; ++i) {
			n += dis2rob_.valid[i] ? 1 : 0;
		}
		return n;
	}

	int debug_wb2rob_valid_count() const {
		int n = 0;
		for (int i = 0; i < ISSUE_WIDTH; ++i) {
			n += wb2rob_.valid[i] ? 1 : 0;
		}
		return n;
	}

private:
	// stage-to-stage IO
	RenDecIO ren2dec_;
	DisRenIO dis2ren_;
	RenDisIO ren2dis_;
	DisIssIO dis2iss_;
	DisRobIO dis2rob_;
	IssDisIO iss2dis_;
	RobDisIO rob2dis_;
	IssExeIO iss2exe_;
	IssLsuIO iss2lsu_;
	ExeWbIO exu2wb_;
	LsuWbIO lsu2wb_;
	WbRobIO wb2rob_;
	RobBroadcastIO commit_bcast_;
	Prf prf_;
	CsrFile csr_;

	Rename rename_;
	Dispatch dispatch_;
	Issue issue_;
	Exu exu_;
	Lsu lsu_;
	WriteBack writeback_;
	Rob rob_;
	Commit commit_;
};
