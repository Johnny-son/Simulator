#include "include/Exu.h"

void Exu::comb_begin() {
	if (out.exe2wb != nullptr) {
		*(out.exe2wb) = {};
	}
}

void Exu::comb_exec() {
	if (in.iss2exe == nullptr || out.exe2wb == nullptr) {
		return;
	}

	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		out.exe2wb->valid[i] = 0;
		out.exe2wb->wb[i] = {};

		if (!in.iss2exe->valid[i]) {
			continue;
		}

		ExecUop ex = in.iss2exe->req[i];
		ex.cplt_mask = ex.expect_mask;
		out.exe2wb->valid[i] = 1;
		out.exe2wb->wb[i] = ex;
	}
}

void Exu::seq() {}

