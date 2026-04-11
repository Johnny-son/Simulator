#pragma once

#include "../../include/IO.h"

struct RobIn {
	WbRobIO *wb2rob = nullptr;
	RobBroadcastIO *commit_bcast = nullptr;
};

struct RobOut {
	RobCommitIO *rob_commit = nullptr;
	RobBroadcastIO *rob_bcast = nullptr;
};

class Rob {
public:
	void init();
	void comb_begin();
	void comb_complete();
	void comb_commit();
	void comb_flush();
	void seq();

	RobIn in;
	RobOut out;

private:
	struct Slot {
		wire<1> valid;
		ExecUop uop;
	};

	Slot entry_[ROB_SIZE];
	Slot entry_n_[ROB_SIZE];
	RingPtr<ROB_SIZE> enq_ptr_;
	RingPtr<ROB_SIZE> deq_ptr_;
	RingPtr<ROB_SIZE> enq_ptr_n_;
	RingPtr<ROB_SIZE> deq_ptr_n_;
	RobBroadcastIO rob_bcast_n_;

	bool is_empty() const;
	bool is_full() const;
	void advance_ptr(RingPtr<ROB_SIZE> &ptr);
};

