# 后端流水线模块开发者 API 手册

> 本文档基于 `varification-design.md` 第二轮更新（spec change-20260408-013040）。
> 第二轮同步补全（spec change-20260408-165944）已加入：§13.1 UopCommitInfo 6 个新字段、§13.4 commitOneUop 完整示例、§13.6 原子事务 uarchstatus_cpy 时机、§6.2 writeback 级、§10.4.1 13 个模块查询接口契约、§十四 模拟器作为 REF 时的 ROB 角色。

本文档面向模拟器后端流水线模块的开发者（Rename、Dispatch、IssueQueue、ExecUnit、ROB、PRF、LSU 等），说明如何在验证框架之上正确开发各自的模块。

***

## 一、概述

验证框架为你开发的每个后端模块提供四项支撑能力：

1. **模块可替换** -- 你的模块通过统一的基类和端口接口接入系统，替换时只需提供相同接口的新实现。
2. **模块可独立运行** -- 你的模块可以脱离完整 CPU 被独立测试，上下游由框架提供的存根模块替代。
3. **模块可录制** -- 端口上的数据传输可被自动捕获，用于回放验证和调试。
4. **模块可桥接** -- 端口定义同时携带逻辑语义和信号映射信息，支持与 RTL 的联合仿真。

你不需要主动实现这些能力——只要遵循本文档的接口规范和设计约束，框架会自动赋予你的模块上述四项能力。

***

## 二、基类继承体系

所有后端模块通过继承 `ClockedObject` 参与仿真。`ClockedObject` 继承自 `SimModule`，二者共同构成模块的基础能力。

### 2.1 SimModule

SimModule 是所有仿真组件的基类，提供层次化组织、生命周期管理和端口自省。

```cpp
class SimModule {
public:
    explicit SimModule(const std::string& name, SimModule* parent = nullptr);
    virtual ~SimModule() = default;

    // -- 层次化命名 --
    const std::string& name() const;           // 短名称，如 "rename"
    std::string fullPath() const;              // 层次路径，如 "cpu.backend.rename"
    SimModule* parent() const;

    // -- 子模块管理 --
    const std::vector<SimModule*>& children() const;
    SimModule* findChild(const std::string& name) const;

    // -- 生命周期 --
    virtual void init() {}                     // 内部状态初始化（端口尚未连接）
    virtual void startup() {}                  // 端口连接完成后的启动逻辑
    virtual void tick() = 0;                   // 每周期执行（纯虚）

    // -- 端口访问 --
    virtual Port* getPort(const std::string& portName, int idx = -1);
    const std::vector<Port*>& allPorts() const;

    // -- 状态序列化与恢复 --
    virtual void serialize(std::ostream& os) const {}
    virtual void unserialize(std::istream& is) {}

protected:
    void addChild(SimModule* child);           // 由子模块构造函数自动调用
    void registerPort(Port* port);             // 端口创建时自动注册

private:
    std::string name_;
    SimModule* parent_;
    std::vector<SimModule*> children_;
    std::vector<Port*> ports_;
};
```

### 2.2 ClockedObject

ClockedObject 在 SimModule 之上增加了时钟周期管理。你的模块应继承此类。

```cpp
class ClockedObject : public SimModule {
public:
    ClockedObject(const std::string& name, SimModule* parent,
                  uint64_t clockPeriod = 1);

    uint64_t curCycle() const;       // 当前周期数
    uint64_t clockPeriod() const;    // 时钟周期
    uint64_t frequency() const;      // 时钟频率

    void tick() final;               // 更新周期计数后调用 process()
    virtual void process() = 0;      // 子类在此实现每周期逻辑

private:
    uint64_t cycle_ = 0;
    uint64_t clockPeriod_;
};
```

`tick()` 被标记为 `final`，你不能覆写它。所有周期逻辑集中在 `process()` 中实现。

### 2.3 生命周期

模块的生命周期分为三个阶段：

1. **构造** -- 调用构造函数，创建端口和内部数据结构。
2. **初始化** -- 框架调用 `init()` 完成内部状态初始化；随后框架执行端口 `bind()`；最后框架调用 `startup()` 完成依赖端口连接的初始化。
3. **仿真循环** -- 框架反复调用 `tick()`，`tick()` 内部自动递增周期计数并调用你的 `process()`。

```
构造函数 --> init() --> 端口 bind() --> startup() --> [tick() 循环]
```

***

## 三、Port 系统使用指南

所有模块间的数据传输都通过 Port 系统完成。模块不允许直接访问其他模块的内部数据。

### 3.1 Port 基类

```cpp
class Port {
public:
    Port(const std::string& name, SimModule* owner);
    virtual ~Port() = default;

    const std::string& name() const;
    SimModule* owner() const;
    bool isConnected() const;

    // 录制钩子：启用后每次数据传输（send/sendReq/broadcast）自动序列化到 trace 文件
    void enableTrace(TraceRecorder* recorder);
    void disableTrace();

protected:
    Port* peer_ = nullptr;
    TraceRecorder* recorder_ = nullptr;

    friend void bind(Port& a, Port& b);
};

// 建立双向连接：同时设置双方的 peer_ 指针
void bind(Port& a, Port& b);
```

`bind` 是自由函数，调用后双方都能通过 `peer_` 访问对端。端口在构造时创建，并注册到 PortBundle 中。

### 3.2 DataPort -- 单向流水线级间传输

这是最常用的端口类型，用于流水线级间的数据传递（如 Decode -> Rename、Rename -> Dispatch）。

```cpp
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

### 3.3 SignalPort -- 广播信号

用于 flush、stall、wakeup 等一对多信号场景。

```cpp
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

### 3.4 RequestPort / ResponsePort -- 双向通信

用于需要请求-响应交互的场景（如 LSU 与 DCache 之间的通信）。

```cpp
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

### 3.5 PortBundle -- 端口组织

将相关端口组合为一组，便于管理和接口兼容性检查。

```cpp
class PortBundle {
public:
    PortBundle(const std::string& name, SimModule* owner);

    void addPort(const std::string& portName, Port* port);
    Port* getPort(const std::string& portName) const;

    // 遍历所有端口（用于自动 Trace 挂载）
    const std::map<std::string, Port*>& ports() const;

    // 束级别的 Trace 控制（等价于对束内每个端口逐一调用 enableTrace/disableTrace）
    void enableTraceAll(TraceRecorder* recorder);
    void disableTraceAll();

private:
    std::string name_;
    SimModule* owner_;
    std::map<std::string, Port*> ports_;
};
```

端口在构造函数中创建，并通过 `addPort` 注册到 PortBundle。`getPort` 按名称查找端口，通常在 `getPort()` 覆写中使用。

### 3.5.1 级间结构体的占位字段集合（spec change-20260408-165944，反向核对补全）

后端开发者实现 Decode/Rename/Dispatch/IssueQueue 时需要定义级间数据结构。以下三个占位字段集合来自 `varification-design.md` §1.3.1，是 `DataPort<T>` / `TimeBuffer<T>` 的标准载荷类型。**字段名已锚定**，位宽与 `uop_type_placeholder` 的最终枚举待 uOP 正式版填充。

```cpp
// 来源：varification-design.md §1.3.1 + verification-backend-api.md §1
// 占位结构体，字段名已确定，位宽待 uOP 正式版填充

struct DecodeToRename {
    bool     valid;
    uint16_t uop_type_placeholder;  // 占位枚举（暂用 code/uOP 命名空间）
    uint8_t  src1_areg;             // 架构寄存器号 from backend §1
    uint8_t  src2_areg;
    uint8_t  dst_areg;
    uint64_t imm;                   // 立即数（8/12/16/32/64 位粒度由 uop_type 决定）
    uint8_t  width;                 // 操作位宽（8/16/32/64）
    uint8_t  sign;                  // 0=无符号, 1=有符号
    MemAttr  mem_attr;              // 4 类访存模式
    uint16_t ctrl_flag;             // is_branch / is_call / is_ret / predicted_taken
    uint64_t isa_meta_pc;           // 该 uop 所属宏指令的 PC
    uint8_t  except_hint;
    uint8_t  macro_id;
    bool     last_uop;
};

struct RenameToDispatch {
    bool     valid;
    uint16_t uop_type_placeholder;
    uint16_t src1_preg;             // 物理寄存器号占位（位宽 = log2(PRF_SIZE)）
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

> 这三个结构体覆盖 `Decode→Rename`、`Rename→Dispatch`、`Dispatch→IssueQueue` 三段最关键的级间通信。所有 `DataPort<T>` 与 `TimeBuffer<T>` 在使用时优先实例化为这三类。位宽和 `uop_type_placeholder` 最终值出来之前，**字段名和数量保持稳定**。

### 3.5.2 已确定的初始字段位宽常量（spec change-20260408-165944，反向核对补全）

部分字段位宽**不依赖** uOP 正式版，可以直接作为 `Field<T>` 模板实参使用（来自 `varification-design.md` §3.3.1）：

```cpp
constexpr int kFieldWidthValid    = 1;   // 单 bit valid
constexpr int kFieldWidthLastUop  = 1;   // backend §1 last_uop
constexpr int kFieldWidthMacroId  = 8;   // 同一宏指令最多 256 条 uOP
constexpr int kFieldWidthInstSeq  = 64;  // 指令序列号
constexpr int kFieldWidthPC       = 64;  // 64 位 PC

// 待 uOP 正式版填充的位宽：uop_type / src/dst 寄存器号 / imm / ctrl_flag / except_hint
```

### 3.6 TraceRecorder / TracePlayer — 端口录制与回放

框架通过 `enableTrace()` 在端口上挂载录制器，每次数据传输时自动将周期号、端口名、数据内容写入 trace 文件。`TracePlayer` 用于在独立测试中回放录制的输入序列。

```cpp
class TraceRecorder {
public:
    TraceRecorder(const std::string& outputPath);
    ~TraceRecorder();

    // 记录一条端口数据（由 Port 的录制钩子自动调用，无需手动调用）
    void record(uint64_t cycle, const std::string& portName,
                const void* data, size_t size);

    void writeHeader(const std::vector<PortInfo>& ports);
    void flush();
};

class TracePlayer {
public:
    TracePlayer(const std::string& inputPath);

    bool readNext(uint64_t& cycle, std::string& portName,
                  void* data, size_t maxSize);
    bool eof() const;
};

struct PortInfo {
    std::string name;           // 端口名称
    std::string modulePath;     // 所属模块的层次路径（如 "cpu.backend.rename"）
    size_t dataSize;            // 每条数据的字节数
};
```

**三种典型用法**：

1. **全系统录制**：对目标模块的端口束调用 `enableTraceAll(recorder)`，运行仿真，得到 trace 文件。
2. **独立回放验证**：用 `TracePlayer` + `StubDataPort`（参见第十二章）将录制的输入序列灌入模块，比较输出与参考输出。
3. **Bug 定位**：解析 trace 文件，定位第一个输出偏差出现的周期和端口。

> 你不需要直接操作 `TraceRecorder::record()`——它由端口录制钩子自动调用。你只需在测试代码中创建 `TraceRecorder` 实例并传入 `enableTrace()` / `enableTraceAll()`。

***

## 四、TimeBuffer 使用指南

TimeBuffer 是流水线级间传递数据的延迟缓冲区。数据写入后需经过固定的周期延迟才能被下游读取。

### 4.1 TimeBufferBase

```cpp
class TimeBufferBase {
public:
    virtual ~TimeBufferBase() = default;
    virtual void advance() = 0;
};
```

`TimeBufferBase` 提供抽象接口，使仿真器可以统一管理不同数据类型的 TimeBuffer。

### 4.2 TimeBuffer

```cpp
template<typename T>
class TimeBuffer : public TimeBufferBase {
public:
    // delay: 数据从写入到可读取需要经过的周期数
    // width: 每个周期最多可写入的数据条数（对应流水线宽度）
    TimeBuffer(int delay, int width = 1);

    // -- 写端（上游模块调用）--
    T& writeSlot(int idx = 0);
    void setValid(int idx, bool valid);

    // -- 读端（下游模块调用）--
    const T& readSlot(int idx = 0) const;
    bool isValid(int idx) const;

    // -- wire 内部类：通过时间偏移读写 --
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

    // -- 周期推进（由仿真主循环调用）--
    void advance() override;

private:
    int delay_;
    int width_;
    // 环形缓冲区：深度 = delay + 1
    std::vector<std::vector<T>> buffer_;
    std::vector<std::vector<bool>> valid_;
    int writePtr_;    // 当前写入位置

    T& access(int offset);
};
```

**核心概念**：

- **固定延迟** -- 构造时确定，运行时不可更改。延迟值反映流水线寄存器的级数。
- **环形缓冲区** -- `buffer_` 大小为 `(delay + 1) * width`。`advance()` 移动 `writePtr_`，读取位置通过 `(writePtr_ - delay)` 计算。
- **多槽位** -- `width` 参数支持超标量流水线，每周期可传递多条数据。
- **wire 偏移读写** -- `getWire(offset)` 返回特定时间偏移的数据访问器。上游通过 `getWire(0)` 写入当前周期数据，下游通过 `getWire(-delay)` 读取延迟后的数据。偏移为负数表示读取过去的数据，0 表示当前周期，正数表示未来（较少使用）。

**使用示例**：

```cpp
// 构造一个延迟 2 周期、宽度 4 的 TimeBuffer
TimeBuffer<RenameToDispatchData> renToDisBuf(2, 4);

// 上游（Rename）写入
auto w = renToDisBuf.getWire(0);
w->instrData = ...;  // 写入当前周期

// 下游（Dispatch）读取（延迟 2 周期后可见）
auto r = renToDisBuf.getWire(-2);
auto data = *r;      // 读取 2 周期前写入的数据
```

`advance()` 由仿真主循环在每周期末统一调用，你的模块不需要手动调用。

#### 4.x 三个标准 TimeBuffer 实例化点（spec change-20260408-165944，来自 varification-design.md §1.4.1）

以下三个 TimeBuffer 是后续 Dispatch 反压、Rename 误预测恢复、IQ 唤醒时序等模块级断言/测试用例的最常见挂载点。后端开发者实现 Decode/Rename/Dispatch/IssueQueue 时直接复用这三个名字：

```cpp
// varification-design.md §1.4.1 已固化的三个标准实例化点
// dispatch_width 与 issue_width 是模拟器构建期常量（初始可统一为 4）
TimeBuffer<DecodeToRename>   decode2rename  {/*delay=*/1, /*width=*/dispatch_width};
TimeBuffer<RenameToDispatch> rename2dispatch{/*delay=*/1, /*width=*/dispatch_width};
TimeBuffer<DispatchToIQ>     dispatch2iq    {/*delay=*/1, /*width=*/issue_width};
```

`DecodeToRename` / `RenameToDispatch` / `DispatchToIQ` 三个级间结构体的字段集合见 `varification-design.md` §1.3.1。其它级间通信（Issue→Execute、Execute→Writeback、Writeback→Commit 等）按相同模式按需添加。

***

## 五、DelayedResult — 模块内多周期延迟建模

模拟器采用"高层逻辑 + 时序计数"的建模方式：模块在输入到达的周期立即用 C++ 计算出结果，然后模拟硬件的多周期延迟，在 N 拍后才将结果输出。例如乘法器在第 0 拍算出乘积，但在第 3 拍才将结果写入输出端口。

TimeBuffer 用于级间通信（固定延迟、由仿真主循环统一推进），不适合模块内部的延迟建模——模块内部的延迟可能因操作类型而异，且多个结果可能同时在途。框架为此提供 `DelayedResult<T>` 工具类：

```cpp
template<typename T>
class DelayedResult {
public:
    /// @param maxInflight 最大在途结果数量。
    ///   流水线化模块（如 MUL）设为流水线深度（如 3），允许每拍接受新输入；
    ///   非流水线模块（如 DIV）设为 1，执行期间阻塞新输入。
    ///   0 = 不限制。
    explicit DelayedResult(int maxInflight = 0);

    void push(const T& result, int latency);  // 放入结果，latency 拍后到期
    bool hasReady() const;                     // 本周期是否有结果到期
    const T& peekReady() const;                // 查看最老的到期结果
    T popReady();                              // 取出并消费到期结果
    int inflightCount() const;                 // 在途数量（用于断言）
    bool canAccept() const;                    // 是否可接受新输入
    bool empty() const;                        // 是否无任何在途结果
    void tick();                               // 每周期推进（由你的 process() 调用）
};
```

### 与 TimeBuffer 的区别

| 维度 | TimeBuffer          | DelayedResult            |
| -- | ------------------- | ------------------------ |
| 用途 | 级间通信（Decode→Rename） | 模块内延迟建模（DIV N拍）          |
| 延迟 | 构造时固定，所有数据相同        | 每条数据可以不同                 |
| 推进 | 由仿真主循环统一推进          | 由模块的 process() 调用 tick() |
| 宽度 | 固定槽位数               | 动态队列，可选最大在途数             |

### 使用示例：流水线化乘法器（3 拍延迟）

```cpp
class MulUnit : public ClockedObject {
    DelayedResult<ExecResult> pipeline_{3};  // maxInflight = 3

    void process() override {
        pipeline_.tick();

        // 接受新输入
        if (inputPort_.isValid() && pipeline_.canAccept()) {
            auto& uop = inputPort_.peek();
            ExecResult res;
            res.value = uop.src1 * uop.src2;  // 立即计算
            res.robId = uop.robId;
            pipeline_.push(res, 3);             // 3 拍后到期
        }

        // 输出到期结果
        if (pipeline_.hasReady()) {
            outputPort_.send(pipeline_.popReady());
        }

        SIM_ASSERT(pipeline_.inflightCount() <= 3,
                   fullPath() + ": MUL pipeline overflow");
    }
};
```

### 使用示例：非流水线除法器（可变延迟）

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
                   fullPath() + ": DIV accepted new input while busy");
    }
};
```

`maxInflight` 是区分流水线化和非流水线化的关键参数：流水线化模块设为流水线深度，非流水线模块设为 1。

***

## 六、仿真主循环与 tick 机制

### 6.1 Simulator 类

```cpp
class Simulator {
public:
    // -- 系统构建 --
    void buildSystem(SimModule* top);          // 构建模块树 -> 绑定端口 -> init() -> startup()
    void registerModule(ClockedObject* module);// 注册模块到 tick 调度列表
    void registerBuffer(TimeBufferBase* buffer);// 注册 TimeBuffer

    // -- 仿真执行 --
    void runOneCycle();                        // 按反向顺序调用各模块的 tick()，完毕后调用 advanceBuffers() 和 checkGlobalInvariants()
    void advanceBuffers();                     // 推进所有 TimeBuffer
    void run(uint64_t numCycles);              // 完整仿真循环

private:
    std::vector<ClockedObject*> tickOrder_;    // 按从后往前排列
    std::vector<TimeBufferBase*> timeBuffers_;
    uint64_t curCycle_ = 0;

    // 每周期末在 SIM_ENABLE_ASSERTIONS 模式下自动调用，检查跨模块资源守恒
    void checkGlobalInvariants();
};
```

### 6.2 反向 tick 顺序

仿真主循环按**从后级到前级的顺序**调用各模块的 `tick()`。以下是一个示意性的执行时序：

```
每个仿真周期（spec change-20260408-165944：补 writeback 级）：
  1. commit.tick()      // 提交级先执行，释放 ROB 条目和物理寄存器
  2. writeback.tick()   // 写回级（spec change-20260408-165944 新增）
  3. execute.tick()     // 执行级
  4. issue.tick()       // 发射级
  5. dispatch.tick()    // 分派级
  6. rename.tick()      // 重命名级
  7. decode.tick()      // 解码级
  8. fetch.tick()       // 取指级最后执行
  9. advanceBuffers()   // 所有 TimeBuffer 推进一格
```

> 具体的流水级划分和拓扑是后端开发小组的设计工作，以上 8 级与 `verification-backend-api.md` §0 / `varification-design.md` §1.6 保持一致。

**为什么要反向 tick？** 后级先执行使得前级能在同一周期看到后级释放的资源（如 ROB 条目、空闲物理寄存器），避免资源释放延迟一个周期。例如，commit 阶段先执行释放了 ROB 条目，随后 dispatch 阶段执行时就能立即利用这些条目，而不需要等到下一个周期。

> **补充（spec change-20260408-165944）**：writeback 在 commit 之后但在 execute 之前执行，使本周期完成的执行结果在 commit 阶段可见，同时让 execute 在本周期能看到 commit 释放的物理寄存器。如果遗漏 writeback 级，会出现"执行结果未写回 PRF 就被下一条指令读取"的隐蔽 bug。

`advanceBuffers()` 在所有模块的 `tick()` 执行完毕之后统一调用，将所有 TimeBuffer 的环形缓冲区推进一格，使本周期写入的数据在经过固定延迟后对下游可见。

***

## 七、断言规范

### 7.1 SIM\_ASSERT 宏

```cpp
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

void sim_assert_fail(const char* file, int line,
                     const char* condStr, const std::string& msg);
```

断言失败时，`sim_assert_fail` 会报告具体文件、行号、违反的条件表达式和自定义消息，帮助精确定位问题。

### 7.2 checkInvariants() 组织模式

在 `process()` 末尾调用 private 的 `checkInvariants()` 方法，将所有断言集中在一处：

```cpp
class RenameModule : public ClockedObject {
public:
    void process() override {
        // 1. 读取输入
        // 2. 执行重命名逻辑
        // 3. 写入输出
        // 4. 断言检查
        checkInvariants();
    }

private:
    void checkInvariants() {
        // 寄存器守恒：空闲 + 已分配 = 总数
        SIM_ASSERT(freeList_.count() + allocatedCount() == PRF_SIZE,
                   "Physical register count mismatch: free=" +
                   std::to_string(freeList_.count()) +
                   " alloc=" + std::to_string(allocatedCount()) +
                   " total=" + std::to_string(PRF_SIZE));

        // spec_RAT 有效性：每个条目指向的物理寄存器不在 free list 中
        for (int i = 0; i < ARCH_REG_NUM; ++i) {
            PhysReg pr = specRAT_[i];
            SIM_ASSERT(!freeList_.contains(pr),
                       "spec_RAT[" + std::to_string(i) +
                       "] points to free register p" + std::to_string(pr));
        }

        // arch_RAT 已写回：每个条目指向的物理寄存器 busy 位为 0
        for (int i = 0; i < ARCH_REG_NUM; ++i) {
            PhysReg pr = archRAT_[i];
            SIM_ASSERT(!busyTable_[pr],
                       "arch_RAT[" + std::to_string(i) +
                       "] points to busy register p" + std::to_string(pr));
        }

        // checkpoint 上限
        SIM_ASSERT(activeCheckpoints_ <= MAX_BR_NUM,
                   "Active checkpoints (" +
                   std::to_string(activeCheckpoints_) +
                   ") exceeds MAX_BR_NUM (" +
                   std::to_string(MAX_BR_NUM) + ")");
    }
};
```

**关键原则**：断言应在编写 `process()` 逻辑时同步编写，不是事后补充，而是模块规格的一部分。

具体的断言条件和定向测试场景参见 `verification-requirments.md` 4.3 的"运行时断言清单"和"定向 I/O 测试清单"。

***

## 八、序列化接口

### 8.1 用途

`serialize()` 和 `unserialize()` 用于：

- **检查点保存/恢复** -- 保存仿真状态到磁盘，后续从断点恢复继续运行。
- **调试** -- 导出模块内部状态用于分析和问题定位。

### 8.2 实现规范

有状态的模块必须覆写这两个方法。需要导出的状态包括：

- 队列内容（如 ROB 条目、IssueQueue 条目、STQ/LDQ 条目）
- 计数器和指针（如 ROB 的 enq\_ptr/deq\_ptr、空闲寄存器计数）
- 状态机当前状态（如 LSU 的 load 状态机）
- 映射表（如 RAT、busy table）

```cpp
void serialize(std::ostream& os) const override {
    // 导出所有关键内部状态
    // 格式由模块自行定义，需保证 unserialize 能完整恢复
}

void unserialize(std::istream& is) override {
    // 从流中恢复内部状态
    // 恢复后模块应处于与序列化时完全相同的状态
}
```

序列化和反序列化必须对称——`serialize` 写出的数据能被 `unserialize` 完整恢复，恢复后的模块行为与序列化时完全一致。

***

## 九、模块骨架示例

以下是一个完整的模块实现模板，展示了所有接口约束如何落地。

```cpp
class ExampleModule : public ClockedObject {
public:
    ExampleModule(const std::string& name, SimModule* parent)
        : ClockedObject(name, parent)
        , inputBundle_("input", this)
        , outputBundle_("output", this)
        , dataInPort_("data_in", this)
        , dataOutPort_("data_out", this)
        , flushPort_("flush", this)
    {
        // 端口在构造时创建并注册到 PortBundle
        inputBundle_.addPort("data_in", &dataInPort_);
        inputBundle_.addPort("flush", &flushPort_);
        outputBundle_.addPort("data_out", &dataOutPort_);
    }

    void process() override {
        // --- 1. 检查控制信号 ---
        if (flushPort_.hasSignal()) {
            handleFlush();
            checkInvariants();
            return;
        }

        // --- 2. 从输入端口读取数据 ---
        if (dataInPort_.isValid()) {
            const auto& input = dataInPort_.peek();
            // 处理输入 ...
        }

        // --- 3. 执行本模块的周期逻辑 ---
        // 内部状态更新 ...

        // --- 4. 将结果写入输出端口 ---
        if (hasOutput) {
            dataOutPort_.send(outputData);
            dataOutPort_.setValid(true);
        } else {
            dataOutPort_.setValid(false);
        }

        // --- 5. 断言检查 ---
        checkInvariants();

        // 注：如果你实现的是 ROB/Commit 模块，请在 process() 末尾构造 UopCommitInfo 并
        //     调用 difftestChecker_.onUopCommit(info)（详见 §13.4 的完整字段填充示例）。
        //     spec change-20260408-165944
    }

    Port* getPort(const std::string& portName, int idx) override {
        auto* p = inputBundle_.getPort(portName);
        if (p) return p;
        return outputBundle_.getPort(portName);
    }

    void serialize(std::ostream& os) const override {
        // 导出队列内容、计数器、状态机等内部状态
        // os.write(...);
    }

    void unserialize(std::istream& is) override {
        // 从流中恢复内部状态
        // is.read(...);
    }

    // -- public 查询接口，用于全局不变量检查 --
    size_t queueSize() const { return queue_.size(); }
    size_t freeCount() const { return capacity_ - queue_.size(); }
    bool isFull() const { return queue_.size() >= capacity_; }

private:
    // -- 端口 --
    PortBundle inputBundle_;
    PortBundle outputBundle_;
    DataPort<InputData> dataInPort_;
    DataPort<OutputData> dataOutPort_;
    SignalPort<FlushSignal> flushPort_;

    // -- 内部状态 --
    std::deque<Entry> queue_;
    size_t capacity_;

    // -- 断言检查 --
    void checkInvariants() {
        SIM_ASSERT(queue_.size() <= capacity_,
                   "Queue overflow: size=" + std::to_string(queue_.size()) +
                   " capacity=" + std::to_string(capacity_));

        // 更多断言 ...
    }

    void handleFlush() {
        // flush 处理逻辑
    }
};
```

***

## 十、你需要向验证框架提供什么

作为后端模块的开发者，你需要交付以下接口：

### 10.1 端口定义

通过 `getPort()` 暴露模块的全部端口。每个端口需要明确：

- **端口名** -- 唯一的字符串标识（如 `"decode_in"`、`"dispatch_out"`、`"flush"`）
- **数据类型** -- 端口传输的数据结构类型（如 `DataPort<RenameToDispatchData>`）
- **方向** -- 输入端口（读端）或输出端口（写端）

### 10.2 serialize / unserialize 实现

导出和恢复模块的全部内部状态，包括但不限于：

- 队列内容（条目数据和有效标记）
- 计数器和指针（如头尾指针、分配计数）
- 状态机当前状态
- 映射表（如 RAT、busy table）

### 10.3 checkInvariants() 断言实现

在 `process()` 末尾调用的断言检查方法。具体的断言条件参照 `verification-requirments.md` 4.3 的"运行时断言清单"，你的模块对应哪些断言就实现哪些。

### 10.4 public 查询接口

提供只读的 public 方法暴露关键内部状态，用于全局不变量检查。例如：

- `queueSize()` -- 当前队列占用量
- `freeCount()` -- 当前可用空间
- `isFull()` -- 是否已满
- `allocatedCount()` -- 已分配资源数

这些接口使框架和其他模块能在不破坏封装的前提下查询你的模块状态，用于跨模块的全局不变量检查（如"PRF 分配数 + PRF 空闲数 = PRF 总数"）。

**全局不变量的调用时机**：`Simulator::checkGlobalInvariants()` 在 `SIM_ENABLE_ASSERTIONS` 编译模式下于每个仿真周期末自动调用（在 `advanceBuffers()` 之后）。它通过你的 public 查询接口（如 `prf.freeCount() + prf.allocCount() == PRF_SIZE`）跨模块验证资源守恒。你不需要手动调用此函数，但必须确保查询接口返回值实时、准确地反映模块内部状态。

#### 10.4.1 各模块完整 public 查询接口契约（spec change-20260408-165944）

下表来自 `varification-design.md` §5.2.1，是后端开发者在实现具体模块时**必须**暴露的 public 查询接口集合。每个接口对应 `verification-requirments.md` §4.3 的一条或多条运行时断言。具体接口签名（参数列表、返回值类型）由模块设计者定义；本表只锚定接口名，便于上层 `checkInvariants()` 调用。

| 模块 | 必须暴露的 public 查询接口 |
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

> **ROB 的 `macroIdLastUopMonotonic()`**（spec change-20260408-165944，对应 verification-requirments.md F5）：用于断言"同一宏指令的全部 uOP 在 ROB 内连续占据条目，提交时按 macro_id 一次性出队，且 last_uop 标志在该宏指令的出队序列中只在最后一条上为真"。这是 ROB 实现 §13 Difftest 集成时的硬性前置条件——若违反，整个原子事务和宏指令融合的一致性都会被破坏。

> **8 个流水线模块的最小端口集合表**（来自 `varification-design.md` §1.7.1，spec change-20260408-165944）：

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

实现具体流水线模块时，至少要在 `getPort()` 中暴露上表中列出的端口名；额外端口（如多端口 IQ 的 `issue_out_0/1/...`）通过 `getPort(name, idx)` 的 `idx` 参数扩展。

***

## 十一、可测试性设计约束

以下 9 条约束是模块设计的硬性要求。前 5 条为验证接口约束，后 4 条为设计原则。

### 验证接口约束

**1. 继承 ClockedObject，实现 process()**

所有周期逻辑集中在 `process()` 一个方法中。不要将逻辑分散到多个回调或事件处理器中。

**2. 通过 getPort() 暴露全部 I/O 端口**

所有输入和输出都必须通过 Port 系统传递，不允许模块间直接共享状态。这是模块可替换性的基础。

→ 各流水线模块的最小端口集合见 `varification-design.md` §1.7.1（已在本文件 §10.4.1 内嵌入完整表格）。

**3. 实现 serialize() / unserialize()**

有状态的模块必须能导出和恢复内部状态，用于检查点和调试。

**4. 在 process() 末尾调用断言检查**

有状态模块应在每周期逻辑执行完毕后调用 `checkInvariants()`，检查内部不变量。断言应在编写 `process()` 逻辑时同步编写。

**5. 端口使用 PortBundle 组织**

将相关端口组合为端口束（如输入束、输出束），使模块替换时能一次性验证接口兼容性。

### 设计原则

**6. 接口清晰**

模块的输入和输出通过明确的端口定义，不依赖全局状态或隐式的共享变量。模块通过端口与外界通信，而非直接访问其他模块的内部数据。

**7. 可独立实例化**

模块可以脱离完整 CPU 环境单独创建，上下游用存根替代即可运行。不需要初始化整个 CPU 才能测试一个子模块。

**8. 状态可观测**

模块的关键内部状态（如队列深度、计数器值、有限状态机当前状态）可以被测试代码查询。具体实现为 `serialize()` 导出 + 提供 public 查询接口（如 `queueSize()`、`freeCount()`）。

**9. 确定性**

相同的输入序列产生相同的输出序列，不依赖未初始化的状态或外部随机源。

***

## 十二、StubModule — 独立测试存根

当你需要脱离完整 CPU 对单个模块进行独立测试时，上下游模块用 `StubModule` + `StubDataPort<T>` 替代。

### 12.1 设计粒度：端口级

Stub 的粒度是**端口级**而非模块级——对每个端口独立挂载存根行为，不要求在编译期知道被替代模块的完整端口束类型。这降低了编译期依赖：修改某个模块的端口 A 不会影响端口 B 的存根代码。

### 12.2 StubDataPort

```cpp
enum class StubMode {
    Constant,    // 每周期输出固定值
    Random,      // 每周期输出随机值（按字段随机填充）
    TraceReplay  // 从录制的 Trace 文件按周期回放
};

template<typename T>
class StubDataPort : public DataPort<T> {
public:
    StubDataPort(const std::string& name, SimModule* owner,
                 StubMode mode = StubMode::Constant);

    void setConstantOutput(const T& value);       // Constant 模式
    void setRandomSeed(uint64_t seed);             // Random 模式
    void loadTrace(const std::string& tracePath);  // TraceReplay 模式

    // 每周期由 StubModule 的 process() 驱动调用，按模式生成一条数据
    void generate();
};
```

### 12.3 StubModule

```cpp
class StubModule : public ClockedObject {
public:
    StubModule(const std::string& name, SimModule* parent);

    // 注册输出存根端口（由 StubModule 每周期驱动 generate()）
    template<typename T>
    StubDataPort<T>* addOutputStub(const std::string& portName,
                                    StubMode mode = StubMode::Constant);

    // 注册输入占位端口（接收但丢弃数据，用于占位连接）
    template<typename T>
    DataPort<T>* addInputSink(const std::string& portName);

    void process() override;
    Port* getPort(const std::string& portName, int idx) override;
};
```

### 12.4 三种模式的适用场景

| 模式 | 适用场景 |
| -- | -- |
| `Constant` | 探索性测试，每周期灌入固定数据（如"每周期发送 4 条有效指令"） |
| `Random` | 压力测试，随机输入覆盖边界条件 |
| `TraceReplay` | 回归验证，回放全系统仿真录制的输入，独立验证模块行为 |

### 12.5 使用示例

```cpp
// 测试 Dispatch 模块，上游用 StubModule 模拟 Rename，下游用 StubModule 模拟 IssueQueue

StubModule upstreamStub("rename_stub", nullptr);
auto* renameOut = upstreamStub.addOutputStub<RenameToDispatchData>(
    "dispatch_out", StubMode::Constant);
renameOut->setConstantOutput(testData);

DispatchModule dispatch("dispatch", nullptr);

StubModule downstreamStub("iq_stub", nullptr);
downstreamStub.addInputSink<DispatchToIQData>("dispatch_in");

bind(*upstreamStub.getPort("dispatch_out"), *dispatch.getPort("rename_in"));
bind(*dispatch.getPort("iq_out"), *downstreamStub.getPort("dispatch_in"));

Simulator sim;
sim.registerModule(&upstreamStub);
sim.registerModule(&dispatch);
sim.registerModule(&downstreamStub);
sim.run(100);
```

> `TraceReplay` 模式中 `StubDataPort<T>` 内部持有一个 `TracePlayer`，通过 `loadTrace()` 指定 trace 文件路径。Trace 文件由全系统仿真录制（参见第三章 TraceRecorder）。

> **loadTrace 文件格式（spec change-20260408-165944）**：`StubDataPort::loadTrace()` 期望的文件格式与 §3.6 `TraceRecorder` 写出的格式完全一致——使用 `PortInfo` 头（端口名 + 模块层次路径 + 数据字节数）+ 周期 + 端口名 + 二进制 payload 的序列。两端的格式由同一份 `TraceRecorder` / `TracePlayer` 实现保证一致，开发者不需要手写解析代码。

### 12.6 ModuleTestHarness 与定向 I/O 测试（spec change-20260408-165944，反向核对补全）

`StubModule` 提供"上下游存根"机制，但要为每个模块编写**定向 I/O 测试**，框架还提供 `ModuleTestHarness` 基类（来自 `varification-design.md` §5.3）：

```cpp
class ModuleTestHarness {
public:
    virtual ~ModuleTestHarness() = default;
    virtual void setup() = 0;                       // 创建被测模块和上下游存根
    virtual void run() = 0;                         // 灌入输入序列，收集输出
    virtual bool verify() const = 0;                // 验证输出是否符合预期
    virtual std::string description() const = 0;
    virtual std::string name() const = 0;
protected:
    void tickModule(ClockedObject* module, uint64_t numCycles);
};
```

后端开发者实现某个模块时，应同时实现该模块对应的若干 ModuleTestHarness 子类。`varification-design.md` §5.3.1 已经为 13 个模块写出了 **40 个 ModuleTestHarness 子类骨架**（每个类的 `name()` 和 `description()` 已固化，仅函数体待端口类型确定后填）。常见的子类示例：

- BPU：`BPU_PatternRecognitionTest` / `BPU_RAS_CallReturnTest` / `BPU_BTB_IndirectJumpTest`
- Rename：`Rename_WAWTest` / `Rename_RegExhaustionTest` / `Rename_MispredictRecoveryTest` / `Rename_GlobalFlushTest`
- IssueQueue：`IQ_DepWakeupTest` / `IQ_OldestFirstTest` / `IQ_MultiCycleWakeupTest` / `IQ_QueueFullTest`
- ROB：`ROB_FullStallTest` / `ROB_ExceptionCommitTest` / `ROB_SameCycleCompleteTest` / `ROB_InterruptResponseTest`
- LSU：`LSU_StlfHitTest` / `LSU_StlfNotReadyTest` / `LSU_QueueFullTest` / `LSU_MispredictRecoveryTest`
- DCache：`DCache_HitTest` / `DCache_MissTest` / `DCache_DirtyReplaceTest` / `DCache_MshrFullTest`

→ 完整 40 个子类骨架见 `varification-design.md` §5.3.1。

### 12.7 ModuleBridge 与 RTL 联合仿真（spec change-20260408-165944，反向核对补全）

当 LLM/BSD 生成 RTL 后，每个模块可以与 Verilator 编译的 RTL 同时跑在全系统仿真中，由 `ModuleBridge`（`varification-design.md` §3.1）每周期比较两侧 I/O。`ModuleBridge::tick()` 已固化为 `final` 模板方法，子类只能通过四个虚函数 `driveRtlInputs / evalRtl / readRtlOutputs / compareOutputs` 接入。

后端开发者**不需要**手动实现 ModuleBridge——只要遵循本文件 §11 的 9 条约束（端口清晰 / 可独立实例化 / 状态可观测 / 确定性），框架会自动为模块挂载 Bridge。但需要注意：

- 模块的 I/O 必须**完全通过端口**传递，不能依赖全局状态——否则 Bridge 无法在同一周期把相同输入灌入两侧
- `serialize()` / `unserialize()` 必须完整——Bridge 在 trace 重放和 checkpoint 恢复时会调用
- 模块的 tick() 行为必须**确定性**——相同输入序列产生相同输出序列

→ 详见 `varification-design.md` §3.1（ModuleBridge）、§3.3（Field<T> 端口映射规范）。

### 12.8 MicroBenchmark — 全系统构造性验证（spec change-20260408-165944，反向核对补全）

除了模块级的 ModuleTestHarness，框架还提供 `MicroBenchmark` 接口（`varification-design.md` §5.4）用于全系统层面的构造性验证——在完整的 CPU 上跑特定的微基准测试程序，人工计算预期周期数并验证。

```cpp
class MicroBenchmark {
public:
    virtual void setup(Simulator& sim) = 0;
    virtual uint64_t expectedCycles() const = 0;
    virtual void run(Simulator& sim) = 0;
    virtual bool verify(const Simulator& sim) const = 0;
    virtual std::string description() const = 0;
};
```

`MicroBenchmark` 验证的是**跨模块协作的时序正确性**（如数据旁路延迟、分支误预测惩罚、DCache miss 阻塞等），不是单模块的 I/O 行为。这类测试通常由验证团队或集成团队编写，**不要求**单个模块开发者实现，但每个模块的设计必须支持这类测试（即满足 §11 的 9 条约束）。

→ 详见 `varification-design.md` §5.4 与 verification-requirments.md §4.3 "构造性验证 (全系统 Micro-benchmark)" 表格的 10 个场景。

***

## 十三、Difftest 集成接口（ROB/Commit 模块）

本章仅适用于实现 ROB/Commit 级的开发者。其他模块不需要与 difftest 直接交互。

> **设计依据（spec change-20260408-165944）**：本章内容来自 verification-requirments.md §4.2（含补注 F1-F5）、varification-design.md §2.5（UopCommitInfo）、§2.6（特殊处理 16 场景）、§2.7（RflagsMask）、§2.8（StoreCommitQueue）、§2.9（DifftestChecker）。

### 13.1 UopCommitInfo — uOP 提交信息（spec change-20260408-165944：与 varification-design.md §2.5 同步）

ROB 在每条 uOP 提交时构造此结构并传递给 `DifftestChecker`。**第二轮新增 6 个字段**：`is_sc` / `macro_id` / `uop_type_placeholder` / `mem_attr` / `fpr_writeback_mode` / `except_code`。这些字段由 Decoder 阶段在 uOP 上预填，ROB 提交时只需透传。

```cpp
struct UopCommitInfo {
    // ── 已固化字段（不再依赖 uOP 正式版） ──
    uint64_t inst_seq;              // 宏指令序列号
    uint64_t pc;                    // 宏指令 PC（64 位）
    bool     is_microop;            // 是否为 micro-op（false = 单 uOP 指令，不拆分）
    bool     is_last_microop;       // 是否为宏指令的最后一条 micro-op（= backend §1 last_uop）
    bool     is_fusion;             // 是否为融合指令（两条原始指令合并）
    bool     is_mmio;               // 是否为 MMIO 访问
    bool     is_store;              // 是否包含存储操作
    bool     is_branch;             // 是否包含分支操作
    bool     is_sc;                 // 是否为 SC 类原子操作（spec change-20260408-165944）
    uint8_t  macro_id;              // 同一宏指令的连续编号 from backend §1（spec change-20260408-165944）
    uint16_t uop_type_placeholder;  // 占位 uOP 类型（暂用 code/uOP 命名空间，spec change-20260408-165944）
    MemAttr  mem_attr;              // 访存属性 4 类（spec change-20260408-165944）
    uint8_t  fpr_writeback_mode;    // 0=full / 1=rem / 2=clr（spec change-20260408-165944）
    uint32_t except_code;           // 0 = 无异常，非零为内部统一异常码（spec change-20260408-165944）

    // DUT 侧执行结果（由 ROB 提交时填充）
    static constexpr int MAX_DEST_REGS = 4;
    std::pair<int, uint64_t> dest_regs[MAX_DEST_REGS];  // (寄存器索引, 值)
    int num_dest_regs = 0;

    uint64_t store_addr;            // 存储地址（仅存储指令有效）
    uint64_t store_data;
    uint64_t store_mask;
};
```

### 13.2 shouldDiff — uOP 过滤

difftest 在宏指令粒度进行比较，ROB 以 uOP 粒度提交。过滤规则如下：

```cpp
// 只在以下情况触发 difftest 比较：
// 1. 非 micro-op（单 uOP 指令，不拆分）
// 2. 宏指令的最后一条 micro-op
bool shouldDiff(const UopCommitInfo& info) {
    return !info.is_microop || info.is_last_microop;
}
```

中间 micro-op 提交时跳过 difftest，等到最后一条 micro-op 再触发比较，此时 REF 执行一条宏指令，DUT 的宏指令效果已完全体现在寄存器堆中。

### 13.3 DifftestChecker — 公开接口

ROB/Commit 模块通过以下接口与 `DifftestChecker` 交互：

```cpp
class DifftestChecker {
public:
    DifftestChecker(ISAType isa, std::unique_ptr<RefProxy> ref);

    // 初始化：加载参考模型，同步初始内存和 PC
    void init(uint8_t* memory, size_t memSize, uint64_t startPC);

    // ── ROB 提交 uOP 时调用 ──
    void onUopCommit(const UopCommitInfo& info);

    // ── 中断响应时调用（在 ROB 提交中断处理结果后调用）──
    void onInterrupt(uint64_t intNo);

    // ── 异常响应时调用（在 ROB 提交异常处理结果后调用）──
    void onException(uint64_t excNo, uint64_t mtval, uint64_t stval,
                     uint64_t jumpTarget);
};
```

> **`jumpTarget` 参数说明（spec change-20260408-165944，对应 verification-requirments.md F4）**：异常处理入口地址由 DUT 的 CSR/控制寄存器决定（RISC-V 的 mtvec/stvec、x86 的 IDT），但 REF 端的相应寄存器可能未及时同步。通过 `guided_exec()` 直接告知跳转目标，避免 REF 因控制寄存器不同步而跳到错误地址。详见 verification-requirments.md §4.2 引导执行段。

### 13.4 ROB commit 集成模式（spec change-20260408-165944：完整字段填充）

以下示例覆盖 §13.1 中**全部字段**的填充。后端开发者实现 ROB::commitOneUop 时必须保证每个字段都有正确来源，否则 difftest 会因字段缺失而报错或漏报。

```cpp
void ROBModule::commitOneUop(const ROBEntry& entry) {
    // ... 更新 arch_RAT、释放物理寄存器、写入 store buffer 等 commit 逻辑 ...

    // 构造提交信息（spec change-20260408-165944：覆盖所有字段）
    UopCommitInfo info;

    // ── ROB 阶段填充的字段 ──
    info.inst_seq            = entry.instSeq;          // ROB 入队时分配
    info.pc                  = entry.pc;               // 来自 Decode
    info.num_dest_regs       = entry.numDestRegs;
    for (int i = 0; i < entry.numDestRegs; ++i) {
        info.dest_regs[i] = {entry.archDestReg[i], prf_[entry.physDestReg[i]]};
    }
    if (entry.isStore) {
        info.store_addr = entry.storeAddr;
        info.store_data = entry.storeData;
        info.store_mask = entry.storeMask;
    }

    // ── Decoder 阶段预填、ROB 透传的字段 ──
    info.is_microop          = entry.isMicroop;
    info.is_last_microop     = entry.isLastMicroop;    // = backend §1 last_uop
    info.is_fusion           = entry.isFusion;
    info.is_mmio             = entry.isMmio;
    info.is_store            = entry.isStore;
    info.is_branch           = entry.isBranch;
    info.is_sc               = entry.isSc;             // SC 类原子操作（spec change-20260408-165944）
    info.macro_id            = entry.macroId;          // backend §1 字段（spec change-20260408-165944）
    info.uop_type_placeholder= entry.uopTypePlaceholder;  // 来自 code/uOP 命名空间
    info.mem_attr            = entry.memAttr;          // 4 类访存模式
    info.fpr_writeback_mode  = entry.fprWritebackMode; // 0=full / 1=rem / 2=clr
    info.except_code         = entry.exceptCode;       // 0 = 无异常

    // 通知 DifftestChecker
    difftestChecker_.onUopCommit(info);
}
```

`DifftestChecker::onUopCommit()` 内部先调用 `shouldDiff()` 过滤中间 micro-op，通过过滤后再驱动 REF 执行并比较状态。ROB 不需要自行判断是否触发比较。

### 13.5 回调调用顺序示例（spec change-20260408-165944）

```cpp
// 正常指令提交：
rob.commitOneUop(entry);  // 内部调用 difftestChecker_.onUopCommit(info)

// 异常引导（DUT 检测到异常）：
rob.commitOneUop(entry);                                          // 先 onUopCommit
difftestChecker_.onException(excNo, mtval, stval, jumpTarget);    // 再 onException

// 中断响应（外部中断到达）：
rob.commitOneUop(entry);                                          // 先 onUopCommit（前一条正常指令）
difftestChecker_.onInterrupt(intNo);                              // 再 onInterrupt
// 之后开始执行中断处理程序的第一条指令
```

### 13.6 原子事务的 uarchstatus_cpy 触发时机（spec change-20260408-165944，对应 verification-requirments.md F4）

对于 RISC-V LR/SC 与 x86 LOCK CMPXCHG 等原子事务，`uarchstatus_cpy` / SC 成功标志的注入时机必须落在 ROB 提交"**原子事务最后一条 uOP**"那一拍（即 `is_last_microop=true` 的那条 uOP 的 difftest 触发点）。

ROB 实现要点：

1. 原子事务的全部 uOP 必须按序连续提交（参见 §10.4.1 ROB 的 `macroIdLastUopMonotonic` 断言）
2. 在最后一条 uOP（`is_sc=true` 或对应 AMO 序列的最后一条 STD）提交时，`DifftestChecker` 内部会自动触发 `uarchstatus_cpy`；ROB 不需要主动调用
3. 中间 uOP 不需要跳过 difftest——`shouldDiff()` 已经按 `is_last_microop` 过滤，无需 ROB 额外判断

→ 设计依据：verification-requirments.md §4.2 引导执行段（spec change-20260408-013040 补注 F4），varification-design.md §2.6.1。

### 13.7 冲刷处理

ROB 保证按序提交，冲刷发生在提交之前。被冲刷的指令不会进入 commit 流程，因此不存在"部分 uOP 已提交、部分被取消"的情况，ROB 不需要通知 DifftestChecker 执行任何回滚。

> **macroIdLastUopMonotonic 断言**（spec change-20260408-165944，对应 verification-requirments.md F5）：ROB 必须保证同一宏指令的全部 uOP 在 ROB 内**连续占据条目**，提交时按 `macro_id` 一次性出队，且 `is_last_microop=true` 标志在该宏指令的出队序列中只在最后一条上为真。这条断言对应 ROB 的 `macroIdLastUopMonotonic()` 查询接口（参见 §10.4.1）。一旦该断言失败，意味着原子事务和宏指令融合的一致性都会受影响，必须立即修复。

***

## 十四、模拟器作为 REF 时的 ROB 角色（spec change-20260408-165944）

`varification-design.md` §四 说明了模拟器编译为 `.so`、由外部 RTL 仿真程序作为 REF（golden reference）调用的场景。在这个场景下，ROB 仍按正常 `commitOneUop` 流程工作，但需要注意以下几点：

1. **不需要改 ROB 代码**：`SimRefState` 单例在 `.so` 模式下被外部 RTL 仿真程序通过 `difftest_*` API 调用（参见 varification-design.md §4.2）。这些 API 内部驱动模拟器主循环跑 `runOneCycle()`，ROB 的 `commitOneUop` 路径与正常单进程仿真完全相同。`DifftestChecker::onUopCommit` 会自动捕获 ROB 的提交事件并写入 SimRefState 的内部状态。

2. **`serialize()` / `unserialize()` 必须完整**：`.so` 模式下 RTL 仿真程序可能要求模拟器从某个 checkpoint 恢复，或者在多次 difftest 调用之间保存/恢复状态。ROB 的所有内部状态（队列内容、指针、`macro_id` 计数器、checkpoint 列表等）必须通过 `serialize()` 完整导出，且能由 `unserialize()` 完全恢复。

3. **不要在 ROB 内调用任何全局单例**：`SimRefState` 是 `.so` 模式特有的封装层，ROB 通过常规的 `difftestChecker_` 成员引用就能正常工作；不要在 ROB 内引用 `SimRefState::instance()`，否则在普通仿真模式下会引入不必要的耦合。

4. **`difftest_init` / `difftest_memcpy` / `difftest_regcpy` 等外部 API 在 `.so` 模式下被调用时**，会进入 `SimRefState` 的对应方法，最终触发模拟器初始化和 `runOneCycle` 循环——ROB 在这个循环里仍按正常 8 级流水线 tick 顺序运行。

→ 设计依据：varification-design.md §四（模拟器作为 REF）。

***

## 附：参考资料

→ 参考资料：`varification-design.md` 附录 A（临时 uOP 基线，详见 `code/uOP/uop指令说明.md` 字典约 400 项命名空间）和 附录 B（基线 vs 后端 uOP 缺口对照表）。后端实现 ROB 的存储/分支/原子模板时，附录 B 中标记为"宏指令级 stub"的指令需要走特殊提交路径。spec change-20260408-165944
