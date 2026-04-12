#include "include/BackTop.h"

void BackTop::init() {
	prf_.reset();
	csr_.reset();

	rename_.in.enq2buf = &in.enq2buf;
	rename_.in.dis2ren = &dis2ren_;
	rename_.in.rob_bcast = &out.rob_bcast;
	rename_.out.enq2dec = &out.enq2dec;
	rename_.out.ren2dec = &ren2dec_;
	rename_.out.ren2dis = &ren2dis_;
	rename_.init();

	dispatch_.in.ren2dis = &ren2dis_;
	dispatch_.in.iss2dis = &iss2dis_;
	dispatch_.in.rob2dis = &rob2dis_;
	dispatch_.in.rob_bcast = &out.rob_bcast;
	dispatch_.out.dis2ren = &dis2ren_;
	dispatch_.out.dis2iss = &dis2iss_;
	dispatch_.out.dis2rob = &dis2rob_;

	issue_.in.dis2iss = &dis2iss_;
	issue_.in.rob_bcast = &out.rob_bcast;
	issue_.out.iss2dis = &iss2dis_;
	issue_.out.iss2exe = &iss2exe_;
	issue_.out.iss2lsu = &iss2lsu_;

	exu_.in.iss2exe = &iss2exe_;
	exu_.in.rob_bcast = &out.rob_bcast;
	exu_.out.exe2wb = &exu2wb_;
	exu_.bind_prf(&prf_);
	exu_.bind_csr(&csr_);

	lsu_.in.iss2lsu = &iss2lsu_;
	lsu_.in.rob_bcast = &out.rob_bcast;
	lsu_.out.lsu2wb = &lsu2wb_;
	lsu_.bind_prf(&prf_);

	writeback_.in.exe2wb = &exu2wb_;
	writeback_.in.lsu2wb = &lsu2wb_;
	writeback_.in.rob_bcast = &out.rob_bcast;
	writeback_.out.wb2rob = &wb2rob_;
	writeback_.bind_prf(&prf_);

	rob_.in.dis2rob = &dis2rob_;
	rob_.in.wb2rob = &wb2rob_;
	rob_.in.commit_bcast = &commit_bcast_;
	rob_.out.rob2dis = &rob2dis_;
	rob_.out.rob_commit = &out.rob_commit;
	rob_.out.rob_bcast = &out.rob_bcast;
	rob_.init();

	commit_.in.rob_commit = &out.rob_commit;
	commit_.out.commit_bcast = &commit_bcast_;

	out.enq2dec.ready = 1;
}

void BackTop::comb_begin() {
	out.enq2dec = {};
	out.rob_commit = {};
	out.rob_bcast = {};
	commit_bcast_ = {};
	ren2dec_ = {};
	dis2rob_ = {};
	rob2dis_ = {};

	rename_.comb_begin();
	dispatch_.comb_begin();
	issue_.comb_begin();
	exu_.comb_begin();
	lsu_.comb_begin();
	writeback_.comb_begin();
	rob_.comb_begin();
	commit_.comb_begin();
	prf_.comb_begin();
	csr_.comb_begin();
}

void BackTop::comb() {
	// Running skeleton order:
	// produce upstream stage outputs first, then consume them in later stages.
	// This keeps the simplified comb-only pipeline moving before full registered
	// stage latches are introduced.
	rob_.comb_ready();
	dispatch_.comb_ready();
	rename_.comb_alloc();
	rename_.comb_rename();
	dispatch_.comb_dispatch();
	rob_.comb_alloc();

	issue_.comb_ready();
	issue_.comb_issue();
	exu_.comb_exec();
	lsu_.comb_exec();
	writeback_.comb_writeback();
	rob_.comb_complete();

	rob_.comb_commit();
	commit_.comb_commit();
	rob_.comb_flush();
}

void BackTop::seq() {
	rename_.seq();
	dispatch_.seq();
	issue_.seq();
	exu_.seq();
	lsu_.seq();
	writeback_.seq();
	rob_.seq();
	commit_.seq();
	prf_.seq();
	csr_.seq();
}
