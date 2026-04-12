#include "include/Rename.h"

void Rename::init() {
	for (int i = 0; i < AREG_NUM; ++i) {
		arch_rat_[i] = i;
	}
	buf2ren_ = {};
	uop_buffer_.reset();
}

void Rename::comb_begin() {
	buf2ren_ = {};
	if (out.enq2dec != nullptr) {
		*(out.enq2dec) = {};
	}
	if (out.ren2dis != nullptr) {
		*(out.ren2dis) = {};
	}
	uop_buffer_.comb_begin();
}

void Rename::comb_alloc() {
	if (out.ren2dec == nullptr) {
		return;
	}
	out.ren2dec->ready = 1;
	if (in.dis2ren != nullptr) {
		out.ren2dec->ready = in.dis2ren->ready;
	}
}

void Rename::comb_rename() {
	if (out.ren2dec == nullptr || out.enq2dec == nullptr) {
		return;
	}

	if (in.enq2buf != nullptr) {
		uop_buffer_.in.enq2buf = *(in.enq2buf);
	} else {
		uop_buffer_.in.enq2buf = {};
	}
	uop_buffer_.in.ren2dec = *(out.ren2dec);
	if (in.rob_bcast != nullptr) {
		uop_buffer_.in.rob_bcast = *(in.rob_bcast);
	} else {
		uop_buffer_.in.rob_bcast = {};
	}
	uop_buffer_.comb();
	*(out.enq2dec) = uop_buffer_.out.enq2dec;
	buf2ren_ = uop_buffer_.out.buf2ren;

	if (out.ren2dis == nullptr) {
		return;
	}

	for (int i = 0; i < RENAME_WIDTH; ++i) {
		out.ren2dis->valid[i] = 0;
		out.ren2dis->uop[i] = {};

		if (!out.ren2dec->ready || !buf2ren_.valid[i]) {
			continue;
		}

		ExecUop ex;
		ex.valid = 1;
		ex.front = buf2ren_.uop[i];
		ex.src_preg[0] = arch_rat_[static_cast<uint32_t>(ex.front.src_areg[0])];
		ex.src_preg[1] = arch_rat_[static_cast<uint32_t>(ex.front.src_areg[1])];
		ex.src_preg[2] = arch_rat_[static_cast<uint32_t>(ex.front.src_areg[2])];
		ex.src_ready[0] = 1;
		ex.src_ready[1] = 1;
		ex.src_ready[2] = 1;
		ex.dst_preg = arch_rat_[static_cast<uint32_t>(ex.front.dst_areg)];
		ex.old_dst_preg = ex.dst_preg;
		ex.expect_mask = 0x1;
		ex.cplt_mask = 0;
		ex.fu_class = static_cast<uint8_t>(FuClass::INT);

		out.ren2dis->valid[i] = 1;
		out.ren2dis->uop[i] = ex;
	}
}

void Rename::seq() { uop_buffer_.seq(); }
