# Flush / Redirect 精确恢复

## 1. 目标与范围

本文件定义“何时 flush、flush 杀谁、谁负责 redirect、何时重启取指”的统一语义。

核心目标：

1. 保证精确异常：已提交不可回滚，未提交 younger 可回滚。
2. 保证一致恢复：全机只接受一个最终 `FlushEvent`。
3. 保证可验证：每条规则都能通过断言与定向测试覆盖。

---

## 2. 统一事件模型

### 2.1 `FlushEvent`

最小字段：

1. `valid`
2. `cause`：`mispredict | exception | replay | fence`
3. `flush_seq_id`
4. `redirect_pc`
5. `rob_idx/rob_epoch`（可选年龄锚点）

### 2.2 事件来源

允许来源：

1. BRU 发现错判
2. Commit 发现异常/系统控制指令触发恢复
3. Replay 控制器请求重放

禁止：

- 非仲裁点模块自行广播最终 flush。

---

## 3. 仲裁优先级

推荐全机优先级：

1. `exception`
2. `mispredict`
3. `replay`
4. `fence`

说明：
- 异常优先于错判，保证精确异常优先语义。
- 同拍多事件只产出 1 个最终 `FlushEvent`。

---

## 4. 生效时序

定义“检测拍 N，生效拍 N/N+1”：

1. 拍 N：仲裁产生 `FlushEvent`。
2. 拍 N：各级 kill younger（`valid=0`）。
3. 拍 N+1：`PIFetch` 使用 `redirect_pc` 重启。

约束：
- flush 生效拍优先于普通前推。
- flush 与 backpressure 并发时，先 kill 再恢复。

---

## 5. Younger 判定

## 5.1 基于 `seq_id`

逻辑定义（概念）：

1. 保留：`seq <= flush_seq`
2. 清除：`seq > flush_seq`

如果 `seq_id` 会回卷，必须使用带 `head_seq` 的环形比较函数：

```cpp
bool is_younger_seq(uint64_t a, uint64_t b, uint64_t head_seq);
```

### 5.2 基于 ROB 指针

当恢复锚点来自 ROB 时，使用：

```cpp
bool is_younger_ptr(RingPtr a, RingPtr b, RingPtr head);
```

禁止：
- 直接使用 `a_idx > b_idx` 判断年龄。

---

## 6. 模块响应职责

### 6.1 前端（`PIFetch/IFetch/PreDecode/Decode/FrontEnqueue`）

1. 收到 flush 当拍清空 younger 有效项。
2. 取消旧路径输出。
3. 下一拍从 `redirect_pc` 重启。

### 6.2 `UopBuffer`

1. 按年龄规则删除 younger。
2. 保留 older。
3. 刷新 `head/tail/epoch` 到一致状态。

### 6.3 `Rename/Dispatch/Issue/LSQ`

1. 逐队列按年龄 kill younger。
2. checkpoint 按分支 ID 回滚（若为错判恢复）。
3. 清理本拍未提交的暂态分配结果。

### 6.4 `Exu/Lsu/WriteBack`

1. younger 的完成回传必须屏蔽。
2. older 已完成信息可保留供提交。

### 6.5 `ROB/Commit`

1. 作为最终仲裁点输出唯一 `FlushEvent`。
2. 保证“已提交不可回滚”。
3. 对同拍冲突事件按优先级裁决。

---

## 7. 典型冲突场景

### 7.1 同拍 `mispredict + exception`

规则：

1. 仅输出一个事件。
2. `cause=exception`。
3. `redirect_pc` 使用异常入口。

### 7.2 flush 与写回并发

规则：

1. 提交口径以 ROB head 精确状态为准。
2. younger 的写回结果不可污染可见架构态。

### 7.3 flush 与指针回卷并发

规则：

1. 年龄比较必须基于统一函数。
2. 禁止按裸数值大小判定。

### 7.4 连续两拍 flush

规则：

1. 新事件覆盖旧事件。
2. 前端以“最后一次有效 flush”作为重启目标。

---

## 8. 断言建议（运行时）

1. flush 当拍后，所有 younger 队列项 `valid=0`。
2. flush 不得回滚已提交项。
3. `redirect_pc` 在 flush 事件有效时必须有定义值。
4. 同拍多事件仲裁后 `FlushEvent` 仅一份有效。
5. flush 后 N 拍内前端必须出现来自 `redirect_pc` 的取指。

---

## 9. 最小验证用例

1. 线性流 + 人工触发 exception。
2. 分支错判恢复。
3. mispredict 与 exception 同拍冲突。
4. flush 与持续 `ready=0` 并发。
5. head/tail/seq 回卷边界下的 flush。
6. 连续两拍 flush 的覆盖行为。

---

## 10. 落地建议

1. 先实现统一 `FlushEvent` 数据结构。
2. 再实现统一年龄比较函数（seq + ptr 两套）。
3. 最后接入每个模块的 kill 策略与断言。
