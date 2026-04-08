# 新一代 CPU 模拟器验证接口实现设计

本文档描述新模拟器**验证接口**的代码架构设计，包括需要构造的类、接口函数和数据结构。所有设计严格遵循 `verification-requirments.md` 中的设计思路框架。

本文档是 `verification-requirments.md` 的实现层补充。设计思路（为什么这样做）见 `verification-requirments.md`，本文档只回答"具体怎么做"。

> **范围说明**：本文档聚焦于验证接口的实现设计，覆盖以下几个层面：
> - **核心框架**：SimModule、Port、TimeBuffer 等支撑模块可替换、可独立运行、可录制、可桥接的基础设施
> - **Difftest 验证层**：模拟器作为 DUT 与参考模型逐指令比较
> - **RTL 验证层**：Bridge、Trace、端口映射等模块级联合仿真基础设施
> - **模拟器作为 REF**：编译为 .so 供 RTL 仿真加载
> - **模拟器自身正确性验证（两个支柱）**：运行时断言框架 + 定向 I/O 测试框架 + 构造性验证接口——这些不是"补充手段"，而是与 Difftest 并列的正确性验证支柱（见 `verification-requirments.md` 4.3）
>
> 模拟器采用**高层逻辑 + 时序计数**的建模方式（见 `verification-requirments.md` 4.1），内部状态与 RTL 不可比较，验证严格限定在模块 I/O 边界。模拟器的具体流水线模块设计和通用 uOP 划分方案当前处于待定状态，不在本文档范围内。文档中明确标注了哪些设计在当前信息下可完成、哪些需要等待进一步确认。

---

## 宏观实现框架

本节从整体上描述验证接口的实现架构，帮助读者在阅读各章节的详细设计之前建立全局认知。

验证接口的实现由四个相互协作的层次构成：**核心仿真框架层**、**Difftest 验证层**、**RTL 验证层**、以及**模拟器自身正确性验证层**。这四个层次自底向上层层依赖，同时各自承担独立的验证职责。

**核心仿真框架层**是整个验证接口的基础设施。它定义了所有仿真模块的统一基类（SimModule / ClockedObject），提供层次化的模块组织（CPU → Frontend/Backend/MemSubsystem → 具体模块 → 子组件的四级模块树）、端口系统（Port / DataPort / SignalPort）实现模块间的类型安全通信、以及 TimeBuffer 实现流水线级间的定延迟数据传递。这一层的核心设计目标是让每个模块具备"可替换、可独立运行、可录制、可桥接"四个能力。模块通过端口与外界通信而非直接访问其他模块的内部数据，模块可以脱离完整 CPU 环境由存根（Stub）驱动独立运行，端口上的数据传输可被 Trace 录制器自动捕获，端口定义同时携带逻辑语义和信号映射信息以支持与 RTL 的桥接。这些能力是上层三个验证层的共同前提。

**Difftest 验证层**负责验证模拟器作为 DUT 的功能正确性。它将模拟器与外部参考模型（RISC-V 使用 Spike/NEMU，x86-64 使用 QEMU TCG）逐指令比较提交结果。这一层的核心挑战是双 ISA 支持——两种 ISA 的架构状态空间（寄存器数量、标志位语义、特权模型）完全不同，参考模型也不同。实现上，通过 ISA 无关的状态容器统一两种 ISA 的状态表示，通过 Proxy 工厂模式在运行时按 ISA 加载对应的参考模型共享库，通过 uOP 过滤机制（isMicroop / isLastMicroop 标志）将后端的 uOP 粒度提交转换为宏指令粒度的 difftest 比较。此外，这一层还实现了一系列 DUT 与 REF 不完全同步场景的处理机制，包括 MMIO 跳过、中断显式注入、异常引导执行、系统指令 CSR 跳过、RFLAGS 部分更新掩码（x86-64 特有）等。

**RTL 验证层**负责验证 LLM 或 BSD 生成的 RTL 代码的正确性。此时模拟器的角色反转——从 DUT 变为 REF（golden reference）。这一层包含两个子层次。第一个子层次是整体指令级 difftest：模拟器编译为共享库（.so），RTL 仿真程序通过 dlopen 加载后逐指令比较，大量复用 Difftest 验证层的基础设施。第二个子层次是模块级联合仿真：在全系统仿真中，某个 C++ 模块与 Verilator 编译的 RTL 模块并行运行，通过 Bridge 层桥接——Bridge 每周期读取上游输入、转换为 RTL 信号灌入、两侧分别执行、读取并比较两侧输出。模块级联合仿真的比较严格限定在模块 I/O 边界（这是高层逻辑 + 时序计数的建模方式所决定的必然约束），配合端口映射规范（声明式端口定义同时携带逻辑语义和位级信号映射）和 Trace 录制回放机制，实现对单个 RTL 模块的周期级 I/O 验证。

**模拟器自身正确性验证层**与 Difftest 验证层并列，构成模拟器正确性的"两个支柱"。Difftest 保证整体功能正确性但无法定位 bug 到具体模块，也无法检测不影响提交结果的微架构内部错误。这一层通过三种互补手段弥补 Difftest 的局限：运行时断言（SIM_ASSERT 宏）嵌入在每个模块的 tick() 逻辑中，在全系统仿真中始终检查内部不变量（如"空闲物理寄存器数 + 已分配物理寄存器数 = 总数"）；模块级定向 I/O 测试（ModuleTestHarness 框架）将模块单独实例化并灌入精心设计的输入，检查输出值和输出时序；构造性验证（MicroBenchmark 框架）在全系统环境中运行特定的微基准测试程序，人工计算预期结果和周期数并验证实际是否匹配。这三种手段不依赖外部参考模型，直接检验模拟器的行为是否符合设计者意图。

四个层次的协作关系如下。核心仿真框架层提供模块化和端口化的基础设施，使得 Difftest 验证层能在指令提交点采集状态进行逐指令比较，RTL 验证层能通过端口映射和 Bridge 在模块 I/O 边界进行逐周期比较，自身正确性验证层能通过模块独立实例化和端口自省进行断言检查和定向测试。Difftest 验证层和自身正确性验证层共同保证模拟器本身的正确性（功能正确 + 模块级行为正确），为 RTL 验证层提供可信的 golden reference。RTL 验证层的两个子层次互补：模块级联合仿真用于开发阶段逐模块验证，整体 difftest 用于集成阶段验证组装后的完整 RTL CPU。

---

## 设计依赖与可设计性分析

当前有两个维度的设计信息处于待定状态：

- **模拟器具体设计待定**：流水线各模块的具体类设计、级间数据结构的具体字段、模块间的拓扑连接方式。
- **uOP 方案待定**：通用 uOP 的具体类型枚举、字段定义、位宽、ISA 指令到 uOP 的拆分规则。

以下按三级标注各设计组件对这两个维度的依赖程度：

| 依赖程度 | 含义 | 涉及模块 |
|----------|------|---------|
| ✅ 可立即设计 | 不依赖模拟器具体设计和 uOP 规格，可完成完整设计 | SimModule 基类、ClockedObject、仿真主循环、ISA 状态容器、ISA 无关状态接口、Difftest API（16 个函数）、Proxy 工厂、特殊处理逻辑（16 个场景）、RFLAGS 掩码框架、StoreCommitQueue、DifftestChecker、DPI-C 接口、差异报告、StubModule 框架、运行时断言框架（SIM_ASSERT 宏 + 断言失败处理）、构造性验证接口（MicroBenchmark 基类）、模拟器 .so 编译目标与导出 API |
| ⚠️ 部分可设计 | 框架和接口可设计，具体数据类型/字段需等待 | Port 系统（端口束具体字段）、TimeBuffer（载荷数据类型）、uOP 过滤逻辑（isMicroop/isLastMicroop 标志在 DynInst 中的具体位置）、Bridge 类（端口映射的具体字段）、端口映射（Field<T> 的具体位宽）、Trace 录制回放（序列化格式）、模块接口约束（具体端口定义）、RFLAGS 掩码表内容、x87 FPU transcendental 精度容差阈值、**定向 I/O 测试驱动框架**（ModuleTestHarness 骨架可设计，具体端口类型需等待）、**模块级断言实现**（断言条件已在 verification-requirments.md 4.3 完整列举，但引用的内部数据结构需等待模块设计） |
| ❌ 需等待 | 设计完全依赖待定信息 | 端口声明的具体字段定义、BSD 位级展开层的具体位宽、级间通信结构体的完整定义、**各模块断言的具体实现代码**（依赖模块内部状态的具体数据结构） |

**核心结论**：验证接口框架的骨架（SimModule、Port 机制、Difftest 全流程、Bridge 模式、Trace 框架、断言框架、测试驱动框架）可以在模拟器和 uOP 设计确定前完成设计和实现。待定信息确定后，主要工作是填充端口的具体数据类型、uOP 聚合的具体逻辑、Field<T> 的位宽、以及各模块的具体断言实现代码。

**各章节与 `verification-requirments.md` 的映射**：

| 本文档章节 | verification-requirments.md 对应章节 |
|-----------|---------------------------|
| 第一章：验证框架核心组件 | 第三章（验证接口的设计需求，含 3.3 模块可测试性设计原则） |
| 第二章：Difftest 验证层 | 第四章 4.1-4.2（核心认知 + Difftest 方法论） |
| 第三章：RTL 验证层 | 第五章 5.2-5.6（模块级联合仿真，含 5.2 I/O 比较边界） |
| 第四章：模拟器作为 REF | 第五章 5.1（整体指令级 difftest） |
| 第五章：模拟器自身正确性验证（两个支柱的实现） | 第四章 4.3（综合验证策略：两个支柱 + 各模块完整检查方案） |
| 第六章：可设计性总结与实现路线 | 第五章 5.7（分阶段实现路径） |

---

## 一、验证框架核心组件

> 对应 `verification-requirments.md` 第三章（验证接口的设计需求）。以下是支撑"模块可替换、可独立运行、可录制、可桥接"的核心 C++ 类设计。

### 1.1 SimModule 基类 ✅

SimModule 是所有仿真组件的基类，提供统一的生命周期管理、层次化组织和端口自省。通过 parent/child 关系实现 `verification-requirments.md` 3.1 的 4 级模块树（CPU → Frontend/Backend/MemSubsystem → 具体模块 → 子组件）。

```cpp
class SimModule {
public:
    explicit SimModule(const std::string& name, SimModule* parent = nullptr);
    virtual ~SimModule() = default;

    // ── 层次化命名 ──
    const std::string& name() const;           // 返回短名称（如 "rename"）
    std::string fullPath() const;              // 返回层次路径（如 "cpu.backend.rename"）
    SimModule* parent() const;

    // ── 子模块管理 ──
    const std::vector<SimModule*>& children() const;
    SimModule* findChild(const std::string& name) const;

    // ── 生命周期 ──
    virtual void init() {}                     // 创建后初始化（端口尚未连接）
    virtual void startup() {}                  // 端口连接完成后启动
    virtual void tick() = 0;                   // 每周期执行逻辑（纯虚，子类必须实现）

    // ── 端口访问 ──
    virtual Port* getPort(const std::string& portName, int idx = -1);

    // 返回本模块注册的所有端口（用于自动 Trace 挂载和 Bridge 端口发现）
    const std::vector<Port*>& allPorts() const;

    // ── 状态序列化与恢复（用于 Trace 录制、检查点和调试）──
    virtual void serialize(std::ostream& os) const {}
    virtual void unserialize(std::istream& is) {}

protected:
    void addChild(SimModule* child);           // 由子模块构造函数自动调用
    void registerPort(Port* port);             // 端口创建时自动注册

private:
    std::string name_;
    SimModule* parent_;
    std::vector<SimModule*> children_;
    std::vector<Port*> ports_;                 // 本模块拥有的所有端口
};
```

**设计要点**：

- **层次化命名**：通过 `parent_` 链构建层次路径。`fullPath()` 沿 `parent_` 链拼接，如 `cpu.backend.rename`。使日志和差异报告能精确定位模块。
- **生命周期分三阶段**：构造 → `init()`（内部状态初始化）→ 端口 `bind()` → `startup()`（依赖端口连接的初始化）→ 仿真循环中反复调用 `tick()`。
- **`tick()` 纯虚**：强制每个具体模块实现自己的周期逻辑。
- **`getPort()` 支持索引**：`idx` 参数用于多实例端口（如多个 IssueQueue 各有独立端口，通过 `getPort("issue", 0)` 区分）。
- **`allPorts()` 端口自省**：返回本模块注册的全部端口，供 Trace 录制器自动挂载、Bridge 层自动发现端口映射。
- **`serialize()/unserialize()`**：默认空实现，有状态的模块按需覆写。用于 Trace 录制（快照模块内部状态）、检查点保存/恢复、以及 StubModule 的状态注入。

**使用示例**（展示模块接口约束的典型实现）：

```cpp
class RenameModule : public ClockedObject {
public:
    RenameModule(SimModule* parent)
        : ClockedObject("rename", parent) {}

    void process() override {
        // 从输入端口读取解码后的指令
        // 执行寄存器重命名逻辑
        // 将结果写入输出端口
        // 末尾调用断言检查内部不变量
    }

    Port* getPort(const std::string& portName, int idx) override {
        if (portName == "decode_in") return &decodeInPort_;
        if (portName == "dispatch_out") return &dispatchOutPort_;
        if (portName == "freelist") return &freelistPort_;
        if (portName == "flush") return &flushPort_;
        return nullptr;
    }

    void serialize(std::ostream& os) const override {
        // 导出 RAT、空闲列表等内部状态
    }

    void unserialize(std::istream& is) override {
        // 恢复内部状态
    }
};
```

### 1.2 ClockedObject ✅

```cpp
class ClockedObject : public SimModule {
public:
    ClockedObject(const std::string& name, SimModule* parent,
                  uint64_t clockPeriod = 1);

    uint64_t curCycle() const;       // 当前周期数
    uint64_t clockPeriod() const;    // 时钟周期（用于多频率支持）
    uint64_t frequency() const;      // 时钟频率

    // tick() 内部先更新周期计数，再调用 process()
    void tick() final;
    virtual void process() = 0;      // 子类实现具体逻辑

private:
    uint64_t cycle_ = 0;
    uint64_t clockPeriod_;
};
```

**设计要点**：

- **初始版本假设全局统一时钟**：所有模块的 `clockPeriod` 为 1。
- **预留多频率接口**：通过 `clockPeriod_` 参数支持未来不同时钟域（如 CPU 核心与缓存频率不同）。
- **`tick()` 标记为 `final`**：确保周期计数更新逻辑不被子类覆盖，子类通过 `process()` 实现业务逻辑。

### 1.3 Port 系统 ⚠️

Port 系统实现模块间的类型安全通信。框架部分（Port 基类、bind 机制、录制钩子、端口束）可立即设计；具体传输的数据类型（如 uOP 结构体）需等待 uOP 和模拟器设计确定。

```cpp
// ── 端口基类 ──
class Port {
public:
    Port(const std::string& name, SimModule* owner);
    virtual ~Port() = default;

    const std::string& name() const;
    SimModule* owner() const;
    bool isConnected() const;

    // 录制钩子：启用后每次数据传输自动记录到 trace
    void enableTrace(TraceRecorder* recorder);
    void disableTrace();

protected:
    Port* peer_ = nullptr;       // 对端端口
    TraceRecorder* recorder_ = nullptr;

    friend void bind(Port& a, Port& b);
};

// 建立双向连接
void bind(Port& a, Port& b);
```

```cpp
// ── 类型化的请求/响应端口（用于存储子系统等需要双向通信的场景）──
template<typename ReqType, typename RespType = void>
class RequestPort : public Port {
public:
    using Port::Port;

    bool sendReq(const ReqType& req);
    bool recvResp(RespType& resp);
};

template<typename ReqType, typename RespType = void>
class ResponsePort : public Port {
public:
    using Port::Port;

    bool recvReq(ReqType& req);
    bool sendResp(const RespType& resp);
};
```

```cpp
// ── 单向数据端口（用于流水线级间传输，最常用场景）──
template<typename T>
class DataPort : public Port {
public:
    using Port::Port;

    // 写端（上游模块调用）
    void send(const T& data);
    void setValid(bool valid);

    // 读端（下游模块调用）
    const T& peek() const;
    bool isValid() const;
};
```

```cpp
// ── 广播信号端口（用于 flush、stall、wakeup 等一对多信号）──
template<typename T>
class SignalPort : public Port {
public:
    SignalPort(const std::string& name, SimModule* owner);

    // 发送方
    void broadcast(const T& signal);

    // 接收方（可有多个）
    void addListener(Port* listener);
    const T& read() const;
    bool hasSignal() const;

private:
    std::vector<Port*> listeners_;
};
```

```cpp
// ── 端口束：将相关端口组合为一组 ──
class PortBundle {
public:
    PortBundle(const std::string& name, SimModule* owner);

    // 注册端口到束中
    void addPort(const std::string& portName, Port* port);

    // 按名称获取端口
    Port* getPort(const std::string& portName) const;

    // 遍历所有端口（用于自动 Trace 挂载）
    const std::map<std::string, Port*>& ports() const;

    // 束级别的 Trace 控制
    void enableTraceAll(TraceRecorder* recorder);
    void disableTraceAll();

private:
    std::string name_;
    SimModule* owner_;
    std::map<std::string, Port*> ports_;
};
```

**设计要点**：

- **三种端口类型**：`RequestPort/ResponsePort` 用于双向通信（如 LSU↔DCache）；`DataPort` 用于单向流水线级间传输（最常用，如 Decode→Rename）；`SignalPort` 用于广播信号（如冲刷信号一对多扇出到所有流水线级）。
- **录制钩子**：`enableTrace()` 为端口挂载录制器，每次 `send()/sendReq()/broadcast()` 时自动序列化数据。这是 Port 作为"验证观测点"的实现方式。
- **端口束**：`PortBundle` 将一个模块的多个相关端口组合在一起（如 Rename 模块的输入束包含"解码数据端口 + 空闲列表端口 + 冲刷信号端口"）。端口束使模块替换时能一次性验证接口兼容性。
- **bind 是自由函数**：`bind(portA, portB)` 同时设置双方的 `peer_` 指针，保证连接的对称性。
- **⚠️ uOP 依赖**：Port 框架本身不依赖 uOP。但实际使用时，`DataPort<RenameInput>` 中的 `RenameInput` 结构体包含 uOP 字段，该结构体的定义需要等待 uOP 和模拟器设计确定。

#### 1.3.1 级间结构体的占位字段集合（spec change-20260408-013040）

虽然 uOP 字段最终位宽未定，但结合 `verification-backend-api.md` §1 给出的统一 uop 字段集合（uop_type/src/dst/imm/width/sign/mem/ctrl/isa_meta/except_hint）以及暂时基线 `code/uOP/uop指令说明.md`，可以**先把级间结构体的字段名锚定下来**，位宽稍后填实。这让 Port 系统、TimeBuffer、Bridge 的客户代码可以提前编译通过。

```cpp
// ⚠️ 占位结构体，字段名已确定，位宽与 uop_type 枚举待 uOP 正式版填充
// 来源：verification-backend-api.md §1 统一 uop 字段
struct DecodeToRename {
    bool     valid;
    uint16_t uop_type_placeholder;  // 占位枚举（暂用 code/uOP 命名空间，例如 add_32U / subs_32U / ldx_w_shift）
    uint8_t  src1_areg;             // 架构寄存器号 from backend §1
    uint8_t  src2_areg;             // from backend §1
    uint8_t  dst_areg;              // from backend §1
    uint64_t imm;                   // 立即数（粒度可在 8/12/16/32/64 位之间，由 uop_type 决定）
    uint8_t  width;                 // 操作位宽 from backend §1（8/16/32/64）
    uint8_t  sign;                  // 0=无符号, 1=有符号 from backend §1
    MemAttr  mem_attr;              // 访存属性占位 from backend §1
    uint16_t ctrl_flag;             // is_branch / is_call / is_ret / predicted_taken from backend §1
    uint64_t isa_meta_pc;           // 该 uop 所属宏指令的 PC from backend §1
    uint8_t  except_hint;           // 可能触发的异常类别 from backend §1
    uint8_t  macro_id;              // 同一宏指令的连续编号 from backend §1
    bool     last_uop;              // 是否为该宏指令的最后一条 uop from backend §1
};

struct RenameToDispatch {
    bool     valid;
    uint16_t uop_type_placeholder;
    uint16_t src1_preg;             // 物理寄存器号占位（位宽 = log2(PRF_SIZE)，PRF_SIZE 待定）
    uint16_t src2_preg;
    uint16_t dst_preg;
    uint16_t old_dst_preg;          // 用于提交时释放
    uint64_t imm;
    MemAttr  mem_attr;
    uint16_t ctrl_flag;
    uint16_t rob_id;                // ROB 索引占位（位宽 = log2(ROB_SIZE)）
    uint8_t  macro_id;
    bool     last_uop;
};

struct DispatchToIQ {
    bool     valid;
    uint16_t uop_type_placeholder;
    uint16_t src1_preg, src2_preg, dst_preg;
    bool     src1_ready, src2_ready;
    uint64_t imm;
    uint8_t  fu_class;              // Int / Mem / Br / FP
    uint16_t rob_id;
    uint8_t  macro_id;
    bool     last_uop;
};
```

> 上述三个结构体覆盖了 `Decode→Rename`、`Rename→Dispatch`、`Dispatch→IssueQueue` 三段最关键的级间通信。所有 `DataPort<T>` 与 `TimeBuffer<T>` 在使用时优先实例化为这三类。位宽和 uop_type 最终值出来之前，**字段名和数量保持稳定**，避免上层代码反复修改。

### 1.4 TimeBuffer ⚠️

TimeBuffer 是流水线级间传递数据的延迟缓冲区。框架可立即设计，缓冲区内传输的数据类型（包含 uOP 的结构体）需等待确定。

```cpp
// ── TimeBuffer 抽象基类（用于 Simulator 的异构集合）──
class TimeBufferBase {
public:
    virtual ~TimeBufferBase() = default;
    virtual void advance() = 0;
};

template<typename T>
class TimeBuffer : public TimeBufferBase {
public:
    // delay: 数据从写入到可读取需要经过的周期数
    // width: 每个周期最多可写入的数据条数（对应流水线宽度）
    TimeBuffer(int delay, int width = 1);

    // ── 写端（上游模块调用）──
    T& writeSlot(int idx = 0);
    void setValid(int idx, bool valid);

    // ── 读端（下游模块调用）──
    const T& readSlot(int idx = 0) const;
    bool isValid(int idx) const;

    // ── wire 内部类：通过时间偏移索引访问 ──
    class wire {
    public:
        wire(TimeBuffer<T>* buf, int offset);
        T* operator->() { return &buf_->access(offset_); }
        T& operator*() { return buf_->access(offset_); }
    private:
        TimeBuffer<T>* buf_;
        int offset_;     // 负数=过去，0=当前，正数=未来
    };

    wire getWire(int offset);

    // ── 周期推进（由仿真主循环调用）──
    void advance() override;

private:
    int delay_;
    int width_;
    // 环形缓冲区：depth = delay + 1
    std::vector<std::vector<T>> buffer_;
    std::vector<std::vector<bool>> valid_;
    int writePtr_;    // 当前写入位置

    T& access(int offset);
};
```

**设计要点**：

- **固定延迟**：构造时确定延迟值，运行时不可更改。延迟值反映流水线寄存器的级数。
- **环形缓冲区**：`buffer_` 大小为 `(delay + 1) * width`，`advance()` 移动 `writePtr_`，读取位置通过 `(writePtr_ - delay)` 计算。
- **多槽位**：`width` 参数支持超标量流水线——每周期可传递多条指令/uOP。
- **wire 内部类**：参考 gem5 `TimeBuffer<T>::wire`，`getWire(offset)` 返回特定时间偏移的数据访问器。上游通过 `getWire(0)` 写入当前周期，下游通过 `getWire(-delay)` 读取。
- **TimeBufferBase 基类**：使 `Simulator` 可以持有异构 TimeBuffer 集合并统一调用 `advance()`。
- **⚠️ uOP 依赖**：TimeBuffer 的模板参数 `T` 在实际使用中会是包含 uOP 信息的结构体（如 `DecodeToRenameData`），该结构体的具体字段依赖 uOP 和模拟器设计。但 TimeBuffer 框架本身完全通用。

#### 1.4.1 实际使用的三个典型实例化点（spec change-20260408-013040）

结合 §1.3.1 中已经确定的级间结构体，TimeBuffer 在新模拟器流水线中的典型实例化点如下（来自 `verification-backend-api.md` §1-§4 的级间约定）：

```cpp
// delay=1 对应一级流水线寄存器；width 取该级的流水线宽度参数。
// dispatch_width 与 issue_width 是模拟器构建期常量，初始可统一为 4，由参数化构造覆盖。
TimeBuffer<DecodeToRename>   decode2rename  {/*delay=*/1, /*width=*/dispatch_width};
TimeBuffer<RenameToDispatch> rename2dispatch{/*delay=*/1, /*width=*/dispatch_width};
TimeBuffer<DispatchToIQ>     dispatch2iq    {/*delay=*/1, /*width=*/issue_width};
```

> 这三个 TimeBuffer 是后续 Dispatch 反压、Rename 误预测恢复、IQ 唤醒时序等模块级断言/测试用例的最常见挂载点。本节固定它们的载荷类型，但**实际 delay 值可以在模拟器初始化时按微架构参数覆盖**——TimeBuffer 构造参数允许传入而非硬编码 1。其它级间通信（Issue→Execute、Execute→Writeback、Writeback→Commit 等）可参考相同模式按需添加，框架不再列穷举。

### 1.5 DelayedResult — 模块内多周期延迟建模 ✅

> **设计动机**：模拟器采用"高层逻辑 + 时序计数"的建模方式（见 `verification-requirments.md` 4.1）——模块在输入到达的周期立即用 C++ 计算出结果，然后模拟硬件的多周期延迟，在 N 拍后才将结果输出。例如乘法器在第 0 拍算出乘积，但在第 3 拍才将结果写入输出端口。这个"立即计算 + 延迟输出"的模式是几乎所有多周期模块（MUL、DIV、FPU、Cache 访问、PTW 遍历）的共同需求。
>
> TimeBuffer 解决的是**级间通信**问题（固定延迟、固定宽度、所有槽位同步推进），不适合**模块内部**的延迟建模——模块内部的延迟可能因操作类型而异（ALU 1 拍 vs MUL 3 拍 vs DIV 10+ 拍），且多个结果可能同时在途（流水线化的乘法器）。DelayedResult 作为框架提供的轻量工具类，填补这一空缺。

```cpp
/// 延迟结果队列：模块内部用于模拟多周期操作的"高层逻辑 + 时序计数"。
/// 模块在输入到达的周期立即计算结果，调用 push() 放入队列并指定延迟拍数，
/// 每周期调用 tick() 推进，延迟到期后通过 hasReady()/popReady() 取出结果。
template<typename T>
class DelayedResult {
public:
    /// @param maxInflight 最大在途结果数量。
    ///   流水线化模块（如 MUL）设为流水线深度（如 3），允许每拍接受新输入；
    ///   非流水线模块（如 DIV）设为 1，执行期间阻塞新输入。
    explicit DelayedResult(int maxInflight = 0);  // 0 = 不限制

    /// 放入一个结果，latency 拍后可取出。
    /// @pre canAccept() 为 true（若设置了 maxInflight 限制）
    void push(const T& result, int latency);

    /// 本周期是否有结果到期
    bool hasReady() const;

    /// 查看最老的到期结果（不消费）
    const T& peekReady() const;

    /// 取出并消费最老的到期结果
    T popReady();

    /// 当前在途结果数量（用于断言和反压判断）
    int inflightCount() const;

    /// 是否有空间接受新输入
    /// maxInflight > 0 时：inflightCount() < maxInflight
    /// maxInflight == 0 时：始终返回 true
    bool canAccept() const;

    /// 是否为空（无任何在途结果）
    bool empty() const;

    /// 每周期推进（由模块的 process() 在逻辑开头调用）
    void tick();

private:
    struct Entry {
        T result;
        uint64_t readyCycle;   // 结果到期的绝对周期
    };
    std::deque<Entry> pending_;
    uint64_t curCycle_ = 0;
    int maxInflight_;          // 0 = 不限制
};
```

**设计要点**：

- **与 TimeBuffer 的区别**：TimeBuffer 用于级间通信（由 Simulator 统一推进），DelayedResult 用于模块内部延迟建模（由模块自己的 `process()` 调用 `tick()` 推进）。两者互补，不替代。

| 维度 | TimeBuffer | DelayedResult |
|------|-----------|--------------|
| 用途 | 级间通信（Decode→Rename） | 模块内延迟建模（MUL 3拍、DIV N拍） |
| 延迟 | 构造时固定，所有数据相同 | 每条数据可以不同 |
| 推进 | 由 Simulator::advanceBuffers() 统一推进 | 由模块的 process() 调用 tick() |
| 宽度 | 固定槽位数（对应流水线宽度） | 动态队列，可选最大在途数 |
| 典型使用者 | 上下游两个模块 | 单个模块内部 |

- **maxInflight 区分流水线化与非流水线化**：流水线化的乘法器设 `maxInflight = 3`（3 级流水线，每拍可接受新输入，最多 3 个结果同时在途）；非流水线的除法器设 `maxInflight = 1`（执行期间 `canAccept()` 返回 false，阻塞上游发射新指令）。
- **结果按 readyCycle 排序**：`pending_` 队列中条目按到期时间排序。`hasReady()` 检查队头的 `readyCycle <= curCycle_`。对于流水线化模块，先入先出天然保证顺序。
- **断言支持**：`inflightCount()` 使模块可以断言"在途数量不超过 maxInflight"，`empty()` 使模块可以断言"冲刷后 DelayedResult 已清空"。

**使用示例**（流水线化乘法器，3 拍延迟）：

```cpp
class MulUnit : public ClockedObject {
    DelayedResult<ExecResult> pipeline_{3};  // maxInflight = 3

    void process() override {
        pipeline_.tick();

        // 1. 检查新输入
        if (inputPort_.isValid() && pipeline_.canAccept()) {
            auto& uop = inputPort_.peek();
            ExecResult res;
            res.value = uop.src1 * uop.src2;  // 立即计算
            res.robId = uop.robId;
            pipeline_.push(res, 3);             // 3 拍后到期
        }

        // 2. 输出到期结果
        if (pipeline_.hasReady()) {
            outputPort_.send(pipeline_.popReady());
        }

        // 3. 断言
        SIM_ASSERT(pipeline_.inflightCount() <= 3,
                   fullPath() + ": MUL pipeline overflow");
    }
};
```

**使用示例**（非流水线除法器，可变延迟）：

```cpp
class DivUnit : public ClockedObject {
    DelayedResult<ExecResult> divResult_{1};  // maxInflight = 1，执行期间阻塞

    void process() override {
        divResult_.tick();

        if (inputPort_.isValid() && divResult_.canAccept()) {
            auto& uop = inputPort_.peek();
            ExecResult res;
            res.value = uop.src1 / uop.src2;
            res.robId = uop.robId;
            int latency = computeDivLatency(uop);  // 延迟取决于操作数
            divResult_.push(res, latency);
        }

        if (divResult_.hasReady()) {
            outputPort_.send(divResult_.popReady());
        }

        SIM_ASSERT(divResult_.inflightCount() <= 1,
                   fullPath() + ": DIV unit accepted new input while busy");
    }
};
```

### 1.6 仿真主循环 ✅

```cpp
class Simulator {
public:
    // ── 系统构建 ──
    // 构建模块树 → 绑定端口 → 调用 init() → startup()
    void buildSystem(SimModule* top);

    // 注册模块到 tick 调度列表
    void registerModule(ClockedObject* module);

    // 注册 TimeBuffer
    void registerBuffer(TimeBufferBase* buffer);

    // ── 仿真执行 ──
    // 按反向顺序调用各模块的 tick()
    void runOneCycle();

    // 推进所有 TimeBuffer
    void advanceBuffers();

    // 完整仿真循环
    void run(uint64_t numCycles);

    // ── 检查点 ──
    void serialize(const std::string& path);
    void unserialize(const std::string& path);

    // ── 统计 ──
    void dumpStats(std::ostream& os);

private:
    std::vector<ClockedObject*> tickOrder_;      // 按从后往前排列
    std::vector<TimeBufferBase*> timeBuffers_;   // 所有 TimeBuffer
    uint64_t curCycle_ = 0;
};
```

**运行时序**：

```
每个仿真周期：
  1. commit.tick()      // 提交级先执行，释放 ROB 条目
  2. execute.tick()     // 执行级
  3. issue.tick()       // 发射级
  4. dispatch.tick()    // 分派级
  5. rename.tick()      // 重命名级
  6. decode.tick()      // 解码级
  7. fetch.tick()       // 取指级最后执行
  8. advanceBuffers()   // 所有 TimeBuffer 推进一格
```

**反向 tick 顺序的原因**（参考 gem5 O3 CPU）：后级先执行使得前级能在同一周期看到后级释放的资源（如 ROB 条目、空闲物理寄存器），避免资源释放延迟一个周期。

**多核扩展性**：当前 `Simulator` 类为单核设计（单一 `tickOrder_` 和 `curCycle_`）。`verification-requirments.md` 要求"架构需保证多核可扩展性"。多核演进方向如下：

- **每个核心对应一个独立的模块树**：SimModule 的 parent/child 层次天然支持多实例——构建多棵以 `CPUCore_0`、`CPUCore_1` 为根的子树即可。
- **Simulator 扩展为多核调度器**：持有 `std::vector<CoreContext>` 而非单一 `tickOrder_`，每个 `CoreContext` 包含该核心的模块列表和 TimeBuffer 集合。`runOneCycle()` 按核心遍历，核心间的交互（如缓存一致性、共享 LLC）通过 Port 系统在核心间建立连接。
- **Difftest 的多核支持**：`DifftestChecker` 已接受 `coreid` 参数。多核下每个核心持有独立的 DifftestChecker 实例。参考模型方面，NEMU 已原生支持多核（`NemuProxy` 的 `multiCore_` 参数），Spike 可配置多 hart。
- **当前决策**：当前阶段聚焦单核实现和验证。上述扩展方向确保 Simulator、SimModule、DifftestChecker 的接口设计不会阻塞未来的多核演进，但多核的具体调度策略、缓存一致性协议、核间通信机制等需要在多核需求明确后再详细设计。

**⚠️ 待确认设计**：以下两个横切机制依赖模拟器的具体模块设计，当前标注为等待确认：

- **Flush 传播机制**：`verification-requirments.md` 全局不变量要求"flush 信号在所有模块的同一个周期生效"。flush 的发起者（ROB/CommitStage）、广播方式（通过 SignalPort 一对多广播还是 Simulator 主循环统一分发）、以及各模块收到 flush 后的标准处理接口，需要在模拟器具体设计确定后补充。当前框架已通过 `SignalPort` 的广播能力和模块接口约束（所有通信通过 Port）为 flush 机制提供了实现基础。
- **内存子系统接口**：DCache、PTW、LSU 与内存系统的交互协议（`MemRequest`/`MemResponse` 结构体定义、内存系统拓扑）依赖存储子系统的具体设计。当前框架已通过 `RequestPort<ReqType, RespType>` / `ResponsePort<ReqType, RespType>` 提供了双向通信的类型化端口，具体的请求/响应类型需等待模拟器设计确定后填充。

### 1.7 模块接口约束与可测试性设计原则 ⚠️

> **依赖说明**：以下定义的是验证接口对**任意流水线模块**的约束条件。这些约束独立于具体模块设计——无论模块内部如何实现，只要满足这些约束，就能被验证框架（Trace 录制、Bridge 桥接、Stub 替换、定向 I/O 测试）所支持。

每个流水线模块必须满足以下接口约束（前 5 条为验证接口约束，后 4 条为可测试性设计原则，对应 `verification-requirments.md` 3.3）：

**验证接口约束**：

1. **继承 `ClockedObject`，实现 `process()`**：所有周期逻辑集中在一个方法中，LLM 可一次性阅读理解（对应 `verification-requirments.md` 2.2）。

2. **通过 `getPort()` 暴露全部 I/O 端口**：所有输入和输出都必须通过 Port 系统传递，不允许模块间直接共享状态。这是模块可替换性的基础（对应 `verification-requirments.md` 3.2）。

3. **实现 `serialize()/unserialize()`**：有状态的模块必须能导出和恢复内部状态，用于 Trace 录制、检查点和 StubModule 状态注入（对应 `verification-requirments.md` 3.3）。

4. **在 `process()` 末尾调用断言检查**：有状态模块应在每周期逻辑执行完毕后检查内部不变量（对应 `verification-requirments.md` 4.3 运行时断言）。断言不是事后补充的测试，而是模块规格的一部分——应在编写 `process()` 逻辑时同步编写。

5. **端口使用 `PortBundle` 组织**：将相关端口组合为端口束，使模块替换时能一次性验证接口兼容性。

**可测试性设计原则**（对应 `verification-requirments.md` 3.3 "模块可测试性设计原则"，是模拟器正确性验证策略的硬性设计约束）：

6. **接口清晰**：模块的输入和输出通过明确的端口定义，不依赖全局状态或隐式的共享变量。模块通过端口与外界通信，而非直接访问其他模块的内部数据。

7. **可独立实例化**：模块可以脱离完整 CPU 环境单独创建，上下游用 StubModule 替代即可运行。不需要初始化整个 CPU 才能测试一个子模块。这是支撑定向 I/O 测试（`verification-requirments.md` 3.3 方式三）和模块级联合仿真（`verification-requirments.md` 5.2）的前提。

8. **状态可观测**：模块的关键内部状态（如队列深度、计数器值、有限状态机当前状态）可以被测试代码查询，用于编写断言和检查。具体实现为 `serialize()` 导出 + 提供 public 查询接口（如 `queueSize()`、`freeCount()`）。

9. **确定性**：相同的输入序列产生相同的输出序列，不依赖未初始化的状态或外部随机源。这是 Trace 回放验证和定向 I/O 测试可复现的前提。

**模块骨架示例**（展示约束如何体现）：

```cpp
// ⚠️ 以下为示例骨架，具体模块的端口类型和内部状态待模拟器设计确定

class ExampleModule : public ClockedObject {
public:
    ExampleModule(const std::string& name, SimModule* parent)
        : ClockedObject(name, parent)
        , inputBundle_("input", this)
        , outputBundle_("output", this)
    {
        // 端口在构造时创建并注册到 PortBundle
        // inputBundle_.addPort("data", &dataInPort_);
        // outputBundle_.addPort("data", &dataOutPort_);
    }

    void process() override {
        // 1. 从输入端口读取数据
        // 2. 执行本模块的周期逻辑
        // 3. 将结果写入输出端口
        // 4. 调用断言检查内部不变量
        //    SIM_ASSERT(invariantHolds(), "Invariant violated in " + fullPath());
    }

    Port* getPort(const std::string& portName, int idx) override {
        auto* p = inputBundle_.getPort(portName);
        if (p) return p;
        return outputBundle_.getPort(portName);
    }

    void serialize(std::ostream& os) const override {
        // 导出内部状态（队列内容、计数器、状态机等）
    }

    void unserialize(std::istream& is) override {
        // 恢复内部状态
    }

private:
    PortBundle inputBundle_;
    PortBundle outputBundle_;
    // 具体端口和内部状态待模拟器设计确定
};
```

#### 1.7.1 各流水线模块的最小端口集合（spec change-20260408-013040）

下表汇总了新模拟器各流水线模块在 `getPort()` 中必须暴露的最小端口名（来自 `verification-backend-api.md` §2-§7）。端口名是稳定锚点——具体载荷类型可以在 §1.3.1 已经写下的占位结构体之间选取，未来 uOP 字段填实时不需要修改端口名。

| 模块 | 输入端口 | 输出端口 | 控制端口 |
|---|---|---|---|
| **Decode** | `fetch_in` | `decode_out` | `flush` |
| **Rename** | `decode_in` | `dispatch_out` | `freelist`, `flush`, `checkpoint_save`, `checkpoint_restore` |
| **Dispatch** | `rename_in` | `iq_int_out`, `iq_mem_out`, `iq_br_out`, `rob_enq`, `lsq_enq` | `flush` |
| **IssueQueue** | `dispatch_in`, `wakeup_in` | `issue_out` | `flush`, `replay` |
| **ExecUnit** | `issue_in` | `wb_out`, `redirect_out` | `flush` |
| **Writeback** | `exec_in` | `prf_write`, `wakeup_broadcast`, `rob_complete` | — |
| **Commit** | `rob_head_in` | `arch_state_update`, `store_buffer_out`, `flush_out` | `intr_pending` |
| **LSU** | `agu_in`, `dcache_resp` | `dcache_req`, `wb_out` | `flush` |

> 这张表是 §1.7 模块接口约束第 2 条"通过 getPort() 暴露全部 I/O 端口"的最小落地版。模块实现者只需保证至少存在表中列出的端口名；额外端口（如多端口 IQ 的 `issue_out_0/1/...`）通过 `getPort(name, idx)` 的 `idx` 参数扩展。FP/Vec 相关端口在 Phase 3 增加 FPIssueQueue / VecExecUnit 时再追加。

### 1.8 StubModule ✅

> 对应 `verification-requirments.md` 3.3 方式一（存根驱动）：被测模块的上下游模块用存根替代。

Stub 的设计粒度为**端口级**而非模块级——对每个端口独立挂载存根行为，而非要求在编译期知道被替代模块的完整端口束类型。这样做的好处是：模块接口变化时只需更新受影响的端口存根，不会导致所有使用该模块 Stub 的测试全部重编译；也更容易组合——测试某个模块时，上游用 `StubDataPort` 灌数据，下游用 `StubDataPort` 收数据，无需构造完整的对称存根模块。

```cpp
enum class StubMode {
    Constant,    // 每周期输出固定值
    Random,      // 每周期输出随机值
    TraceReplay  // 从录制的 Trace 文件回放
};

// ── 端口级存根：直接挂载到单个端口上 ──
template<typename T>
class StubDataPort : public DataPort<T> {
public:
    StubDataPort(const std::string& name, SimModule* owner,
                 StubMode mode = StubMode::Constant);

    // 设置固定输出值（Constant 模式）
    void setConstantOutput(const T& value);

    // 设置随机种子（Random 模式，按 T 的各字段随机填充）
    void setRandomSeed(uint64_t seed);

    // 加载 Trace 文件（TraceReplay 模式）
    void loadTrace(const std::string& tracePath);

    // 每周期由 StubModule 的 process() 驱动，按模式生成一条数据
    void generate();

private:
    StubMode mode_;
    T constantValue_;
    std::unique_ptr<TracePlayer> tracePlayer_;
    uint64_t randomSeed_;
};

// ── 模块级存根：组合端口级存根，提供统一的 tick 驱动 ──
class StubModule : public ClockedObject {
public:
    StubModule(const std::string& name, SimModule* parent);

    // 注册一个输出存根端口（由 StubModule 每周期驱动 generate()）
    template<typename T>
    StubDataPort<T>* addOutputStub(const std::string& portName,
                                    StubMode mode = StubMode::Constant);

    // 注册一个输入端口（接收但忽略数据，用于占位连接）
    template<typename T>
    DataPort<T>* addInputSink(const std::string& portName);

    void process() override;

    Port* getPort(const std::string& portName, int idx) override;

private:
    std::map<std::string, Port*> ports_;
    std::vector<std::function<void()>> generators_;  // 每周期调用所有输出存根的 generate()
};
```

**设计要点**：

- **端口级粒度**：`StubDataPort<T>` 只关心单个端口的数据类型 `T`，不需要知道被替代模块的完整端口束定义。这降低了编译期依赖——修改某个模块的端口 A 不会影响端口 B 的存根代码。
- **StubModule 为组合容器**：将多个端口级存根组合在一起，提供统一的 `process()` 驱动。测试代码通过 `addOutputStub<T>()` 逐个注册所需的输出端口和对应的存根模式。
- **三种模式互补**：
  - `Constant` 模式：最简单，每周期输出固定值（如"每周期发送 N 条有效指令"），适合探索性测试。
  - `Random` 模式：随机输入用于压力测试。
  - `TraceReplay` 模式：回放录制的 Trace 数据，适合回归验证——先录制全系统仿真中某模块的 I/O，再用 Stub 驱动该模块独立运行并比较输出。
- **⚠️ 端口数据类型**（`StubDataPort<T>` 中的 `T`）需要在模拟器设计确定后使用具体类型实例化。

---

## 二、Difftest 验证层

> 对应 `verification-requirments.md` 第四章（正确性验证方法论）。以下是 difftest 验证框架的 C++ 实现设计。

### 2.1 ISA 状态容器 ✅

每个 ISA 定义独立的 `CPU_state` 结构体，采用香山 NEMU 的哨兵模式（`difftest_state_end`）标记同步区域。

**RISC-V 状态容器**（参考 XS-GEM5 的 `riscv64_CPU_regfile` 和 NEMU `riscv64_CPU_state`）：

```cpp
struct RV64_CPU_state {
    /*** 以下字段由 regcpy 在 difftest 时同步，不要修改顺序 ***/

    // 通用寄存器
    union { uint64_t _64; } gpr[32];    // x0-x31（x0 恒为 0，比较时可跳过）

    // 浮点寄存器（RV64F/D 独立 FPR）
    union { uint64_t _64; } fpr[32];    // f0-f31

    // 程序计数器
    uint64_t pc;

    // 特权级
    uint64_t mode;                      // 0=U, 1=S, 3=M（可选 VS/VU）
    uint64_t v;                         // 虚拟化模式标志（0=非虚拟化, 1=虚拟化）

    // CSR（基本集）
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

    // CSR（Hypervisor 扩展，可选）
    uint64_t mtval2;
    uint64_t mtinst;
    uint64_t hstatus;
    uint64_t hideleg;
    uint64_t hedeleg;
    uint64_t hcounteren;
    uint64_t htval;
    uint64_t htinst;
    uint64_t hgatp;
    uint64_t vsstatus;
    uint64_t vstvec;
    uint64_t vsepc;
    uint64_t vscause;
    uint64_t vstval;
    uint64_t vsatp;
    uint64_t vsscratch;

    // 向量扩展（RVV）
    union { uint64_t _64[VENUM64]; } vr[32];  // 32 个向量寄存器
    uint64_t vstart;
    uint64_t vxsat;
    uint64_t vxrm;
    uint64_t vcsr;
    uint64_t vl;
    uint64_t vtype;
    uint64_t vlenb;

    // 存储写入（提交时逐条比较）
    uint64_t store_addr;
    uint64_t store_data;
    uint64_t store_mask;

    // LR/SC 保留状态（单核通常不需要同步，多核通过 SyncState 传递）
    bool     reserve_valid;
    uint64_t reserve_addr;

    // ── 哨兵字段：标记同步区域终点 ──
    uint64_t difftest_state_end;

    /*** 以上字段由 regcpy 同步。以下为运行时状态，不参与 difftest ***/
    // ...（内部微架构状态、性能计数器等）
};
```

**x86-64 状态容器**：

```cpp
struct x86_64_CPU_state {
    /*** 以下字段由 regcpy 在 difftest 时同步 ***/

    // 通用寄存器
    uint64_t gpr[16];                   // RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8-R15

    // 程序计数器
    uint64_t rip;

    // 标志寄存器
    uint64_t rflags;                    // CF, ZF, SF, OF, PF, AF 等（高 32 位保留）

    // 特权级
    uint32_t cpl;                       // 当前特权级 Ring 0-3

    // 段寄存器（选择子）
    uint16_t cs, ss, ds, es, fs, gs;

    // 控制寄存器
    uint64_t cr0;                       // 保护模式/分页控制
    uint64_t cr2;                       // 缺页线性地址
    uint64_t cr3;                       // PML4 基址
    uint64_t cr4;                       // 扩展特性
    uint64_t cr8;                       // 任务优先级（可选）

    // x87 浮点单元
    uint8_t  fpu_stack[8][10];          // 8 个 80-bit 浮点栈寄存器
    uint16_t fpu_status;                // 浮点状态字
    uint16_t fpu_control;               // 浮点控制字
    uint16_t fpu_tag;                   // 浮点标签字

    // SSE
    uint64_t xmm[16][2];               // 16 个 128-bit XMM 寄存器（每个用 2 个 uint64_t 表示）
    uint32_t mxcsr;                     // SSE 控制/状态寄存器

    // 异常状态
    uint32_t exception_num;             // 异常号（#PF=14, #GP=13, #SS=12 等）
    uint32_t error_code;                // 错误码

    // 存储写入
    uint64_t store_addr;
    uint64_t store_data;
    uint64_t store_mask;

    // ── 哨兵字段 ──
    uint64_t difftest_state_end;

    /*** 以上字段由 regcpy 同步 ***/
};
```

**同步逻辑**：

```cpp
template<typename CPUState>
constexpr size_t difftest_reg_size() {
    return offsetof(CPUState, difftest_state_end);
}

static_assert(difftest_reg_size<RV64_CPU_state>() > 0);
static_assert(difftest_reg_size<x86_64_CPU_state>() > 0);
```

**扩展方式**：在 `difftest_state_end` 之前添加新字段即可，无需修改 `regcpy()` 的同步逻辑。

### 2.2 ISA 无关状态接口 ✅

使 DifftestChecker 能以统一方式访问不同 ISA 的状态，无需在比较逻辑中区分 ISA。

```cpp
class DifftestState {
public:
    virtual ~DifftestState() = default;

    // ── 通用寄存器 ──
    virtual uint64_t getGPR(int idx) const = 0;
    virtual void setGPR(int idx, uint64_t val) = 0;
    virtual int numGPRs() const = 0;

    // ── 浮点寄存器 ──
    virtual uint64_t getFPR(int idx) const = 0;
    virtual void setFPR(int idx, uint64_t val) = 0;
    virtual int numFPRs() const = 0;    // RV64: 32, x86-64: 0（通过 x87/XMM 单独访问）

    // ── PC ──
    virtual uint64_t getPC() const = 0;
    virtual void setPC(uint64_t pc) = 0;

    // ── 特权级 ──
    virtual uint32_t getPrivilege() const = 0;

    // ── CSR/控制寄存器 ──
    virtual uint64_t getCSR(int idx) const = 0;
    virtual int numCSRs() const = 0;

    // ── 存储写入 ──
    virtual uint64_t getStoreAddr() const = 0;
    virtual uint64_t getStoreData() const = 0;
    virtual uint64_t getStoreMask() const = 0;

    // ── 原始状态指针（用于 regcpy 批量同步）──
    virtual void* rawPtr() = 0;
    virtual size_t syncSize() const = 0;
};

class RV64DifftestState : public DifftestState {
public:
    explicit RV64DifftestState(RV64_CPU_state* state);
    // 实现所有纯虚函数，委托到 RV64_CPU_state 字段
    // numGPRs() → 32, numFPRs() → 32, numCSRs() → 40（基本17 + H扩展16 + 向量7）
private:
    RV64_CPU_state* state_;
};

class X86DifftestState : public DifftestState {
public:
    explicit X86DifftestState(x86_64_CPU_state* state);
    // 实现所有纯虚函数，委托到 x86_64_CPU_state 字段
    // numGPRs() → 16（RAX-R15）, numCSRs() → 5（CR0/CR2/CR3/CR4/CR8）
private:
    x86_64_CPU_state* state_;
};
```

### 2.3 Difftest API 接口 ✅

参考 XS-GEM5 的 RefProxy 接口设计，定义以下函数接口。分为核心 API（所有参考模型必须实现）和扩展 API（按需实现）。ISA 差异封装在状态容器中，API 签名不含 ISA 特有概念。

```cpp
// ── 方向常量（参考 XS-GEM5）──
constexpr bool DIFFTEST_TO_REF   = true;   // DUT → REF 方向
constexpr bool REF_TO_DIFFTEST   = false;  // REF → DUT 方向

// ── 核心 API（所有参考模型必须实现）──

void difftest_init(int port);                                              // 初始化参考模型
void difftest_exec(uint64_t n);                                            // 执行 n 条指令
void difftest_regcpy(void* dut, bool direction);                           // GPR + FPR 同步
void difftest_csrcpy(void* dut, bool direction);                           // CSR 同步
void difftest_memcpy(paddr_t addr, void* buf, size_t n, bool direction);   // 内存同步
void difftest_raise_intr(uint64_t no);                                     // 中断注入
vaddr_t difftest_guided_exec(void* guide);                                 // 引导执行（异常处理）
int  difftest_store_commit(uint64_t* addr, uint64_t* data, uint8_t* mask); // 存储提交比较
void difftest_uarchstatus_cpy(void* dut, bool direction);                  // 微架构状态同步（LR/SC）

// ── 扩展 API（按需实现，函数指针可为 nullptr）──

void difftest_memcpy_init(paddr_t addr, void* buf, size_t n, bool direction);  // 初始化内存同步（可优化）
void difftest_get_backed_memory(void* backed_mem, size_t n);                   // COW 内存共享
vaddr_t difftest_update_config(void* config);                                  // 动态配置（调试模式等）
void difftest_query(void* result, uint64_t type);                              // 查询 REF 内部状态
void difftest_isa_reg_display(void);                                           // 调试用寄存器显示
void difftest_debug_mem_sync(paddr_t addr, void* bytes, size_t size);          // 调试内存同步
```

**ExecutionGuide 结构体**（参考 XS-GEM5 `ExecutionGuide`）：

```cpp
struct ExecutionGuide {
    // 强制触发异常
    bool     force_raise_exception;
    uint64_t exception_num;        // RISC-V: mcause 值; x86-64: 向量号
    uint64_t mtval;                // RISC-V: mtval; x86-64: CR2（缺页线性地址）
    uint64_t stval;                // RISC-V: stval; x86-64: error_code

    // Hypervisor 扩展（RISC-V H 扩展，虚拟化模式下使用）
    uint64_t mtval2;               // 二级异常值（如 guest 物理地址）
    uint64_t htval;                // Hypervisor trap value
    uint64_t vstval;               // VS 模式的 trap value

    // 强制设置跳转目标
    bool     force_set_jump_target;
    uint64_t jump_target;          // 异常/中断处理入口地址
};
```

**DynamicConfig 结构体**（参考 XS-GEM5 `DynamicConfig`，用于运行时调整 REF 行为）：

```cpp
struct DynamicConfig {
    bool ignore_illegal_mem_access;    // 忽略非法内存访问（调试用）
    bool debug_difftest;               // 启用 REF 的调试输出
};
```

**SyncState 结构体**（参考 XS-GEM5，用于 LR/SC 原子操作同步）：

```cpp
struct SyncState {
    uint64_t lrscValid;                // SC 操作是否成功
    uint64_t lrscAddr;                 // LR/SC 监控的物理地址
};
```

**DiffAt 枚举**（参考 XS-GEM5，差异类型分类，用于 PC 不匹配恢复等逻辑）：

```cpp
enum class DiffAt {
    NoneDiff,    // 无差异
    PCDiff,      // PC 不匹配
    NPCDiff,     // 下一 PC 不匹配
    InstDiff,    // 指令编码不匹配
    ValueDiff    // 寄存器/CSR 值不匹配
};
```

**store_commit_t 结构体**（参考 NEMU `store_commit_t`）：

```cpp
struct store_commit_t {
    uint64_t addr;                 // 存储物理地址
    uint64_t data;                 // 存储数据
    uint8_t  mask;                 // 写掩码（按字节）
};
```

### 2.4 Proxy 工厂类 ✅

Proxy 工厂模式参考 xs-gem5 的 `RefProxy` 基类，通过 `dlopen/dlsym` 在运行时加载不同的参考模型共享库。

```cpp
class RefProxy {
public:
    virtual ~RefProxy();

    virtual void initState(int coreid, uint8_t* goldenMem) = 0;

    // ── 函数指针（由 dlsym 填充，参考 XS-GEM5 RefProxy）──
    // 内存操作
    void  (*memcpy)(paddr_t addr, void* buf, size_t n, bool direction) = nullptr;
    void  (*memcpy_init)(paddr_t addr, void* buf, size_t n, bool direction) = nullptr;
    void  (*ref_get_backed_memory)(void* backed_mem, size_t n) = nullptr;
    // 寄存器操作
    void  (*regcpy)(void* dut, bool direction) = nullptr;
    void  (*csrcpy)(void* dut, bool direction) = nullptr;
    void  (*uarchstatus_cpy)(void* dut, bool direction) = nullptr;  // LR/SC 状态同步
    // 执行
    void  (*exec)(uint64_t n) = nullptr;
    vaddr_t (*guided_exec)(void* guide) = nullptr;
    // 中断和控制
    void  (*raise_intr)(uint64_t no) = nullptr;
    vaddr_t (*update_config)(void* config) = nullptr;
    void  (*query)(void* result, uint64_t type) = nullptr;
    // 存储和调试
    int   (*store_commit)(uint64_t* addr, uint64_t* data, uint8_t* mask) = nullptr;
    void  (*isa_reg_display)() = nullptr;
    void  (*debug_mem_sync)(paddr_t addr, void* bytes, size_t size) = nullptr;

protected:
    void* handle_ = nullptr;       // dlopen 句柄

    template<typename FuncPtr>
    FuncPtr loadSymbol(const char* name) {
        auto sym = dlsym(handle_, name);
        assert(sym && "Failed to load difftest symbol");
        return reinterpret_cast<FuncPtr>(sym);
    }
};
```

**RISC-V Proxy（加载 Spike .so，单核优先）**：

```cpp
class SpikeProxy : public RefProxy {
public:
    explicit SpikeProxy(int coreid, const char* soPath = "libspike_ref.so");
    void initState(int coreid, uint8_t* goldenMem) override;
private:
    void loadSharedLib(const char* soPath);
};
```

**RISC-V Proxy（加载 NEMU .so，支持多核）**：

```cpp
class NemuProxy : public RefProxy {
public:
    explicit NemuProxy(int coreid, const char* soPath, bool multiCore = false);
    void initState(int coreid, uint8_t* goldenMem) override;
private:
    bool multiCore_;
    void loadSharedLib(const char* soPath);
};
```

**x86-64 Proxy（加载 QEMU TCG .so）**：

```cpp
class QemuProxy : public RefProxy {
public:
    explicit QemuProxy(int coreid, const char* soPath = "libqemu_x86_64_ref.so");
    void initState(int coreid, uint8_t* goldenMem) override;
private:
    void loadSharedLib(const char* soPath);
};
```

> **QEMU 封装策略**：QEMU 不像 Spike 有原生的 difftest 接口，需要编写 wrapper 层将 TCG 执行封装为 step-by-step 接口。参考 XiangShan NEMU 的 `tools/qemu-dl-diff/` 实现：通过 `setjmp/longjmp` 桥接 QEMU 的执行循环，使每次 `exec(1)` 精确执行一条指令后返回；处理 QEMU 全局状态隔离，确保 `dlmopen` 加载后不与 DUT 的符号冲突。

**librefcpu Proxy（静态链接启蒙3号参考模型）**：

```cpp
class LibRefCpuProxy : public RefProxy {
public:
    explicit LibRefCpuProxy(int coreid);
    void initState(int coreid, uint8_t* goldenMem) override;
    // 不需要 dlopen，直接链接 librefcpu.a 的 API
    // init/reset/step、get_state/set_state
};
```

**Proxy 工厂函数**：

使用 ISA 特定的参考模型枚举，在类型层面保证 ISA 与参考模型的合法组合，避免运行时传入无效组合（如 RV64 + QEMU 或 x86-64 + Spike）。

```cpp
enum class ISAType { RV64, x86_64 };

// ISA 特定的参考模型类型——编译期保证合法组合
enum class RV64RefType  { Spike, NEMU, LibRefCpu };
enum class X86RefType   { QEMU };

// RV64 工厂
std::unique_ptr<RefProxy> createRV64RefProxy(RV64RefType ref, int coreid) {
    switch (ref) {
        case RV64RefType::Spike:     return std::make_unique<SpikeProxy>(coreid);
        case RV64RefType::NEMU:      return std::make_unique<NemuProxy>(coreid, "libnemu_ref.so");
        case RV64RefType::LibRefCpu: return std::make_unique<LibRefCpuProxy>(coreid);
    }
}

// x86-64 工厂
std::unique_ptr<RefProxy> createX86RefProxy(X86RefType ref, int coreid) {
    switch (ref) {
        case X86RefType::QEMU:       return std::make_unique<QemuProxy>(coreid);
    }
}
```

**设计要点**：

- **类型安全的 ISA-Ref 组合**：将参考模型枚举按 ISA 分为 `RV64RefType` 和 `X86RefType`，使用两个独立的工厂函数。调用方在编译期就被限制只能传入合法的 ISA-Ref 组合，不可能构造出 `RV64 + QEMU` 或 `x86-64 + Spike` 这样的非法组合。
- **dlopen 隔离**：使用 `dlmopen(LM_ID_NEWLM, ...)` 替代 `dlopen` 实现符号隔离，防止参考模型的全局符号与模拟器自身冲突。
- **RISC-V 可选三后端**：Spike（单核优先）、NEMU（支持多核）、启蒙3号的 `librefcpu.a`（对应 `verification-requirments.md` 4.4 Task2）。通过 `RV64RefType` 参数切换。

### 2.5 uOP 过滤与宏指令提交 ✅（spec change-20260408-013040）

> **依赖说明**：过滤逻辑的框架及字段集合已经在 `code/uOP` 占位基线 + `verification-backend-api.md` §1 字段集合下完整确定。仅 `uop_type` 的最终枚举值与 `except_code` 到 ISA 异常号的映射仍需等待后端正式 uOP 版本。

**核心问题**（对应 `verification-requirments.md` 4.2 "uOP 过滤与融合指令"）：后端以 uOP 为粒度运行，ROB 中存的是 uOP。但 difftest 在宏指令粒度比较。采用 XS-GEM5 验证的 `isLastMicroop()` 过滤模式——无需显式的聚合数据结构，只需在提交时判断当前 uOP 是否为宏指令的最后一条。

> **占位 uOP 命名空间约定**：`uop_type_placeholder` 字段当前**暂用 `code/uOP/uop指令说明.md` 字典的 uOP 名称**作为枚举值（如 `add_32U`、`subs_32U`、`ldx_w_shift`、`fadd_s_rem`、`pcaddi_32` 等），共 ~400 项。后端正式 uOP 出来后**仅需替换该枚举的具体常量集合**，`UopCommitInfo` 结构体的字段名和数量保持不变。详见附录 A。

```cpp
// 每条 uOP 提交时传递给 DifftestChecker 的信息
struct UopCommitInfo {
    // ── 已固化字段（不再依赖 uOP 正式版） ──
    uint64_t inst_seq;             // 宏指令序列号
    uint64_t pc;                   // 宏指令 PC（64 位）
    bool     is_microop;           // 是否为 micro-op（false = 单 uOP 指令，不拆分）
    bool     is_last_microop;      // 是否为宏指令的最后一条 micro-op（= backend §1 last_uop）
    bool     is_fusion;            // 是否为融合指令（两条原始指令合并）
    bool     is_mmio;              // 是否为 MMIO 访问（strictlyOrdered）
    bool     is_store;             // 是否包含存储操作
    bool     is_branch;            // 是否包含分支操作
    bool     is_sc;                // 是否为 SC 原子操作（用于 uarchstatus_cpy 触发）
    uint8_t  macro_id;             // 同一宏指令的连续编号 from backend §1
    uint16_t uop_type_placeholder; // 占位 uOP 类型（来自 code/uOP 命名空间，详见附录 A）
    MemAttr  mem_attr;             // 访存属性（普通/索引/PC 相对/前后更新）
    uint8_t  fpr_writeback_mode;   // 0=full, 1=rem(保留高位), 2=clr(清高位)
    uint32_t except_code;          // 0 表示无异常；非零为内部统一异常码

    // DUT 侧的执行结果（由 ROB 提交时填充）
    // 固定大小数组：单条宏指令的目的寄存器数量有硬上限（通常 ≤ 2-3），
    // 避免 std::vector 的堆分配开销（该结构体在每条指令提交时创建，频率极高）
    static constexpr int MAX_DEST_REGS = 4;
    std::pair<int, uint64_t> dest_regs[MAX_DEST_REGS];  // (寄存器索引, 值)
    int num_dest_regs = 0;                               // 实际目的寄存器数量
    uint64_t store_addr;           // 存储地址（仅存储指令）
    uint64_t store_data;           // 存储数据
    uint64_t store_mask;           // 存储掩码

    // ⚠️ 仍待后端正式 uOP 版本的项：
    // - uop_type_placeholder 的最终枚举值（从 code/uOP 命名空间替换为后端通用 uOP）
    // - except_code 到 ISA 可见异常号（mcause / IDT vector）的具体映射表
};
```

**过滤逻辑**（参考 XS-GEM5 的 `difftestStep`）：

```cpp
bool shouldDiff(const UopCommitInfo& info) {
    // 只在以下情况触发 difftest：
    // 1. 非 micro-op（单 uOP 指令，不拆分）
    // 2. 宏指令的最后一条 micro-op
    return !info.is_microop || info.is_last_microop;
}
```

**与 ROB commit 的集成点**：ROB 的 `commit()` 方法在每个 uOP 提交时构造 `UopCommitInfo` 并调用 `DifftestChecker::onUopCommit()`。Checker 内部通过 `shouldDiff()` 判断是否触发 difftest 比较。

**冲刷处理**：ROB 保证按序提交，冲刷发生在提交之前。被冲刷的指令不会进入 commit 流程，因此不存在"部分 uOP 已提交、部分被取消"的情况，无需回滚 REF 状态。

### 2.6 特殊处理逻辑 ✅

以下是 `verification-requirments.md` 4.2 节统一表格中 16 个特殊处理场景的实现方案。按逻辑分组说明。

#### 2.6.1 uOP 过滤与融合指令（共有）

```cpp
void DifftestChecker::onUopCommit(const UopCommitInfo& info) {
    if (!shouldDiff(info)) return;  // 中间 micro-op 直接跳过

    if (info.is_mmio) {
        handleMmio(info);           // MMIO 指令特殊处理
        return;
    }

    // SC 指令需在执行前同步 LR/SC 状态
    if (info.is_sc) {
        ref_->uarchstatus_cpy(&syncState_, DIFFTEST_TO_REF);
    }

    // 正常指令：让 REF 执行并比较
    ref_->exec(1);
    if (info.is_fusion) {
        ref_->exec(1);             // 融合指令需让 REF 执行两步
    }
    ref_->regcpy(refState_->rawPtr(), REF_TO_DIFFTEST);

    auto diff_at = compareAll(info);
    if (diff_at != DiffAt::NoneDiff) {
        diff_at = handleMismatch(diff_at, info);
    }
}
```

#### 2.6.2 MMIO 跳过（共有）

```cpp
void DifftestChecker::handleMmio(const UopCommitInfo& info) {
    // 跳过 REF 执行，直接用 DUT 结果同步 REF
    // 更新 REF 的 PC 和目标寄存器值
    ref_->regcpy(dutState_->rawPtr(), DIFFTEST_TO_REF);
}
```

MMIO 地址范围通过可配置的 `MmioRegion` 注册，覆盖 UART、PLIC、CLINT 等全部 MMIO 地址。

#### 2.6.3 中断注入（共有）

```cpp
void DifftestChecker::onInterrupt(uint64_t intNo) {
    ref_->raise_intr(intNo);
    // 同步 DUT 完整状态到 REF（中断处理后的 PC、CSR 等）
    ref_->regcpy(dutState_->rawPtr(), DIFFTEST_TO_REF);
    // RISC-V: intNo = mcause 值（最高位=1 表示中断）
    // x86-64: intNo = IDT 向量号
    // ISA 差异由 REF（Spike/QEMU）内部处理
}
```

#### 2.6.4 异常引导执行（共有）

```cpp
void DifftestChecker::onException(uint64_t excNo, uint64_t mtval, uint64_t stval,
                                   uint64_t jumpTarget) {
    ExecutionGuide guide = {};
    guide.force_raise_exception = true;
    guide.exception_num = excNo;
    guide.mtval = mtval;           // RISC-V: mtval; x86-64: CR2
    guide.stval = stval;           // RISC-V: stval; x86-64: error_code
    // Hypervisor 扩展字段（H 扩展启用时填充）
    if (enableRVH_) {
        guide.mtval2 = readCSR(CSR_MTVAL2);
        guide.htval  = readCSR(CSR_HTVAL);
        guide.vstval = readCSR(CSR_VSTVAL);
    }
    guide.force_set_jump_target = true;
    guide.jump_target = jumpTarget;
    ref_->guided_exec(&guide);
    ref_->regcpy(refState_->rawPtr(), REF_TO_DIFFTEST);
}
```

`ExecutionGuide` 的字段在两种 ISA 下的映射：

| 字段 | RISC-V | x86-64 |
|------|--------|--------|
| `exception_num` | mcause/scause 值（如 12=指令缺页, 13=加载缺页） | 向量号（如 13=#GP, 14=#PF, 12=#SS） |
| `mtval` | mtval（触发异常的虚拟地址） | CR2（缺页线性地址） |
| `stval` | stval | error_code（异常专属错误码） |
| `jump_target` | mtvec/stvec 指定的异常处理入口 | IDT 中查找的处理入口 |

#### 2.6.5 系统指令 CSR/MSR 跳过（共有）

```cpp
class SkipCSRList {
public:
    // 按指令编码的 CSR 字段匹配（参考 XS-GEM5 skipCSRs）
    void addCSREncoding(uint64_t encoding);
    void addInstType(uint32_t instType);
    bool shouldSkip(uint64_t machInst) const;
    // RISC-V: ecall, ebreak, mret, sret, csrrw/csrrs/csrrc
    // x86-64: INT, IRET, MOV CR, WRMSR, LGDT/LIDT 等
private:
    std::set<uint64_t> csrEncodings_;
    std::set<uint32_t> instTypes_;
};
```

跳过时只比较 GPR/FPR 和 PC，将 DUT 的 CSR/MSR 值同步到 REF。清单可配置。

#### 2.6.6 性能计数器/时间戳跳过（共有）

```cpp
class PerfCntSkipList {
public:
    void initRV64();     // 注册 mcycle, minstret, mhpmcounter3-31 等
    void initX86_64();   // 注册 TSC, IA32_PERF_*, IA32_TSC_AUX
    bool shouldSkip(uint64_t machInst) const;
    // 匹配时将 DUT 值同步到 REF
};
```

#### 2.6.7 NaN Boxing 容忍（RISC-V）

```cpp
bool DifftestChecker::compareFPR(int idx) {
    uint64_t dut_val = dutState_->getFPR(idx);
    uint64_t ref_val = refState_->getFPR(idx);
    if (dut_val == ref_val) return true;
    // NaN Boxing 容忍：高 32 位差异为 0xFFFFFFFF 时忽略
    if (isa_ == ISAType::RV64 && (dut_val ^ ref_val) == 0xFFFFFFFF00000000ULL) {
        return true;
    }
    return false;
}
```

#### 2.6.8 PC 不匹配恢复（共有）

```cpp
DiffAt DifftestChecker::handleMismatch(DiffAt diff_at, const UopCommitInfo& info) {
    if (diff_at == DiffAt::PCDiff) {
        // 检查 npc 是否匹配——若匹配，让 REF 多执行一步再比较
        ref_->exec(1);
        ref_->regcpy(refState_->rawPtr(), REF_TO_DIFFTEST);
        auto retry = compareAll(info);
        if (retry == DiffAt::NoneDiff) return retry;  // 恢复成功
    }
    reportMismatch(diff_at, info);
    return diff_at;
}
```

#### 2.6.9 RFLAGS 部分更新掩码（x86-64）

详见 2.7 节 `RflagsMask` 类。

#### 2.6.10 x87 FPU 栈状态比较（x86-64）

```cpp
bool DifftestChecker::compareX87FPU() {
    auto* dut = static_cast<x86_64_CPU_state*>(dutState_->rawPtr());
    auto* ref = static_cast<x86_64_CPU_state*>(refState_->rawPtr());
    // TOP 指针必须一致
    uint8_t dut_top = (dut->fpu_status >> 11) & 0x7;
    uint8_t ref_top = (ref->fpu_status >> 11) & 0x7;
    if (dut_top != ref_top) return false;
    // 按 Tag 字跳过空寄存器
    for (int i = 0; i < 8; i++) {
        uint8_t tag = (dut->fpu_tag >> (i * 2)) & 0x3;
        if (tag == 0x3) continue;  // 空寄存器
        if (memcmp(dut->fpu_stack[i], ref->fpu_stack[i], 10) != 0) return false;
    }
    return true;
}
```

#### 2.6.11 MXCSR 部分更新、段寄存器排除、x86-64 MSR 跳过

这三个场景的实现分别集成在 `RflagsMask`（MXCSR 异常标志位掩码）、`compareFull()`（段寄存器只比较选择子）、`PerfCntSkipList`（MSR 跳过列表）中，不需要独立的实现类。

### 2.7 x86-64 RFLAGS 掩码比较 ✅

> 对应 `verification-requirments.md` 4.2：x86-64 RFLAGS 需"按指令类型生成掩码，只检查被修改的位（如 INC 不修改 CF）"。实际使用的标志位（CF/ZF/SF/OF/PF/AF）与 32 位 EFLAGS 相同，高 32 位为保留位。

```cpp
class RflagsMask {
public:
    // 按指令类别注册掩码（参考 verification-requirments.md 掩码分类表）
    // 例如：算术类修改 CF|ZF|SF|OF|PF|AF，自增减类修改 ZF|SF|OF|PF|AF（不修改 CF）
    void registerMask(uint32_t instCategory, uint64_t mask);

    // 获取掩码：未注册返回全 1（比较所有位）
    uint64_t getMask(uint32_t instCategory) const;

    // 按掩码比较 RFLAGS（只检查掩码覆盖的位）
    bool compareRflags(uint64_t dutRflags, uint64_t refRflags,
                       uint32_t instCategory) const;

    // MXCSR 使用类似机制：异常标志位按 SSE 指令类型掩码比较
    bool compareMxcsr(uint32_t dutMxcsr, uint32_t refMxcsr,
                      uint32_t instCategory) const;

private:
    std::unordered_map<uint32_t, uint64_t> rflagsMasks_;
    std::unordered_map<uint32_t, uint32_t> mxcsrMasks_;
};
```

**设计要点**：

- 只在 `ISAType::x86_64` 模式下使用，RISC-V 无需此机制。
- 掩码表按指令类别组织（算术/逻辑/移位/自增减/乘法/除法），参考 `verification-requirments.md` 的掩码分类表。
- ⚠️ 具体的指令类别→掩码映射表内容，依赖 x86-64 Decoder 的指令分类设计。框架本身可立即实现。

#### 2.7.1 初始 instCategory 与掩码常量（spec change-20260408-013040）

借助 `code/uOP/uop指令说明.md` 字典中的 `s` 后缀约定（带 `s` 的 uOP 才会通过 `helper_lbt_x86*` 写 EFLAGS），可以**立即固化** 6 类初始 instCategory 与对应的 64 位掩码常量。后续 x86-64 Decoder 的指令类目映射表只需把每个 x86 opcode 对应到这 6 个常量，无需修改 RflagsMask 类签名。

```cpp
namespace x86_inst_category {
    constexpr uint32_t kArith   = 1;  // ADD/SUB/CMP/NEG → 修改 CF|OF|SF|ZF|PF|AF
    constexpr uint32_t kLogic   = 2;  // AND/OR/XOR/TEST → 修改 SF|ZF|PF（CF/OF 强制为 0）
    constexpr uint32_t kShift   = 3;  // SHL/SHR/SAR     → 修改 CF|OF|SF|ZF|PF
    constexpr uint32_t kIncDec  = 4;  // INC/DEC         → 修改 OF|SF|ZF|PF|AF（CF 不变）
    constexpr uint32_t kMul     = 5;  // MUL/IMUL        → 修改 CF|OF
    constexpr uint32_t kDiv     = 6;  // DIV/IDIV        → 不修改任何标志位
}

// 6 类掩码常量（位定义按 RFLAGS 标准位号：CF=0, PF=2, AF=4, ZF=6, SF=7, OF=11）
constexpr uint64_t kRflagsMaskArith  = (1ULL<<0)|(1ULL<<2)|(1ULL<<4)|(1ULL<<6)|(1ULL<<7)|(1ULL<<11); // CF|PF|AF|ZF|SF|OF
constexpr uint64_t kRflagsMaskLogic  = (1ULL<<2)|(1ULL<<6)|(1ULL<<7);                                  // PF|ZF|SF（CF/OF 强制清零，单独处理）
constexpr uint64_t kRflagsMaskShift  = (1ULL<<0)|(1ULL<<2)|(1ULL<<6)|(1ULL<<7)|(1ULL<<11);            // CF|PF|ZF|SF|OF
constexpr uint64_t kRflagsMaskIncDec = (1ULL<<2)|(1ULL<<4)|(1ULL<<6)|(1ULL<<7)|(1ULL<<11);            // PF|AF|ZF|SF|OF
constexpr uint64_t kRflagsMaskMul    = (1ULL<<0)|(1ULL<<11);                                           // CF|OF
constexpr uint64_t kRflagsMaskDiv    = 0ULL;                                                            // 无

// 工厂方法：在 RflagsMask 构造时一次性注册全部 6 类
inline void registerInitialX86Categories(RflagsMask& m) {
    m.registerMask(x86_inst_category::kArith,  kRflagsMaskArith);
    m.registerMask(x86_inst_category::kLogic,  kRflagsMaskLogic);
    m.registerMask(x86_inst_category::kShift,  kRflagsMaskShift);
    m.registerMask(x86_inst_category::kIncDec, kRflagsMaskIncDec);
    m.registerMask(x86_inst_category::kMul,    kRflagsMaskMul);
    m.registerMask(x86_inst_category::kDiv,    kRflagsMaskDiv);
}
```

> 上述 6 类直接来自 `code/uOP` 字典 `s` 后缀写者的实际行为：所有 `adds_*`、`subs_*` → kArith；`ands_*`、`ors_*`、`xors_*` → kLogic；`shls_*`、`shrs_*`、`sars_*`、`rcls_*`、`rcrs_*`、`rols_*`、`rors_*` → kShift；`inc_*`/`dec_*` 总是写 EFLAGS → kIncDec；`imuls_*` → kMul；`code/uOP` 字典里没有写 EFLAGS 的除法变体，对应 kDiv（DIV/IDIV 在 x86 也不写 RFLAGS）。该映射在 uOP 字典正式版替换后只需重写常量值或类目集合，**RflagsMask 类签名与初始化入口保持稳定**。

### 2.8 Store Commit Queue ✅

> 对应 `verification-requirments.md` 4.2 存储写入比较和术语表 store commit queue：记录每条存储指令提交时的地址、数据和写掩码，与 REF 侧逐一比较。

```cpp
class StoreCommitQueue {
public:
    explicit StoreCommitQueue(size_t capacity = 64);

    // DUT 侧存储指令提交时入队
    void enqueue(const store_commit_t& entry);

    // 与 REF 比较（调用 ref_->store_commit()）
    // 返回 true 表示匹配，false 表示不匹配
    bool checkNext(RefProxy* ref);

    // 宏指令被冲刷时清理对应的存储记录
    void flush(uint64_t from_seq);

    bool empty() const;
    size_t size() const;

private:
    std::deque<store_commit_t> queue_;
    size_t capacity_;
};
```

**设计要点**：

- 支持 uOP 架构：一条存储宏指令可能拆分为多个存储 uOP（如 x86 PUSH 拆为"地址计算 + 数据写入"）。所有存储 uOP 的 store_commit_t 在宏指令聚合完成后才入队。
- 队列容量可配置，默认 64 条（足以覆盖 ROB 中的并发存储指令）。
- 冲刷时需要清理被取消指令的存储记录（通过 `flush(from_seq)`）。

### 2.9 DifftestChecker 整体流程 ✅

将以上组件组装为完整的 difftest 检查器：

```cpp
class DifftestChecker {
public:
    // 构造函数接受已创建的 RefProxy（由 createRV64RefProxy / createX86RefProxy 工厂函数创建）
    DifftestChecker(ISAType isa, std::unique_ptr<RefProxy> ref);
    ~DifftestChecker();

    // 初始化：加载参考模型，同步初始状态
    void init(uint8_t* memory, size_t memSize, uint64_t startPC);

    // ROB 提交 uOP 时调用（集成点，参考 2.5 节的 UopCommitInfo）
    void onUopCommit(const UopCommitInfo& info);

    // 中断响应时调用
    void onInterrupt(uint64_t intNo);

    // 异常响应时调用
    void onException(uint64_t excNo, uint64_t mtval, uint64_t stval, uint64_t jumpTarget);

    // SC 结果设置（LR/SC 同步）
    void setSCSuccess(bool success, uint64_t addr);

    // 运行时切换 ISA（用于双 ISA 支持）
    void setISAType(ISAType isa);

    // 动态配置（调试模式等）
    void updateConfig(const DynamicConfig& config);

private:
    ISAType isa_;
    std::unique_ptr<RefProxy> ref_;
    MmioRegion mmioRegion_;
    SkipCSRList csrSkipList_;
    PerfCntSkipList perfCntSkipList_;
    StoreCommitQueue storeQueue_;
    SyncState syncState_;                       // LR/SC 状态
    std::unique_ptr<RflagsMask> rflagsMask_;    // 仅 x86-64 模式使用
    bool enableRVH_ = false;                    // H 扩展使能
    bool enableRVV_ = false;                    // V 扩展使能

    // ISA 无关状态访问
    std::unique_ptr<DifftestState> dutState_;
    std::unique_ptr<DifftestState> refState_;

    // 实际存储空间
    RV64_CPU_state rv64DutState_, rv64RefState_;
    x86_64_CPU_state x86DutState_, x86RefState_;

    // 过滤判断
    bool shouldDiff(const UopCommitInfo& info) const;
    // MMIO 处理
    void handleMmio(const UopCommitInfo& info);
    // 比较逻辑
    DiffAt compareAll(const UopCommitInfo& info);
    void compareGPR();
    void compareFPR();              // 含 NaN Boxing 容忍（RV64）
    void compareVecReg();           // 向量寄存器（RVV）
    void comparePC();
    void compareCSR();
    bool compareX87FPU();           // x87 FPU 栈（x86-64）
    void compareStore();
    // 不匹配处理（含 PC 恢复）
    DiffAt handleMismatch(DiffAt diff_at, const UopCommitInfo& info);
    void reportMismatch(DiffAt diff_at, const UopCommitInfo& info);

    // 指令提交历史（用于差异报告，保留最近 N 条）
    std::deque<std::string> commitHistory_;
    static constexpr size_t MAX_COMMIT_HISTORY = 20;
};
```

---

## 三、RTL 验证层

> 对应 `verification-requirments.md` 第五章 5.2-5.6（模块级联合仿真）。以下是 RTL 验证框架的 C++ 实现设计。

### 3.1 Bridge 类架构 ⚠️

> **依赖说明**：Bridge 的骨架框架（基类、生命周期、比较流程）可以立即设计。但具体的端口映射（如何将 uOP 结构体转换为 RTL 信号）需要等待 uOP 和端口定义确定。
>
> **比较边界原则**（对应 `verification-requirments.md` 5.2）：模块级联合仿真的比较**严格限定在模块的输入输出端口**，不涉及模块内部状态的对比。这不是可选的设计取舍，而是建模层次决定的必然约束——模拟器用高层逻辑 + 时序计数实现功能，RTL 用寄存器级流水线实现同一功能，两者的内部状态组织方式根本不同。但 I/O 层比较足以验证时序正确性：如果两侧在每个周期的输出端口值都一致，则 RTL 的 I/O 时序行为与模拟器完全匹配。

```cpp
class ModuleBridge : public SimModule {
public:
    ModuleBridge(const std::string& name, SimModule* parent,
                 SimModule* cppRef);
    virtual ~ModuleBridge();

    void tick() final;  // spec change-20260408-013040: 模板方法已固化为 final

protected:
    // 子类实现以下四个虚函数，完成具体模块的桥接
    virtual void driveRtlInputs() = 0;        // 将 C++ 侧输入转换为 RTL 信号
    virtual void evalRtl() = 0;                // 驱动 RTL 模型执行一个周期
    virtual void readRtlOutputs() = 0;         // 读取 RTL 输出并转换回逻辑类型
    virtual void compareOutputs() = 0;         // 比较 C++ 和 RTL 的输出

    SimModule* cppRef_;                        // C++ 参考实现
    DiffReport lastDiff_;                      // 最近一次比较结果
};

> **设计说明（spec change-20260408-013040）**：`ModuleBridge::tick()` 是模板方法，已固化为 `final`。子类只能通过四个虚函数 `driveRtlInputs / evalRtl / readRtlOutputs / compareOutputs` 接入具体桥接逻辑，不能覆盖 `tick()` 自身——这保证所有 Bridge 子类都遵循"先 C++ 参考再 RTL，最后比较"的统一时序。该约束让 Bridge 框架部分完全 ✅，子类只需关心字段映射。

// tick() 的标准流程
void ModuleBridge::tick() {
    // 1. C++ 参考模块执行
    cppRef_->tick();

    // 2. 将输入转换为 RTL 信号并灌入
    driveRtlInputs();

    // 3. RTL 模块执行一个周期（clock 0→1）
    evalRtl();

    // 4. 读取 RTL 输出
    readRtlOutputs();

    // 5. 比较两侧输出
    compareOutputs();
}
```

**具体模块的 Bridge 示例**（以 Rename 为例，⚠️ 端口字段待确定）：

```cpp
class RenameBridge : public ModuleBridge {
public:
    RenameBridge(SimModule* parent, RenameModule* cppRef, VRename* rtlDut)
        : ModuleBridge("rename_bridge", parent, cppRef)
        , rtlDut_(rtlDut) {}

protected:
    void driveRtlInputs() override {
        // 从上游 TimeBuffer 读取输入，转换为 RTL 信号
        // auto& input = inputBuffer_->readSlot();
        // rtlDut_->io_in_valid = input.valid;
        // ... 更多字段映射（依赖 uOP 和端口定义）
    }

    void evalRtl() override {
        rtlDut_->clock = 0;
        rtlDut_->eval();
        rtlDut_->clock = 1;
        rtlDut_->eval();
    }

    void readRtlOutputs() override {
        // rtlOutput_.valid = rtlDut_->io_out_valid;
        // ... 更多字段映射
    }

    void compareOutputs() override {
        // auto diff = fieldCompare(cppOutput, rtlOutput_);
        // if (diff.hasMismatch()) reportMismatch(diff);
    }

private:
    VRename* rtlDut_;              // Verilator 生成的 RTL 模型
};
```

### 3.2 DPI-C 接口设计 ✅

DPI-C 用于 RTL 与 C++ 之间的通信，主要服务两个场景。

**场景 1：调试信号导出**（RTL → C++，用于整体 RTL 验证时 RTL CPU 导出提交信号）

```verilog
// RTL 侧（SystemVerilog）
import "DPI-C" function void difftest_commit(
    input longint pc,
    input int     inst,
    input byte    rf_wnum,
    input longint rf_wdata,
    input byte    is_store,
    input longint store_addr,
    input longint store_data,
    input byte    store_mask
);

always @(posedge clock) begin
    if (commit_valid) begin
        difftest_commit(commit_pc, commit_inst,
                        commit_rf_wnum, commit_rf_wdata,
                        commit_is_store, commit_store_addr,
                        commit_store_data, commit_store_mask);
    end
end
```

```cpp
// C++ 侧
extern "C" void difftest_commit(
    uint64_t pc, uint32_t inst,
    uint8_t rf_wnum, uint64_t rf_wdata,
    uint8_t is_store, uint64_t store_addr,
    uint64_t store_data, uint8_t store_mask)
{
    difftestChecker->onRtlCommit(pc, inst, rf_wnum, rf_wdata,
                                  is_store, store_addr, store_data, store_mask);
}
```

**场景 2：模块级端口桥接**

在模块级联合仿真中，Bridge 层通常直接通过 Verilator 生成的 C++ 类成员访问 RTL 信号（如 `rtlDut_->io_in_valid`），不需要额外的 DPI-C 函数。DPI-C 主要用于整体 RTL 验证场景（RTL CPU 作为 DUT 时导出提交信号）。

### 3.3 端口映射规范 ⚠️

> 对应 `verification-requirments.md` 5.4（声明式端口定义）。

**声明式端口定义**：每个端口用 `Field<T>` 模板同时携带逻辑语义和信号映射信息。确保满足 `verification-requirments.md` 5.4 的三个目标：LLM 可读、Bridge 自动生成映射、uOP 唯一依赖点。

#### 3.3.1 已确定的初始位宽常量（spec change-20260408-013040）

部分字段的位宽**不依赖** uOP 正式版，可以现在写死作为 `Field<T>` 的模板实参；其余字段（uop_type、src/dst 寄存器号、imm、ctrl_flag、except_hint）的位宽仍待 uOP 与 PRF 大小确定。

```cpp
// 已确定的字段位宽（不依赖 uOP 正式版）
constexpr int kFieldWidthValid    = 1;   // 单 bit valid
constexpr int kFieldWidthLastUop  = 1;   // backend §1 last_uop
constexpr int kFieldWidthMacroId  = 8;   // 同一宏指令最多 256 条 uOP，足够覆盖 x86 复杂指令
constexpr int kFieldWidthInstSeq  = 64;  // 指令序列号
constexpr int kFieldWidthPC       = 64;  // 64 位 PC

// 待 uOP 正式版填充的位宽：
//   - kFieldWidthUopType   = log2(uop_type 枚举大小)
//   - kFieldWidthSrcReg    = log2(PRF_SIZE)
//   - kFieldWidthDstReg    = log2(PRF_SIZE)
//   - kFieldWidthImm       = 8/12/16/32/64 之间，由 uop_type 决定
//   - kFieldWidthCtrlFlag  = 16（占位）
//   - kFieldWidthExceptHint= 8（占位）
```

```cpp
template<typename LogicType, int BitWidth = sizeof(LogicType) * 8>
struct Field {
    LogicType value;

    static constexpr int bitWidth() { return BitWidth; }
    static constexpr int byteWidth() { return (BitWidth + 7) / 8; }

    // 序列化为位向量（用于 Verilator 桥接）
    void toBits(uint8_t* buf) const;
    void fromBits(const uint8_t* buf);
};

// ── 信号映射描述 ──
struct SignalMapping {
    std::string fieldName;         // C++ 字段名（如 "valid"）
    std::string rtlSignalName;     // RTL 信号名（如 "io_in_valid"）
    int bitWidth;                  // 位宽
    int bitOffset;                 // 在打包位向量中的偏移
};

// ── 端口声明示例（⚠️ uOP 规格确定后填充具体字段）──
struct RenameInputPort {
    Field<bool, kFieldWidthValid>       valid;  // 使用 §3.3.1 已确定的位宽常量
    // Field<UopType, ?>  uop_type;        // 微操作类型（位宽待定）
    // Field<RegIdx, ?>   src1_areg;       // 源操作数 1（位宽待定）
    // Field<RegIdx, ?>   src2_areg;       // 源操作数 2
    // Field<RegIdx, ?>   dest_areg;       // 目的操作数
    // ...

    // 自动生成 Verilator 信号映射
    static const std::vector<SignalMapping>& signalMap();
};
```

**设计要点**：

- **双层描述统一**：`Field<LogicType, BitWidth>` 的模板参数同时承载逻辑类型（LLM 可读）和位宽（Verilator 可接）。
- **信号映射自动化**：`SignalMapping` 表可通过宏或 constexpr 在编译期生成，Bridge 层据此自动完成 C++ ↔ RTL 信号转换。
- **⚠️ uOP 字段占位**：注释掉的字段需要在 uOP 和端口设计确定后取消注释并填写正确的位宽。

### 3.4 Trace 录制与回放 ⚠️

> 对应 `verification-requirments.md` 3.3 方式二（Trace 回放驱动）和 5.2（Trace 录制与回放）。

```cpp
class TraceRecorder {
public:
    TraceRecorder(const std::string& outputPath);
    ~TraceRecorder();

    // 记录一条端口数据（由 Port 的录制钩子自动调用）
    void record(uint64_t cycle, const std::string& portName,
                const void* data, size_t size);

    // 写入文件头（记录端口列表和数据格式）
    void writeHeader(const std::vector<PortInfo>& ports);

    void flush();

private:
    std::ofstream file_;
};

class TracePlayer {
public:
    TracePlayer(const std::string& inputPath);

    bool readNext(uint64_t& cycle, std::string& portName,
                  void* data, size_t maxSize);

    bool eof() const;

private:
    std::ifstream file_;
};

struct PortInfo {
    std::string name;              // 端口名称
    std::string modulePath;        // 所属模块的层次路径
    size_t dataSize;               // 每条数据的字节数
};
```

**三种使用场景**：

1. **全系统仿真录制**：启用目标模块所有端口的 `enableTrace()`，运行仿真，得到 trace 文件。
2. **独立回放验证**：用 `TracePlayer` 读取输入 trace，驱动模块（或 Verilator RTL）运行，比较输出与录制的参考输出。
3. **Bug 定位**：分析 trace 文件，找到第一个输出偏差出现的周期和端口。

### 3.5 BSD 位级展开层 ❌

> **依赖说明**：BSD 需要端口的精确位宽信息。在 uOP 规格和端口声明确定前，该模块无法进行具体设计。以下仅描述接口框架。

```cpp
class BitLevelFlattener {
public:
    // 将端口数据展开为 bool 数组（BSD 输入格式）
    static std::vector<bool> flatten(const void* portData,
                                      const std::vector<SignalMapping>& mapping);

    // 从 bool 数组恢复端口数据
    static void unflatten(const std::vector<bool>& bits,
                          void* portData,
                          const std::vector<SignalMapping>& mapping);
};
```

### 3.6 差异报告 ✅

```cpp
struct FieldDiff {
    std::string fieldName;         // 字段名
    uint64_t cppValue;             // C++ 参考值
    uint64_t rtlValue;             // RTL 实际值
    int bitWidth;                  // 字段位宽
};

struct DiffReport {
    uint64_t cycle;                // 出现差异的周期
    std::string modulePath;        // 模块层次路径
    std::vector<FieldDiff> diffs;  // 不一致的字段列表

    bool hasMismatch() const { return !diffs.empty(); }

    // 格式化输出（彩色标记差异字段，参考 xs-gem5 reportDiffMismatch）
    std::string format() const;

    // 转储最近 N 个周期的输入序列（辅助调试）
    void dumpRecentInputs(int numCycles) const;
};
```

---

## 四、模拟器作为 REF

> 对应 `verification-requirments.md` 第五章 5.1（整体指令级 difftest：复用 DUT 框架）。当 LLM 或 BSD 生成了 RTL 后，模拟器角色反转——从 DUT 变为 REF。

### 4.1 .so 编译目标 ✅

```makefile
# Makefile 新增共享库编译目标
libsimref.so: $(SIM_OBJS)
    $(CXX) -shared -fPIC -fvisibility=hidden -o $@ $^ $(LDFLAGS)
```

**符号可见性控制**：

```cpp
// 只有 difftest API 函数被导出，其他符号隐藏
#define SIM_EXPORT __attribute__((visibility("default")))

extern "C" {
    SIM_EXPORT void difftest_init(int port);
    SIM_EXPORT void difftest_exec(uint64_t n);
    SIM_EXPORT void difftest_regcpy(void* dut, bool direction);
    SIM_EXPORT void difftest_memcpy(paddr_t addr, void* buf, size_t n, bool direction);
    SIM_EXPORT void difftest_raise_intr(uint64_t no);
    SIM_EXPORT void difftest_guided_exec(void* guide);
    SIM_EXPORT int  difftest_store_commit(uint64_t* addr, uint64_t* data, uint8_t* mask);
}
```

**符号隔离**：RTL 仿真驱动通过 `dlmopen(LM_ID_NEWLM, "libsimref.so", RTLD_NOW)` 加载模拟器 .so，确保模拟器的全局符号不与 RTL 仿真程序自身冲突。

### 4.2 SimRefState 内部管理 ✅

```cpp
// .so 内部的模拟器实例管理
class SimRefState {
public:
    static SimRefState& instance();

    void init(int port);
    void exec(uint64_t n);
    void regcpy(void* dut, bool direction);
    void memcpy(paddr_t addr, void* buf, size_t n, bool direction);
    void raiseIntr(uint64_t no);
    void guidedExec(void* guide);
    int  storeCommit(uint64_t* addr, uint64_t* data, uint8_t* mask);

private:
    SimRefState() = default;
    std::unique_ptr<Simulator> sim_;
    // ISA 状态、内存等
};

// extern "C" 函数委托到 SimRefState
extern "C" SIM_EXPORT void difftest_init(int port) {
    SimRefState::instance().init(port);
}
// ... 其他 6 个函数类似
```

### 4.3 DUT 框架复用 ✅

> 对应 `verification-requirments.md` 5.1 中的复用/新开发表格。

| 组件 | 复用方式 |
|------|---------|
| difftest API 签名 | 完全复用，方向参数反转 |
| ISA 状态容器（RV64_CPU_state / x86_64_CPU_state） | 完全复用 |
| ISA 无关状态接口（DifftestState） | 完全复用 |
| 特殊处理逻辑（MMIO 跳过、中断注入、异常引导、CSR 跳过） | 大部分复用，"谁信任谁"方向反转 |
| 比较范围（GPR、PC、CSR、存储写入） | 完全复用 |

| 组件 | 说明 |
|------|------|
| 模拟器 .so 编译目标 | 新增 Makefile 目标，处理符号导出和隔离 |
| SimRefState 单例 | 管理 .so 内部的模拟器实例 |
| uOP 聚合（RTL 侧） | 如果 RTL 是 uOP 架构，由 RTL 提交逻辑聚合后触发 difftest；如果 RTL 是指令级提交（如 BSD 生成），则不需要 |

### 4.4 与 RTL 测试台集成 ⚠️

RTL 仿真驱动（Verilator 生成的 C++ testbench）加载 `libsimref.so` 的流程：

1. `dlmopen` 加载 `libsimref.so`。
2. `dlsym` 获取 7 个 difftest API 的函数指针。
3. 调用 `difftest_init()` 初始化模拟器，`difftest_memcpy()` 同步初始内存镜像。
4. 仿真循环中，RTL CPU 每提交一条指令，通过 DPI-C 导出提交信息，调用 `difftest_exec(1)` + `difftest_regcpy()` 比较。

uOP 聚合的责任归属：
- **RTL 是 uOP 架构**：RTL 内部的 ROB 提交逻辑负责聚合 uOP 到宏指令边界，聚合完成后通过 DPI-C 触发一次 difftest 比较。
- **RTL 是指令级提交**（如 BSD 生成）：每条指令提交直接触发比较，不需要聚合。

---

## 五、模拟器自身正确性验证（两个支柱的实现）

> 对应 `verification-requirments.md` 第四章 4.3（综合验证策略）。模拟器自身正确性依赖两个互补的支柱：**支柱一：Difftest**（第二章已覆盖）保证整体功能正确性；**支柱二：模块级检查**（本章）保证各模块行为正确性。两者缺一不可。
>
> 支柱二包括三个层次：运行时断言（每个模块的 tick() 中嵌入不变量检查）、定向 I/O 测试（将模块独立实例化，灌入精心设计的输入检查输出）、构造性验证（全系统微基准测试验证跨模块时序）。本章描述这三个层次的实现框架。

### 5.1 运行时断言框架 ✅

```cpp
// ── 断言宏 ──
#ifdef SIM_ENABLE_ASSERTIONS
#define SIM_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            sim_assert_fail(__FILE__, __LINE__, #cond, msg); \
        } \
    } while (0)
#else
#define SIM_ASSERT(cond, msg) ((void)0)
#endif

// 断言失败处理：打印模块路径、违反条件、当前状态后终止仿真
void sim_assert_fail(const char* file, int line,
                     const char* condStr, const std::string& msg);
```

断言通过编译期宏 `SIM_ENABLE_ASSERTIONS` 控制启用/禁用。在验证阶段默认启用，性能测试时可关闭。

**断言与 `process()` 的集成规范**：

每个模块的 `process()` 方法应在末尾调用断言检查，断言应在编写 `process()` 逻辑时同步编写——不是事后补充的测试，而是模块规格的一部分。

```cpp
// ⚠️ 以下为示例骨架，具体模块的内部状态待模拟器设计确定
void RenameModule::process() {
    // 1. 从输入端口读取解码后的指令
    // 2. 执行寄存器重命名逻辑
    // 3. 将结果写入输出端口

    // 4. 断言检查（与功能逻辑同步编写）
    SIM_ASSERT(freeCount_ + allocCount_ == PRF_SIZE,
               fullPath() + ": 物理寄存器守恒违反");
    SIM_ASSERT(!specRatPointsToFreeReg(),
               fullPath() + ": spec_RAT 条目指向 free list 中的寄存器");
    SIM_ASSERT(activeCheckpoints_ <= MAX_BR_NUM,
               fullPath() + ": 活跃 checkpoint 数超过上限");
}
```

### 5.2 模块级断言实现规范 ⚠️

> **依赖说明**：`verification-requirments.md` 4.3 已完整列举了 14 个模块共 56 条运行时断言和 4 条全局不变量的具体条件。断言框架（SIM_ASSERT 宏）可立即设计，但各断言引用的内部数据结构（如 free list、spec_RAT、busy table、MSHR 等）依赖各模块的具体类设计。

**断言的组织方式**：

每个模块将断言封装为 private 的 `checkInvariants()` 方法，在 `process()` 末尾调用：

```cpp
class SomeModule : public ClockedObject {
    // ...
    void process() override {
        // ... 功能逻辑 ...
        checkInvariants();
    }

private:
    void checkInvariants() {
        // 此模块的全部断言集中在这里
        // 断言条件来自 verification-requirments.md 4.3 的断言清单
    }
};
```

**各模块断言的实现状态**：

| 模块 | 断言数量 | 当前可实现程度 | 阻塞因素 |
|------|---------|-------------|---------|
| BPU（BTB/RAS/TAGE） | 4 | ⚠️ 框架可写 | BTB/RAS/TAGE 的具体数据结构 |
| ICache（含 ITLB） | 4 | ⚠️ 框架可写 | cache 行组织、替换算法接口 |
| Decoder | 3 | ⚠️ 框架可写 | uOP 类型定义、寄存器编号范围 |
| FTQ | 4 | ⚠️ 框架可写 | FTQ 条目结构、指针类型 |
| Rename | 6 | ⚠️ 框架可写 | RAT 表、free list、checkpoint 数据结构 |
| Dispatch | 4 | ⚠️ 框架可写 | 分派宽度配置、资源查询接口 |
| IssueQueue | 5 | ⚠️ 框架可写 | 唤醒矩阵、busy table、延迟计数器 |
| ExecUnit | 4 | ⚠️ 框架可写 | FU 类型、延迟配置、写端口冲突检测 |
| PRF | 4 | ⚠️ 框架可写 | 旁路网络结构、busy table |
| ROB | 6 | ⚠️ 框架可写 | ROB 条目结构、组提交逻辑 |
| LSU | 6 | ⚠️ 框架可写 | STQ/LDQ 结构、STLF 逻辑 |
| DCache | 4 | ⚠️ 框架可写 | cache 行组织、MSHR 结构 |
| PTW | 3 | ⚠️ 框架可写 | 页表遍历状态机 |
| 全局不变量 | 4 | ⚠️ 框架可写 | 跨模块接口（需各模块查询接口就绪） |

> **完整的断言条件列表**见 `verification-requirments.md` 4.3"运行时断言清单"的四个表格（前端 15 条、后端 30 条、存储子系统 7 条、全局 4 条）。

#### 5.2.1 各模块 checkInvariants() 查询接口契约（spec change-20260408-013040）

各断言要落地必须依赖模块的 public 查询接口。以下表格固化每个模块需要暴露的查询接口名（参数和返回值在模块实现时再细化）。一旦模块实现这些接口，对应的 `checkInvariants()` 代码可以零改动复用。

| 模块 | public 查询接口 |
|---|---|
| **BPU** | `btbAlignedToInstBoundary()`、`rasTopInRange()`、`tageCounterInRange()`、`predictionConsistent()` |
| **ICache** | `hasNoDuplicateLine(set)`、`replacementWayInRange()`、`refillLengthMatchesLine()`、`itlbPhysAddrInRange()` |
| **Decoder** | `uopCountWithinLimit()`、`illegalOpcodeFlagged()`、`registerIndexInRange()` |
| **FTQ** | `enqDeqInBound()`、`startPCBeforeEndPC()`、`stallWhenFull()`、`recycledMatchesIsLast()` |
| **Rename** | `freeCount()`、`allocCount()`、`specRatPointsToFreeReg()`、`archRatBusyClear()`、`activeCheckpoints()`、`specRatEqualsArchRatAfterFlush()` |
| **Dispatch** | `atomicPacketDispatched()`、`dispatchedWithinWidth()`、`resourcesAvailable(packet)`、`storeSplitToStaStd()` |
| **IssueQueue** | `occupiedEntries()`、`readyOperandsNonBusy()`、`wakeupMatrixConsistent()`、`oldestFirstSelected()`、`wakeupTimingCorrect()` |
| **ExecUnit** | `nonPipelinedBusyExclusive()`、`outputRobIdMatchesInput()`、`noWritePortConflict()`、`fixedLatencyHonored()` |
| **PRF** | `bypassValueMatches()`、`bypassPriorityCorrect()`、`oldestMisspecArbitrated()`、`busyTableConsistent()` |
| **ROB** | `fullEmptyExclusive()`、`commitOrderMonotonic()`、`groupCommitComplete()`、`exceptionPrecise()`、`stallWhenFull()`、`completionMatchesEntry()`、`macroIdLastUopMonotonic()` |
| **LSU** | `stqPointerOrder()`、`storeAddrDataValid()`、`stlfRangeWithinSnapshot()`、`killedNotReused()`、`queueWithinCapacity()`、`onlyCommittedStoreWritten()` |
| **DCache** | `hasNoDuplicateLine(set)`、`dirtyLineWrittenBackBeforeReplace()`、`mshrWithinCapacity()`、`refillWayMatchesReplaceWay()` |
| **PTW** | `walkLevelsWithinLimit()`、`intermediatePAddrInRange()`、`refillVPNMatchesRequest()` |

> ROB 的 `macroIdLastUopMonotonic()` 对应 verification-requirments.md §4.3 ROB 表中本轮新增的"宏指令连续提交"断言。其余接口名与 `verification-requirments.md` §4.3 的断言条件一一对应——断言代码模板为 `SIM_ASSERT(<query>(), fullPath() + ": <message>");`。具体接口签名（参数列表、返回值类型）由模块设计者在实现各模块时定义；本节只锚定接口名以便上层 `checkInvariants()` 提前编写。

**全局不变量的实现位置**：

全局不变量（如"物理寄存器守恒"、"flush 同步"、"指令唯一性"）跨越多个模块，不能放在单个模块的 `checkInvariants()` 中。这些断言在 `Simulator::runOneCycle()` 的末尾执行：

```cpp
void Simulator::runOneCycle() {
    // ... 按反向顺序调用各模块 tick() ...
    advanceBuffers();

    // 全局不变量检查
    #ifdef SIM_ENABLE_ASSERTIONS
    checkGlobalInvariants();
    #endif
}

void Simulator::checkGlobalInvariants() {
    // ⚠️ 具体实现需等待各模块查询接口就绪
    // SIM_ASSERT(prf.freeCount() + prf.allocCount() == PRF_SIZE, "PRF 守恒违反");
    // SIM_ASSERT(rob.usedEntries() <= ROB_SIZE, "ROB 超容量");
    // ...
}
```

### 5.3 定向 I/O 测试框架 ⚠️

> **依赖说明**：测试驱动框架（ModuleTestHarness 基类）可立即设计。具体的测试用例需要等待各模块的端口类型确定后才能实现。`verification-requirments.md` 4.3 "定向 I/O 测试清单"已列举了 40 个测试场景（前端 12 + 后端 21 + 存储 7），本节提供实现这些场景的代码框架。

**测试驱动框架**：

```cpp
// ── 模块级测试驱动器基类 ──
class ModuleTestHarness {
public:
    virtual ~ModuleTestHarness() = default;

    // 创建被测模块和上下游存根
    virtual void setup() = 0;

    // 执行测试：灌入输入序列，收集输出序列
    virtual void run() = 0;

    // 验证输出是否符合预期
    virtual bool verify() const = 0;

    // 测试描述信息
    virtual std::string description() const = 0;

    // 测试名称（用于报告）
    virtual std::string name() const = 0;

protected:
    // 辅助方法：驱动模块运行 N 个周期
    void tickModule(ClockedObject* module, uint64_t numCycles) {
        for (uint64_t i = 0; i < numCycles; i++) {
            module->tick();
        }
    }
};

// ── 测试套件：批量运行模块级测试 ──
class ModuleTestSuite {
public:
    void addTest(std::unique_ptr<ModuleTestHarness> test);

    // 运行全部测试，返回失败数
    int runAll();

    // 按名称运行单个测试
    bool runByName(const std::string& name);

private:
    std::vector<std::unique_ptr<ModuleTestHarness>> tests_;
};
```

**测试用例骨架示例**（以 IssueQueue 的"依赖唤醒"测试为例，⚠️ 具体端口类型待模块设计确定）：

```cpp
class IQWakeupTest : public ModuleTestHarness {
public:
    std::string name() const override { return "IQ_wakeup_dependency"; }
    std::string description() const override {
        return "两条背靠背依赖指令，A 写回后 B 在下一拍就绪并发射";
    }

    void setup() override {
        // 实例化 IssueQueue 模块
        // iq_ = std::make_unique<IssueQueue>("iq_test", nullptr);
        // 创建上游存根（模拟 Dispatch 灌入指令）和下游存根（模拟 FU 接收）
        // upstreamStub_ = ...;
        // downstreamStub_ = ...;
        // 绑定端口
    }

    void run() override {
        // 周期 0: 灌入指令 A（无依赖，立即就绪）
        // 周期 1: 灌入指令 B（源操作数 = A 的目标），同时 A 被发射
        // 周期 2: A 的写回信号到达，B 应被唤醒标记为就绪
        // 周期 3: B 应被发射
        // tickModule(iq_.get(), 4);
    }

    bool verify() const override {
        // 检查 B 在周期 3 被发射（而不是更早或更晚）
        return true; // ⚠️ 具体检查逻辑待模块设计确定
    }

private:
    // std::unique_ptr<IssueQueue> iq_;
    // std::unique_ptr<StubModule> upstreamStub_;
    // std::unique_ptr<StubModule> downstreamStub_;
};
```

**各模块定向 I/O 测试的实现状态**：

| 模块 | 测试场景数 | 当前可实现程度 | 阻塞因素 |
|------|----------|-------------|---------|
| BPU（BTB/RAS） | 3 | ⚠️ 框架可写 | 分支预测接口类型 |
| ICache | 3 | ⚠️ 框架可写 | cache 请求/响应接口 |
| Decoder | 3 | ⚠️ 框架可写 | uOP 类型定义、ISA 编码 |
| FTQ | 3 | ⚠️ 框架可写 | FTQ 条目端口类型 |
| Rename | 4 | ⚠️ 框架可写 | 重命名输入/输出端口 |
| Dispatch | 3 | ⚠️ 框架可写 | 分派端口类型、资源查询接口 |
| IssueQueue | 4 | ⚠️ 框架可写 | 指令端口类型、唤醒接口 |
| ExecUnit | 4 | ⚠️ 框架可写 | FU 输入/输出端口 |
| PRF | 2 | ⚠️ 框架可写 | 旁路网络端口 |
| ROB | 4 | ⚠️ 框架可写 | ROB 提交/完成接口 |
| LSU | 4 | ⚠️ 框架可写 | 访存请求/响应端口、STQ 接口 |
| DCache | 4 | ⚠️ 框架可写 | cache 请求/响应接口、MSHR |
| PTW | 3 | ⚠️ 框架可写 | 页表遍历请求/响应 |

> **完整的测试场景列表**（输入构造方式和预期行为）见 `verification-requirments.md` 4.3"定向 I/O 测试清单"的三个表格（前端 12 场景、后端 21 场景、存储 7 场景）。

#### 5.3.1 ModuleTestHarness 子类骨架清单（spec change-20260408-013040）

把 verification-requirments.md §4.3 的 40 个测试场景一次性转写为 ModuleTestHarness 子类的 `name()` 与 `description()`，作为后续填充 setup/run/verify 函数体的稳定锚点。具体函数体仍待端口类型确定。

```cpp
// === BPU 模块测试 ===
class BPU_PatternRecognitionTest : public ModuleTestHarness {
    std::string name() const override { return "bpu_pattern_recognition"; }
    std::string description() const override {
        return "构造全取/全不取/交替取分支序列，几轮训练后 TAGE 准确率应 >95%";
    }
};
class BPU_RAS_CallReturnTest : public ModuleTestHarness {
    std::string name() const override { return "bpu_ras_call_return"; }
    std::string description() const override {
        return "连续 CALL 后连续 RET，RAS 返回地址与 CALL 下一条 PC 精确匹配；嵌套深度超容时优雅降级";
    }
};
class BPU_BTB_IndirectJumpTest : public ModuleTestHarness {
    std::string name() const override { return "bpu_btb_indirect_jump"; }
    std::string description() const override {
        return "同一 PC 的间接跳转先后跳向不同目标，BTB 更新后下次预测为最新目标";
    }
};

// === ICache 测试 ===
class ICache_ReplacementTest : public ModuleTestHarness {
    std::string name() const override { return "icache_replacement"; }
    std::string description() const override {
        return "顺序访问填满一个 set 后再访问新地址，替换算法选 LRU 最久未用的 way";
    }
};
class ICache_ThrashingTest : public ModuleTestHarness {
    std::string name() const override { return "icache_thrashing"; }
    std::string description() const override {
        return "交替访问映射到同一 set 的两组地址（超过 way 数），命中率降至 0";
    }
};
class ICache_CrossLineFetchTest : public ModuleTestHarness {
    std::string name() const override { return "icache_cross_line_fetch"; }
    std::string description() const override {
        return "取指地址恰好跨越 cache line 边界，两次请求正确拼接为完整取指块";
    }
};

// === Decoder 测试 ===
class Decoder_TypeCoverageTest : public ModuleTestHarness {
    std::string name() const override { return "decoder_type_coverage"; }
    std::string description() const override {
        return "覆盖每种 ISA 指令类型（算术/访存/分支/系统/浮点/向量），uOP 拆分数量和类型正确";
    }
};
class Decoder_IllegalOpcodeTest : public ModuleTestHarness {
    std::string name() const override { return "decoder_illegal_opcode"; }
    std::string description() const override {
        return "构造无效 opcode 编码，产生非法指令异常标记，不崩溃";
    }
};
class Decoder_CISCSplitTest : public ModuleTestHarness {
    std::string name() const override { return "decoder_cisc_split"; }
    std::string description() const override {
        return "x86-64 PUSH/CALL/ENTER 等复杂指令拆分为正确数量的 uOP";
    }
};

// === FTQ 测试 ===
class FTQ_FullStallTest : public ModuleTestHarness {
    std::string name() const override { return "ftq_full_stall"; }
    std::string description() const override {
        return "连续发射取指块直到 FTQ 满，前端取指停滞且 FTQ 不溢出";
    }
};
class FTQ_MispredictRollbackTest : public ModuleTestHarness {
    std::string name() const override { return "ftq_mispredict_rollback"; }
    std::string description() const override {
        return "FTQ 半满时触发分支误预测，尾指针回退到误预测分支处，后续条目释放";
    }
};
class FTQ_RecoveryTest : public ModuleTestHarness {
    std::string name() const override { return "ftq_recovery"; }
    std::string description() const override {
        return "FTQ 满后立即提交释放若干条目，FTQ 恢复可分配且前端取指恢复";
    }
};

// === Rename 测试 ===
class Rename_WAWTest : public ModuleTestHarness {
    std::string name() const override { return "rename_waw"; }
    std::string description() const override {
        return "连续写同一架构寄存器，每次分配新物理寄存器，提交时释放旧物理寄存器";
    }
};
class Rename_RegExhaustionTest : public ModuleTestHarness {
    std::string name() const override { return "rename_reg_exhaustion"; }
    std::string description() const override {
        return "连续写不同架构寄存器直到 PRF 耗尽，Rename 反压 Decoder 不再分配";
    }
};
class Rename_MispredictRecoveryTest : public ModuleTestHarness {
    std::string name() const override { return "rename_mispredict_recovery"; }
    std::string description() const override {
        return "分支后接 N 条指令触发误预测，spec_RAT 恢复为 checkpoint 快照，后续物理寄存器释放";
    }
};
class Rename_GlobalFlushTest : public ModuleTestHarness {
    std::string name() const override { return "rename_global_flush"; }
    std::string description() const override {
        return "模拟异常触发 flush，spec_RAT 恢复为 arch_RAT，所有 checkpoint 清空，in-flight 物理寄存器释放";
    }
};

// === Dispatch 测试 ===
class Dispatch_RobFullStallTest : public ModuleTestHarness {
    std::string name() const override { return "dispatch_rob_full_stall"; }
    std::string description() const override {
        return "ROB 剩余空间 < fetch packet 宽度，整个 packet 停滞，不存在部分分派";
    }
};
class Dispatch_IqFullStallTest : public ModuleTestHarness {
    std::string name() const override { return "dispatch_iq_full_stall"; }
    std::string description() const override {
        return "构造全是整数指令的 packet 而整数 IQ 已满，packet 停滞，其他 IQ 有空也不分派";
    }
};
class Dispatch_StoreSplitTest : public ModuleTestHarness {
    std::string name() const override { return "dispatch_store_split"; }
    std::string description() const override {
        return "一条 Store 指令正确拆分为 STA + STD，分别进入 IQ_STA 和 IQ_STD";
    }
};

// === IssueQueue 测试 ===
class IQ_DepWakeupTest : public ModuleTestHarness {
    std::string name() const override { return "iq_dep_wakeup"; }
    std::string description() const override {
        return "两条背靠背依赖指令，A 写回后 B 在下一拍标记就绪并被发射";
    }
};
class IQ_OldestFirstTest : public ModuleTestHarness {
    std::string name() const override { return "iq_oldest_first"; }
    std::string description() const override {
        return "3 条无依赖指令同周期就绪但只有 1 个发射端口，ROB ID 最老的优先发射";
    }
};
class IQ_MultiCycleWakeupTest : public ModuleTestHarness {
    std::string name() const override { return "iq_multi_cycle_wakeup"; }
    std::string description() const override {
        return "MUL（3 拍）后接依赖指令，依赖指令恰好在 MUL 输入后第 3 拍被唤醒";
    }
};
class IQ_QueueFullTest : public ModuleTestHarness {
    std::string name() const override { return "iq_queue_full"; }
    std::string description() const override {
        return "连续灌入指令直到 IQ 满，Dispatch 被反压";
    }
};

// === ExecUnit 测试 ===
class ExecUnit_AluLatencyTest : public ModuleTestHarness {
    std::string name() const override { return "exec_alu_latency"; }
    std::string description() const override {
        return "一条 ADD 指令在输入后第 1 拍输出结果（valid=1 + 正确数据）";
    }
};
class ExecUnit_MulLatencyTest : public ModuleTestHarness {
    std::string name() const override { return "exec_mul_latency"; }
    std::string description() const override {
        return "一条 MUL 指令在输入后第 3 拍输出，第 1-2 拍 valid=0";
    }
};
class ExecUnit_DivBlockTest : public ModuleTestHarness {
    std::string name() const override { return "exec_div_block"; }
    std::string description() const override {
        return "DIV 执行期间灌入新指令，新指令被拒绝（ready=0），直到 DIV 完成";
    }
};
class ExecUnit_BranchRedirectTest : public ModuleTestHarness {
    std::string name() const override { return "exec_branch_redirect"; }
    std::string description() const override {
        return "分支指令执行发现误预测，产生 redirect 信号（重定向 PC + 分支 tag）";
    }
};

// === PRF 测试 ===
class PRF_BypassForwardingTest : public ModuleTestHarness {
    std::string name() const override { return "prf_bypass_forwarding"; }
    std::string description() const override {
        return "背靠背依赖指令（A→B，延迟 1 拍），B 从旁路网络获取 A 的结果";
    }
};
class PRF_MultiBranchArbitrationTest : public ModuleTestHarness {
    std::string name() const override { return "prf_multi_branch_arbitration"; }
    std::string description() const override {
        return "同周期两条分支都报告误预测，只有 ROB ID 更老的分支生效";
    }
};

// === ROB 测试 ===
class ROB_FullStallTest : public ModuleTestHarness {
    std::string name() const override { return "rob_full_stall"; }
    std::string description() const override {
        return "连续分配直到 ROB 满，Dispatch 被反压，新指令停滞";
    }
};
class ROB_ExceptionCommitTest : public ModuleTestHarness {
    std::string name() const override { return "rob_exception_commit"; }
    std::string description() const override {
        return "异常指令前有 N 条正常指令，N 条全部提交后异常指令触发 flush";
    }
};
class ROB_SameCycleCompleteTest : public ModuleTestHarness {
    std::string name() const override { return "rob_same_cycle_complete"; }
    std::string description() const override {
        return "多条指令在同一周期完成，所有完成标记正确更新，组提交在整行完成后触发";
    }
};
class ROB_InterruptResponseTest : public ModuleTestHarness {
    std::string name() const override { return "rob_interrupt_response"; }
    std::string description() const override {
        return "在两条指令之间注入中断，前一条提交后响应中断，进入中断处理";
    }
};

// === LSU 测试 ===
class LSU_StlfHitTest : public ModuleTestHarness {
    std::string name() const override { return "lsu_stlf_hit"; }
    std::string description() const override {
        return "Store [addr]; Load [addr]，Load 从 STQ 转发获取 Store 数据，不访问 DCache";
    }
};
class LSU_StlfNotReadyTest : public ModuleTestHarness {
    std::string name() const override { return "lsu_stlf_not_ready"; }
    std::string description() const override {
        return "Store 地址已知但数据未就绪，后续 Load 同地址进入 Retry 状态";
    }
};
class LSU_QueueFullTest : public ModuleTestHarness {
    std::string name() const override { return "lsu_queue_full"; }
    std::string description() const override {
        return "连续 Store 指令填满 STQ，Dispatch 停止分派新的 Store";
    }
};
class LSU_MispredictRecoveryTest : public ModuleTestHarness {
    std::string name() const override { return "lsu_mispredict_recovery"; }
    std::string description() const override {
        return "误预测路径上的 Load/Store：未发送的 Load 立即释放；已发送的 Load 标记 killed；STQ 条目回收";
    }
};

// === DCache 测试 ===
class DCache_HitTest : public ModuleTestHarness {
    std::string name() const override { return "dcache_hit"; }
    std::string description() const override {
        return "访问已在 cache 中的地址，1 拍返回数据";
    }
};
class DCache_MissTest : public ModuleTestHarness {
    std::string name() const override { return "dcache_miss"; }
    std::string description() const override {
        return "访问不在 cache 中的地址，分配 MSHR 发起 refill，延迟 N 拍后返回";
    }
};
class DCache_DirtyReplaceTest : public ModuleTestHarness {
    std::string name() const override { return "dcache_dirty_replace"; }
    std::string description() const override {
        return "写入地址导致 dirty 行被替换，先写回下一级再执行 refill";
    }
};
class DCache_MshrFullTest : public ModuleTestHarness {
    std::string name() const override { return "dcache_mshr_full"; }
    std::string description() const override {
        return "短时间内大量 miss，后续 miss 请求被阻塞直到有 MSHR 释放";
    }
};

// === PTW 测试 ===
class PTW_TlbMissWalkTest : public ModuleTestHarness {
    std::string name() const override { return "ptw_tlb_miss_walk"; }
    std::string description() const override {
        return "访问不在 TLB 中的虚拟地址，发起正确级数的页表遍历，最终 refill TLB";
    }
};
class PTW_InvalidPteTest : public ModuleTestHarness {
    std::string name() const override { return "ptw_invalid_pte"; }
    std::string description() const override {
        return "遍历过程中遇到 valid=0 的页表项，报告 Page Fault，不继续遍历";
    }
};
class PTW_ConcurrentMissTest : public ModuleTestHarness {
    std::string name() const override { return "ptw_concurrent_miss"; }
    std::string description() const override {
        return "两个不同虚拟地址同时 TLB miss，遍历请求排队或并行处理，互不干扰";
    }
};
```

> 上述 40 个 ModuleTestHarness 子类与 verification-requirments.md §4.3 三个测试清单逐项对应。每个类的 setup/run/verify 函数体都待对应模块的端口类型确定后填入（参考 §5.3 IQWakeupTest 示例的占位风格）。

### 5.4 构造性验证接口 ✅

> 构造性验证（micro-benchmark）在全系统环境中运行特定程序，人工计算预期结果和周期数，验证模拟器的跨模块时序行为是否符合设计者意图。这是支柱二在全系统层面的补充——定向 I/O 测试验证单模块行为，构造性验证验证多模块协作的时序链路。

```cpp
class MicroBenchmark {
public:
    virtual ~MicroBenchmark() = default;

    // 设置测试环境（加载测试程序、配置模拟器参数）
    virtual void setup(Simulator& sim) = 0;

    // 返回人工计算的预期周期数
    virtual uint64_t expectedCycles() const = 0;

    // 运行测试
    virtual void run(Simulator& sim) = 0;

    // 验证结果是否符合预期
    virtual bool verify(const Simulator& sim) const = 0;

    // 描述信息
    virtual std::string description() const = 0;
};

// ── 测试套件：批量运行构造性验证 ──
class MicroBenchmarkSuite {
public:
    void addBenchmark(std::unique_ptr<MicroBenchmark> bench);
    int runAll(Simulator& sim);
private:
    std::vector<std::unique_ptr<MicroBenchmark>> benchmarks_;
};
```

**10 个构造性验证场景**（对应 `verification-requirments.md` 4.3"构造性验证"表格）：

| 场景 | 预期行为 | 当前可实现程度 |
|------|---------|-------------|
| 稳态吞吐量 | 流水线充满后每周期提交 COMMIT_WIDTH 条 | ⚠️ 需流水线深度和提交宽度确定 |
| 数据旁路延迟 | 链式依赖每条在前一条完成后 1 拍发射 | ⚠️ 需旁路延迟参数 |
| 乘法器延迟 | 依赖指令在 MUL 输入后第 N 拍唤醒 | ⚠️ 需 MUL 延迟参数 |
| 分支误预测惩罚 | 误预测后经 N 拍才有新指令进入后端 | ⚠️ 需前端重启延迟参数 |
| DCache miss 阻塞 | 依赖指令提交时刻 = Load 发射 + miss 延迟 + 1 | ⚠️ 需 cache miss 惩罚参数 |
| STLF 延迟 | Load 从 STQ 获取数据的延迟拍数 | ⚠️ 需 STLF 延迟参数 |
| ROB 满阻塞恢复 | 长延迟指令完成后 ROB 释放，Dispatch 恢复 | ⚠️ 需 ROB 大小参数 |
| IQ 满阻塞恢复 | IQ 满后 Dispatch 停滞，发射后恢复 | ⚠️ 需 IQ 大小参数 |
| 中断响应延迟 | 中断在当前提交后固定拍数内响应 | ⚠️ 需中断响应延迟参数 |
| 多级页表遍历延迟 | TLB miss → PTW → DCache → TLB refill 的总延迟 | ⚠️ 需 PTW + memory 延迟参数 |

### 5.5 启蒙3号对比范围 ✅

> 对应 `verification-requirments.md` 4.3"启蒙3号作为参考的适用范围"。新模拟器后端以 uOP 为调度粒度，与启蒙3号（RISC-V 指令粒度）存在根本性差异。

| 对比类别 | 是否可行 | 说明 |
|----------|---------|------|
| 指令提交结果 | **可精确对比** | 功能语义不变，difftest 完全有效 |
| 前端行为（BPU、ICache、FTQ） | **可精确对比** | 前端不受 uOP 拆分影响 |
| 缓存/TLB 行为 | **可精确对比** | 存储子系统不关心请求来自指令还是 uOP |
| IPC | **可对比趋势** | 绝对值因 uOP 开销会有偏差，但量级应一致；偏差超过 2 倍可能是 bug |
| 后端内部状态（ROB、IQ、PRF） | **不可对比** | 粒度根本不同，对比无意义 |
| 指令提交周期 | **有系统性偏差** | 可容忍可预期的延迟差，但不能作为精确基线 |

### 5.6 验证层次总览 ✅

> 对应 `verification-requirments.md` 4.3"验证层次总览"。

| 验证层次 | 方法 | 参考对象 | 实现设计章节 | 优先级 |
|----------|------|----------|-------------|--------|
| 模拟器功能正确性 | Difftest 逐指令比较（支柱一） | Spike / QEMU / librefcpu | 第二章 | 最高 |
| 模拟器模块正确性 | 运行时断言 + 定向 I/O 测试（支柱二） | 无需外部参考 / 人工预期值 | 5.1-5.3 | 最高 |
| 模拟器时序合理性 | 构造性验证 micro-benchmark（支柱二） | 人工计算的预期周期数 | 5.4 | 高 |
| 模拟器前端/缓存行为 | 时序事件对比 | 启蒙3号 | 5.5 | 高 |
| RTL 整体正确性 | Difftest 模拟器作为 REF | 模拟器自身 | 第四章 | 高 |
| RTL 模块正确性 | 模块级联合仿真 | 模拟器单个模块 | 第三章 | 高 |
| 性能合理性 | IPC 量级对比 | 启蒙3号（趋势参考） | 5.5 | 中 |
| BSD 兼容性 | 位级展开 + BSD 验证 | 模拟器模块的位级 I/O | 3.5 | 按需 |

---

## 六、可设计性总结与实现路线

### 6.1 可设计性矩阵

| 组件 | 模拟器设计待定 | uOP 方案待定 | 当前可设计程度 |
|------|-------------|-------------|-------------|
| SimModule 基类 | 不影响 | 不影响 | ✅ 完整设计 |
| ClockedObject | 不影响 | 不影响 | ✅ 完整设计 |
| Port 基类 + bind | 不影响 | 不影响 | ✅ 完整设计 |
| DataPort/SignalPort/PortBundle | 框架不影响，具体端口束定义需等待 | 端口束内的数据类型需等待 | ⚠️ 框架可设计（占位字段集合已写入 §1.3.1） |
| TimeBuffer | 框架不影响 | 载荷类型需等待 | ⚠️ 框架可设计（实例化点已写入 §1.4.1） |
| Simulator 主循环 | 模块注册顺序需等待 | 不影响 | ✅ 框架完整 |
| StubModule + StubDataPort | 框架不影响，端口数据类型需等待 | 端口数据类型需等待 | ⚠️ 框架可设计 |
| RV64_CPU_state | 不影响 | 不影响 | ✅ 完整设计 |
| x86_64_CPU_state | 不影响 | 不影响 | ✅ 完整设计 |
| DifftestState 接口 | 不影响 | 不影响 | ✅ 完整设计 |
| Difftest API（16 函数） | 不影响 | 不影响 | ✅ 完整设计 |
| RefProxy + ISA 特定工厂（RV64RefType/X86RefType） | 不影响 | 不影响 | ✅ 完整设计 |
| uOP 过滤（shouldDiff） | 框架不影响 | isMicroop/isLastMicroop 标志位置需等待 | ✅ 完整设计（UopCommitInfo 字段已固化于 §2.5） |
| 特殊处理逻辑（16 场景） | 不影响 | 不影响 | ✅ 完整设计 |
| RflagsMask | 不影响 | 不影响（框架层面） | ✅ 框架完整 + 6 类初始 instCategory 已固化于 §2.7.1 |
| StoreCommitQueue | 不影响 | 不影响 | ✅ 完整设计 |
| DifftestChecker | 不影响 | 不影响 | ✅ 完整设计 |
| ModuleBridge | 框架不影响 | 端口映射需等待 | ⚠️ 框架可设计（tick 模板方法已 final 于 §3.1） |
| DPI-C 接口 | 不影响 | 不影响 | ✅ 完整设计 |
| Field<T> + SignalMapping | 框架不影响 | 具体位宽需等待 | ⚠️ 框架可设计（valid/last_uop/macro_id/inst_seq/PC 5 个字段位宽已写入 §3.3.1） |
| TraceRecorder/Player | 框架不影响 | 序列化格式需等待 | ⚠️ 框架可设计 |
| BitLevelFlattener | 需等待端口定义 | 需等待具体位宽 | ❌ 需等待 |
| DiffReport | 不影响 | 不影响 | ✅ 完整设计 |
| .so 编译目标 + SimRefAPI | 不影响 | 不影响 | ✅ 完整设计 |
| SIM_ASSERT 断言宏 | 不影响 | 不影响 | ✅ 完整设计 |
| 各模块 checkInvariants() | 需等待各模块内部数据结构 | 部分断言涉及 uOP 类型 | ⚠️ 接口契约已写入 §5.2.1，仅实现代码待模块 |
| ModuleTestHarness 框架 | 不影响 | 不影响 | ✅ 完整设计 |
| 各模块定向 I/O 测试用例 | 需等待各模块端口类型 | 部分测试涉及 uOP | ⚠️ 类名/描述已写入 §5.3.1，仅 setup/run/verify 函数体待模块 |
| MicroBenchmark 框架 | 不影响 | 不影响 | ✅ 框架完整 |
| 各 MicroBenchmark 测试用例 | 需等待流水线参数（深度、延迟） | 不影响 | ⚠️ 场景已列举，预期值需参数确定 |

### 6.2 分阶段实现路线

> 对应 `verification-requirments.md` 5.7。

**Phase 1：DUT + REF 指令级 difftest + 断言框架**

- 核心框架：SimModule、ClockedObject、Port 基类、TimeBuffer、Simulator
- DUT 方向：ISA 状态容器 + Difftest API + SpikeProxy（或 LibRefCpuProxy）+ 特殊处理（5 场景）+ DifftestChecker
- REF 方向：模拟器 .so 编译目标 + SimRefAPI
- **支柱二基础设施**：SIM_ASSERT 断言宏 + sim_assert_fail 处理 + ModuleTestHarness 基类 + ModuleTestSuite + MicroBenchmark 基类
- 重点：uOP→指令边界聚合、ISA 无关状态容器

**Phase 2：模块级联合仿真框架 + 模块级检查填充**

- Bridge 层骨架 + Trace 录制/回放 + 端口映射规范（Field<T> + SignalMapping）
- StubModule 框架
- **随各模块实现同步完成**：各模块的 checkInvariants() 实现（参照 `verification-requirments.md` 4.3 断言清单）、各模块的定向 I/O 测试用例实现（参照 `verification-requirments.md` 4.3 测试清单）
- 构造性验证用例实现（需流水线参数确定后填入预期周期数）
- 用一个简单模块（如 ALU）跑通"C++ 模块 ↔ Verilator RTL"的闭环

**Phase 3：ISA 无关 + x86-64 支持**

- `DifftestState` 替换硬编码结构
- RflagsMask 掩码表填充（按指令类别：算术/逻辑/移位/自增减/乘法/除法）
- 封装 QEMU x86-64 TCG 为 .so，实现 QemuProxy + x86-64 特殊处理（RFLAGS 掩码、REP 前缀、段寄存器、x87 FPU 栈、MXCSR）
- BSD 位级展开层（如需）

---

## 附录 A 临时 uOP 基线（来自 code/uOP）

> spec change-20260408-013040 引入。本附录记录验证接口当前所参考的 uOP 占位字典。

本附录列出当前验证接口设计所参考的 uOP 占位字典——`code/uOP/uop指令说明.md`。该字典实际是 LoongArch (LA64) 风格的低层指令集，并不等同于后端最终的通用 uOP，但作为推进验证接口的暂时基线已经足够。后端正式 uOP 出来后，仅需替换 `UopCommitInfo.uop_type_placeholder` 与 `RflagsMask` 的初始化常量，**不影响 SimModule/Port/TimeBuffer/DifftestChecker/ModuleBridge 等核心结构**。

### A.1 命名约定

- `8l/8h`：取寄存器低/高 8 位作为源操作数
- `16/32/64`：以对应位宽运算
- `32U/32S`：32 位无符号/有符号
- 名称中带 `U/S` 决定符号扩展方式
- `s` 后缀（如 `adds_`、`subs_`、`ands_`、`shls_`、`rcls_` 等）：通过 `helper_lbt_x86*` 更新 x86 EFLAGS，并在执行前 `CHECK_BTE`
- `_rem`：保留浮点寄存器高位（只写低 32/64 位）
- `_c`：清零浮点寄存器高位
- `_pre/_post`：访存后更新基址寄存器（pre 在 rj+imm 处访问后更新；post 在 rj 处访问后更新）
- `_shift`：地址 = `rj + (rk << imm)`
- `pc` 相关：以 `env->pc` 为基址；`npc` 相关：以 `env->next_pc` 为基址

### A.2 11 个分组

1. **整数算术与逻辑（RRR）**：`add_*`、`sub_*`、`and_*`、`or_*`、`xor_*`、`imul_*` 及其 `s` 版本
2. **自增/自减**：`inc_*`、`dec_*`（总是更新 EFLAGS）
3. **拼接 / 短宽度除法 / 扩展**：`concatl_16/32`、`div_16_8`、`div_32_16`、`div_64_32`、`mod_*`、`ext_*`
4. **立即数算术 / 逻辑**：8 位（`addi_8l` 等）、12 位（`addi_64_12` 等）、16 位（`addi_32_16U` 等）、32/64 位（`addi_32U`、`addi_64` 等）
5. **移位与旋转**：`sar_*`、`shl_*`、`shr_*`、`rcl_*`、`rcr_*`、`rol_*`、`ror_*` 及立即数版本
6. **访存与地址更新**：基址+立即数（`ld_*_16/32`、`st_*`）、基址+索引（`ldx_*_shift`、`stx_*_shift`）、PC 相对（`ldpc_*`、`stpc_*`）、前后更新（`ld_pre_*`、`ld_post_*`、`st_pre_*`、`st_post_*`、`st_npc_pre/post4/8`）
7. **浮点 rem/c 系列**：`fadd_s_rem`、`fsub_d_rem`、`fmul_*_rem`、`fdiv_*_rem`、`fcvt_*_rem`、`fsel_*_rem`
8. **浮点比较**：`fcmp_s_s`、`fcmp_s_d`、`fcmp_c_s`、`fcmp_c_d`
9. **浮点 load/store 与 clear-high**：`fld_*` / `fldx_*` / `fldpc_*` 及对应的 `_c` 变体；`movgr2fr_*_c`
10. **向量 load/store**：`vldx_shift`、`vld_*`、`vldpc_*`、`vstx_shift`、`vst_*`、`vstpc_*`
11. **分支/控制类**：立即数比较分支（`beqi_*`、`bnei_*`、`blti_*`、`bgei_*`、`bltui_*`、`bgeui_*`、`bai_*`、`blei_*`、`bgi_*`）、寄存器比较分支（`beq_*`、`bne_*`、`blt_*`、`bge_*`、`bltu_*`、`bgeu_*`）、其他控制类（`li32_32U/S`、`li20_32U/S`、`pcaddi_32`）

### A.3 注意事项

- 该字典命名空间共 ~400 项，可直接作为 `UopCommitInfo.uop_type_placeholder` 的有效枚举值集合。
- 字典源文件（`code/uOP/uop指令说明.md`、`code/uOP/LA_EMU/`）不在 simulator 主仓库内，是项目根目录下的兄弟目录，仅作为参考。
- 该字典使用 LA 风格的 `rj/rk/rd` 寄存器命名，**实现侧的端口/接口必须使用 `src1_areg/src2_areg/dst_areg`（来自 backend §1）等抽象命名**，避免 LA 风格泄漏到上层。

---

## 附录 B 临时 uOP 基线 vs 后端 uOP 缺口对照表

> spec change-20260408-013040 引入。本附录把 `verification-uop-api.md` §4 列出的缺口与 `code/uOP` 当前支持程度逐项对照。

| 缺口类别 | 后端建议补充的 uOP | code/uOP 当前支持程度 | 验证侧的临时处理方式 |
|---|---|---|---|
| **跳转** | `jmp_imm`、`jmp_reg` | 仅有 `pcaddi_32` 与各类比较分支，无寄存器间接跳转 | JALR/CALL/RET 走宏指令级 stub；模块级 BPU/RAS/IQ-Br 测试暂用宏指令打桩，IQ-Br 间接跳转回填测试推迟到正式 uOP 出来后 |
| **系统控制** | `csr_rw` / `csr_rs` / `csr_rc`、`ecall`、`ebreak`、`wfi`、`mret`、`sret`、`sfence_vma`、`fence_i` | 全部缺失 | difftest `SkipCSRList`（§2.6.5）按"是否系统指令"过滤；模拟器执行层在 Commit 阶段走宏指令级特殊路径；verification-requirments.md §4.2 已建议把 skipCSRs 改为按 uOP 子类型过滤 |
| **RV32M 完整** | `mulh_32S`、`mulhu_32U`、`mulhsu_32`、`div_32U`、`mod_32U` | 仅有 `imul_*`、短宽度 `div_64_32`/`mod_64_32` | RV64 高半积/无符号变体走宏指令级 helper，验证侧不区分 |
| **原子事务** | `lr_w`、`sc_w`、`amo_{add,xor,and,or,min,max,minu,maxu}_w`、原子事务标记位 | 全部缺失 | difftest 通过 `uarchstatus_cpy`（§2.6.1 + verification-requirments.md §4.2 补注）同步；LSU 模块级"原子事务不可分割"断言推迟到最终 uOP 后落地 |
| **比较置位** | `slt_32S`、`sltu_32U` | 缺失 | Decode→uOP 翻译时用"减法 + 取符号位"组合，验证层无影响 |

> 以上 5 类缺口仅影响**模拟器执行层**，对验证接口的可设计性没有阻塞。验证层只需在涉及这些场景的具体 ModuleTestHarness 子类（§5.3.1）和 SkipCSRList 配置上注明"宏指令级 stub"即可。后端正式 uOP 出来后，本附录中的对照关系会自然过期，届时只需删除附录 B 即可——附录 A/B 都是**为了让验证接口设计在等待期间不停顿**而存在的过渡说明。
