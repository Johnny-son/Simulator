#include "include/WriteBack.h"

void WriteBack::comb_begin() {
	if (out.wb2rob != nullptr) {
		*(out.wb2rob) = {};
	}
}

void WriteBack::comb_writeback() {
	if (in.exe2wb == nullptr || in.lsu2wb == nullptr || out.wb2rob == nullptr) {
		return;
	}

	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		out.wb2rob->valid[i] = 0;
		out.wb2rob->wb[i] = {};

		if (in.exe2wb->valid[i]) {
			out.wb2rob->valid[i] = 1;
			out.wb2rob->wb[i] = in.exe2wb->wb[i];
			continue;
		}

		if (in.lsu2wb->valid[i]) {
			out.wb2rob->valid[i] = 1;
			out.wb2rob->wb[i] = in.lsu2wb->wb[i];
		}
	}
}

void WriteBack::seq() {}
