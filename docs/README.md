# HBFSim 设计文档

本目录记录 HBFSim v0.3.0 的架构、模型语义和实验方法。文档以当前代码实现为准。

## 文档索引

- [ARCHITECTURE.md](ARCHITECTURE.md)：总体架构、模块边界、事件流与调度机制。
- [MEDIA_AND_TIMING_MODEL.md](MEDIA_AND_TIMING_MODEL.md)：Page/Block 状态机、NAND 时序、可靠性与 ECC 模型。
- [EXPERIMENT_GUIDE.md](EXPERIMENT_GUIDE.md)：构建运行、配置项、trace 格式、统计指标与实验建议。
- [V0.1.1_OPTIMIZATION.md](V0.1.1_OPTIMIZATION.md)：本轮架构收敛内容、兼容性和后续边界。
- [HOST_MANAGED_STRIPE_MAPPING.md](HOST_MANAGED_STRIPE_MAPPING.md)：v0.2 Host-managed 顺序条带、隐式 Reverse Mapping、当前实现状态、失败恢复和主动 GC 设计。
- [V0.2.4_AUTOMATIC_REFRESH.md](V0.2.4_AUTOMATIC_REFRESH.md)：Retention Deadline、RefreshManager 与 CopyEngine 复用。
- [V0.2.5_WEAR_BAD_BLOCK_CAPACITY.md](V0.2.5_WEAR_BAD_BLOCK_CAPACITY.md)：P/E 磨损、坏块退休与容量降级。
- [V0.2.6_SCALABLE_STATS_EXPERIMENTS.md](V0.2.6_SCALABLE_STATS_EXPERIMENTS.md)：在线资源统计、队列采样和可复现实验 Runner。
- [V0.2.7_MODEL_VALIDATION.md](V0.2.7_MODEL_VALIDATION.md)：扩展性、媒体管理不变量和回归测试组成的发布门禁。
- [V0.3.0_PARALLELISM_GROUPS.md](V0.3.0_PARALLELISM_GROUPS.md)：可配置条带宽度、地址公式、分配策略和组级故障域。

## 设计定位

HBFSim 是面向 HBF 风格 NAND 堆叠设备的单线程、trace-driven、离散事件仿真器。它关注主机接口、Base-Die Fabric、NAND 阵列并行度、介质状态和命令级可靠性，不追求晶体管、模拟电压或逐比特协议精度。

如文档与代码不一致，应优先检查：

1. `include/hbfsim/core.h` 中的数据结构和公开接口；
2. `src/scheduler.cpp` 中的准入、仲裁与高级命令逻辑；
3. `src/events.cpp` 中的完成事件和状态迁移；
4. `tests/test_advanced.cpp` 中的可执行语义断言。
