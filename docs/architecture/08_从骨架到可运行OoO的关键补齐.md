# 从骨架到可运行 OoO 的关键补齐

## 1. 这份补齐文档解决什么问题

当前代码已经具备“模块拆分正确、主流水可走通”的基础，但还没有形成真正的乱序语义闭环。  
本文件给出三件最关键补齐项的可执行方案：

1. `Rename` 从“字段填充”升级为“真实寄存器重命名”。
2. `Issue` 从“直通分流”升级为“真实 IQ + wakeup/select”。
3. `ROB + Commit` 从“最小退休”升级为“可恢复、可验证的精确提交”。

---

## 2. 当前缺口总览（按风险排序）

## 2.1 P0：影响架构正确性的缺口

1. `Rename` 没有 `specRAT/freeList/checkpoint`，`dst_preg` 未真实分配。
2. `Issue` 没有队列与唤醒，无法体现数据相关与执行时延。
3. `Commit` 只处理单项退休，恢复事件简化，无法覆盖真实 mispredict/exception 流程。

## 2.2 P1：影响吞吐与可扩展性的缺口

1. `EXU/LSU` 目前同拍完成，无法反映多周期执行和端口竞争。
2. 前端目前是组合骨架，`ready` 背压场景下“保持稳定”语义需要由缓冲级保证。
3. 多 ISA 插件接口已经在文档定义，但代码里仍是单一 RISC-V 路径。

---

## 3. 补齐项 A：Rename 真实化

## 3.1 最小状态定义

建议在 `Rename` 内固定以下状态：

```cpp
struct RenameState {
  wire<PRF_IDX_WIDTH> arch_rat[AREG_NUM]; // 仅 commit 更新
  wire<PRF_IDX_WIDTH> spec_rat[AREG_NUM]; // rename 前推更新
  wire<1>             preg_ready[PRF_NUM];

  RingPtr<PRF_NUM> freelist_head;
  RingPtr<PRF_NUM> freelist_tail;
  wire<PRF_IDX_WIDTH> freelist_q[PRF_NUM];

  struct Checkpoint {
    wire<1> valid;
    wire<PRF_IDX_WIDTH> spec_rat_snap[AREG_NUM];
    RingPtr<PRF_NUM> freelist_head_snap;
    RingPtr<PRF_NUM> freelist_tail_snap;
  } br_ckpt[BR_TAG_NUM];
};
```

## 3.2 单拍重命名流程（组合）

1. 先做资源探测：本拍输入 bundle 需要多少个 `dst_en=1`，`freeList` 是否足够。
2. 若资源不足：本拍整包不前推，保持握手一致性（不做半包提交）。
3. 若资源充足：按 lane 顺序执行：
- 读取 `spec_rat` 作为 `src_preg`。
- 对 `dst_en=1` 分配新 `dst_preg`，记录 `old_dst_preg`。
- 立即更新工作副本 `spec_rat[dst_areg] = new_preg`，保证同拍后续 lane 可见前序 lane 的 rename 结果。
4. 若遇到分支：分配/绑定 `br_tag` 并写入 checkpoint。

## 3.3 时序更新（seq）

1. 正常路径：锁存 `spec_rat/freeList/preg_ready` 工作副本。
2. 提交路径：仅 commit 更新 `arch_rat`，并回收 `old_dst_preg` 到 freeList。
3. flush 路径：
- mispredict：恢复到目标分支 checkpoint。
- exception/full flush：恢复到“安全锚点”（通常与 commit 同步）。

## 3.4 必须长期保持的不变量

1. `arch_rat` 仅 commit 修改。
2. `spec_rat` 仅 rename/flush 修改。
3. 任意时刻 `allocated + free == PRF_NUM`。
4. `dst_en=1` 的 uop 必须满足 `dst_preg != old_dst_preg`（除架构明确允许的特例）。

---

## 4. 补齐项 B：Issue 升级为真实 IQ

## 4.1 最小条目结构

```cpp
struct IqEntry {
  wire<1> valid;
  ExecUop uop;
  wire<1> src_ready[3];
  wire<1> issued;
  wire<8> age; // 可先用循环年龄或 rob 距离近似
};
```

## 4.2 组合流程分三步

1. `enq`：接收 `Dispatch` 输入，写入空槽。
2. `wakeup`：根据 `WriteBack` 广播的 `dst_preg` 置位匹配条目的 `src_ready`。
3. `select`：在 `ready && valid && !issued` 条目中按固定仲裁选择发射（例如 oldest-first + FU 端口限制）。

## 4.3 发射与端口规则（首版可简化）

1. 同拍同端口最多发射 1 条。
2. 同一 IQ 条目同拍只能发射一次。
3. flush 拍禁止 younger 条目继续发射。

## 4.4 与 `expect_mask/cplt_mask` 的关系

1. `Dispatch` 负责设置 `expect_mask`（例如纯 INT 用 bit0，load 可能 bit1，store 可能 bit2）。
2. `Issue/Exu/Lsu` 只负责产生来源位，不修改预期集合。
3. `ROB` 只做 `cplt_mask |= source_mask`，不覆盖旧位。

---

## 5. 补齐项 C：ROB + Commit 精确恢复化

## 5.1 ROB 生命周期（固定）

1. `ALLOC`：Dispatch 入队，写入静态元数据。
2. `COMPLETE`：WriteBack 回写完成位与结果侧带。
3. `COMMIT`：头部满足 `(cplt_mask & expect_mask) == expect_mask` 才能退休。
4. `RECOVER`：收到 flush 后按年龄 kill younger，保留 older。

## 5.2 Commit 输出建议

`Commit` 产出统一事件结构，至少包含：

1. `flush`
2. `cause`（mispredict/exception/replay/fence）
3. `redirect_pc`
4. `redirect_rob_idx/epoch`
5. `flush_seq_id`（用于统一年龄比较）

## 5.3 多发射提交策略（建议）

1. `COMMIT_WIDTH` 可以保留为 4。
2. 每拍从 ROB 头开始顺序扫描，最多提交 `COMMIT_WIDTH` 条。
3. 一旦遇到未完成条目，停止本拍后续提交（保持程序序）。

## 5.4 恢复优先级（建议固定）

1. `exception` 高于 `mispredict`。
2. `mispredict` 高于普通 `replay/fence`。
3. 同拍冲突只允许一个“全机恢复源”胜出，其余延后。

---

## 6. BackTop 组合分组建议（按可验证顺序）

建议固定为以下组序，便于保持一致时序直觉：

1. `Group A`：提交候选与恢复仲裁（`ROB.commit -> Commit.arb`）。
2. `Group B`：恢复广播与 younger kill（`ROB.flush + FE kill`）。
3. `Group C`：执行与写回（`Issue.select -> Exu/Lsu -> WriteBack`）。
4. `Group D`：完成位汇入 ROB（`ROB.complete`）。
5. `Group E`：后端资源探测与前推（`ROB.ready + IQ.ready + Rename + Dispatch + ROB.alloc`）。

---

## 7. 分三周落地里程碑（可直接执行）

## 7.1 里程碑 M1：Rename 正确性闭环

1. 完成 `specRAT/freeList`。
2. 打通 `old_dst_preg` 提交回收。
3. 增加 3 类断言：
- `free + allocated == PRF_NUM`
- `arch_rat` 非 commit 不变
- `dst_en -> 新旧物理寄存器关系合法`

## 7.2 里程碑 M2：Issue 可观测乱序行为

1. 引入最小 IQ（入队、wakeup、oldest-first 发射）。
2. EXU 引入固定 1~2 拍延迟模型，LSU 引入最小 load/store 完成路径。
3. 增加统计：
- `issue_queue_occupancy`
- `wakeup_hits`
- `issue_stall_cycles_fu_busy`

## 7.3 里程碑 M3：ROB/Commit 精确恢复

1. 支持 `COMMIT_WIDTH` 多条退休。
2. 完成 `mispredict/exception` 优先级与统一 flush event。
3. 增加恢复回归：
- 分支错判 + 背压并发
- exception + wb 并发
- 指针回卷边界

---

## 8. 验收标准（达到以下即视为“可运行 OoO 骨干完成”）

1. 指令不再“同拍全完成”，能观察到真实的 ready/wakeup/issue 行为。
2. 提交严格按程序序，多发射提交在断点处停止。
3. flush 后 younger 全部失效，且前后端下一拍从 `redirect_pc` 重启。
4. 单 ISA RISC-V 长程序稳定运行，不变量断言长期为真。
5. 新增第二 ISA 时无需改后端主数据通路。

