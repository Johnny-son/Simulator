#pragma once

#include "../../include/IO.h"

class FrontEnqueue {
public:
	void comb(const DecEnqIO &dec2enq, const EnqDecIO &enq2dec,
			  EnqRenIO &enq2buf) const;
};
