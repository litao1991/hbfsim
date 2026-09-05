# Host-managed 顺序条带与隐式 Reverse Mapping 设计

## 1. 文档状态

本文定义 HBFSim v0.2 的 Host-managed 映射、Program Failure 恢复和主动 GC 语义，也是实现状态清单。

截至 v0.2.2，阶段 A/B 已完成；阶段 C 的失败通知、Host replay、流水数据搬运和 destination retry 已完成；阶段 D 支持 Host 显式选择 victim、有效 slot 搬运、原子提交和源条带擦除。Automatic Refresh、自动 victim 策略，以及完整 extent/sparse fallback 仍在后续阶段。

设计目标是利用上层提供的严格顺序约束，把传统逐 Page L2P/P2L 表收敛为条带级元数据，同时仍然准确模拟物理 Page 状态、数据搬运流量、资源竞争和失败恢复延迟。

## 2. 上层必须保证的约束

隐式 Reverse Mapping 只有在以下条件同时成立时才安全：

1. 一个条带对应连续且按条带容量对齐的 LPN 区间；
2. Plane/Die/Lane 的交织函数固定且可逆；
3. 条带内只能按照 slot 递增顺序 Program；
4. 默认不允许跳写；如允许，必须记录 `hole_bitmap`；
5. Program Failure 仍消耗当前 slot，并写入 `failed_bitmap`；
6. 不允许原地覆盖，更新必须写入新条带；
7. 物理条带每次重新分配都必须使用新的 `generation`；
8. Program Failure 的数据重搬由 Host 感知并发起；
9. GC 的 Victim 选择、有效数据 Copy 和映射提交由 Host 主动执行。

如果 workload 或策略无法满足这些约束，该条带必须退化到显式 extent/sparse mapping，不能继续使用单一 `base_lpn + slot` 公式。

## 3. 条带几何与可逆交织

一个 `StripeBlock` 由 `W` 个固定 Lane 上、具有相同 Block index 的物理 Block 组成。Lane 可以展开表示 Stack 内的 Die/Plane 组合；具体展开方式由配置固定。

```text
StripeBlock
├── Lane 0: Die/Plane/Block
├── Lane 1: Die/Plane/Block
├── ...
└── Lane W-1: Die/Plane/Block
```

对于逻辑起点 `L0` 和条带 slot `s`：

```text
lpn  = L0 + s
lane = s % W
row  = s / W
ppa  = stripe_base + lane_to_die_plane(lane) + page(row)
```

反向计算为：

```text
s   = page_row × W + die_plane_to_lane(ppa)
lpn = L0 + s
```

因此正常条带的 LPN→PPA 和 PPA→LPN 都是 O(1)，不需要逐 Page 哈希表。

交织函数必须满足：

- 在一个条带内为双射；
- 不随请求时间、队列状态或随机数变化；
- 正向和反向实现共享同一份几何描述；
- 配置变更不能作用于已经分配的 generation。

## 4. 核心元数据

当前实现使用以下概念结构（另有内部 `reserved_bitmap` 和 `erased_lane_bitmap` 管理在途 Program 与多 Lane Erase）：

```cpp
enum class StripeState {
  Free,
  Open,
  Sealed,
  Degraded,
  Migrating,
  Stale,
  Erasing,
  Bad,
};

struct StripeId {
  std::uint64_t physical_id;
  std::uint32_t generation;
};

struct ExtentRun {
  std::uint32_t physical_start_slot;
  std::uint32_t slot_count;
  std::uint64_t logical_base_lpn;
};

struct StripeDescriptor {
  StripeId id;
  std::uint64_t logical_base_lpn;
  std::uint32_t next_program_slot;
  StripeState state;

  Bitmap valid_bitmap;
  Bitmap invalid_bitmap;
  Bitmap failed_bitmap;
  Bitmap hole_bitmap;

  std::vector<ExtentRun> extent_runs;
  SparseExceptionMap exceptions;
};
```

其中：

- `next_program_slot` 是唯一合法的下一编程位置；
- 正常连续条带不分配 `extent_runs` 和 `exceptions`；
- bitmap 延续 v0.1.1 的 lazy allocation 策略；
- `invalid_bitmap` 只在部分失效或部分迁移时需要；整条带原子替换可以直接使用 `Stale`；
- `hole_bitmap` 仅为显式允许的跳写保留；默认配置禁止跳写；
- `exceptions` 仅记录无法由隐式公式表达的少量映射。

## 5. 模块边界

```text
HostPlacementManager
├── StripeAllocator
├── StripeMappingTable
├── RecoveryManager
└── HostGcManager

NandMediaModel
├── 顺序 Program 校验
├── Page/Block/Stripe 状态
├── Program/Erase Failure
└── Read/Program/Erase 时序
```

职责划分：

- `StripeAllocator` 管理 Free/Open/Sealed/Stale Stripe；
- `StripeMappingTable` 保存逻辑区间到活动 `StripeId` 的映射；
- `RecoveryManager` 接收 Program Failure 通知并生成 Host Copy；
- `HostGcManager` 选择 Victim，生成 Copy、Commit 和 Erase；
- `NandMediaModel` 不替 Host 选择新位置，也不静默修改逻辑映射。

当前 `AddressMapper` 中的逐页 L2P 和每 Plane frontier 已由 `StripeMappingTable` 替代。非 Host-managed mapping policy 继续保留现有确定性 placement。

## 6. 正常写入路径

```text
ALLOC_STRIPE(logical_base)
        ↓
Stripe = OPEN, next_slot = 0
        ↓
Host 接受 PROGRAM(slot = next_slot)
        ↓
检查 generation 和顺序
        ↓
reserved[slot] = 1, next_slot++
        ↓
Program success
        ↓
reserved[slot] = 0, valid[slot] = 1
        ↓
写满或 Host 显式 SEAL
        ↓
Stripe = SEALED
```

slot 在 Host 命令拆分阶段预留，而不是等到 NAND 阵列开始执行时才分配。这样多个并发在途写仍能按 Host 提交顺序获得唯一 PPA。Program 的准入条件：

```text
command.stripe_generation == descriptor.generation
descriptor.state == OPEN
command.slot == descriptor.next_program_slot
valid/invalid/failed/hole 均未占用该 slot
```

任何失败的准入检查都必须产生明确错误，不能自动寻找其他 Page。

## 7. Read 路径

Host Read 先在 `StripeMappingTable` 中找到覆盖目标 LPN 的活动条带，再使用可逆公式计算 PPA。

```text
LPN
 ↓ interval lookup
active StripeDescriptor
 ↓ slot = LPN - logical_base
fixed interleave
 ↓
PPA + expected generation
```

介质访问必须再次检查 generation。若命令携带的 generation 已过期，则返回 `STALE_GENERATION`，防止擦除、复用后的旧请求访问新数据。

## 8. Program Failure 与 Host Recovery

Program Failure 发生时：

```text
reserved_bitmap[slot] = 0
failed_bitmap[slot] = 1
Stripe = DEGRADED
当前逻辑映射保持不变
```

设备向 Host 返回：

```cpp
struct ProgramFailureNotice {
  StripeId stripe;
  std::uint32_t failed_slot;
  PhysicalAddr failed_ppa;
  std::uint32_t committed_slots;
};
```

Host 恢复流程：

```text
Program Failure Notice
        ↓
Host 分配 destination Stripe
        ↓
按逻辑 slot 顺序读取 source 中已提交数据
        ↓
通过真实 DataFabric/Host/NAND 路径写入 destination
        ↓
Host 重新提供 failed slot 的原始 payload
        ↓
destination 完成并 SEAL
        ↓
REMAP_COMMIT(source, destination)
        ↓
source → STALE → ERASE/BAD
```

复制期间 source 仍是读请求的权威版本，destination 处于构建状态。只有完整成功的 `REMAP_COMMIT` 能切换映射；如果 destination 再次失败，则放弃该 generation 并重新分配目标条带。

v0.2.2 的 CopyEngine 会通过真实 Erase 事务回收被放弃的 destination；达到 `max_recovery_attempts` 后停止重试、保留 source 为活动权威版本，并记录失败的 Recovery job。

CopyEngine 不再按 `READ_i → PROGRAM_i → READ_i+1` 串行执行。它在 `prefetch_window_pages` 范围内预取有效 slot，并同时受 `max_inflight_reads`、`max_inflight_programs` 和 `copy_buffer_size` 约束。Read 可以乱序完成并进入 Host Copy Buffer，但 destination 的 slot 只能按逻辑顺序预留；无效 slot 同样按顺序转换为 hole。Read 获得的 buffer credit 在目标 Program 完成后释放，因此慢 Program 会自然反压新的 Read。

若任一在途 destination Program 失败，CopyEngine 停止发射新事务，等待已发射 Read/Program 全部完成，之后才执行 `ABORT_MIGRATION → ERASE destination → retry`。这样不会在 destination 仍有 reserved slot 时错误地切换 generation。状态表只保存预取窗口和在途 slot，不按完整条带容量物化每 Page 对象。

## 9. Host 主动 GC

HBFSim 不在设备侧隐式启动 GC。Host GC 由以下显式步骤组成：

```text
Host 选择 Victim Stripe
        ↓
标记 MIGRATING，禁止新的 Program
        ↓
READ live slot
        ↓
Data Out / Host Copy Buffer
        ↓
Data In / PROGRAM destination slot
        ↓
destination SEAL
        ↓
REMAP_COMMIT
        ↓
source STALE
        ↓
ERASE source
```

每一次 Copy 都必须生成正常的 NAND Read、Fabric Transfer 和 NAND Program 事务，并分别标记：

```cpp
TransactionSource::GarbageCollection
TransactionSource::Recovery
```

这样前台请求延迟、队列竞争、Host/Fabric 带宽和写放大才会真实反映主动 GC 与 Recovery 的成本。

如果没有覆盖、删除或条带退化，条带没有无效数据，Host 不需要为了传统 FTL 空间回收而启动 GC。GC 的主要触发来源是显式释放、部分失效、失败恢复后的旧条带回收，以及上层容量整理策略。

## 10. GC 重组和稀疏异常

优先要求 Host 把目标条带按照逻辑地址顺序重组，使其继续满足单一 `logical_base_lpn + slot` 公式。

如果一个目标条带必须容纳多个不连续逻辑区间，则使用少量 `ExtentRun`：

```text
physical slots [0, 127]   → logical [L0, L0+127]
physical slots [128,255]  → logical [L1, L1+127]
```

反向查询在有序 run 中完成，复杂度为 O(log R)，其中 R 是条带内 extent 数，而不是 Page 数。只有单个 slot 偏离 run 时才写入 `SparseExceptionMap`。

当异常或 extent 数超过配置阈值时，该条带应标记为 `ExplicitMapping`，物化完整 P2L；不能让异常查找无限增长。正常实验应单独统计发生退化的条带比例。

## 11. Generation 与原子提交

`generation` 属于物理条带身份的一部分。每次 Erase 后重新分配该物理条带时递增，且不能回绕后静默复用。

所有持久访问和控制命令携带：

```text
physical_stripe_id + expected_generation
```

`REMAP_COMMIT` 必须验证：

1. source 仍是当前活动映射；
2. source/destination generation 与命令一致；
3. destination 已经 SEALED；
4. destination 覆盖所需逻辑区间；
5. destination 没有未恢复的 failed/hole slot；
6. 同一逻辑区间不存在更新版本的 Host commit。

验证成功后一次性切换活动 `StripeId`。旧 source 变为 STALE，但在后续 Erase 完成前仍保留物理状态和统计信息。

## 12. 建议控制命令

v0.2 至少需要以下 Host 控制语义：

```text
ALLOC_STRIPE
PROGRAM
SEAL_STRIPE
BEGIN_MIGRATION
REMAP_COMMIT
ABORT_MIGRATION
ERASE_STRIPE
RETIRE_STRIPE
```

Copy 不是零延迟元数据命令，而是由 Read/Data Move/Program 事务组合而成。控制命令本身仍应占用 Host command resource。

## 13. 统计输出

新增建议指标：

- `host_gc_read_bytes`、`host_gc_program_bytes`；
- `recovery_read_bytes`、`recovery_program_bytes`；
- `program_failure_notices`；
- `remap_commits`、`aborted_migrations`；
- `stale_generation_rejects`；
- `stripe_write_amplification`；
- `recovery_latency` 的 mean/p95/p99；
- Free/Open/Sealed/Degraded/Migrating/Stale/Bad Stripe 数量时序；
- implicit/extent/sparse/explicit mapping 条带数量。

前台 goodput 必须与 GC/Recovery 搬运流量分开报告。

## 14. 复杂度

正常连续条带：

| 操作 | 时间复杂度 | 映射空间 |
|---|---:|---:|
| LPN → PPA | O(1) | O(Stripe 数) |
| PPA → LPN | O(1) | O(Stripe 数) |
| Program commit | O(1) | bitmap 按需增长 |
| 整 Stripe Erase | O(1) 元数据切换 | 不扫描全局 L2P |
| GC/Recovery 枚举 | O(Stripe slot 数) | O(有效 bitmap) |

非连续 GC 重组时，查询成本变为 O(log R)，R 为 extent run 数。只有显式退化条带才承担 O(Page 数) 的 P2L 空间。

## 15. 必须测试的不变量

1. 连续逻辑 LPN 正反向计算互为逆函数；
2. 所有 Lane/Die/Plane 组合无 slot 冲突；
3. 跳过 `next_program_slot` 必须失败；
4. Program Failure 消耗 slot，但不提交新活动映射；
5. Recovery/GC 完成前 Read 仍访问 source；
6. destination 未 Seal 时 `REMAP_COMMIT` 必须失败；
7. Commit 后新 Read 访问 destination；
8. 旧 generation 的到达事件必须被拒绝；
9. source Erase 不得清除 destination 映射；
10. destination 再次失败时 source 仍保持可恢复；
11. GC/Recovery Copy 必须计入真实资源利用率；
12. 整条带 Erase 不得扫描全局 L2P；
13. extent/sparse 退化不改变正常条带的公式映射；
14. 相同配置、Trace 和 seed 产生确定性结果。

## 16. 分阶段实施

### 阶段 A：条带几何和元数据

- [x] 定义 StripeId、StripeState、StripeDescriptor；
- [x] 实现正向/反向 slot 公式；
- [x] 加入 generation 校验和顺序 Program 不变量。

### 阶段 B：映射提交

- [x] 实现条带分配和 StripeMappingTable；
- [x] 增加 ALLOC/SEAL/BEGIN_MIGRATION/REMAP_COMMIT/ABORT；
- [x] 从 Host-managed `AddressMapper` 移除逐 Page L2P/frontier。

### 阶段 C：Program Failure Recovery

- [x] 产生 Host 可见 failure notice；
- [x] 用 Recovery transaction 真实搬运数据；
- [x] 支持 destination failure 状态、abort 和重新分配的控制面语义；
- [x] 在事件引擎中自动编排 destination failure 后的再次恢复。

### 阶段 D：Host GC

- [x] Host 通过 `start_host_gc` 显式选择 Victim；
- [x] 复制有效 slot，并为失效 slot 在 destination 保留 hole；
- [x] 原子 Commit 与 source `STALE` 状态切换；
- [x] 整 Stripe 多 Lane Erase；
- [x] 补充 Copy 流量、写放大、恢复/GC 延迟与完成状态统计；
- [ ] 增加条带状态数量的时间序列统计。

### 阶段 E：退化映射

- [x] 提供 lazy hole bitmap、ExtentRun 和 SparseExceptionMap 数据结构；
- [x] 增加 GC 使用的顺序 hole 接口；
- [ ] 增加 extent/sparse 的 Host 写入接口；
- [ ] 设置退化阈值和完整 P2L fallback；
- [x] 保证正常路径不承担异常路径的常驻内存成本。

## 17. 非目标

本设计不引入设备自主 FTL、设备自主 GC、隐式后台 Copy 或原地覆盖。Automatic Refresh、Wear Leveling、坏块容量降级和 HBM/HBF 联合模型仍是独立后续模块；它们必须通过相同 Host/Media 边界接入，不能绕过 generation 和原子映射提交规则。
