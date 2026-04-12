#pragma once

#include "../../include/IO.h"
#include <cstdint>
#include <cstring>

struct UopBufferIn {
	EnqRenIO enq2buf;
	RenDecIO ren2dec;
	RobBroadcastIO rob_bcast;

	UopBufferIn() { std::memset(this, 0, sizeof(UopBufferIn)); }
};

struct UopBufferOut {
	EnqDecIO enq2dec;
	DecRenIO buf2ren;

	UopBufferOut() { std::memset(this, 0, sizeof(UopBufferOut)); }
};

class UopBuffer {
public:
	UopBufferIn in;
	UopBufferOut out;

	void reset() {
		std::memset(slot_, 0, sizeof(slot_));
		std::memset(slot_n_, 0, sizeof(slot_n_));
		head_ptr_ = {};
		tail_ptr_ = {};
		head_ptr_n_ = {};
		tail_ptr_n_ = {};
		out = {};
		out.enq2dec.ready = 1;
	}

	void comb_begin() {
		std::memcpy(slot_n_, slot_, sizeof(slot_));
		head_ptr_n_ = head_ptr_;
		tail_ptr_n_ = tail_ptr_;
		out = {};
	}

	void comb() {
		if (static_cast<bool>(in.rob_bcast.flush)) {
			std::memset(slot_n_, 0, sizeof(slot_n_));
			head_ptr_n_ = {};
			tail_ptr_n_ = {};
			out = {};
			out.enq2dec.ready = 1;
			return;
		}

		const bool deq_valid = !is_empty(head_ptr_n_, tail_ptr_n_);
		const bool deq_ready = static_cast<bool>(in.ren2dec.ready);
		const bool enq_valid = has_valid_lane(in.enq2buf);
		const bool queue_full = is_full(head_ptr_n_, tail_ptr_n_);

		if (deq_valid) {
			const uint32_t head_idx = static_cast<uint32_t>(head_ptr_n_.idx);
			for (int i = 0; i < DECODE_WIDTH; ++i) {
				out.buf2ren.valid[i] = slot_n_[head_idx].valid[i];
				out.buf2ren.uop[i] = slot_n_[head_idx].uop[i];
			}
		}

		// Full queue can still accept one bundle when rename pops in the same cycle.
		const bool enq_ready = !queue_full || (deq_valid && deq_ready);
		out.enq2dec.ready = enq_ready ? 1 : 0;

		const bool deq_fire = deq_valid && deq_ready;
		const bool enq_fire = enq_valid && enq_ready;

		if (deq_fire) {
			const uint32_t deq_idx = static_cast<uint32_t>(head_ptr_n_.idx);
			slot_n_[deq_idx] = {};
			advance_ptr(head_ptr_n_);
		}

		if (enq_fire) {
			const uint32_t enq_idx = static_cast<uint32_t>(tail_ptr_n_.idx);
			slot_n_[enq_idx] = in.enq2buf;
			advance_ptr(tail_ptr_n_);
		}
	}

	void seq() {
		std::memcpy(slot_, slot_n_, sizeof(slot_));
		head_ptr_ = head_ptr_n_;
		tail_ptr_ = tail_ptr_n_;
	}

private:
	static bool has_valid_lane(const EnqRenIO &bundle) {
		for (int i = 0; i < DECODE_WIDTH; ++i) {
			if (static_cast<bool>(bundle.valid[i])) {
				return true;
			}
		}
		return false;
	}

	static bool is_empty(const RingPtr<UOP_BUFFER_DEPTH> &head,
					 const RingPtr<UOP_BUFFER_DEPTH> &tail) {
		return (head.idx == tail.idx) && (head.epoch == tail.epoch);
	}

	static bool is_full(const RingPtr<UOP_BUFFER_DEPTH> &head,
					const RingPtr<UOP_BUFFER_DEPTH> &tail) {
		return (head.idx == tail.idx) && (head.epoch != tail.epoch);
	}

	static void advance_ptr(RingPtr<UOP_BUFFER_DEPTH> &ptr) {
		const uint32_t next =
			(static_cast<uint32_t>(ptr.idx) + 1u) % UOP_BUFFER_DEPTH;
		if (next == 0u) {
			ptr.epoch = !static_cast<bool>(ptr.epoch);
		}
		ptr.idx = next;
	}

	EnqRenIO slot_[UOP_BUFFER_DEPTH];
	EnqRenIO slot_n_[UOP_BUFFER_DEPTH];
	RingPtr<UOP_BUFFER_DEPTH> head_ptr_;
	RingPtr<UOP_BUFFER_DEPTH> tail_ptr_;
	RingPtr<UOP_BUFFER_DEPTH> head_ptr_n_;
	RingPtr<UOP_BUFFER_DEPTH> tail_ptr_n_;
};
