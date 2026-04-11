#include "include/Lsu.h"

void Lsu::comb_begin() {
	if (out.lsu2wb != nullptr) {
		*(out.lsu2wb) = {};
	}
}

void Lsu::comb_exec() {
	if (in.iss2lsu == nullptr || out.lsu2wb == nullptr) {
		return;
	}

	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		out.lsu2wb->valid[i] = 0;
		out.lsu2wb->wb[i] = {};

		if (!in.iss2lsu->valid[i]) {
			continue;
		}

		ExecUop mem = in.iss2lsu->req[i];
		mem.cplt_mask = mem.expect_mask;
		out.lsu2wb->valid[i] = 1;
		out.lsu2wb->wb[i] = mem;
	}
}

void Lsu::seq() {}

