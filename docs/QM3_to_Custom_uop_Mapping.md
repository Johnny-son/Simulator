# 启蒙三号（QM3）到自定义uop映射草案

## 1. 目标与范围

本文基于当前代码实现，给出“启蒙三号指令语义（RISC-V前端） -> 自定义uop指令文档”映射建议。

- QM3指令粗分类来源：IDU 的 InstType 归类。
- QM3实际分解来源：Dispatch 的 decompose_inst。
- 算术/分支/访存细语义来源：EXU/LSU。

说明：这是一份工程映射草案，分为三类：
1. 可直接映射
2. 可通过多条uop组合映射
3. 当前自定义uop集合缺失（需补充新uop）

## 2. QM3现有分解模型（代码事实）

QM3并非“1条ISA指令=1条uop”，而是两级分解：

1. IDU先归类为 InstType（ADD/BR/LOAD/STORE/JAL/JALR/MUL/DIV/AMO/CSR/FP...）。
2. Dispatch再拆成执行uop：
   - STORE -> STA + STD（2条）
   - JAL/JALR -> ADD + JUMP（2条）
   - AMO -> LR:1条，SC/其他RMW:3条

因此，本映射文档也按“单条或多条自定义uop”给出。

## 3. 映射总表（按QM3指令族）

### 3.1 RV32I 基础整数

| QM3指令族 | QM3内部类型 | 建议映射到自定义uop | 映射方式 | 备注 |
|---|---|---|---|---|
| LUI | ADD | addi_32U（rj=x0, imm=immU） | 1条 | 依赖前端把 immU 组织为与LUI一致语义 |
| AUIPC | ADD | pcaddi_32 | 1条 | 语义直接匹配 |
| JAL | JAL | pcaddi_32 + 条件恒真分支uop | 2条 | 第一条写回rd=pc+4，第二条完成跳转 |
| JALR | JALR | addi_32U(写回pc+4) + 间接跳转uop | 2条 | 需有“寄存器间接跳转”能力 |
| BEQ/BNE/BLT/BGE/BLTU/BGEU | BR | beq_w/bne_w/blt_w/bge_w/bltu_w/bgeu_w | 1条 | 直接可映射 |
| LB/LH/LW | LOAD | ld_b_32 / ld_h_32 / ld_w_32 | 1条 | 符号扩展匹配 |
| LBU/LHU | LOAD | ld_bu_32 / ld_hu_32 | 1条 | 零扩展匹配 |
| SB/SH/SW | STORE | st_b_32 / st_h_32 / st_w_32（或拆为地址+数据） | 1或2条 | 若后端保持STA/STD结构，建议定义显式STA/STD风格uop |
| ADD/ADDI | ADD | add_32U / addi_32U | 1条 | RV32位宽下结果按32位回写 |
| SUB | ADD | sub_32U | 1条 | 位级语义一致 |
| XOR/XORI | ADD | xor_32 / xori_32 | 1条 | 直接可映射 |
| OR/ORI | ADD | or_32 / ori_32 | 1条 | 直接可映射 |
| AND/ANDI | ADD | and_32 / andi_32 | 1条 | 直接可映射 |
| SLL/SLLI | ADD | shl_32U / shli_32_8 | 1条 | 立即数移位可用 *_8 形式承载 |
| SRL/SRLI | ADD | shr_32U / shri_32_8 | 1条 | 直接可映射 |
| SRA/SRAI | ADD | sar_32U / sari_32_8 | 1条 | 直接可映射 |
| FENCE | NOP | NOP（可不发射） | 0或1条 | QM3中普通FENCE按NOP处理 |
| FENCE.I | FENCE_I | 专用控制uop（建议保留） | 1条 | 当前自定义uop文档未列该控制类 |
| ECALL/EBREAK/WFI/MRET/SRET/SFENCE.VMA | 控制类 | 专用控制uop（建议保留） | 1条 | 当前自定义uop文档未覆盖系统控制语义 |

### 3.2 RV32M 乘除扩展

| QM3指令族 | QM3内部类型 | 建议映射到自定义uop | 映射方式 | 备注 |
|---|---|---|---|---|
| MUL | MUL | imul_32 | 1条 | 可直接映射 |
| MULH/MULHU/MULHSU | MUL | 需新增高半乘积uop | 1条 | 当前自定义uop仅有 imul_32，不含高位乘积变体 |
| DIV/DIVU | DIV | div_64_32 或新增 div_32_32[u/s] | 1条 | 现有 div_64_32 需额外约束输入/截断 |
| REM/REMU | DIV | mod_64_32 或新增 mod_32_32[u/s] | 1条 | 同上，建议补32位原生变体 |

### 3.3 RV32A 原子扩展（QM3中重点）

QM3中AMO会拆成 1 或 3 条（LOAD + STA + STD）。

| AMO类别 | QM3分解 | 建议映射到自定义uop | 备注 |
|---|---|---|---|
| LR.W | LOAD | ld_w_32 | 需要保留reservation语义 |
| SC.W | INT占位 + STA + STD | 条件存储序列（建议新增 sc_w 专用uop 或原子事务标记） | 需要返回0/1并清reservation |
| AMOSWAP/AMOADD/AMOXOR/AMOAND/AMOOR/AMOMIN/MAX/MINU/MAXU | LOAD + STA + STD | ld_w_32 + 运算uop + st_w_32（原子事务） | 需要“原子不可分割”与内存序保证 |

结论：AMO可映射，但必须给uop增加“原子事务语义位”或引入专用原子uop；仅靠普通ld/st无法保证一致性。

### 3.4 RV32F（QM3当前实现）

QM3当前FPU只实现了单精度：FADD/FSUB/FMUL。

| QM3指令族 | QM3内部类型 | 建议映射到自定义uop | 备注 |
|---|---|---|---|
| FADD.S | FP | fadd_s_rem（或新增非rem版fadd_s） | 若使用_rem，需确认“保留高位”是否符合QM3寄存器模型 |
| FSUB.S | FP | fsub_s_rem（或新增非rem版fsub_s） | 同上 |
| FMUL.S | FP | fmul_s_rem（或新增非rem版fmul_s） | 同上 |

## 4. 与当前自定义uop文档的关键差距

以下能力在QM3中有明确需求，但在当前自定义uop文档中未完整覆盖或语义不充分：

1. 跳转类
- 需要显式无条件跳转与寄存器间接跳转语义（JAL/JALR第二条）。

2. 系统控制类
- FENCE.I、ECALL、EBREAK、WFI、MRET、SRET、SFENCE.VMA、CSR读改写。

3. RV32M完整性
- MULH/MULHU/MULHSU、DIVU/REMU的32位原生语义变体。

4. 原子事务语义
- LR/SC reservation、SC成功失败返回值、AMO不可分割提交。

5. 比较置位类
- SLT/SLTU/SLTI/SLTIU（可新增 setlt_32S / setltu_32U 一类uop）。

## 5. 推荐的最小补充uop集合

为让QM3可较低成本映射到该uop体系，建议最少补充：

1. 跳转
- jmp_imm
- jmp_reg

2. 系统
- csr_rw / csr_rs / csr_rc
- ecall / ebreak / wfi / mret / sret / sfence_vma / fence_i

3. 比较置位
- slt_32S
- sltu_32U

4. 乘除补齐
- mulh_32S
- mulhu_32U
- mulhsu_32
- div_32U / mod_32U

5. 原子
- lr_w
- sc_w
- amo_{add,xor,and,or,min,max,minu,maxu}_w

## 6. 可执行映射策略（建议）

1. 保持QM3现有两级结构：
- 第一级：RISC-V decode -> 抽象InstType
- 第二级：InstType -> 自定义uop序列

2. 对于“缺失语义”，优先补uop而不是在单个uop里塞过多隐式行为。

3. 映射验证按三层做回归：
- 指令级对拍（ISA结果）
- uop级trace（每条uop输入/输出）
- 异常与原子语义（页故障、SC成功率、flush/mispred恢复）

---

如果你认可这版草案，下一步我可以继续给出“按QM3源码可直接落地的映射配置表（YAML/CSV）”，把每个 opcode/funct3/funct7 的映射目标写成机器可读格式，便于你接到解码器里直接使用。
