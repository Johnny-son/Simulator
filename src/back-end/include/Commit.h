#pragma once

#include "../../include/IO.h"

struct CommitIn {
	RobCommitIO *rob_commit = nullptr;
};

struct CommitOut {
	RobBroadcastIO *commit_bcast = nullptr;
};

class Commit {
public:
	void comb_begin();
	void comb_commit();
	void seq();

	CommitIn in;
	CommitOut out;
};

