#include "include/Issue.h"

void Issue::comb_begin() {
	if (out.iss2exe != nullptr) {
		*(out.iss2exe) = {};
	}
	if (out.iss2lsu != nullptr) {
		*(out.iss2lsu) = {};
	}
}

void Issue::comb_ready() {
	if (out.iss2dis != nullptr) {
		out.iss2dis->ready_num = ISSUE_WIDTH;
	}
}

void Issue::comb_issue() {
	if (in.dis2iss == nullptr || out.iss2exe == nullptr) {
		return;
	}
	if (out.iss2lsu == nullptr) {
		return;
	}

	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		out.iss2exe->valid[i] = 0;
		out.iss2exe->req[i] = {};
		out.iss2lsu->valid[i] = 0;
		out.iss2lsu->req[i] = {};

		if (!in.dis2iss->valid[i]) {
			continue;
		}

		const ExecUop uop = in.dis2iss->req[i];
		const bool is_mem = (uop.front.op_class == static_cast<uint8_t>(OpClass::MEM));
		if (is_mem) {
			out.iss2lsu->valid[i] = 1;
			out.iss2lsu->req[i] = uop;
		} else {
			out.iss2exe->valid[i] = 1;
			out.iss2exe->req[i] = uop;
		}
	}
}

void Issue::seq() {}

