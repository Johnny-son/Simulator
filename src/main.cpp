#include "include/SimCpu.h"
#include "front-end/include/Decode_riscv.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct RunOptions {
	std::string program_path;
	uint64_t max_cycles = 2000;
	bool trace = false;
};

void print_usage(const char *argv0) {
	std::cout << "Usage: " << argv0
			  << " [--program <hex_file>] [--max-cycles <N>] [--trace]\n";
	std::cout << "  --program <hex_file>  One 32-bit instruction per line (hex)\n";
	std::cout << "  --max-cycles <N>      Stop after N cycles if not finished\n";
	std::cout << "  --trace               Print per-cycle progress\n";
}

bool parse_u64(const std::string &s, uint64_t &out) {
	try {
		size_t used = 0;
		out = std::stoull(s, &used, 10);
		return used == s.size();
	} catch (...) {
		return false;
	}
}

bool parse_args(int argc, char **argv, RunOptions &opt) {
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--program") {
			if (i + 1 >= argc) {
				std::cerr << "missing value for --program\n";
				return false;
			}
			opt.program_path = argv[++i];
		} else if (a == "--max-cycles") {
			if (i + 1 >= argc) {
				std::cerr << "missing value for --max-cycles\n";
				return false;
			}
			uint64_t v = 0;
			if (!parse_u64(argv[++i], v) || v == 0) {
				std::cerr << "invalid --max-cycles value\n";
				return false;
			}
			opt.max_cycles = v;
		} else if (a == "--trace") {
			opt.trace = true;
		} else if (a == "-h" || a == "--help") {
			print_usage(argv[0]);
			return false;
		} else {
			std::cerr << "unknown argument: " << a << "\n";
			return false;
		}
	}
	return true;
}

uint32_t encode_i(uint32_t opcode, uint32_t rd, uint32_t funct3, uint32_t rs1,
				 int32_t imm12) {
	const uint32_t imm = static_cast<uint32_t>(imm12) & 0xFFFu;
	return (imm << 20) | ((rs1 & 0x1Fu) << 15) | ((funct3 & 0x7u) << 12) |
		   ((rd & 0x1Fu) << 7) | (opcode & 0x7Fu);
}

uint32_t encode_r(uint32_t opcode, uint32_t rd, uint32_t funct3, uint32_t rs1,
				 uint32_t rs2, uint32_t funct7) {
	return ((funct7 & 0x7Fu) << 25) | ((rs2 & 0x1Fu) << 20) |
		   ((rs1 & 0x1Fu) << 15) | ((funct3 & 0x7u) << 12) |
		   ((rd & 0x1Fu) << 7) | (opcode & 0x7Fu);
}

uint32_t encode_s(uint32_t opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2,
				 int32_t imm12) {
	const uint32_t imm = static_cast<uint32_t>(imm12) & 0xFFFu;
	const uint32_t imm_11_5 = (imm >> 5) & 0x7Fu;
	const uint32_t imm_4_0 = imm & 0x1Fu;
	return (imm_11_5 << 25) | ((rs2 & 0x1Fu) << 20) | ((rs1 & 0x1Fu) << 15) |
		   ((funct3 & 0x7u) << 12) | (imm_4_0 << 7) | (opcode & 0x7Fu);
}

std::vector<uint32_t> built_in_program() {
	// Reuse opcode naming from the reference simulator (number_* enum in RISCV.h).
	std::vector<uint32_t> p;
	p.push_back(encode_i(number_7_opcode_addi, 1, 0x0, 0, 5)); // addi x1,x0,5
	p.push_back(encode_i(number_7_opcode_addi, 2, 0x0, 0, 7)); // addi x2,x0,7
	p.push_back(encode_r(number_8_opcode_add, 3, 0x0, 1, 2, 0x00)); // add x3,x1,x2
	p.push_back(encode_s(number_6_opcode_sb, 0x2, 0, 3, 0)); // sw x3,0(x0)
	p.push_back(encode_i(number_5_opcode_lb, 4, 0x2, 0, 0)); // lw x4,0(x0)
	p.push_back(encode_i(number_7_opcode_addi, 5, 0x0, 4, 1)); // addi x5,x4,1
	p.push_back(INST_EBREAK); // visible end marker in trace
	return p;
}

bool parse_hex_u32(const std::string &tok, uint32_t &out) {
	try {
		size_t used = 0;
		unsigned long v = std::stoul(tok, &used, 16);
		if (used != tok.size() || v > 0xFFFFFFFFul) {
			return false;
		}
		out = static_cast<uint32_t>(v);
		return true;
	} catch (...) {
		return false;
	}
}

std::string strip_comment(const std::string &line) {
	size_t p = line.find('#');
	size_t q = line.find("//");
	size_t cut = std::string::npos;
	if (p != std::string::npos) {
		cut = p;
	}
	if (q != std::string::npos) {
		cut = (cut == std::string::npos) ? q : std::min(cut, q);
	}
	return (cut == std::string::npos) ? line : line.substr(0, cut);
}

bool load_program_hex(const std::string &path, std::vector<uint32_t> &out) {
	std::ifstream fin(path);
	if (!fin.is_open()) {
		std::cerr << "failed to open program file: " << path << "\n";
		return false;
	}

	std::string line;
	uint64_t line_no = 0;
	while (std::getline(fin, line)) {
		++line_no;
		const std::string body = strip_comment(line);
		std::istringstream iss(body);
		std::string tok;
		if (!(iss >> tok)) {
			continue;
		}
		if (tok.rfind("0x", 0) == 0 || tok.rfind("0X", 0) == 0) {
			tok = tok.substr(2);
		}
		uint32_t inst = 0;
		if (!parse_hex_u32(tok, inst)) {
			std::cerr << "bad hex at line " << line_no << ": " << line << "\n";
			return false;
		}
		out.push_back(inst);
	}
	return !out.empty();
}

void drive_boot_request(FrontTopIn &in, uint64_t base_pc, size_t program_size) {
	in = {};
	in.valid[0] = 1;
	in.pc[0] = base_pc;
	in.limit_en = 1;
	in.limit_pc = base_pc + static_cast<uint64_t>(program_size * 4ull);
}

void dump_uop_mapping(const std::vector<uint32_t> &program, uint64_t base_pc) {
	DecodeRiscv decoder;
	std::cout << "[Decode] RISC-V -> FrontUop\n";
	for (size_t i = 0; i < program.size(); ++i) {
		const uint64_t pc = base_pc + static_cast<uint64_t>(i * 4ull);
		const FrontUop u = decoder.decode_lane(program[i], pc, 0);
		std::cout << "  idx=" << i << " pc=0x" << std::hex << pc << " inst=0x"
				  << std::setw(8) << std::setfill('0') << program[i] << std::dec
				  << " op_class=" << static_cast<uint32_t>(u.op_class)
				  << " sub_op=" << static_cast<uint32_t>(u.sub_op)
				  << " src(" << static_cast<uint32_t>(u.src_areg[0]) << ","
				  << static_cast<uint32_t>(u.src_areg[1]) << ","
				  << static_cast<uint32_t>(u.src_areg[2]) << ")"
				  << " dst=" << static_cast<uint32_t>(u.dst_areg) << "\n";
	}
}

} // namespace

int main(int argc, char **argv) {
	RunOptions opt;
	if (!parse_args(argc, argv, opt)) {
		return 1;
	}

	std::vector<uint32_t> program;
	if (!opt.program_path.empty()) {
		if (!load_program_hex(opt.program_path, program)) {
			return 1;
		}
	} else {
		program = built_in_program();
	}

	const uint64_t base_pc = 0x80000000ull;
	dump_uop_mapping(program, base_pc);

	SimContext ctx;
	SimCpu cpu(&ctx);
	cpu.reset();
	cpu.mem_reset();
	cpu.load_program(program, base_pc);
	uint64_t last_commit = 0;

	for (uint64_t cyc = 0; cyc < opt.max_cycles; ++cyc) {
		drive_boot_request(cpu.front_top_.in, base_pc, program.size());

		cpu.tick();

		const uint64_t delta_commit = ctx.committed_inst - last_commit;
		last_commit = ctx.committed_inst;

		if (opt.trace) {
			uint32_t fe_out_valid = 0;
			uint32_t be_in_valid = 0;
			for (int i = 0; i < FETCH_WIDTH; ++i) {
				if (cpu.front_top_.out.enq2buf.valid[i]) {
					++fe_out_valid;
				}
				if (cpu.back_top_.in.enq2buf.valid[i]) {
					++be_in_valid;
				}
			}
			std::cout << "[Cycle " << cyc << "] commit_delta=" << delta_commit
					  << " commit_total=" << ctx.committed_inst
					  << " front_ready=" << static_cast<uint32_t>(cpu.back_top_.out.enq2dec.ready)
					  << " flush=" << static_cast<uint32_t>(cpu.back_top_.out.rob_bcast.flush)
					  << " fe_out_valid=" << fe_out_valid
					  << " fe_pc0=0x" << std::hex
					  << static_cast<uint64_t>(cpu.front_top_.out.enq2buf.uop[0].pc)
					  << std::dec
					  << " be_in_valid=" << be_in_valid
					  << " ren2dis_valid=" << cpu.back_top_.debug_ren2dis_valid_count()
					  << " dis2rob_valid=" << cpu.back_top_.debug_dis2rob_valid_count()
					  << " wb2rob_valid=" << cpu.back_top_.debug_wb2rob_valid_count()
					  << " rob_commit0=" << static_cast<uint32_t>(cpu.back_top_.out.rob_commit.commit_entry[0].valid)
					  << " commit_pc=0x" << std::hex
					  << static_cast<uint64_t>(cpu.back_top_.out.rob_commit.commit_entry[0].pc)
					  << std::dec
					  << " commit_dst="
					  << static_cast<uint32_t>(cpu.back_top_.out.rob_commit.commit_entry[0].dst_areg)
					  << " commit_val="
					  << static_cast<uint64_t>(cpu.back_top_.out.rob_commit.commit_entry[0].dst_value)
					  << " commit_store="
					  << static_cast<uint32_t>(cpu.back_top_.out.rob_commit.commit_entry[0].is_store)
					  << "\n";
		}

		if (ctx.committed_inst >= program.size()) {
			std::cout << "[Done] committed=" << ctx.committed_inst
					  << " cycles=" << ctx.cycle
					  << " axi_read=" << cpu.axi_read_req_count()
					  << " axi_write=" << cpu.axi_write_req_count()
					  << " icache_read=" << cpu.axi_icache_read_req_count()
					  << " dcache_read=" << cpu.axi_dcache_read_req_count()
					  << " ptw_read=" << cpu.axi_ptw_read_req_count() << "\n";
			return 0;
		}
	}

	std::cout << "[Timeout] committed=" << ctx.committed_inst
			  << " cycles=" << ctx.cycle
			  << " axi_read=" << cpu.axi_read_req_count()
			  << " axi_write=" << cpu.axi_write_req_count()
			  << " icache_read=" << cpu.axi_icache_read_req_count()
			  << " dcache_read=" << cpu.axi_dcache_read_req_count()
			  << " ptw_read=" << cpu.axi_ptw_read_req_count()
			  << " (max_cycles reached)\n";
	return 2;
}
