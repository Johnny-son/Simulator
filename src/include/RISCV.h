#pragma once
#include <string>

#define INST_EBREAK 0x00100073
#define INST_ECALL 0x00000073
#define INST_MRET 0x30200073
#define INST_SRET 0x10200073
#define INST_WFI 0x10500073
#define INST_NOP 0x00000013

const std::string reg_names[32] = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "fp", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

const std::string csr_names[21] = {
    "mtvec",   "mepc",    "mcause",  "mie",  "mip",   "mtval",   "mscratch",
    "mstatus", "mideleg", "medeleg", "sepc", "stvec", "scause",  "sscratch",
    "stval",   "sstatus", "sie",     "sip",  "satp",  "mhartid", "misa"};