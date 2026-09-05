# HBFSim 实验与配置指南

## 1. 构建与测试

```sh
cd /Users/litao/Code/hbfsim
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

运行示例：

```sh
./build/hbfsim configs/hbf_baseline.yaml traces/example.csv
./build/hbfsim configs/hbf_small_test.yaml traces/read_write_erase.csv
./build/hbfsim configs/hbf_host_gc.yaml traces/host_gc_cycle.csv
./build/hbfsim configs/hbf_parallelism_groups.yaml traces/parallelism_groups.csv
```

命令行只接收两个参数：配置文件和 trace 文件。输出目录由 `statistics.output_dir` 决定。

## 2. Trace 格式

CSV 列：

```text
timestamp_ns,op,address,size,stream[,axi_id,axi_port]
```

| 字段 | 说明 |
|---|---|
| `timestamp_ns` | 请求到达仿真时间，单位 ns |
| `op` | `R/READ`、`W/WRITE`、`E/ERASE`、`REFRESH`、`I/INVALIDATE/TRIM/DISCARD` |
| `address` | 字节地址，支持十进制或 `0x` 十六进制 |
| `size` | 字节数，支持 `KiB/MiB/GiB/TiB` |
| `stream` | 可选 stream ID，当前记录但不参与调度 |
| `axi_id` | 可选 AXI ID；缺省为 0，只在规范 Profile 中参与同 ID completion 保序 |
| `axi_port` | 可选 AXI Port；缺省时由地址 interleave 推导，显式值必须与推导结果一致 |

`media_research` Profile 中 Read/Write/Invalidate 的 `size` 必须非零且按 Page 对齐；`hbf_v0_7`/`ai_system` Profile 的 Read/Write 地址和长度按 64B 对齐，且单个请求不能跨越 Channel 或 AXI Port 地址域。规范 Write 以 64B fragment 累积成 4KiB DLU。Invalidate 是 Host-managed 映射的显式逻辑失效控制，不产生 NAND 数据事务；Erase/Refresh 使用地址定位 Block，size 可以为零。Trace 必须按时间戳非递减排列；解析器逐条读取，不把整个文件或全部 HostArrival 事件装入内存。

## 3. 配置参考

### 3.1 仿真控制

| Key | 含义 |
|---|---|
| `simulation.profile` | `media_research/hbf_v0_7/ai_system`；决定兼容研究路径或规范路径 |
| `simulation.max_requests` | 最大提交请求数；0 表示无限制 |
| `simulation.warmup_requests` | 前 N 个请求执行并完全排空后进入测量阶段 |
| `protocol.abstraction` | 当前只支持 `transaction`；`flit` 会被明确拒绝 |
| `initialization.mode` | `empty/image_loaded/preconditioned`；后两者按需物化读到的有效页 |

### 3.2 拓扑与主机接口

| Key | 含义 |
|---|---|
| `device.stacks` | Stack 数 |
| `host_interface.channels_per_stack` | 每 Stack Host Channel 数 |
| `host_interface.bandwidth_per_channel` | 每 Channel 字节带宽，如 `256GBps` |
| `host_interface.fixed_latency_ns` | Host Link 固定传播延迟 |
| `host_interface.full_duplex` | `true` 时 Command/H2D/D2H 使用独立资源；`false` 时共享串行资源 |

### 3.3 HBF Channel、AXI 与 DLU

| Key | 含义 |
|---|---|
| `hbf.channel_count` | HBF Channel 总数；0 表示使用 `stacks × channels_per_stack` |
| `hbf.channel_interleave` | Global Address 到 Channel 的 interleave，支持 64B–4KiB 的 2 的幂 |
| `hbf.page0_auto_erase` | 在 dirty Block 上写 Page 0 时执行 Erase+Program 组合操作 |
| `hbf.dlu.size` | DLU 大小；规范 Profile 固定为 4KiB |
| `hbf.dlu.max_pending` | 每个 Channel 同时存在的 Pending DLU 上限 |
| `hbf.dlu.accumulation_timeout_ns` | 从首个 fragment 到齐套的最长时间 |
| `axi.ports_per_channel` | 每 Channel AXI Port 数；规范 Profile 支持 1/2/4 |
| `axi.port_interleave` | Channel Local Address 到 AXI Port 的 interleave，支持 64B–4KiB 的 2 的幂 |
| `axi.id_count` | 可使用的 AXI ID 数量 |
| `axi.max_outstanding_per_id` | 每 `(Channel, Port, ID)` 最大未完成事务数 |

AXI 按 `(Channel, Port, ID)` 保存 issue FIFO：同 ID completion 严格有序，不同 ID 可乱序。Pending DLU 对已覆盖的 Read 范围直接转发；未覆盖范围返回 Read Pending Write。超时、重叠和容量压力通过 `HbfResponse` 状态返回。

### 3.4 NAND 组织

| Key | 含义 |
|---|---|
| `nand.dies_per_stack` | 每 Stack Die 数 |
| `nand.planes_per_die` | 每 Die Plane 数 |
| `nand.blocks_per_plane` | 每 Plane Block 数 |
| `nand.pages_per_block` | 每 Block Page 数 |
| `nand.page_size` | Page 大小 |
| `nand.strict_media_validation` | 是否拒绝读取未编程 Page |

### 3.5 NAND 时序

| Key | 含义 |
|---|---|
| `nand.timing.read_ns` | 阵列读取时间 |
| `nand.timing.program_ns` | 阵列编程时间 |
| `nand.timing.erase_ns` | Block 擦除时间 |
| `nand.timing.t_ccs_ns` | 同 Die 命令间隔 |
| `nand.timing.t_adl_ns` | Write 到 Data In 延迟 |
| `nand.timing.t_whr_ns` | 操作后恢复时间 |
| `nand.timing.suspend_ns` | 挂起开销 |
| `nand.timing.resume_ns` | 恢复开销 |
| `nand.timing.multi_plane_setup_ns` | Multi-plane 收集窗口 |
| `nand.timing.cache_program_setup_ns` | Cache Program 准备开销 |
| `nand.timing.read_retry_ns` | Read Retry 额外间隔 |

### 3.6 并行和高级功能

| Key | 含义 |
|---|---|
| `nand.parallelism.max_active_planes_per_die` | 每 Die 同时活跃阵列 Plane 上限 |
| `nand.parallelism.max_active_planes_per_stack` | 每 Stack 同时活跃阵列 Plane 上限 |
| `nand.features.suspend_resume` | 启用 Program/Erase Suspend/Resume |
| `nand.features.multi_plane` | 启用兼容 Multi-plane batch |
| `nand.features.max_multi_plane_width` | 一个 batch 最大 Plane 数 |
| `nand.features.cache_program` | 启用一页 Cache Program |

### 3.7 Base-Die Fabric

| Key | 含义 |
|---|---|
| `internal_fabric.ports_per_stack` | 每 Stack 数据端口数 |
| `internal_fabric.aggregate_bandwidth` | Stack 内部总带宽 |
| `internal_fabric.port_bandwidth` | 单 DataPort 峰值带宽，不再由总带宽除以端口数隐式计算 |
| `internal_fabric.fixed_latency_ns` | Fabric 固定传播延迟 |

### 3.8 映射与调度

| Key | 含义 |
|---|---|
| `mapping.policy` | `linear/fine_stripe/burst_stripe/host_managed` |
| `mapping.burst_size` | Burst Stripe 的连续 burst 大小 |
| `stripe.scope` | Host-managed 条带范围：`device/stack/custom` |
| `stripe.lanes` | `custom` Group 的 Plane/Lane 数；省略 scope 时非零值自动选择 custom |
| `scheduler.write_starvation_us` | 非 Read 最大等待阈值，单位 us |
| `scheduler.write_starvation_ns` | 非 Read 最大等待阈值，单位 ns；若同时设置则覆盖旧的 us key |
| `scheduler.source_aging_ns` | GC 等低优先级来源提升为最高仲裁级别前的等待时间 |
| `scheduler.max_consecutive_reads` | 连续读上限 |
| `host_management.auto_recovery` | Program Failure 后由仿真 Host 自动运行 Recovery Copy |
| `host_management.max_recovery_attempts` | destination 失败后的最大条带构建次数 |
| `copy_engine.max_inflight_reads` | 单个 Copy job 最大在途 NAND Read 数 |
| `copy_engine.max_inflight_programs` | 单个 Copy job 最大在途 destination Program 数 |
| `copy_engine.copy_buffer_size` | Host Copy Buffer 容量；不得小于一个 Page |
| `copy_engine.prefetch_window_pages` | 相对顺序 Program frontier 的最大 Read-ahead 窗口 |

`burst_stripe` 要求 burst_size 是 page_size 的整数倍，并且每个 Stack 的 Page 容量能容纳整数个 burst。

Host-managed Parallelism Group 的有效 Lane 数必须整除全设备 Plane 数，且不能切开 Stack 边界：一个 Group 必须均匀包含在单个 Stack 内，或覆盖整数个完整 Stack。`device` 是兼容默认值；`stack` 的宽度等于每 Stack 的 Die×Plane 数；`custom` 必须显式设置非零 `stripe.lanes`。

### 3.9 Automatic Host GC

| Key | 含义 |
|---|---|
| `host_gc.enabled` | 启用由仿真 Host policy 执行的水位触发 GC；仅支持 `host_managed` 映射 |
| `host_gc.low_watermark` | free stripe 数不高于该容量比例时进入回收压力状态，支持小数或 `%` |
| `host_gc.high_watermark` | 回收目标水位；free stripe 达到该比例后结束本轮压力状态 |
| `host_gc.overprovisioning_ratio` | 从 Host 可见逻辑容量中保留的物理 stripe 比例 |
| `host_gc.victim_policy` | `invalid_ratio` 或 `greedy`；相同得分以 physical stripe ID 确定性打破平局 |

全失效 Victim 直接执行多 Lane Erase，不占用 destination。部分失效 Victim 复用 CopyEngine 搬运有效 Page。当前一条逻辑区间只映射到一个物理条带，因此部分条带整理本身不会净增加 free stripe；若达不到高水位且没有新 Victim，管理器记录 stall，并在介质状态变化前抑制重复尝试。

### 3.10 可靠性

| Key | 含义 |
|---|---|
| `nand.reliability.program_failure_rate` | 每次 Program 的失败概率 `[0,1]` |
| `nand.reliability.program_failure_rate_per_erase` | 每次 P/E 后增加的 Program Failure 概率 |
| `nand.reliability.program_failure_budget` | 最多注入的 Program Failure 数；`0` 表示不限制 |
| `nand.reliability.raw_bit_error_rate` | 初次 Read 的原始位错误率 `[0,1]` |
| `nand.reliability.raw_bit_error_rate_per_erase` | 每次 P/E 后增加的 RBER |
| `nand.reliability.erase_failure_rate` | Erase 基础失败概率 |
| `nand.reliability.erase_failure_rate_per_erase` | 每次 P/E 后增加的 Erase Failure 概率 |
| `nand.reliability.max_erase_cycles` | 成功擦除达到该次数后退休；0 表示无限制 |
| `nand.reliability.retry_ber_multiplier` | 每次 Retry 后 BER 乘数 `[0,1]` |
| `nand.reliability.ecc_correctable_bits` | 每 Page/子请求可纠正 bit 数 |
| `nand.reliability.max_read_retries` | 最大重试次数，不含初次 Read |
| `nand.reliability.random_seed` | 确定性随机种子 |

### 3.11 Automatic Refresh

| Key | 含义 |
|---|---|
| `refresh.enabled` | 启用基于 Retention Deadline 的 Host-managed Refresh |
| `refresh.retention_time_ns` | 从条带首次成功 Program 起计算的 retention 时间 |
| `refresh.guard_time_ns` | 在 deadline 前提前启动 Copy 的时间 |
| `refresh.max_concurrent_jobs` | 同时存在的 Refresh Copy job 上限 |

Automatic Refresh 仅支持 `host_managed`，复用 Recovery/GC 的 CopyEngine，但使用独立 `TransactionSource::Refresh` 统计和优先级。

### 3.12 输出

| Key | 含义 |
|---|---|
| `statistics.output_dir` | CSV 输出目录 |
| `statistics.queue_depth_sample_interval_ns` | Queue Depth 最小采样间隔；0 表示记录每次变化 |

## 4. 输出指标

`summary.csv` 包含：

- 请求总数、Read/Write 数；
- Read/Write 数据面的 `completed_bytes` 与 `successful_bytes`；Invalidate 的范围字节数单独出现在 `op_INVALIDATE_bytes`；
- `failed_requests`；
- `parallelism_groups`、`stripe_width_pages`、`stripe_capacity_pages`；
- `program_failures`；
- `program_failure_notices`、`remap_commits`、`aborted_migrations`；
- Recovery/Host GC 完成与失败 job 数；
- Recovery/Host GC Read/Program bytes、恢复延迟和条带写放大；
- Recovery/Host GC Copy Buffer high-water mark；
- Automatic Host GC cycle、high-watermark completion、stall、任务数、erase-only 任务数、Host-visible stripe 与最小 free stripe 数；
- `corrected_reads`、`uncorrectable_reads`、`read_retries`；
- makespan 与 measurement duration；
- mean latency、p50/p95/p99/p99.9 latency；
- effective bandwidth 与 effective goodput；
- 每种操作的数量、字节数、带宽、mean、p99。

`plane_utilization.csv` 对所有 Plane 输出：

```text
plane,busy_ns,utilization
```

这里的 busy 表示 NAND array 占用时间，不重复计算与 Program 重叠的 Cache Data In。

其他输出：

- `latency_breakdown.csv`：Host command、Host data、NAND queue、array、fabric 的平均等待/服务时间；
- `source_latency_breakdown.csv`：按照 `TransactionSource × OpType` 分组的字节、失败数和延迟分解；
- `resource_utilization.csv`：每 Stack 的 Array-only、Fabric-only、Overlap、Idle，以及 Host 利用率和活跃 Plane；
- `queue_depth.csv`：按配置间隔采样的 Read/Write/Erase/Refresh 深度和活跃 Plane，并保留最终状态；
- `data_port_utilization.csv`、`die_utilization.csv`、`host_channel_utilization.csv`：细粒度资源占用。

每次直接运行还会生成 `resolved_config.yaml`。批量实验使用 `python3 tools/experiment_runner.py experiments/example_sweep.json --binary build/hbfsim`；Runner 会记录 Git SHA、Trace/config SHA-256、参数和日志，汇总 `sweep_summary.csv` 并自动生成 SVG。详细契约见 [V0.2.6_SCALABLE_STATS_EXPERIMENTS.md](V0.2.6_SCALABLE_STATS_EXPERIMENTS.md)。

## 5. 推荐实验矩阵

### 5.1 并行度扩展

保持 trace 和时序不变，扫描：

```text
stacks: 1, 2, 4, 8
channels_per_stack: 1, 2, 4
ports_per_stack: 8, 16, 32
max_active_planes_per_die: 1, 2, 4, ...
```

观察 mean/p99、goodput 和 Plane 利用率。如果增加 Plane 但 goodput 不再增长，优先检查 Host Channel 或 Fabric 总带宽。

### 5.2 映射策略

对同一 trace 比较：

```text
linear
fine_stripe
burst_stripe
host_managed
```

重点观察热点 Plane、尾延迟和写入顺序约束。

### 5.3 高级命令收益

分别打开和关闭：

- `multi_plane`：适合跨 Plane 同行地址请求；
- `cache_program`：适合连续顺序写；
- `suspend_resume`：适合长 Program/Erase 与延迟敏感读混合负载。

一次只改变一个功能，保持随机种子固定。

### 5.4 可靠性敏感性

建议扫描：

```text
raw_bit_error_rate
ecc_correctable_bits
max_read_retries
retry_ber_multiplier
program_failure_rate
```

同时报告 latency、failed_requests 和 effective_goodput，避免只看吞吐而忽略错误请求。

## 6. 复现实验原则

每次实验应保存：

1. 配置文件副本；
2. Trace 来源、单位和预处理方式；
3. HBFSim 代码版本或提交号；
4. `random_seed`；
5. `summary.csv` 和 `plane_utilization.csv`；
6. 是否启用 warm-up 和 strict validation。

不要在不同实验间复用同一个输出目录，否则结果文件会被覆盖。

## 7. 基本正确性检查

- 所有测试通过：`ctest --test-dir build --output-on-failure`；
- Read-only trace 在关闭 strict validation 时可运行；
- 开启 strict validation 后，应先写再读；
- `failed_requests == 0` 时 goodput 应等于 completed bandwidth；
- Plane utilization 不应因 Cache Data In 重叠而机械超过 1；
- 相同 seed 重复运行应得到相同可靠性统计；
- `write → erase → write` 不应触发顺序写违规。

## 8. 已有测试覆盖

| 测试 | 覆盖内容 |
|---|---|
| `test_mapper` | 映射唯一性与 Burst Stripe |
| `test_scheduler` | 读优先和防饥饿基础路径 |
| `test_timing` | 多 Plane 基础时序 |
| `test_erase` | Write/Erase/Rewrite/Read 生命周期 |
| `test_resources` | Link、显式 DataPort 带宽、Host/DataPort 解耦和全双工 |
| `test_channels` | Host Channel 并行收益 |
| `test_warmup` | Warm-up 排除统计 |
| `test_advanced` | Page 瞬态、ready、失败、ECC、Retry、Multi-plane、Cache、Suspend/Resume |
| `test_v011` | 初始化模式、严格 Warmup phase、流式 source 和新增统计文件 |
| `test_stripe_mapping` | Host-managed 条带公式映射、generation、位图和 remap 不变量 |
| `test_copy_engine` | Recovery/GC 流水、Copy Buffer、destination failure drain/retry |
| `test_host_gc` | OP 容量边界、Victim 策略、全失效 fast path、部分条带 CopyEngine 和 stall 抑制 |
