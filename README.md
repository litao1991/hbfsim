# HBFSim v0.4.3

HBFSim is a trace-driven, single-threaded discrete-event simulator for HBF-style NAND stacks. It models performance-relevant resources rather than packet- or bit-level hardware details.

## Included through v0.4.3

- v0.4.1 is a behavior-preserving ownership refactor: public declarations are split into narrow headers, `HbfSystem` composes protocol/controller/media/extension components, and `Simulator` no longer owns device media, cache, interconnect, or CopyEngine state.
- v0.4.2 moves controller execution state (active-plane credits, dispatch cursor/wakeup, and program-ready queues) from `Simulator` into `BaseDieController`, preserving the v0.4.1 model behavior.
- v0.4.3 splits per-plane controller scheduling state from NAND media state. `NandMediaSystem` now owns program/read/erase start, completion, failure, page-state, array-ready, and data-register transitions; the controller owns only queue and active-command state.

- Explicit `media_research`, `hbf_v0_7`, and `ai_system` simulation profiles separate compatibility experiments from the specification-oriented path. HBF/AI profiles default research extensions off.
- `HbfSystem` is now the device-model composition root for mapping, routing, reliability, Host GC, and Refresh services; `Simulator` retains time and event ownership.
- `HbfResponse`, `HbfStatus`, and structured error context provide the protocol-independent response foundation; v0.3.4 binds the verified Read/Write status-table encodings at the protocol boundary.
- Specification checks live under `tests/spec/`, carry CTest `spec`/`compliance` labels, and participate in the model-validation gate.
- HBF Channels now expose reversible Global Address → Channel → Local Address translation and disjoint Channel-owned NAND Plane pools. Channel interleave supports power-of-two granularities from 64B through 4KiB.
- Each Channel supports 1/2/4 address-interleaved AXI Ports. Transaction-level flow control limits outstanding commands per `(Channel, Port, AXI ID)` and preserves same-ID completion order while allowing different IDs to complete out of order.
- Spec-profile Read/Write completions carry Table 13 semantic status and protocol code through `HbfResponse`; invalid host requests and media-visible failures no longer require exceptions.
- The write path accumulates aligned 64B fragments in per-Channel 4KiB DLU buffers. Only a complete DLU enters NAND Program; overlap, capacity pressure, timeout, pending-write reads, forwarding, and Page-0 Auto-Erase are modeled.
- Profile construction now shares one defaulting path, protocol status is separated from completion class/data validity, and one HBF Read/Write command is constrained to a single 4KiB Channel-local Page/DLU.
- Specification Channels use an explicit `linear` or `fine_stripe` media-placement policy. `fine_stripe` distributes consecutive Channel-local pages across the Channel-owned Plane pool.
- DLU timing aggregates all contributing H2D fragments; a generation-checked deadline heap provides scalable timeout handling and summary metrics.
- Page-0 Auto-Erase is an explicit Erase completion followed by Program, so metadata, failures, retirement, and timing occur at their real phase boundary.
- Configurable Banks provide independent command-spacing domains. Each Bank has a two-entry 4KiB Read Cache; cache hits bypass NAND Sense while retaining DataFabric and Host D2H contention.

- Host commands and write/read payloads use separate staged events. The research path splits requests at Page boundaries; the specification path additionally respects Channel/AXI Port boundaries and assembles 64B Write fragments into 4KiB DLUs.
- Stack → Die → Plane topology, with per-die and per-stack array-concurrency caps.
- Host routing and NAND placement are independent: logical-address striping selects a Host Channel, while media placement selects DataPort/Die/Plane.
- Host command, H2D, and D2H resources support configurable full-duplex operation; per-stack DataFabric models both explicit per-port bandwidth and aggregate bandwidth.
- NAND read, program, erase, and refresh operation types; read/program/erase timing is configurable.
- `linear`, `fine_stripe`, `burst_stripe`, and implicit stripe-level `host_managed` mapping policies.
- Read-priority scheduling with non-read aging, round-robin plane dispatch, separate user/erase/refresh queues, sequential-program validation, deterministic event ordering, CSV trace replay, and per-operation CSV statistics.
- NAND block lifecycle: `FREE → OPEN → CLOSED → ERASING → FREE`, program-frontier and valid-page tracking, erase-count updates, and optional strict read validation.
- Page lifecycle with stable `ERASED / VALID / INVALID / FAILED` states and sparse `READING / PROGRAMMING` transient states. Stable states use lazy bitmaps so untouched blocks consume no per-page storage.
- Independent Block, Plane, and Die readiness timestamps, plus configurable `tCCS`, `tADL`, `tWHR`, suspend, resume, multi-plane setup, cache-program setup, and read-retry delays.
- Program/erase suspend and resume for queued reads, compatible same-die multi-plane batches, and one-page cache-program overlap per plane.
- Deterministic seeded program-failure injection and Poisson raw-bit-error sampling, with configurable ECC strength, retry count, retry latency, and retry BER reduction.
- Host-managed writes reserve monotonically increasing stripe slots when the Host command is accepted. In-place overwrite and skipped slots are rejected; completion commits `VALID` or `FAILED` state without a page-level L2P entry.
- A Host-managed physical stripe spans the same block index within one configurable Parallelism Group. `device`, `stack`, and custom lane scopes preserve a fixed O(1) LPN→PPA/PPA→LPN interleave while exposing stripe width, GC granularity, recovery cost, and failure-domain tradeoffs.
- Source-aware arbitration separates User, Recovery, Maintenance, Mapping, Refresh, and GC traffic. Critical Recovery and foreground reads receive priority, while configurable aging prevents background starvation.
- Recovery and explicit Host GC use a shared timed CopyEngine. Live pages traverse NAND Read, DataFabric, Host D2H/H2D, DataFabric, and NAND Program before atomic remap and multi-lane source erase. Failed destination stripes are aborted and retried without losing the active source.
- CopyEngine reads are pipelined with configurable read-ahead, in-flight Read/Program limits, and a capacity-bounded Host Copy Buffer. Reads may complete out of order, while destination slots are reserved strictly in increasing order. A destination failure first drains already-issued work before abort/erase/retry.
- `HostGcManager` starts Host-managed reclamation at a configurable free-stripe low watermark and continues toward a high watermark. It supports deterministic `invalid_ratio` and `greedy` victim selection, reserves overprovisioned stripes from Host-visible capacity, and directly erases fully invalid victims without allocating a destination.
- `RefreshManager` tracks a retention deadline from each stripe's first successful program. It starts deadline/guard-time driven migration through the shared `CopyEngine`; Refresh is a separate transaction source so its contention with User Read and Host GC is measurable.
- Block `erase_count` now raises configurable RBER, Program Failure, and Erase Failure probabilities. Erase failure or the configured P/E limit retires the affected block and, for Host-managed mapping, removes the entire physical stripe from allocation while reporting reduced usable capacity.
- Trace `TRIM`/`INVALIDATE`/`DISCARD` requests explicitly invalidate Host-managed logical pages. Rewriting the range is legal only after reclamation has atomically removed the old mapping; implicit overwrite remains forbidden.
- CSV traces stream one record at a time. The production path enforces `INITIALIZE → WARMUP → MEASURE → DRAIN`; warm-up is fully drained before measured requests are admitted.
- `empty`, `image_loaded`, and `preconditioned` initialization modes allow strict validation for read-only traces without allocating metadata for the full device.
- Fixed-memory latency histograms report p50/p95/p99/p99.9. Additional CSVs expose transaction latency breakdown, queue depth, Stack array/fabric overlap, and Host Channel/DataPort/Die utilization.
- `ResourceTracker` accumulates Array/Fabric/Host occupancy and overlap online with memory bounded by topology. Queue depth is interval-sampled rather than retained at every state change.
- `tools/experiment_runner.py` expands Cartesian parameter sweeps, runs them in parallel, records Git SHA and SHA-256 hashes, preserves input and fully resolved configs, aggregates metrics, and creates dependency-free SVG plots.

Explicit in-place Refresh remains available as a maintenance operation. Automatic Refresh uses copy/remap/erase semantics. v0.2.7 added a repeatable model-validation gate; v0.3.0 added configurable Parallelism Groups while retaining full-device stripes by default. Wear leveling, temperature-aware retention, detailed voltage-threshold distributions, HBM overlap, Batch Read, Host-driven Retry/Replay, and packet-level UCIe remain outside v0.4.3.

The reliability model is command-level rather than bit-level: each read samples a raw error count from a Poisson distribution, ECC corrects counts within `ecc_correctable_bits`, and each retry multiplies BER by `retry_ber_multiplier`. A failed program consumes its sequential-program position but does not replace the previous L2P mapping.

## Source layout

The implementation is split by responsibility so that NAND behavior and experiment plumbing can evolve independently:

```text
src/kernel/       simulator lifecycle, event queue, event completion
src/protocol/     Channel/AXI/DLU semantics and ProtocolFrontend
src/controller/   BaseDieController, scheduling policy, host/fabric resources
src/media/        topology, NAND state, Bank Read Cache, reliability
src/mapping/      media placement
src/management/   specification-oriented maintenance
src/extensions/   Host-managed stripes, CopyEngine, experimental Copy GC
src/frontend/     streaming CSV input
src/stats/        online resource and result statistics
src/config/       parsing and resolved configuration
```

`include/hbfsim/core.h` is now a compatibility umbrella over narrow component headers. New code should include the owning header directly. See [`docs/V0.4.1_ARCHITECTURE_REFACTOR.md`](docs/V0.4.1_ARCHITECTURE_REFACTOR.md).

## Build and run

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/hbfsim configs/hbf_baseline.yaml traces/example.csv
```

The trace columns are `timestamp_ns,op,address,size,stream[,axi_id,axi_port]`; timestamps must be nondecreasing. The AXI Port column is optional because the spec profile normally derives it from address interleave. Sizes may use `KiB`, `MiB`, `GiB`, or `TiB`; addresses accept decimal or `0x` hexadecimal notation. Results are emitted under `statistics.output_dir`. Each run writes `summary.csv`, latency/source breakdowns, online resource utilization, sampled `queue_depth.csv`, and a complete `resolved_config.yaml`.

The baseline uses `initialization.mode: image_loaded` with strict validation. Metadata is materialized lazily for pages referenced by reads, so a large read-only image does not require one in-memory object per NAND page.

Extended behavior is configured under `nand.timing`, `nand.features`, and `nand.reliability`; `configs/hbf_small_test.yaml` enables the timing and command features with zero failure rates. `summary.csv` reports failed requests, program failures, ECC-corrected reads, uncorrectable reads, retry count, and successful-byte goodput. Plane utilization now measures NAND-array occupancy; overlapped cache data transfer is not double-counted as array activity.

Multi-plane grouping requires the same operation, die, block index, and page index on different planes. `multi_plane_setup_ns` is the collection window. Cache program provides one staged page per plane, allowing its Data In transfer to overlap the active program operation.

The supplied baseline is a FLINT-like research configuration, not a claim about a mandatory HBF standard timing or topology.

The v0.2 Host-managed mapping contract and implementation status are documented in [`docs/HOST_MANAGED_STRIPE_MAPPING.md`](docs/HOST_MANAGED_STRIPE_MAPPING.md). Automatic Refresh details are in [`docs/V0.2.4_AUTOMATIC_REFRESH.md`](docs/V0.2.4_AUTOMATIC_REFRESH.md).

Parallelism Group geometry and compatibility are documented in [`docs/V0.3.0_PARALLELISM_GROUPS.md`](docs/V0.3.0_PARALLELISM_GROUPS.md). Run its example with `./build/hbfsim configs/hbf_parallelism_groups.yaml traces/parallelism_groups.csv`.

The v0.3.1 Profile, `HbfSystem`, response, and compliance-test boundary is documented in [`docs/V0.3.1_SPEC_FOUNDATION.md`](docs/V0.3.1_SPEC_FOUNDATION.md). `configs/hbf_v0_7_foundation.yaml` is the specification-oriented starter configuration.

The specification-oriented programming-model increments are documented in [`docs/V0.3.2_CHANNEL.md`](docs/V0.3.2_CHANNEL.md), [`docs/V0.3.3_AXI.md`](docs/V0.3.3_AXI.md), [`docs/V0.3.4_STATUS.md`](docs/V0.3.4_STATUS.md), and [`docs/V0.3.5_DLU.md`](docs/V0.3.5_DLU.md).

The semantic-convergence and read-path increments are documented in [`docs/V0.3.6_SEMANTICS.md`](docs/V0.3.6_SEMANTICS.md), [`docs/V0.3.7_CHANNEL_MEDIA_MAPPING.md`](docs/V0.3.7_CHANNEL_MEDIA_MAPPING.md), [`docs/V0.3.8_DLU_OBSERVABILITY.md`](docs/V0.3.8_DLU_OBSERVABILITY.md), [`docs/V0.3.9_AUTO_ERASE_EVENTS.md`](docs/V0.3.9_AUTO_ERASE_EVENTS.md), and [`docs/V0.4.0_BANK_READ_CACHE.md`](docs/V0.4.0_BANK_READ_CACHE.md).

Run the supplied four-point reproducible sweep with `python3 tools/experiment_runner.py experiments/example_sweep.json`. See [`docs/V0.2.6_SCALABLE_STATS_EXPERIMENTS.md`](docs/V0.2.6_SCALABLE_STATS_EXPERIMENTS.md) for the manifest schema and output contract.

Run the release validation gate with `python3 tools/model_validation.py --build-dir build`. See [`docs/V0.2.7_MODEL_VALIDATION.md`](docs/V0.2.7_MODEL_VALIDATION.md) for its coverage and report contract.

For an automatic reclamation example, run `./build/hbfsim configs/hbf_host_gc.yaml traces/host_gc_cycle.csv`. The trace fills Host-visible capacity, explicitly trims one stripe, lets `HostGcManager` reclaim it, and then rewrites and reads the range.

For a write/erase/rewrite lifecycle smoke test, use `traces/read_write_erase.csv` with a small configuration. In `host_managed` mode, Erase expands to every lane in the physical stripe; only after all lane blocks finish erasing can that physical stripe be allocated with a newer generation. The automated erase test also enables strict validation and reads back the rewritten page.
