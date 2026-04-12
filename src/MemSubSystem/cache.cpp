#include "include/cache.h"

#include "AXI_Interconnect.h"
#include "AXI_Router_AXI4.h"
#include "MMIO_Bus_AXI4.h"
#include "PhysMemory.h"
#include "SimDDR.h"
#include <array>
#include <cstdint>
#include <memory>

// AXI kit runtime tick (required by AXI_Interconnect / SimDDR internals).
long long sim_time = 0;

namespace {

constexpr uint8_t kSize4B = 3; // total_size encoding: 3 -> 4 bytes
constexpr int kTagSlots = 16;
constexpr int kBlockingMaxCycles = 4096;
constexpr uint64_t kPageShift = 12ull;
constexpr uint64_t kPtwAddrBase = 0x80000000ull;

uint64_t synthetic_pte_addr(uint64_t vpn) {
	// Minimal MMU/PTW model: walk address is mapped to a dedicated pseudo PTE
	// table region in memory. Translation result is identity mapping in this
	// stage, but PTW traffic is real and visible on the PTW AXI master port.
	return kPtwAddrBase + static_cast<uint64_t>((vpn & 0x3FFull) * 4ull);
}

void clear_axi_master_inputs(axi_interconnect::AXI_Interconnect &interconnect) {
	for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; ++i) {
		auto &port = interconnect.read_ports[i];
		port.req.valid = false;
		port.req.addr = 0;
		port.req.total_size = 0;
		port.req.id = 0;
		port.req.bypass = false;
		port.resp.ready = true;
	}
	for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; ++i) {
		auto &port = interconnect.write_ports[i];
		port.req.valid = false;
		port.req.addr = 0;
		port.req.wdata.clear();
		port.req.wstrb = 0;
		port.req.total_size = 0;
		port.req.id = 0;
		port.req.bypass = false;
		port.resp.ready = true;
	}
}

} // namespace

namespace memsys {

struct AxiLiteBus::Impl {
	struct ReadReqLatch {
		bool valid = false;
		uint32_t addr = 0;
		uint8_t tag = 0;
	};

	struct WriteReqLatch {
		bool valid = false;
		uint32_t addr = 0;
		uint32_t data = 0;
		uint8_t wmask = 0;
		uint8_t tag = 0;
	};

	axi_interconnect::AXI_Interconnect interconnect;
	axi_interconnect::AXI_Router_AXI4 router;
	sim_ddr::SimDDR ddr;
	mmio::MMIO_Bus_AXI4 mmio;

	std::array<ReadReqLatch, axi_interconnect::NUM_READ_MASTERS> read_latch{};
	std::array<WriteReqLatch, axi_interconnect::NUM_WRITE_MASTERS> write_latch{};

	std::array<bool, axi_interconnect::NUM_READ_MASTERS> read_claimed{};
	std::array<bool, axi_interconnect::NUM_WRITE_MASTERS> write_claimed{};

	std::array<std::array<bool, kTagSlots>,
			   axi_interconnect::NUM_READ_MASTERS>
		read_resp_valid{};
	std::array<std::array<uint32_t, kTagSlots>,
			   axi_interconnect::NUM_READ_MASTERS>
		read_resp_data{};
	std::array<std::array<bool, kTagSlots>,
			   axi_interconnect::NUM_WRITE_MASTERS>
		write_resp_valid{};
	std::array<std::array<bool, kTagSlots>,
			   axi_interconnect::NUM_READ_MASTERS>
		read_inflight{};
	std::array<std::array<bool, kTagSlots>,
			   axi_interconnect::NUM_WRITE_MASTERS>
		write_inflight{};

	std::array<uint64_t, axi_interconnect::NUM_READ_MASTERS> read_by_master{};
	std::array<uint64_t, axi_interconnect::NUM_WRITE_MASTERS> write_by_master{};

	void clear_state() {
		for (auto &x : read_latch) {
			x = {};
		}
		for (auto &x : write_latch) {
			x = {};
		}
		for (auto &x : read_claimed) {
			x = false;
		}
		for (auto &x : write_claimed) {
			x = false;
		}
		for (auto &row : read_resp_valid) {
			for (auto &v : row) {
				v = false;
			}
		}
		for (auto &row : read_resp_data) {
			for (auto &v : row) {
				v = 0;
			}
		}
		for (auto &row : write_resp_valid) {
			for (auto &v : row) {
				v = false;
			}
		}
		for (auto &row : read_inflight) {
			for (auto &v : row) {
				v = false;
			}
		}
		for (auto &row : write_inflight) {
			for (auto &v : row) {
				v = false;
			}
		}
		for (auto &v : read_by_master) {
			v = 0;
		}
		for (auto &v : write_by_master) {
			v = 0;
		}
	}

	void init_runtime() {
		clear_axi_master_inputs(interconnect);
		interconnect.init();
		router.init();
		ddr.init();
		mmio.init();
		clear_axi_master_inputs(interconnect);
	}
};

AxiLiteBus::AxiLiteBus() : impl_(std::make_unique<Impl>()) {
	(void)pmem_init();
	reset();
}

AxiLiteBus::~AxiLiteBus() { pmem_release(); }

void AxiLiteBus::reset() {
	(void)pmem_init();
	pmem_clear_all();
	sim_time = 0;
	read_req_count_ = 0;
	write_req_count_ = 0;
	if (impl_ == nullptr) {
		impl_ = std::make_unique<Impl>();
	}
	impl_->clear_state();
	impl_->init_runtime();
}

void AxiLiteBus::load_program(const std::vector<uint32_t> &program,
					  uint64_t base_pc) {
	for (std::size_t i = 0; i < program.size(); ++i) {
		const uint64_t addr64 = base_pc + static_cast<uint64_t>(i * 4ull);
		if (addr64 > 0xFFFFFFFFull) {
			break;
		}
		pmem_write(static_cast<uint32_t>(addr64), program[i]);
	}
}

void AxiLiteBus::cycle_begin() {
	if (impl_ == nullptr) {
		return;
	}
	for (auto &x : impl_->read_claimed) {
		x = false;
	}
	for (auto &x : impl_->write_claimed) {
		x = false;
	}
}

bool AxiLiteBus::request_read32(ReadMasterId master, uint8_t tag, uint64_t addr) {
	if (impl_ == nullptr || addr > 0xFFFFFFFFull) {
		return false;
	}
	const uint8_t m = static_cast<uint8_t>(master);
	const uint8_t t = static_cast<uint8_t>(tag & 0xFu);
	if (m >= impl_->read_latch.size()) {
		return false;
	}
	if (impl_->read_inflight[m][t]) {
		return true;
	}

	auto &l = impl_->read_latch[m];
	if (l.valid) {
		return (l.addr == static_cast<uint32_t>(addr) && l.tag == t);
	}
	if (impl_->read_claimed[m]) {
		return false;
	}

	l.valid = true;
	l.addr = static_cast<uint32_t>(addr);
	l.tag = t;
	impl_->read_claimed[m] = true;
	return true;
}

bool AxiLiteBus::poll_read32(ReadMasterId master, uint8_t tag, uint32_t &data) {
	if (impl_ == nullptr) {
		data = 0;
		return false;
	}
	const uint8_t m = static_cast<uint8_t>(master);
	const uint8_t t = static_cast<uint8_t>(tag & 0xFu);
	if (m >= impl_->read_resp_valid.size()) {
		data = 0;
		return false;
	}
	if (!impl_->read_resp_valid[m][t]) {
		data = 0;
		return false;
	}
	data = impl_->read_resp_data[m][t];
	impl_->read_resp_valid[m][t] = false;
	return true;
}

bool AxiLiteBus::request_write32(WriteMasterId master, uint8_t tag, uint64_t addr,
					 uint32_t data, uint8_t wmask) {
	if (impl_ == nullptr || addr > 0xFFFFFFFFull) {
		return false;
	}
	const uint8_t m = static_cast<uint8_t>(master);
	const uint8_t t = static_cast<uint8_t>(tag & 0xFu);
	if (m >= impl_->write_latch.size()) {
		return false;
	}
	if (impl_->write_inflight[m][t]) {
		return true;
	}

	auto &l = impl_->write_latch[m];
	if (l.valid) {
		return (l.addr == static_cast<uint32_t>(addr) && l.data == data &&
				l.wmask == wmask && l.tag == t);
	}
	if (impl_->write_claimed[m]) {
		return false;
	}

	l.valid = true;
	l.addr = static_cast<uint32_t>(addr);
	l.data = data;
	l.wmask = wmask;
	l.tag = t;
	impl_->write_claimed[m] = true;
	return true;
}

bool AxiLiteBus::poll_write32(WriteMasterId master, uint8_t tag) {
	if (impl_ == nullptr) {
		return false;
	}
	const uint8_t m = static_cast<uint8_t>(master);
	const uint8_t t = static_cast<uint8_t>(tag & 0xFu);
	if (m >= impl_->write_resp_valid.size()) {
		return false;
	}
	if (!impl_->write_resp_valid[m][t]) {
		return false;
	}
	impl_->write_resp_valid[m][t] = false;
	return true;
}

void AxiLiteBus::cycle_end() {
	if (impl_ == nullptr) {
		return;
	}

	auto &interconnect = impl_->interconnect;
	auto &router = impl_->router;
	auto &ddr = impl_->ddr;
	auto &mmio = impl_->mmio;

	clear_axi_master_inputs(interconnect);

	for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; ++i) {
		auto &port = interconnect.read_ports[i];
		const auto &l = impl_->read_latch[static_cast<std::size_t>(i)];
		if (l.valid) {
			port.req.valid = true;
			port.req.addr = l.addr;
			port.req.total_size = kSize4B;
			port.req.id = l.tag;
			port.req.bypass = false;
		}
		port.resp.ready = true;
	}

	for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; ++i) {
		auto &port = interconnect.write_ports[i];
		const auto &l = impl_->write_latch[static_cast<std::size_t>(i)];
		if (l.valid) {
			port.req.valid = true;
			port.req.addr = l.addr;
			port.req.wdata.clear();
			port.req.wdata[0] = l.data;
			port.req.wstrb = static_cast<uint64_t>(l.wmask & 0xFu);
			port.req.total_size = kSize4B;
			port.req.id = l.tag;
			port.req.bypass = false;
		}
		port.resp.ready = true;
	}

	ddr.comb_outputs();
	mmio.comb_outputs();
	router.comb_outputs(interconnect.axi_io, ddr.io, mmio.io);
	interconnect.comb_outputs();

	for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; ++i) {
		auto &port = interconnect.read_ports[i];
		auto &l = impl_->read_latch[static_cast<std::size_t>(i)];
		if (l.valid && port.req.ready) {
			impl_->read_inflight[static_cast<std::size_t>(i)]
							   [static_cast<std::size_t>(l.tag)] = true;
			l.valid = false;
		}
		if (port.resp.valid) {
			const uint8_t t = static_cast<uint8_t>(port.resp.id) & 0xFu;
			impl_->read_inflight[static_cast<std::size_t>(i)]
							   [static_cast<std::size_t>(t)] = false;
			impl_->read_resp_data[static_cast<std::size_t>(i)][t] =
				static_cast<uint32_t>(port.resp.data[0]);
			impl_->read_resp_valid[static_cast<std::size_t>(i)][t] = true;
			++read_req_count_;
			++impl_->read_by_master[static_cast<std::size_t>(i)];
		}
	}

	for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; ++i) {
		auto &port = interconnect.write_ports[i];
		auto &l = impl_->write_latch[static_cast<std::size_t>(i)];
		if (l.valid && port.req.ready) {
			impl_->write_inflight[static_cast<std::size_t>(i)]
								[static_cast<std::size_t>(l.tag)] = true;
			l.valid = false;
		}
		if (port.resp.valid) {
			const uint8_t t = static_cast<uint8_t>(port.resp.id) & 0xFu;
			impl_->write_inflight[static_cast<std::size_t>(i)]
								[static_cast<std::size_t>(t)] = false;
			impl_->write_resp_valid[static_cast<std::size_t>(i)][t] = true;
			++write_req_count_;
			++impl_->write_by_master[static_cast<std::size_t>(i)];
		}
	}

	interconnect.comb_inputs();
	router.comb_inputs(interconnect.axi_io, ddr.io, mmio.io);
	ddr.comb_inputs();
	mmio.comb_inputs();

	interconnect.seq();
	router.seq(interconnect.axi_io, ddr.io, mmio.io);
	ddr.seq();
	mmio.seq();
	++sim_time;
}

bool AxiLiteBus::read32(uint64_t addr, uint32_t &data) {
	for (int cyc = 0; cyc < kBlockingMaxCycles; ++cyc) {
		cycle_begin();
		(void)request_read32(ReadMasterId::DCache, 0, addr);
		cycle_end();
		if (poll_read32(ReadMasterId::DCache, 0, data)) {
			return true;
		}
	}
	data = 0;
	return false;
}

bool AxiLiteBus::write32(uint64_t addr, uint32_t data, uint8_t wmask) {
	for (int cyc = 0; cyc < kBlockingMaxCycles; ++cyc) {
		cycle_begin();
		(void)request_write32(WriteMasterId::DCache, 0, addr, data, wmask);
		cycle_end();
		if (poll_write32(WriteMasterId::DCache, 0)) {
			return true;
		}
	}
	return false;
}

uint64_t AxiLiteBus::read_req_count(ReadMasterId master) const {
	if (impl_ == nullptr) {
		return 0;
	}
	const uint8_t idx = static_cast<uint8_t>(master);
	if (idx >= impl_->read_by_master.size()) {
		return 0;
	}
	return impl_->read_by_master[idx];
}

uint64_t AxiLiteBus::write_req_count(WriteMasterId master) const {
	if (impl_ == nullptr) {
		return 0;
	}
	const uint8_t idx = static_cast<uint8_t>(master);
	if (idx >= impl_->write_by_master.size()) {
		return 0;
	}
	return impl_->write_by_master[idx];
}

bool ICacheSimple::request_fetch32(uint64_t pc, uint8_t tag) {
	if (axi_ == nullptr) {
		return false;
	}
	if (!ensure_translation(pc, tag)) {
		return false;
	}
	return axi_->request_read32(ReadMasterId::ICache, tag, pc);
}

bool ICacheSimple::poll_fetch32(uint8_t tag, uint32_t &inst) {
	if (axi_ == nullptr) {
		inst = 0;
		return false;
	}
	return axi_->poll_read32(ReadMasterId::ICache, tag, inst);
}

bool ICacheSimple::fetch32(uint64_t pc, uint32_t &inst) {
	if (axi_ == nullptr) {
		inst = 0;
		return false;
	}
	for (int cyc = 0; cyc < kBlockingMaxCycles; ++cyc) {
		axi_->cycle_begin();
		(void)request_fetch32(pc, 0);
		axi_->cycle_end();
		if (poll_fetch32(0, inst)) {
			return true;
		}
	}
	inst = 0;
	return false;
}

void ICacheSimple::reset() {
	itlb_pages_.clear();
	for (std::size_t i = 0; i < ptw_wait_.size(); ++i) {
		ptw_wait_[i] = false;
		ptw_vpn_[i] = 0;
	}
}

bool ICacheSimple::ensure_translation(uint64_t pc, uint8_t tag) {
	if (axi_ == nullptr) {
		return false;
	}

	const uint8_t t = static_cast<uint8_t>(tag & 0xFu);
	const uint64_t vpn = pc >> kPageShift;
	if (itlb_pages_.find(vpn) != itlb_pages_.end()) {
		return true;
	}

	if (ptw_wait_[t] && ptw_vpn_[t] != vpn) {
		ptw_wait_[t] = false;
	}
	if (!ptw_wait_[t]) {
		if (axi_->request_read32(ReadMasterId::Ptw, t, synthetic_pte_addr(vpn))) {
			ptw_wait_[t] = true;
			ptw_vpn_[t] = vpn;
		}
	}

	uint32_t pte = 0;
	if (ptw_wait_[t] && axi_->poll_read32(ReadMasterId::Ptw, t, pte)) {
		(void)pte;
		ptw_wait_[t] = false;
		itlb_pages_.insert(vpn);
	}

	return itlb_pages_.find(vpn) != itlb_pages_.end();
}

bool DCacheSimple::request_load32(uint64_t addr, uint8_t tag) {
	if (axi_ == nullptr) {
		return false;
	}
	if (!ensure_translation(addr, tag)) {
		return false;
	}
	return axi_->request_read32(ReadMasterId::DCache, tag, addr);
}

bool DCacheSimple::poll_load32(uint8_t tag, uint32_t &data) {
	if (axi_ == nullptr) {
		data = 0;
		return false;
	}
	return axi_->poll_read32(ReadMasterId::DCache, tag, data);
}

bool DCacheSimple::request_store32(uint64_t addr, uint32_t data, uint8_t wmask,
					   uint8_t tag) {
	if (axi_ == nullptr) {
		return false;
	}
	if (!ensure_translation(addr, tag)) {
		return false;
	}
	return axi_->request_write32(WriteMasterId::DCache, tag, addr, data, wmask);
}

bool DCacheSimple::poll_store32(uint8_t tag) {
	if (axi_ == nullptr) {
		return false;
	}
	return axi_->poll_write32(WriteMasterId::DCache, tag);
}

bool DCacheSimple::load32(uint64_t addr, uint32_t &data) {
	if (axi_ == nullptr) {
		data = 0;
		return false;
	}
	for (int cyc = 0; cyc < kBlockingMaxCycles; ++cyc) {
		axi_->cycle_begin();
		(void)request_load32(addr, 0);
		axi_->cycle_end();
		if (poll_load32(0, data)) {
			return true;
		}
	}
	data = 0;
	return false;
}

bool DCacheSimple::store32(uint64_t addr, uint32_t data, uint8_t wmask) {
	if (axi_ == nullptr) {
		return false;
	}
	for (int cyc = 0; cyc < kBlockingMaxCycles; ++cyc) {
		axi_->cycle_begin();
		(void)request_store32(addr, data, wmask, 0);
		axi_->cycle_end();
		if (poll_store32(0)) {
			return true;
		}
	}
	return false;
}

void DCacheSimple::reset() {
	dtlb_pages_.clear();
	for (std::size_t i = 0; i < ptw_wait_.size(); ++i) {
		ptw_wait_[i] = false;
		ptw_vpn_[i] = 0;
	}
}

bool DCacheSimple::ensure_translation(uint64_t addr, uint8_t tag) {
	if (axi_ == nullptr) {
		return false;
	}

	const uint8_t t = static_cast<uint8_t>(tag & 0xFu);
	const uint64_t vpn = addr >> kPageShift;
	if (dtlb_pages_.find(vpn) != dtlb_pages_.end()) {
		return true;
	}

	if (ptw_wait_[t] && ptw_vpn_[t] != vpn) {
		ptw_wait_[t] = false;
	}
	if (!ptw_wait_[t]) {
		if (axi_->request_read32(ReadMasterId::Ptw, t, synthetic_pte_addr(vpn))) {
			ptw_wait_[t] = true;
			ptw_vpn_[t] = vpn;
		}
	}

	uint32_t pte = 0;
	if (ptw_wait_[t] && axi_->poll_read32(ReadMasterId::Ptw, t, pte)) {
		(void)pte;
		ptw_wait_[t] = false;
		dtlb_pages_.insert(vpn);
	}

	return dtlb_pages_.find(vpn) != dtlb_pages_.end();
}

} // namespace memsys
