#include "include/Decode.h"

void Decode::comb(const FrontDecIO &front2dec, const RenDecIO &ren2dec,
				DecEnqIO &dec2enq, DecFrontIO &dec2front) const {
	dec2front.ready = ren2dec.ready;

	for (int i = 0; i < FETCH_WIDTH; ++i) {
		dec2front.fire[i] = front2dec.valid[i] && ren2dec.ready;
	}

	for (int i = 0; i < DECODE_WIDTH; ++i) {
		dec2enq.valid[i] = 0;
		dec2enq.uop[i] = {};

		if (!front2dec.valid[i] || !ren2dec.ready) {
			continue;
		}

		dec2enq.valid[i] = 1;
		dec2enq.uop[i] =
			riscv_decoder_.decode_lane(front2dec.inst[i], front2dec.pc[i],
							  front2dec.predict_dir[i]);
	}
}

