#pragma once

#include "../../include/IO.h"

class FrontEnqueue {
public:
	void comb(const DecEnqIO &dec2enq, const RenDecIO &ren2dec,
			  DecRenIO &enq2ren) const;
};
