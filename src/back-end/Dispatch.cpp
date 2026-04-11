#include "include/Dispatch.h"

void Dispatch::comb_begin() {
	if (out.dis2iss != nullptr) {
		*(out.dis2iss) = {};
	}
}

void Dispatch::comb_ready() {
	if (out.dis2ren == nullptr) {
		return;
	}
	out.dis2ren->ready = 1;
	if (in.iss2dis != nullptr) {
		out.dis2ren->ready = (in.iss2dis->ready_num >= RENAME_WIDTH) ? 1 : 0;
	}
}

void Dispatch::comb_dispatch() {
	if (in.ren2dis == nullptr || out.dis2iss == nullptr || out.dis2ren == nullptr) {
		return;
	}

	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		out.dis2iss->valid[i] = 0;
		out.dis2iss->req[i] = {};
		if (!out.dis2ren->ready || !in.ren2dis->valid[i]) {
			continue;
		}
		out.dis2iss->valid[i] = 1;
		out.dis2iss->req[i] = in.ren2dis->uop[i];
	}
}

void Dispatch::seq() {}

