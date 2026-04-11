#include "include/Predecode.h"

void Predecode::comb(const FrontDecIO &in, FrontDecIO &out) const {
	for (int i = 0; i < FETCH_WIDTH; ++i) {
		out.valid[i] = in.valid[i];
		out.inst[i] = in.inst[i];
		out.pc[i] = in.pc[i];
		out.predict_dir[i] = in.predict_dir[i];
	}
}

