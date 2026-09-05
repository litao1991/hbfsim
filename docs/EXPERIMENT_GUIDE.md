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
```

命令行只接收两个参数：配置文件和 trace 文件。输出目录由 `statistics.output_dir` 决定。

## 2. Trace 格式

CSV 列：

```text
timestamp_ns,op,address,size,stream
```

| 字段 | 说明 |
|---|---|
| `timestamp_ns` | 请求到达仿真时间，单位 ns |
| `op` | `R/READ`、`W/WRITE`、`E/ERASE`、`REFRESH` |
| `address` | 字节地址，支持十进制或 `0x` 十六进制 |
| `size` | 字节数，支持 `KiB/MiB/GiB/TiB` |
| `stream` | 可选 stream ID，当前记录但不参与调度 |

Read/Write 的 `size` 必须非零。Erase/Refresh 使用地址定位 Block，size 可以为零。Trace 最好按时间戳非递减排列。

## 3. 配置参考

### 3.1 仿真控制

| Key | 含义 |
|---|---|
| `simulation.max_requests` | 最大提交请求数；0 表示无限制 |
| `simulation.warmup_requests` | 前 N 个请求正常执行但不计入统计 |

### 3.2 拓扑与主机接口

| Key | 含义 |
|---|---|
| `device.stacks` | Stack 数 |
| `host_interface.channels_per_stack` | 每 Stack Host Channel 数 |
| `host_interface.bandwidth_per_channel` | 每 Channel 字节带宽，如 `256GBps` |
| `host_interface.fixed_latency_ns` | Host Link 固定传播延迟 |

### 3.3 NAND 组织

| Key | 含义 |
|---|---|
| `nand.dies_per_stack` | 每 Stack Die 数 |
| `nand.planes_per_die` | 每 Die Plane 数 |
| `nand.blocks_per_plane` | 每 Plane Block 数 |
| `nand.pages_per_block` | 每 Block Page 数 |
| `nand.page_size` | Page 大小 |
| `nand.strict_media_validation` | 是否拒绝读取未编程 Page |

### 3.4 NAND 时序

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

### 3.5 并行和高级功能

| Key | 含义 |
|---|---|
| `nand.parallelism.max_active_planes_per_die` | 每 Die 同时活跃阵列 Plane 上限 |
| `nand.parallelism.max_active_planes_per_stack` | 每 Stack 同时活跃阵列 Plane 上限 |
| `nand.features.suspend_resume` | 启用 Program/Erase Suspend/Resume |
| `nand.features.multi_plane` | 启用兼容 Multi-plane batch |
| `nand.features.max_multi_plane_width` | 一个 batch 最大 Plane 数 |
| `nand.features.cache_program` | 启用一页 Cache Program |

### 3.6 Base-Die Fabric

| Key | 含义 |
|---|---|
| `internal_fabric.ports_per_stack` | 每 Stack 数据端口数 |
| `internal_fabric.aggregate_bandwidth` | Stack 内部总带宽 |
| `internal_fabric.fixed_latency_ns` | Fabric 固定传播延迟 |

### 3.7 映射与调度

| Key | 含义 |
|---|---|
| `mapping.policy` | `linear/fine_stripe/burst_stripe/host_managed` |
| `mapping.burst_size` | Burst Stripe 的连续 burst 大小 |
| `scheduler.write_starvation_us` | 非 Read 最大等待阈值，单位 us |
| `scheduler.max_consecutive_reads` | 连续读上限 |

`burst_stripe` 要求 burst_size 是 page_size 的整数倍，并且每个 Stack 的 Page 容量能容纳整数个 burst。

### 3.8 可靠性

| Key | 含义 |
|---|---|
| `nand.reliability.program_failure_rate` | 每次 Program 的失败概率 `[0,1]` |
| `nand.reliability.raw_bit_error_rate` | 初次 Read 的原始位错误率 `[0,1]` |
| `nand.reliability.retry_ber_multiplier` | 每次 Retry 后 BER 乘数 `[0,1]` |
| `nand.reliability.ecc_correctable_bits` | 每 Page/子请求可纠正 bit 数 |
| `nand.reliability.max_read_retries` | 最大重试次数，不含初次 Read |
| `nand.reliability.random_seed` | 确定性随机种子 |

### 3.9 输出

| Key | 含义 |
|---|---|
| `statistics.output_dir` | CSV 输出目录 |

## 4. 输出指标

`summary.csv` 包含：

- 请求总数、Read/Write 数；
- `completed_bytes` 与 `successful_bytes`；
- `failed_requests`；
- `program_failures`；
- `corrected_reads`、`uncorrectable_reads`、`read_retries`；
- makespan 与 measurement duration；
- mean latency、p99 latency；
- effective bandwidth 与 effective goodput；
- 每种操作的数量、字节数、带宽、mean、p99。

`plane_utilization.csv` 对所有 Plane 输出：

```text
plane,busy_ns,utilization
```

这里的 busy 表示 NAND array 占用时间，不重复计算与 Program 重叠的 Cache Data In。

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
| `test_resources` | Link 流水线与 DataFabric 端口 |
| `test_channels` | Host Channel 并行收益 |
| `test_warmup` | Warm-up 排除统计 |
| `test_advanced` | Page 瞬态、ready、失败、ECC、Retry、Multi-plane、Cache、Suspend/Resume |
