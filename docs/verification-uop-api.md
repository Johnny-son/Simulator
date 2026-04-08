# 验证框架对 uOP 小组的接口需求

> 本文档基于 `varification-design.md` 第二轮更新（spec change-20260408-013040）。
> 第二轮同步补全（spec change-20260408-165944）已加入：§四 uOP 元数据与 ROB 提交接口、§五 临时 uOP 基线、§六 RFLAGS 分类约定、§七 暂时缺失 uOP 类的处理约定、§3.5/§3.6/§3.7 三个新子节。

本文档由验证框架团队编写，面向 uOP 开发小组，说明**验证框架对 uOP 小组的直接接口需求**——即 uOP 小组需要定义和维护哪些数据结构和标记，以便验证框架能正确执行功能验证。

模拟器的框架接口（SimModule、Port、TimeBuffer、SIM_ASSERT 等）和 Decoder 模块的接入方式由后端开发小组提供，不在本文档范围内。

---

## 一、概述

验证框架负责验证模拟器整体的功能正确性——在模拟器执行程序时，逐条指令与外部参考模型比较提交结果。为了完成这项工作，验证框架需要 uOP 小组提供两类信息：

1. **ISA 架构状态的完整定义**：验证框架需要知道每种 ISA（RV64、x86-64）的架构状态有哪些字段（寄存器、CSR、控制寄存器等），以便在指令提交时提取和比较这些状态。
2. **特殊指令的标记信息**：某些指令（MMIO 访问、CSR/MSR 修改、融合指令、REP 前缀）需要验证框架采用特殊处理流程。uOP 小组需要告知验证框架哪些指令属于这些类别。

以下各节详细说明这两类需求的具体内容。

---

## 二、ISA 状态容器字段要求

以下结构体定义了每种 ISA 的架构状态。验证框架在每条指令提交时读取这些字段与参考模型比较。**uOP 小组负责定义和维护这些结构体。**

### 2.1 RISC-V（RV64）

#### 必需字段

```cpp
struct RV64_CPU_state {
    /*** 以下字段会被验证框架使用，不要修改顺序 ***/

    // 通用寄存器
    union { uint64_t _64; } gpr[32];    // x0-x31（x0 恒为 0）

    // 浮点寄存器（RV64F/D）
    union { uint64_t _64; } fpr[32];    // f0-f31

    // 程序计数器
    uint64_t pc;

    // 特权级
    uint64_t mode;                      // 0=U, 1=S, 3=M
    uint64_t v;                         // 虚拟化模式标志（0=非虚拟化, 1=虚拟化；不启用 H 扩展时恒为 0）

    // 基本 CSR（17 个）
    uint64_t mstatus;
    uint64_t sstatus;
    uint64_t mepc;
    uint64_t sepc;
    uint64_t mcause;
    uint64_t scause;
    uint64_t mtval;
    uint64_t stval;
    uint64_t mtvec;
    uint64_t stvec;
    uint64_t satp;
    uint64_t mip;
    uint64_t mie;
    uint64_t mscratch;
    uint64_t sscratch;
    uint64_t mideleg;
    uint64_t medeleg;

    // 存储写入（提交时逐条比较）
    uint64_t store_addr;
    uint64_t store_data;
    uint64_t store_mask;

    // ── 哨兵字段 ──
    uint64_t difftest_state_end;
};
```

#### 可选字段（按扩展需求在 `difftest_state_end` 之前添加）

**Hypervisor 扩展**（16 个字段）：

```
mtval2, mtinst, hstatus, hideleg, hedeleg, hcounteren,
htval, htinst, hgatp, vsstatus, vstvec, vsepc,
vscause, vstval, vsatp, vsscratch
```

**向量扩展 RVV**：

```cpp
union { uint64_t _64[VENUM64]; } vr[32];  // 32 个向量寄存器
uint64_t vstart;
uint64_t vxsat;
uint64_t vxrm;
uint64_t vcsr;
uint64_t vl;
uint64_t vtype;
uint64_t vlenb;
```

**LR/SC 保留状态**：

```cpp
bool     reserve_valid;
uint64_t reserve_addr;
```

### 2.2 x86-64

#### 必需字段

```cpp
struct x86_64_CPU_state {
    /*** 以下字段会被验证框架使用，不要修改顺序 ***/

    // 通用寄存器
    uint64_t gpr[16];                   // RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8-R15

    // 程序计数器
    uint64_t rip;

    // 标志寄存器
    uint64_t rflags;                    // CF, ZF, SF, OF, PF, AF 等

    // 特权级
    uint32_t cpl;                       // 当前特权级 Ring 0-3

    // 段寄存器
    uint16_t cs, ss, ds, es, fs, gs;

    // 控制寄存器
    uint64_t cr0;                       // 保护模式 / 分页控制
    uint64_t cr2;                       // 缺页线性地址
    uint64_t cr3;                       // PML4 基址
    uint64_t cr4;                       // 扩展特性

    // 存储写入
    uint64_t store_addr;
    uint64_t store_data;
    uint64_t store_mask;

    // ── 哨兵字段 ──
    uint64_t difftest_state_end;
};
```

#### 可选字段（按需在 `difftest_state_end` 之前添加）

- **`cr8`**：任务优先级寄存器

- **x87 FPU**：
  ```cpp
  uint8_t  fpu_stack[8][10];          // 8 个 80-bit 浮点栈寄存器
  uint16_t fpu_status;                // 浮点状态字
  uint16_t fpu_control;               // 浮点控制字
  uint16_t fpu_tag;                   // 浮点标签字
  ```

- **SSE**：
  ```cpp
  uint64_t xmm[16][2];               // 16 个 128-bit XMM 寄存器
  uint32_t mxcsr;                     // SSE 控制/状态寄存器
  ```

- **异常状态**：
  ```cpp
  uint32_t exception_num;             // 异常号
  uint32_t error_code;                // 错误码
  ```

### 2.3 哨兵字段说明

每个状态容器结构体的末尾有一个 `uint64_t difftest_state_end;` 字段，标记验证框架同步区域的终点。

- 在 `difftest_state_end` **之前**的所有字段会被验证框架使用。
- 在 `difftest_state_end` **之后**的字段不会被验证框架使用（可用于存放内部微架构状态、性能计数器等）。
- **新增字段必须插入在 `difftest_state_end` 之前**，否则验证框架无法感知到新增字段。
- **不要修改已有字段的顺序**，验证框架依赖字段的内存布局进行批量同步。

---

## 三、特殊指令标记规范

以下指令类型需要特殊标记，以便验证框架采用对应的处理流程。

### 3.1 MMIO 指令

访问设备映射地址（如 UART、PLIC、CLINT）的指令需在 uOP 中设置 `is_mmio` 标记。

- 设置条件：指令的访问地址落在 MMIO 地址范围内。
- 设置方式：在 uOP 中设置 `is_mmio = true`。
- 验证框架对 MMIO 指令采用特殊处理流程（详见 varification-design.md §2.6.2），因此正确标记至关重要。

> **补充（spec change-20260408-165944）**：此前的 `strictlyOrdered` 命名已在 varification-design.md 中废弃，统一使用 `is_mmio` 字段名。

### 3.2 CSR/MSR 修改指令清单

uOP 小组需要维护一份"哪些指令会修改 CSR/MSR"的事实清单。验证框架根据此清单决定如何处理这些指令。

**RISC-V 侧**：

| 指令 | 说明 |
|------|------|
| `ecall` | 环境调用，触发异常并修改 mcause/scause、mepc/sepc、mstatus 等 |
| `mret` | M 模式异常返回，修改 mstatus、pc |
| `sret` | S 模式异常返回，修改 sstatus、pc |
| `csrrw` / `csrrwi` | 原子读写 CSR |
| `csrrs` / `csrrsi` | 原子读并置位 CSR |
| `csrrc` / `csrrci` | 原子读并清位 CSR |

**x86-64 侧**：

| 指令 | 说明 |
|------|------|
| `MOV CR, reg` | 写控制寄存器（cr0/cr2/cr3/cr4/cr8） |
| `WRMSR` | 写模型特定寄存器 |
| `LGDT` | 加载全局描述符表寄存器 |
| `LIDT` | 加载中断描述符表寄存器 |
| `LLDT` | 加载局部描述符表寄存器 |
| `LTR` | 加载任务寄存器 |
| `MOV seg, reg` | 写段寄存器 |

> **补充（spec change-20260408-165944，对应 verification-requirments.md F3）**：在新模拟器后端中所有系统指令统一走 Sys/CSR pipe（参考 `verification-backend-api.md` §5），因此 uOP 小组实际向验证框架提供的不是"指令名清单"，而是 **uOP 子类型集合**——哪些 uOP 子类型属于 Sys/CSR 类。验证框架按 uOP 子类型过滤，不按宏指令编码匹配。RV64 启用 H 扩展时还需要把 H 扩展 CSR（hstatus、hideleg、hedeleg、…）和向量 CSR（vtype、vcsr、vl、vlenb 等）相关的 uOP 子类型一并加入这个集合。详见 verification-requirments.md §4.2 系统指令 CSR/MSR 跳过段。

#### 3.2.1 性能计数器 / 时间戳 CSR/MSR 跳过清单（spec change-20260408-165944，反向核对补全）

除了"修改 CSR/MSR 的指令"清单之外，uOP 小组还需要维护一份"**性能计数器 / 时间戳 CSR/MSR**"的事实清单。验证框架在比较时会跳过这些寄存器（DUT 与 REF 的微架构不同，性能计数值不可能一致），并把 DUT 当前值同步到 REF。

**RISC-V 侧需要跳过的 CSR**：

| CSR | 说明 |
|---|---|
| `mcycle` / `mcycleh` | 机器模式周期计数器 |
| `minstret` / `minstreth` | 机器模式已退役指令计数 |
| `mhpmcounter3` ~ `mhpmcounter31` | 硬件性能监控计数器 |
| `time` / `timeh` | 系统时间（取决于物理时间） |
| `cycle` / `cycleh`、`instret` / `instreth` | 用户/管理者模式只读视图 |

**x86-64 侧需要跳过的 MSR**：

| MSR | 说明 |
|---|---|
| `IA32_TIME_STAMP_COUNTER`（TSC） | 时间戳计数器 |
| `IA32_PERF_GLOBAL_CTRL`、`IA32_FIXED_CTR0-2`、`IA32_PERFEVTSEL0-3` 等 | 硬件性能监控 |
| `IA32_TSC_AUX` | TSC 辅助 ID |
| `IA32_APIC_BASE` | 本地 APIC 基地址（依赖外设配置） |

uOP 小组需要把这两份清单交付给验证框架（实现为 `PerfCntSkipList::initRV64()` / `initX86_64()`，参见 varification-design.md §2.6.6）。后续 ISA 扩展时按需追加。

→ 详见 varification-design.md §2.6.6、verification-requirments.md §4.2"性能计数器/时间戳跳过"。

### 3.3 融合指令

前端将两条相邻原始指令融合为一条内部指令时，需要通过 `is_fusion` 标记告知验证框架。

- 典型场景：比较+分支融合（如 x86-64 的 `CMP + JCC` 融合）。
- 验证框架需要此标记来正确处理"一条内部指令对应两条原始指令"的情况，会让 REF 连续执行两步（参见 varification-design.md §2.6.1）。

> **补充（spec change-20260408-165944，对应 verification-requirments.md F2）**：`is_fusion` / `macro_id` / `last_uop` 这三个字段属于 `verification-backend-api.md` §1 的统一 uop 字段集合。**字段存放位置**（DynInst 槽位 / ROB 条目位）由后端小组定义，**字段语义边界**（哪几个 uOP 属于同一宏指令、哪一个 uOP 是该宏指令的最后一条）由 uOP 小组定义。验证框架的 `shouldDiff()` 过滤逻辑只读 `is_microop` / `is_last_microop`，与具体的 uOP 类型枚举完全解耦。详见 varification-design.md §2.5 与 verification-requirments.md §4.2。

### 3.4 REP 前缀指令（x86-64）

REP/REPE/REPNE 前缀将字符串操作（MOVS、STOS、CMPS、SCAS 等）转为重复执行。验证框架需要知道 Decoder 采用哪种处理方式：

- **展开模式**：Decoder 将 REP 展开为 N 条 micro-op，通过 `is_microop` / `is_last_microop` 标志标记。验证框架在最后一条 micro-op 提交时触发比较。
- **单指令模式**：作为单条指令执行，验证框架按普通指令处理。

> **补充（spec change-20260408-165944）**：与 §3.3 相同——`is_microop` / `is_last_microop` 的字段位置由后端小组定义，语义边界（哪一条是 REP 序列的"最后一条"）由 uOP 小组在 Decoder 中正确设置。

uOP 小组需要告知验证框架采用了哪种模式（展开 vs 单指令）。

### 3.5 访存 uOP 的 mem_attr 标记（spec change-20260408-165944）

每个访存 uOP 必须在 Decoder 中标记 `mem_attr` 字段，取值集合（来自 `code/uOP` 字典的 4 类访存模式）：

- `BaseImm` —— 基址 + 立即数寻址。对应 `code/uOP` 中的 `ld_*_16/32`、`st_*_16/32`、`fld_*_16/32`、`fst_*_16/32`
- `BaseIndexShift` —— 基址 + 索引 + 缩放。对应 `ldx_*_shift`、`stx_*_shift`、`fldx_*_shift`、`vldx_shift`、`vstx_shift`
- `PCRel` —— PC 相对寻址。对应 `ldpc_*`、`stpc_*`、`fldpc_*`、`vldpc_*`、`vstpc_*`
- `PrePostUpdate` —— 前/后更新基址寄存器。对应 `ld_pre_*`、`ld_post_*`、`st_pre_*`、`st_post_*`、`st_npc_pre/post4/8`

→ 设计依据：varification-design.md §2.5 `MemAttr` 字段、附录 A.2 第 6 项。

### 3.6 浮点 uOP 的 fpr_writeback_mode 标记（spec change-20260408-165944）

每个浮点 uOP 必须在 Decoder 中标记 `fpr_writeback_mode` 字段，取值集合：

- `0 = full` —— 默认，写整个浮点寄存器。对应 `code/uOP` 中无 `_rem` / `_c` 后缀的浮点 uOP（如 `fcmp_s_s`）
- `1 = rem` —— 保留浮点寄存器高位，只写低 32/64 位。对应 `_rem` 后缀（如 `fadd_s_rem` / `fmul_d_rem` / `fcvt_s_d_rem`）
- `2 = clr` —— 清零浮点寄存器高位，写低 32/64 位。对应 `_c` 后缀（如 `fld_s_c` / `fldx_d_c` / `movgr2fr_w_c`）

→ 设计依据：varification-design.md §2.5 `fpr_writeback_mode` 字段、附录 A.1（`_rem` / `_c` 命名约定）。

### 3.7 原子事务 uOP 标记（spec change-20260408-165944，对应 verification-requirments.md F4）

RISC-V LR/SC 与 x86 LOCK CMPXCHG / AMO 等原子事务需要让验证框架在"原子事务最后一条 uOP 提交"那一拍调用 `uarchstatus_cpy`，否则 difftest 无法正确同步 LR/SC reservation 状态。

uOP 小组需要做以下三件事：

1. **区分 uOP 的原子角色**：LR 类、SC 类、AMO 子序列（LOAD + STA + STD）。
2. **在 Decode 阶段为对应 uOP 设置标记**：
   - `LR.W` 等 → 暂复用 `is_microop=false` 的语义（单 uOP 提交即可触发 difftest）
   - `SC.W` 等 → 设置 `is_sc = true`（参见 varification-design.md §2.5 `UopCommitInfo.is_sc` 字段）
   - AMO 序列的最后一条 uOP（通常是 STD）必须 `is_last_microop = true`，使验证框架在该拍触发 `uarchstatus_cpy`
3. **提供 LR/SC 监控状态**：`reserve_valid` / `reserve_addr` 字段已在本文件 §2.1 RV64_CPU_state 的可选字段段中列出，需要由 uOP/后端小组在 ISA 状态容器中实现。

→ 设计依据：verification-requirments.md §4.2 引导执行段补注 F4；varification-design.md §2.5 / §2.6.1。

---

## 四、uOP 元数据与 ROB 提交接口（spec change-20260408-165944）

`UopCommitInfo` 是 ROB/Commit 模块在每条 uOP 提交时构造、传递给 `DifftestChecker::onUopCommit` 的元数据结构。本节列出 uOP 小组需要在 Decode 或 ROB 阶段填充的字段，并明确填充时机。结构体的完整定义见 varification-design.md §2.5。

### 4.1 字段填充责任分工

| 字段 | 类型 | 填充时机 | 由谁填 | 取值/语义 |
|---|---|---|---|---|
| `inst_seq` | uint64_t | ROB 入队时 | ROB | 宏指令序列号（单调递增） |
| `pc` | uint64_t | Decode | uOP | 宏指令 PC（64 位） |
| `is_microop` | bool | Decode | uOP | 是否为 micro-op（false = 单 uOP 指令，不拆分） |
| `is_last_microop` | bool | Decode | uOP | 是否为该宏指令的最后一条 micro-op（即 backend §1 的 `last_uop`） |
| `is_fusion` | bool | Decode | uOP | 是否为融合指令（两条原始指令合并） |
| `is_mmio` | bool | Decode | uOP | 是否为 MMIO 访问（参见 §3.1） |
| `is_store` | bool | Decode | uOP | 是否包含存储操作 |
| `is_branch` | bool | Decode | uOP | 是否包含分支操作 |
| `is_sc` | bool | Decode | uOP | 是否为 SC 类原子操作（参见 §3.7） |
| `macro_id` | uint8_t | Decode | uOP | 同一宏指令的连续编号（来自 backend §1） |
| `uop_type_placeholder` | uint16_t | Decode | uOP | 占位枚举，暂用 `code/uOP` 字典名（参见 §五） |
| `mem_attr` | MemAttr | Decode | uOP | 4 类访存模式之一（参见 §3.5） |
| `fpr_writeback_mode` | uint8_t | Decode | uOP | 0/1/2 三态（参见 §3.6） |
| `except_code` | uint32_t | Decode 或 ROB | uOP/ROB | 0 = 无异常，非零 = 内部统一异常码 |
| `num_dest_regs` | int | ROB | ROB | 实际目的寄存器数量（≤ MAX_DEST_REGS = 4） |
| `dest_regs[i]` | pair<int,uint64_t> | ROB | ROB | (架构寄存器索引, 提交时的值) |
| `store_addr` / `store_data` / `store_mask` | uint64_t | ROB | ROB | 仅当 `is_store=true` 时有效 |

### 4.2 关键约束

1. **`is_microop` / `is_last_microop` 的语义边界**由 uOP 小组在 Decode 时定义，但**字段存放位置**（DynInst 槽位 / ROB 条目位）由后端小组定义。两者必须协商一致。
2. **`macro_id` 在同一宏指令的所有 uOP 上必须相同**，且 `is_last_microop=true` 的那条 uOP 之后 `macro_id` 严格单调递增。这条约束对应 verification-requirments.md §4.3 ROB 表的 `macroIdLastUopMonotonic` 断言（spec change-20260408-013040 新增）。
3. **`uop_type_placeholder` 暂用 `code/uOP` 命名空间**——uOP 小组只需保证每条 uOP 有一个稳定的 `uop_type_placeholder` 值，实际枚举集合由 §五 决定。
4. **异常字段 `except_code` 的语义**由 uOP 小组与 framework 小组约定（暂用 0 = 无异常 + 自定义编码方案，最终 ISA 异常号映射在 verification-requirments.md §4.2 引导执行段实现）。

### 4.3 backend §1 字段集合 vs UopCommitInfo vs code/uOP 三方对照表

| backend §1 统一字段 | UopCommitInfo 字段 | `code/uOP` 命名空间示例 |
|---|---|---|
| `uop_type` | `uop_type_placeholder` | `add_32U` / `subs_32U` / `ldx_w_shift` / `fadd_s_rem` / `pcaddi_32` |
| `src[0..2]` | （ROB 内部 + dest_regs 的"原寄存器"语义） | `rj` / `rk`（LA 风格命名，上层接口禁止使用） |
| `dst[0..1]` | `dest_regs[]` | `rd` |
| `imm` | （内部 uop 字段，不直接进入 UopCommitInfo） | `a->imm` / `a->uimm`（8/12/16/32 位粒度） |
| `width` | （内部 uop 字段） | `8/16/32/64` 位宽后缀 |
| `sign` | （内部 uop 字段） | `U/S` 后缀 |
| `mem` | `mem_attr` | `_shift` / `_pre` / `_post` / `_16` / `_32` / `pc` / `npc` 后缀 |
| `ctrl` | `is_branch` / `is_call` / `is_ret` 等 | `beq_w` / `bne_w` / `pcaddi_32` |
| `isa_meta` | `pc` / `inst_seq` / `macro_id` | `env->pc` / `env->next_pc` |
| `except_hint` | `except_code` | （需 uOP 小组定义） |

→ 详见 varification-design.md §2.5、附录 A、附录 B。

### 4.4 except_code 内部统一异常码（spec change-20260408-165944，反向核对补全）

`UopCommitInfo.except_code` 是"内部统一异常码"，由 uOP 小组与 framework 团队约定取值集合，最终在 verification-requirments.md §4.2 引导执行段（`onException` 路径）映射为 ISA 可见异常号（RISC-V 的 mcause/scause、x86-64 的中断向量号）。

**当前阶段的临时方案**：

- `except_code = 0` —— 无异常（正常执行）
- `except_code != 0` —— uOP 小组在 Decode 阶段填入内部统一异常码，由 framework 在 `onException` 时通过 `Trap Translator`（参见 verification-backend-api.md §7）映射到 ISA 可见异常号

**uOP 小组需要做的**：

1. 与 framework 团队约定一份"内部统一异常码 → RV/x86 ISA 异常号"的双向映射表（参见 varification-design.md §2.6.4 `ExecutionGuide` 字段）
2. 在 Decoder 检测出非法指令、地址对齐错误、特权级违反等同步异常时，把对应 `except_code` 写入 uOP

最终映射表的具体编码方案待 framework 团队提供，本节仅锚定职责归属。

→ 详见 varification-design.md §2.5（`except_code` 字段）、§2.6.4（`onException` 与 `ExecutionGuide`）、verification-requirments.md §4.2 ISA 异常的引导执行方案。

---

## 五、临时 uOP 基线（来自 code/uOP，spec change-20260408-165944）

为加快验证接口推进，验证框架当前**临时**选用 `code/uOP/uop指令说明.md` 作为后端调度粒度的占位 uOP 字典。该字典实际是 LoongArch (LA64) 风格的低层指令集，并不等同于后端最终的通用 uOP，但作为推进验证接口的暂时基线已经足够。

后端正式 uOP 出来后，**只需替换 `UopCommitInfo.uop_type_placeholder` 的枚举集合与 `RflagsMask` 的初始化常量**，本节列出的字段集合、命名约定与分组结构保持稳定。

### 5.1 命名约定

- `8l/8h`：取寄存器低/高 8 位作为源操作数
- `16/32/64`：以对应位宽运算
- `32U/32S`：32 位无符号 / 有符号
- `s` 后缀（如 `adds_*`、`subs_*`、`ands_*`、`shls_*`、`rcls_*`）：通过 `helper_lbt_x86*` 更新 x86 EFLAGS，并在执行前 `CHECK_BTE`
- `_rem`：保留浮点寄存器高位
- `_c`：清零浮点寄存器高位
- `_pre/_post`：访存后更新基址寄存器
- `_shift`：地址 = `rj + (rk << imm)`
- `pc` 相关：以 `env->pc` 为基址；`npc` 相关：以 `env->next_pc` 为基址

### 5.2 11 个分组

1. 整数算术与逻辑（RRR）：`add_*`、`sub_*`、`and_*`、`or_*`、`xor_*`、`imul_*` 及 `s` 版本
2. 自增 / 自减：`inc_*`、`dec_*`（总是更新 EFLAGS）
3. 拼接 / 短宽度除法 / 扩展：`concatl_16/32`、`div_*`、`mod_*`、`ext_*`
4. 立即数算术 / 逻辑：8/12/16/32 位立即数变体
5. 移位与旋转：`sar_*`、`shl_*`、`shr_*`、`rcl_*`、`rcr_*`、`rol_*`、`ror_*` 及立即数版本
6. 访存与地址更新：基址+立即数 / 基址+索引 / PC 相对 / 前后更新 4 类（→ §3.5 mem_attr 4 类的来源）
7. 浮点 rem/c 系列：`fadd_*_rem`、`fsub_*_rem`、`fmul_*_rem`、`fdiv_*_rem`、`fcvt_*_rem`、`fsel_*_rem`
8. 浮点比较：`fcmp_s_s` / `fcmp_s_d` / `fcmp_c_s` / `fcmp_c_d`
9. 浮点 load/store 与 clear-high：`fld_*` / `fldx_*` / `fldpc_*` 及 `_c` 变体；`movgr2fr_*_c`
10. 向量 load/store：`vldx_shift` / `vld_*` / `vldpc_*` / `vstx_shift` / `vst_*` / `vstpc_*`
11. 分支 / 控制类：立即数比较分支、寄存器比较分支、`li32_*` / `li20_*` / `pcaddi_32`

### 5.3 注意事项

- 该字典命名空间共约 400 项，可直接作为 `UopCommitInfo.uop_type_placeholder` 的有效枚举值集合。
- 字典源文件 `code/uOP/uop指令说明.md` 与 `code/uOP/LA_EMU/` 不在 simulator 主仓库内，是项目根目录下的兄弟目录，仅作为参考。
- 该字典使用 LA 风格的 `rj/rk/rd` 寄存器命名。**本文件以及实现层接口（端口字段、UopCommitInfo 字段）必须使用后端 §1 的抽象命名**（`src1_areg/src2_areg/dst_areg`），LA 风格命名禁止泄漏到上层。

→ 详见 varification-design.md 附录 A。

---

## 六、x86-64 RFLAGS 分类约定（spec change-20260408-165944）

x86-64 不同指令只修改 RFLAGS 的部分位（`verification-requirments.md` §4.2 RFLAGS 部分更新掩码段）。验证框架使用 `RflagsMask` 类按"指令类别"分桶比较，避免因"未定义位"产生误报。`RflagsMask` 的 6 类初始 instCategory 已经在 `varification-design.md` §2.7.1 固化为常量。

### 6.1 6 类 instCategory 与 RFLAGS 修改位

| instCategory | 含义 | 修改的 RFLAGS 位 |
|---|---|---|
| `kArith = 1` | ADD / SUB / CMP / NEG | CF / OF / SF / ZF / PF / AF |
| `kLogic = 2` | AND / OR / XOR / TEST | SF / ZF / PF（CF / OF 强制为 0） |
| `kShift = 3` | SHL / SHR / SAR | CF / OF / SF / ZF / PF |
| `kIncDec = 4` | INC / DEC | OF / SF / ZF / PF / AF（CF 不变） |
| `kMul = 5` | MUL / IMUL | CF / OF |
| `kDiv = 6` | DIV / IDIV | 不修改任何位 |

### 6.2 uOP 小组的交付内容

uOP 小组需要交付一份"**每个 uOP 类型 → instCategory 编号**"的映射表。映射规则直接来自 `code/uOP` 字典 `s` 后缀的实际行为：

- `adds_*` / `subs_*` → `kArith`
- `ands_*` / `ors_*` / `xors_*` → `kLogic`
- `shls_*` / `shrs_*` / `sars_*` / `rcls_*` / `rcrs_*` / `rols_*` / `rors_*` → `kShift`
- `inc_*` / `dec_*` → `kIncDec`
- `imuls_*` → `kMul`
- DIV / IDIV 类（`code/uOP` 字典中无写 EFLAGS 的除法变体） → `kDiv`

### 6.3 MXCSR 部分更新（spec change-20260408-165944，反向核对补全）

MXCSR 是 x86-64 SSE/SSE2 指令的浮点控制/状态寄存器，与 RFLAGS 类似存在"部分位由不同 SSE 指令选择性修改"的问题（参考 verification-requirments.md §4.2 MXCSR 段）。验证框架的 `RflagsMask::compareMxcsr()` 使用与 RFLAGS 相同的"按 instCategory 分桶"机制。

uOP 小组需要为每个 SSE/SSE2 浮点 uOP 标记"哪些异常标志位可能被修改"，作为 MXCSR 比较的输入。具体的 instCategory 划分待 SSE Decoder 实现时与 framework 团队协商；本节仅锚定职责归属。

→ 详见 varification-design.md §2.6.11 MXCSR、verification-requirments.md §4.2 MXCSR 段。

不带 `s` 后缀的 uOP **不写 RFLAGS**，不需要分类。

→ 详见 varification-design.md §2.7、§2.7.1。

---

## 七、暂时缺失 uOP 类的处理约定（spec change-20260408-165944）

`varification-design.md` 附录 B 列出了 5 类后端建议补充但 `code/uOP` 字典暂时缺失的 uOP。在后端正式 uOP 出来之前，这些指令需要 uOP 小组在 Decode 阶段做"宏指令翻译"，验证侧不区分。

| 缺口类别 | 后端建议补充的 uOP | 当前临时翻译方式 |
|---|---|---|
| **跳转** | `jmp_imm`、`jmp_reg` | JALR/CALL/RET 走宏指令级 stub，作为单条 uOP 提交（is_microop=false） |
| **系统控制** | `csr_rw` / `csr_rs` / `csr_rc`、`ecall`、`ebreak`、`wfi`、`mret`、`sret`、`sfence_vma`、`fence_i` | 在 Decode 时翻译为 Sys/CSR 类 uOP；按 §3.2 子类型过滤跳过 CSR 比较 |
| **RV32M 完整** | `mulh_32S`、`mulhu_32U`、`mulhsu_32`、`div_32U`、`mod_32U` | 在 Decode 时用 helper 路径处理，保持单 uOP 提交 |
| **原子事务** | `lr_w`、`sc_w`、`amo_{add,xor,and,or,min,max,minu,maxu}_w` | 用 LOAD + STA + STD 三段序列翻译；最后一条标 `is_sc=true` + `is_last_microop=true`（参见 §3.7） |
| **比较置位** | `slt_32S`、`sltu_32U` | Decode 时翻译为"减法 + 取符号位"的 uOP 序列 |

> 以上 5 类缺口仅影响**模拟器执行层**，对验证接口的可设计性没有阻塞。后端正式 uOP 出来后，本节会被移除。

→ 详见 varification-design.md 附录 B。

---

## 八、ISA 扩展规划（spec change-20260408-165944 已分级）

| 优先级 | 扩展 | 说明 |
|---|---|---|
| **必须支持（Phase 1-2）** | RV64I / M / A / F / D / C | RV64 基础整数 + 乘除 + 原子 + 单精度浮点 + 双精度浮点 + 压缩 |
| **必须支持（Phase 1-2）** | x86-64 长模式整数指令 + SSE/SSE2 + x87 FPU | 16 个 64 位 GPR、16 个 128 位 XMM、8 个 80 位 x87 栈 |
| **必须支持（Phase 1-2）** | M/S/U 三级特权 + SV39/SV48 / PML4 分页 | 异常 / 中断 / TLB |
| **推荐 Phase 3 支持** | RVV 向量扩展 | 32 个向量寄存器，宽度可配置 |
| **推荐 Phase 3 支持** | RV64 H 扩展 | Hypervisor 虚拟化（VS/VU 模式） |
| **推荐 Phase 3 支持** | x86-64 SYSCALL / SYSRET / SYSENTER / SYSEXIT | 快速系统调用入口 |
| **暂不支持** | x86-64 AVX2 / AVX-512 | 256/512 位 SIMD |
| **暂不支持** | RISC-V Zfinx | 浮点存放在整数寄存器（启蒙3号路径，不再使用） |

→ 详见 verification-requirments.md §1.2 功能基线。

---

## 九、你需要向验证框架提供什么（总览）

| 交付物 | 说明 | 详细位置 |
|--------|------|---------|
| **ISA 状态容器定义** | 每个 ISA 的 `CPU_state` 结构体（包含第二节的必需字段 + 项目需要的可选字段），字段顺序固定，新字段在哨兵之前插入 | §二 |
| **特殊指令标记** | MMIO / 系统指令 / 融合 / REP / 访存 mem_attr / 浮点 fpr_writeback_mode / 原子事务 | §三 |
| **uOP 元数据填充** | UopCommitInfo 结构体所有字段的填充时机和合法取值 | §四 |
| **占位 uOP 字典** | 当前阶段以 `code/uOP` 为基线，使用其约 400 项命名空间作为 `uop_type_placeholder` 的有效枚举 | §五 |
| **RFLAGS 分类映射** | 把每个 x86-64 算术/逻辑/移位/自增减/乘法/除法 uOP 映射到 6 类初始 instCategory 之一 | §六 |
| **缺失 uOP 类的宏指令翻译** | 跳转 / 系统控制 / RV32M 完整 / 原子事务 / 比较置位 5 类缺口的临时翻译方式 | §七 |
| **ISA 扩展规划** | 必须支持 / 推荐 Phase 3 支持 / 暂不支持 三档分级 | §八 |
