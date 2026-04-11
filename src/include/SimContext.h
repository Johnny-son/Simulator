#pragma once

#include <cstdint>

enum class ExitReason : uint8_t {
	NONE = 0,
	EBREAK,
	WFI,
	EXCEPTION,
};

struct SimContext {
	uint64_t cycle = 0;
	uint64_t committed_inst = 0;
	ExitReason exit_reason = ExitReason::NONE;

	void on_cycle() { ++cycle; }
	void on_commit(uint32_t n = 1) { committed_inst += n; }
};

