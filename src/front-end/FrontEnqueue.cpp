#include "../include/IO.h"
#include "include/FrontEnqueue.h"

void FrontEnqueue::comb(const DecEnqIO &dec2enq, const EnqDecIO &enq2dec,
				EnqRenIO &enq2buf) const {
	const bool ready = static_cast<bool>(enq2dec.ready);

	for (int i = 0; i < DECODE_WIDTH; ++i) {
		enq2buf.valid[i] = 0;
		enq2buf.uop[i] = {};

		if (!ready || !dec2enq.valid[i]) {
			continue;
		}

		enq2buf.valid[i] = 1;
		enq2buf.uop[i] = dec2enq.uop[i];
	}
}
