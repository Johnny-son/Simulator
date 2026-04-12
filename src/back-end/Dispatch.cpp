#include "include/Dispatch.h"

void Dispatch::comb_begin() {
	if (out.dis2iss != nullptr) {
		*(out.dis2iss) = {};
	}
	if (out.dis2rob != nullptr) {
		*(out.dis2rob) = {};
	}
}

void Dispatch::comb_ready() {
	if (out.dis2ren == nullptr) {
		return;
	}

	uint32_t issue_ready_num = ISSUE_WIDTH;
	if (in.iss2dis != nullptr) {
		issue_ready_num = static_cast<uint32_t>(in.iss2dis->ready_num);
	}

	uint32_t rob_ready_num = RENAME_WIDTH;
	if (in.rob2dis != nullptr) {
		rob_ready_num = static_cast<uint32_t>(in.rob2dis->ready_num);
	}

	out.dis2ren->ready =
		(issue_ready_num >= RENAME_WIDTH && rob_ready_num >= RENAME_WIDTH) ? 1 : 0;
}

void Dispatch::comb_dispatch() {
	if (in.ren2dis == nullptr || out.dis2iss == nullptr || out.dis2rob == nullptr ||
		out.dis2ren == nullptr) {
		return;
	}

	for (int i = 0; i < ISSUE_WIDTH; ++i) {
		out.dis2iss->valid[i] = 0;
		out.dis2iss->req[i] = {};
		if (i >= RENAME_WIDTH || !out.dis2ren->ready || !in.ren2dis->valid[i]) {
			continue;
		}
		ExecUop uop = in.ren2dis->uop[i];
		const bool is_mem =
			(uop.front.op_class == static_cast<uint8_t>(OpClass::MEM));
		uop.fu_class = static_cast<uint8_t>(is_mem ? FuClass::LSU : FuClass::INT);
		uop.expect_mask = static_cast<wire<8>>(is_mem ? 0x2 : 0x1);
		out.dis2iss->valid[i] = 1;
		out.dis2iss->req[i] = uop;
	}

	for (int i = 0; i < RENAME_WIDTH; ++i) {
		out.dis2rob->valid[i] = 0;
		out.dis2rob->req[i] = {};
		if (!out.dis2ren->ready || !in.ren2dis->valid[i]) {
			continue;
		}
		ExecUop uop = in.ren2dis->uop[i];
		const bool is_mem =
			(uop.front.op_class == static_cast<uint8_t>(OpClass::MEM));
		uop.fu_class = static_cast<uint8_t>(is_mem ? FuClass::LSU : FuClass::INT);
		uop.expect_mask = static_cast<wire<8>>(is_mem ? 0x2 : 0x1);
		out.dis2rob->valid[i] = 1;
		out.dis2rob->req[i] = uop;
	}
}

void Dispatch::seq() {}
