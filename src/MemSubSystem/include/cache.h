#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

namespace memsys {

enum class ReadMasterId : uint8_t {
	ICache = 0,
	DCache = 1,
	Ptw = 2,
};

enum class WriteMasterId : uint8_t {
	DCache = 0,
	Ptw = 1,
};

// Shared AXI fabric wrapper (interconnect + router + ddr/mmio), exposed with
// cycle-accurate non-blocking request/poll semantics for each master port.
class AxiLiteBus {
public:
	AxiLiteBus();
	~AxiLiteBus();

	AxiLiteBus(const AxiLiteBus &) = delete;
	AxiLiteBus &operator=(const AxiLiteBus &) = delete;

	void reset();
	void load_program(const std::vector<uint32_t> &program, uint64_t base_pc);

	// One simulation tick of the memory fabric.
	void cycle_begin();
	void cycle_end();

	// Non-blocking read channel API.
	bool request_read32(ReadMasterId master, uint8_t tag, uint64_t addr);
	bool poll_read32(ReadMasterId master, uint8_t tag, uint32_t &data);

	// Non-blocking write channel API.
	bool request_write32(WriteMasterId master, uint8_t tag, uint64_t addr,
					 uint32_t data, uint8_t wmask);
	bool poll_write32(WriteMasterId master, uint8_t tag);

	// Backward-compatible default blocking helpers (D$ master).
	bool read32(uint64_t addr, uint32_t &data);
	bool write32(uint64_t addr, uint32_t data, uint8_t wmask);

	uint64_t read_req_count() const { return read_req_count_; }
	uint64_t write_req_count() const { return write_req_count_; }
	uint64_t read_req_count(ReadMasterId master) const;
	uint64_t write_req_count(WriteMasterId master) const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;

	mutable uint64_t read_req_count_ = 0;
	mutable uint64_t write_req_count_ = 0;
};

class ICacheSimple {
public:
	explicit ICacheSimple(AxiLiteBus *axi = nullptr) : axi_(axi) {}
	void bind(AxiLiteBus *axi) { axi_ = axi; }
	void reset();

	bool request_fetch32(uint64_t pc, uint8_t tag);
	bool poll_fetch32(uint8_t tag, uint32_t &inst);

	// Legacy single-call helper.
	bool fetch32(uint64_t pc, uint32_t &inst);

private:
	bool ensure_translation(uint64_t pc, uint8_t tag);

	AxiLiteBus *axi_ = nullptr;
	std::unordered_set<uint64_t> itlb_pages_;
	std::array<bool, 16> ptw_wait_{};
	std::array<uint64_t, 16> ptw_vpn_{};
};

class DCacheSimple {
public:
	explicit DCacheSimple(AxiLiteBus *axi = nullptr) : axi_(axi) {}
	void bind(AxiLiteBus *axi) { axi_ = axi; }
	void reset();

	bool request_load32(uint64_t addr, uint8_t tag);
	bool poll_load32(uint8_t tag, uint32_t &data);
	bool request_store32(uint64_t addr, uint32_t data, uint8_t wmask,
					uint8_t tag);
	bool poll_store32(uint8_t tag);

	// Legacy single-call helpers.
	bool load32(uint64_t addr, uint32_t &data);
	bool store32(uint64_t addr, uint32_t data, uint8_t wmask);

private:
	bool ensure_translation(uint64_t addr, uint8_t tag);

	AxiLiteBus *axi_ = nullptr;
	std::unordered_set<uint64_t> dtlb_pages_;
	std::array<bool, 16> ptw_wait_{};
	std::array<uint64_t, 16> ptw_vpn_{};
};

} // namespace memsys
