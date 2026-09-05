# HBFSim 设计文档

本目录记录 HBFSim v0.5.2 的架构、模型语义和实验方法。文档以当前代码实现为准。

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
- [V0.3.1_SPEC_FOUNDATION.md](V0.3.1_SPEC_FOUNDATION.md)：Spec Profile、研究扩展边界、`HbfSystem` 组合根、`HbfResponse` 与规范测试目录。
- [V0.3.2_CHANNEL.md](V0.3.2_CHANNEL.md)：Host Channel 地址域、Global→Channel→Local 转换与独立 NAND Pool。
- [V0.3.3_AXI.md](V0.3.3_AXI.md)：1/2/4 AXI Port、地址交织、outstanding 和 AXI ID completion ordering。
- [V0.3.4_STATUS.md](V0.3.4_STATUS.md)：Read/Write Status 编码、`HbfResponse` 接入和异常边界。
- [V0.3.5_DLU.md](V0.3.5_DLU.md)：64B fragment、4KiB DLU、Pending/timeout、Read forwarding 与 Page-0 Auto-Erase。
- [V0.3.6_SEMANTICS.md](V0.3.6_SEMANTICS.md)：Profile 默认值、协议校验、Completion Class 与数据有效性。
- [V0.3.7_CHANNEL_MEDIA_MAPPING.md](V0.3.7_CHANNEL_MEDIA_MAPPING.md)：Channel 内 Linear/Fine Stripe 放置策略和唯一 Channel Domain。
- [V0.3.8_DLU_OBSERVABILITY.md](V0.3.8_DLU_OBSERVABILITY.md)：DLU 聚合时延、H2D 成本、统计和 Deadline Heap。
- [V0.3.9_AUTO_ERASE_EVENTS.md](V0.3.9_AUTO_ERASE_EVENTS.md)：Page-0 Auto-Erase 的独立 Erase/Program 事件阶段。
- [V0.4.0_BANK_READ_CACHE.md](V0.4.0_BANK_READ_CACHE.md)：Bank 命令域与双条目 4KiB Read Cache。
- [V0.4.1_ARCHITECTURE_REFACTOR.md](V0.4.1_ARCHITECTURE_REFACTOR.md)：行为冻结下的组件 ownership、头文件与源码目录重构。
- [V0.4.2_CONTROLLER_EXECUTION_STATE.md](V0.4.2_CONTROLLER_EXECUTION_STATE.md)：Controller execution state ownership 迁移。
- [V0.4.3_MEDIA_CONTROLLER_BOUNDARY.md](V0.4.3_MEDIA_CONTROLLER_BOUNDARY.md)：Plane media/controller 状态拆分与 NAND 状态转换边界。
- [V0.5.0_BATCH_READ_FOUNDATION.md](V0.5.0_BATCH_READ_FOUNDATION.md)：ReadType、Bank Sense queue 与 Batch Read 资源域。
- [V0.5.1_BATCH_READ_PROTOCOL.md](V0.5.1_BATCH_READ_PROTOCOL.md)：Batch hint、聚合窗口、边界、批量发射与统计。
- [V0.5.2_HOST_DRIVEN_RETRY.md](V0.5.2_HOST_DRIVEN_RETRY.md)：UECC retry stage、Host 重发与 research compatibility。

## 设计定位

HBFSim 是面向 HBF 风格 NAND 堆叠设备的单线程、trace-driven、离散事件仿真器。它关注主机接口、Base-Die Fabric、NAND 阵列并行度、介质状态和命令级可靠性，不追求晶体管、模拟电压或逐比特协议精度。

如文档与代码不一致，应优先检查：

1. `include/hbfsim/` 下对应组件的窄头文件；
2. `src/controller/scheduler.cpp` 中的准入、仲裁与高级命令逻辑；
3. `src/kernel/events.cpp` 中的完成事件和状态迁移；
4. `tests/test_advanced.cpp` 中的可执行语义断言。
