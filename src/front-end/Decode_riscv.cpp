#include "include/Decode_riscv.h"

namespace {

void fill_rr_fields(FrontUop &uop, const RiscvDecodedInst &dec) {
	uop.src_en[0] = 1;
	uop.src_areg[0] = dec.rs1;
	uop.src_en[1] = 1;
	uop.src_areg[1] = dec.rs2;
	uop.dst_en = 1;
	uop.dst_areg = dec.rd;
}

} // namespace

FrontUop DecodeRiscv::decode_lane(wire<32> inst, wire<64> pc,
					 wire<1> pred_taken) const {
	FrontUop uop;
	uop.valid = 1;
	uop.isa_tag = static_cast<uint8_t>(IsaTag::RISCV);
	uop.dbg.raw_inst = inst;
	uop.dbg.pc = pc;
	uop.pc = pc;
	uop.pred_taken = pred_taken;

	const RiscvDecodedInst dec = decode_riscv_basic(inst, pc);
	uop.sub_op = static_cast<uint16_t>(RiscvSubOp::NOP);
	uop.op_class = static_cast<uint8_t>(OpClass::NOP);
	uop.except_valid = dec.illegal;
	uop.except_code = dec.illegal ? 2 : 0;

	switch (dec.opcode) {
	case 0x37: // LUI
		uop.sub_op = static_cast<uint16_t>(RiscvSubOp::LUI);
		uop.op_class = static_cast<uint8_t>(OpClass::INT);
		uop.dst_en = 1;
		uop.dst_areg = dec.rd;
		uop.imm = dec.imm_u;
		break;
	case 0x17: // AUIPC
		uop.sub_op = static_cast<uint16_t>(RiscvSubOp::AUIPC);
		uop.op_class = static_cast<uint8_t>(OpClass::INT);
		uop.dst_en = 1;
		uop.dst_areg = dec.rd;
		uop.imm = dec.imm_u;
		uop.src_en[0] = 1;
		uop.src_areg[0] = 0;
		break;
	case 0x6F: // JAL
		uop.sub_op = static_cast<uint16_t>(RiscvSubOp::JAL);
		uop.op_class = static_cast<uint8_t>(OpClass::BR);
		uop.is_branch = 1;
		uop.is_call = (dec.rd == 1 || dec.rd == 5);
		uop.dst_en = 1;
		uop.dst_areg = dec.rd;
		uop.imm = dec.imm_j;
		uop.pred_target = pc + dec.imm_j;
		break;
	case 0x67: // JALR
		uop.sub_op = static_cast<uint16_t>(RiscvSubOp::JALR);
		uop.op_class = static_cast<uint8_t>(OpClass::BR);
		uop.is_branch = 1;
		uop.is_call = (dec.rd == 1 || dec.rd == 5);
		uop.is_ret = (dec.rs1 == 1 || dec.rs1 == 5) && dec.rd == 0;
		uop.dst_en = 1;
		uop.dst_areg = dec.rd;
		uop.src_en[0] = 1;
		uop.src_areg[0] = dec.rs1;
		uop.imm = dec.imm_i;
		break;
	case 0x63: // BRANCH
		uop.op_class = static_cast<uint8_t>(OpClass::BR);
		uop.is_branch = 1;
		uop.src_en[0] = 1;
		uop.src_areg[0] = dec.rs1;
		uop.src_en[1] = 1;
		uop.src_areg[1] = dec.rs2;
		uop.imm = dec.imm_b;
		uop.pred_target = pc + dec.imm_b;
		switch (dec.funct3) {
		case 0x0: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::BEQ); break;
		case 0x1: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::BNE); break;
		case 0x4: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::BLT); break;
		case 0x5: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::BGE); break;
		case 0x6: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::BLTU); break;
		case 0x7: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::BGEU); break;
		default: uop.except_valid = 1; uop.except_code = 2; break;
		}
		break;
	case 0x03: // LOAD
		uop.op_class = static_cast<uint8_t>(OpClass::MEM);
		uop.is_load = 1;
		uop.src_en[0] = 1;
		uop.src_areg[0] = dec.rs1;
		uop.dst_en = 1;
		uop.dst_areg = dec.rd;
		uop.imm = dec.imm_i;
		switch (dec.funct3) {
		case 0x0: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::LB); break;
		case 0x1: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::LH); break;
		case 0x2: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::LW); break;
		case 0x4: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::LBU); break;
		case 0x5: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::LHU); break;
		default: uop.except_valid = 1; uop.except_code = 2; break;
		}
		break;
	case 0x23: // STORE
		uop.op_class = static_cast<uint8_t>(OpClass::MEM);
		uop.is_store = 1;
		uop.src_en[0] = 1;
		uop.src_areg[0] = dec.rs1;
		uop.src_en[1] = 1;
		uop.src_areg[1] = dec.rs2;
		uop.imm = dec.imm_s;
		switch (dec.funct3) {
		case 0x0: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::SB); break;
		case 0x1: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::SH); break;
		case 0x2: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::SW); break;
		default: uop.except_valid = 1; uop.except_code = 2; break;
		}
		break;
	case 0x13: // OP-IMM
		uop.op_class = static_cast<uint8_t>(OpClass::INT);
		uop.src_en[0] = 1;
		uop.src_areg[0] = dec.rs1;
		uop.dst_en = 1;
		uop.dst_areg = dec.rd;
		uop.imm = dec.imm_i;
		switch (dec.funct3) {
		case 0x0: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::ADDI); break;
		case 0x2: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::SLTI); break;
		case 0x3: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::SLTIU); break;
		case 0x4: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::XORI); break;
		case 0x6: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::ORI); break;
		case 0x7: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::ANDI); break;
		case 0x1: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::SLLI); break;
		case 0x5:
			uop.sub_op = (dec.funct7 == 0x20)
						 ? static_cast<uint16_t>(RiscvSubOp::SRAI)
						 : static_cast<uint16_t>(RiscvSubOp::SRLI);
			break;
		default: uop.except_valid = 1; uop.except_code = 2; break;
		}
		break;
	case 0x33: // OP
		uop.op_class = static_cast<uint8_t>(OpClass::INT);
		fill_rr_fields(uop, dec);
		switch (dec.funct3) {
		case 0x0:
			uop.sub_op = (dec.funct7 == 0x20)
						 ? static_cast<uint16_t>(RiscvSubOp::SUB)
						 : static_cast<uint16_t>(RiscvSubOp::ADD);
			break;
		case 0x1: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::SLL); break;
		case 0x2: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::SLT); break;
		case 0x3: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::SLTU); break;
		case 0x4: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::XOR); break;
		case 0x5:
			uop.sub_op = (dec.funct7 == 0x20)
						 ? static_cast<uint16_t>(RiscvSubOp::SRA)
						 : static_cast<uint16_t>(RiscvSubOp::SRL);
			break;
		case 0x6: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::OR); break;
		case 0x7: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::AND); break;
		default: uop.except_valid = 1; uop.except_code = 2; break;
		}
		break;
	case 0x0F:
		uop.op_class = static_cast<uint8_t>(OpClass::CSR);
		uop.sub_op = (dec.funct3 == 0x1)
					 ? static_cast<uint16_t>(RiscvSubOp::FENCE_I)
					 : static_cast<uint16_t>(RiscvSubOp::FENCE);
		break;
	case 0x73:
		uop.op_class = static_cast<uint8_t>(OpClass::CSR);
		uop.src_en[0] = 1;
		uop.src_areg[0] = dec.rs1;
		uop.dst_en = (dec.rd != 0);
		uop.dst_areg = dec.rd;
		switch (dec.funct3) {
		case 0x0:
			if ((inst >> 20) == 0) {
				uop.sub_op = static_cast<uint16_t>(RiscvSubOp::ECALL);
			} else if ((inst >> 20) == 1) {
				uop.sub_op = static_cast<uint16_t>(RiscvSubOp::EBREAK);
			} else {
				uop.except_valid = 1;
				uop.except_code = 2;
			}
			break;
		case 0x1: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::CSRRW); break;
		case 0x2: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::CSRRS); break;
		case 0x3: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::CSRRC); break;
		case 0x5: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::CSRRWI); break;
		case 0x6: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::CSRRSI); break;
		case 0x7: uop.sub_op = static_cast<uint16_t>(RiscvSubOp::CSRRCI); break;
		default: uop.except_valid = 1; uop.except_code = 2; break;
		}
		break;
	default:
		uop.except_valid = 1;
		uop.except_code = 2;
		break;
	}

	return uop;
}

