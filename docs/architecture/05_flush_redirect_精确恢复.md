# Flush / Redirect 精确恢复

## 1. 优先级规则

全机统一优先级（推荐）：

`redirect_mispredict > redirect_exception > replay > normal`

第一阶段可简化为：

`redirect > exception > normal`

同拍多事件仲裁必须由统一仲裁点（建议 ROB/Commit）产生最终 `FlushEvent`，其余模块只消费结果，不自行二次仲裁。

## 2. 生效时序

1. 当拍检测到 flush 事件。
2. 当拍杀掉 younger 在途有效项（各级 `valid=0`）。
3. 下一拍由前端 `PIFetch` 使用 `redirect_pc` 重启。

## 3. Younger 判定

统一使用 `seq_id` 判定：

- 保留：`seq_id <= flush_seq_id`
- 清除：`seq_id > flush_seq_id`

> 实现要求：若 `seq_id` 为环形计数器，必须使用“带回卷”的年龄比较函数，不能直接做裸比较。

推荐统一接口：

- `is_younger(seq_a, seq_b, head_seq)`
- `kill_if_younger(entry_seq, flush_seq)`

## 4. 各模块响应职责

- **前端（PIFetch/IFetch/PreDecode/Decode/FrontEnqueue）**：停止当前错误路径，清理 younger。
- **UopBuffer**：删除 younger，保留 older。
- **后端队列（Issue/LSU 等）**：按 `seq_id` 批量 kill younger。
- **ROB/Commit**：保持精确异常语义，已提交不可回滚。

附加约束：

1. flush 生效拍优先于普通前推。
2. flush 与背压并发时，先 kill 后恢复握手。
3. flush 与写回并发时，提交口径以 ROB 队头精确状态为准。

## 5. 典型冲突场景

### 5.1 同拍分支错判 + 异常

按优先级先处理 `redirect_mispredict` 或 `redirect_exception`（按最终定义），其余事件延迟到下一拍仲裁。

### 5.2 flush 与背压并发

flush 优先于数据前推；先 kill，再恢复 `ready/valid` 正常流动。

### 5.3 flush 与指针回卷并发

若 head/tail/seq 在本拍跨界回卷，年龄比较必须基于统一比较函数；禁止按“数值大小”直接判断 older/younger。
