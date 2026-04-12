#include "include/Decode.h"

void Decode::comb(const FrontDecIO &front2dec, const EnqDecIO &enq2dec,
			  DecEnqIO &dec2enq, DecFrontIO &dec2front) const {
	dec2front.ready = enq2dec.ready;
	const bool ready = static_cast<bool>(enq2dec.ready);

	for (int i = 0; i < FETCH_WIDTH; ++i) {
		dec2front.fire[i] = front2dec.valid[i] && ready;
	}

	for (int i = 0; i < DECODE_WIDTH; ++i) {
		dec2enq.valid[i] = 0;
		dec2enq.uop[i] = {};

		if (!front2dec.valid[i] || !ready) {
			continue;
		}

		dec2enq.valid[i] = 1;
		dec2enq.uop[i] =
			riscv_decoder_.decode_lane(front2dec.inst[i], front2dec.pc[i],
							  front2dec.predict_dir[i]);
	}
}
