#include "include/BackTop.h"

void BackTop::init() {
	rename_.in.dec2ren = &in.dec2ren;
	rename_.in.dis2ren = &dis2ren_;
	rename_.in.rob_bcast = &out.rob_bcast;
	rename_.out.ren2dec = &out.ren2dec;
	rename_.out.ren2dis = &ren2dis_;
	rename_.init();

	dispatch_.in.ren2dis = &ren2dis_;
	dispatch_.in.iss2dis = &iss2dis_;
	dispatch_.in.rob_bcast = &out.rob_bcast;
	dispatch_.out.dis2ren = &dis2ren_;
	dispatch_.out.dis2iss = &dis2iss_;

	issue_.in.dis2iss = &dis2iss_;
	issue_.in.rob_bcast = &out.rob_bcast;
	issue_.out.iss2dis = &iss2dis_;
	issue_.out.iss2exe = &iss2exe_;
	issue_.out.iss2lsu = &iss2lsu_;

	exu_.in.iss2exe = &iss2exe_;
	exu_.in.rob_bcast = &out.rob_bcast;
	exu_.out.exe2wb = &exu2wb_;

	lsu_.in.iss2lsu = &iss2lsu_;
	lsu_.in.rob_bcast = &out.rob_bcast;
	lsu_.out.lsu2wb = &lsu2wb_;

	writeback_.in.exe2wb = &exu2wb_;
	writeback_.in.lsu2wb = &lsu2wb_;
	writeback_.in.rob_bcast = &out.rob_bcast;
	writeback_.out.wb2rob = &wb2rob_;

	rob_.in.wb2rob = &wb2rob_;
	rob_.in.commit_bcast = &commit_bcast_;
	rob_.out.rob_commit = &out.rob_commit;
	rob_.out.rob_bcast = &out.rob_bcast;
	rob_.init();

	commit_.in.rob_commit = &out.rob_commit;
	commit_.out.commit_bcast = &commit_bcast_;
}

void BackTop::comb_begin() {
	out.rob_commit = {};
	out.rob_bcast = {};
	commit_bcast_ = {};

	rename_.comb_begin();
	dispatch_.comb_begin();
	issue_.comb_begin();
	exu_.comb_begin();
	lsu_.comb_begin();
	writeback_.comb_begin();
	rob_.comb_begin();
	commit_.comb_begin();
}

void BackTop::comb() {
	// Group 1: Commit / status
	rob_.comb_commit();
	commit_.comb_commit();

	// Group 2: Recovery
	rob_.comb_flush();

	// Group 3/4: Execute/Issue path (skeleton)
	issue_.comb_ready();
	issue_.comb_issue();
	exu_.comb_exec();
	lsu_.comb_exec();
	writeback_.comb_writeback();
	rob_.comb_complete();

	// Group 5: Allocate / rename / dispatch
	dispatch_.comb_ready();
	rename_.comb_alloc();
	rename_.comb_rename();
	dispatch_.comb_dispatch();
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
}

