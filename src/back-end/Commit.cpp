#include "include/Commit.h"

void Commit::comb_begin() {
	if (out.commit_bcast != nullptr) {
		*(out.commit_bcast) = {};
	}
}

void Commit::comb_commit() {
	if (in.rob_commit == nullptr || out.commit_bcast == nullptr) {
		return;
	}

	const CommitEntry &entry0 = in.rob_commit->commit_entry[0];
	if (!entry0.valid) {
		return;
	}

	if (entry0.except_valid || entry0.flush_pipe || entry0.mispred) {
		out.commit_bcast->flush = 1;
		out.commit_bcast->exception = entry0.except_valid;
		out.commit_bcast->mispred = entry0.mispred;
		out.commit_bcast->redirect_pc = entry0.pc + 4;
	}
}

void Commit::seq() {}

