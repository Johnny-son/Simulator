#pragma once

#include "Uop.h"
#include <cstring>

// ================================
// QM3-style named stage IO blocks
// ================================

struct FrontDecIO {
	wire<32> inst[FETCH_WIDTH];
	wire<64> pc[FETCH_WIDTH];
	wire<1> valid[FETCH_WIDTH];
	wire<1> predict_dir[FETCH_WIDTH];

	FrontDecIO() { std::memset(this, 0, sizeof(FrontDecIO)); }
};

struct DecFrontIO {
	wire<1> fire[FETCH_WIDTH];
	wire<1> ready;

	DecFrontIO() { std::memset(this, 0, sizeof(DecFrontIO)); }
};

struct PIFetchIFetchIO {
	wire<32> inst[FETCH_WIDTH];
	wire<64> pc[FETCH_WIDTH];
	wire<1> valid[FETCH_WIDTH];
	wire<1> predict_dir[FETCH_WIDTH];

	PIFetchIFetchIO() { std::memset(this, 0, sizeof(PIFetchIFetchIO)); }
};

struct DecRenIO {
	FrontUop uop[DECODE_WIDTH];
	wire<1> valid[DECODE_WIDTH];

	DecRenIO() { std::memset(this, 0, sizeof(DecRenIO)); }
};

struct DecEnqIO {
	FrontUop uop[DECODE_WIDTH];
	wire<1> valid[DECODE_WIDTH];

	DecEnqIO() { std::memset(this, 0, sizeof(DecEnqIO)); }
};

struct EnqDecIO {
	wire<1> ready;

	EnqDecIO() { std::memset(this, 0, sizeof(EnqDecIO)); }
};

struct EnqRenIO {
	FrontUop uop[DECODE_WIDTH];
	wire<1> valid[DECODE_WIDTH];

	EnqRenIO() { std::memset(this, 0, sizeof(EnqRenIO)); }
};

struct RenDecIO {
	wire<1> ready;

	RenDecIO() { std::memset(this, 0, sizeof(RenDecIO)); }
};

struct RenDisIO {
	ExecUop uop[RENAME_WIDTH];
	wire<1> valid[RENAME_WIDTH];

	RenDisIO() { std::memset(this, 0, sizeof(RenDisIO)); }
};

struct DisRenIO {
	wire<1> ready;

	DisRenIO() { std::memset(this, 0, sizeof(DisRenIO)); }
};

struct DisIssIO {
	ExecUop req[ISSUE_WIDTH];
	wire<1> valid[ISSUE_WIDTH];

	DisIssIO() { std::memset(this, 0, sizeof(DisIssIO)); }
};

struct DisRobIO {
	ExecUop req[RENAME_WIDTH];
	wire<1> valid[RENAME_WIDTH];

	DisRobIO() { std::memset(this, 0, sizeof(DisRobIO)); }
};

struct IssDisIO {
	wire<8> ready_num;

	IssDisIO() { std::memset(this, 0, sizeof(IssDisIO)); }
};

struct RobDisIO {
	wire<8> ready_num;

	RobDisIO() { std::memset(this, 0, sizeof(RobDisIO)); }
};

struct IssExeIO {
	ExecUop req[ISSUE_WIDTH];
	wire<1> valid[ISSUE_WIDTH];

	IssExeIO() { std::memset(this, 0, sizeof(IssExeIO)); }
};

struct IssLsuIO {
	ExecUop req[ISSUE_WIDTH];
	wire<1> valid[ISSUE_WIDTH];

	IssLsuIO() { std::memset(this, 0, sizeof(IssLsuIO)); }
};

struct ExeWbIO {
	ExecUop wb[ISSUE_WIDTH];
	wire<1> valid[ISSUE_WIDTH];

	ExeWbIO() { std::memset(this, 0, sizeof(ExeWbIO)); }
};

struct LsuWbIO {
	ExecUop wb[ISSUE_WIDTH];
	wire<1> valid[ISSUE_WIDTH];

	LsuWbIO() { std::memset(this, 0, sizeof(LsuWbIO)); }
};

struct WbRobIO {
	ExecUop wb[ISSUE_WIDTH];
	wire<1> valid[ISSUE_WIDTH];

	WbRobIO() { std::memset(this, 0, sizeof(WbRobIO)); }
};

struct RobCommitIO {
	CommitEntry commit_entry[COMMIT_WIDTH];

	RobCommitIO() { std::memset(this, 0, sizeof(RobCommitIO)); }
};

struct RobBroadcastIO {
	wire<1> flush;
	wire<1> mispred;
	wire<1> exception;
	wire<64> redirect_pc;
	wire<ROB_IDX_WIDTH> redirect_rob_idx;

	RobBroadcastIO() { std::memset(this, 0, sizeof(RobBroadcastIO)); }
};
