#include "../include/IO.h"
#include "include/FrontEnqueue.h"

void FrontEnqueue::comb(const DecEnqIO &dec2enq, const RenDecIO &ren2dec,
				DecRenIO &enq2ren) const {

	for (int i = 0; i < DECODE_WIDTH; ++i) {
		enq2ren.valid[i] = 0;
		enq2ren.uop[i] = {};

		if (!ren2dec.ready || !dec2enq.valid[i]) {
			continue;
		}

		enq2ren.valid[i] = 1;
		enq2ren.uop[i] = dec2enq.uop[i];
	}
}
