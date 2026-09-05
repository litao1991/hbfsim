# HBFSim v0.2.6

HBFSim is a trace-driven, single-threaded discrete-event simulator for HBF-style NAND stacks. It models performance-relevant resources rather than packet- or bit-level hardware details.

## Included through v0.2.6

- Host commands and write/read payloads use separate staged events; requests are split into page-sized subrequests.
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
- A Host-managed physical stripe spans the same block index on every configured Stack/Die/Plane lane. Its fixed interleave supports O(1) LPN→PPA and PPA→LPN, lazy state bitmaps, atomic replacement commits, and generation validation after erase/reuse.
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

Explicit in-place Refresh remains available as a maintenance operation. Automatic Refresh uses copy/remap/erase semantics. Wear leveling, temperature-aware retention, detailed voltage-threshold distributions, HBM overlap, and packet-level UCIe remain outside v0.2.6.

The reliability model is command-level rather than bit-level: each read samples a raw error count from a Poisson distribution, ECC corrects counts within `ecc_correctable_bits`, and each retry multiplies BER by `retry_ber_multiplier`. A failed program consumes its sequential-program position but does not replace the previous L2P mapping.

## Source layout

The implementation is split by responsibility so that NAND behavior and experiment plumbing can evolve independently:

```text
src/config.cpp      YAML subset and unit parsing
src/trace.cpp       streaming IRequestSource and CSV trace input
src/event_queue.cpp deterministic event queue
src/mapper.cpp      mapping-policy selection and simulator adapter
src/stripe_mapping.cpp Host-managed stripe allocation, implicit P2L, lifecycle
src/host_gc.cpp    Host-side watermarks, victim selection, and GC admission
src/copy_engine.cpp pipelined Recovery/Host-GC copy and bounded buffering
src/link.cpp        HostRouter, full-duplex HostInterface, and DataFabric
src/reliability.cpp seeded program-failure, raw-error, ECC, and retry model
src/resource_tracker.cpp online topology-bounded occupancy/overlap statistics
src/resolved_config.cpp complete effective configuration serialization
src/scheduler.cpp   queues, readiness checks, batching, cache, suspend/resume
src/events.cpp      command completion and NAND state transitions
src/stats.cpp       fixed-memory latency, sampled queues, and CSV metrics
src/simulator.cpp   construction, request splitting, resources, and event loop
src/internal.h      private parsing helpers shared by config/trace
```

`include/hbfsim/core.h` remains the public model interface; there is no longer a monolithic `core.cpp`.

## Build and run

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/hbfsim configs/hbf_baseline.yaml traces/example.csv
```

The trace columns are `timestamp_ns,op,address,size,stream`; timestamps must be nondecreasing. Sizes may use `KiB`, `MiB`, `GiB`, or `TiB`; addresses accept decimal or `0x` hexadecimal notation. Results are emitted under `statistics.output_dir`. Each run writes `summary.csv`, latency/source breakdowns, online resource utilization, sampled `queue_depth.csv`, and a complete `resolved_config.yaml`.

The baseline uses `initialization.mode: image_loaded` with strict validation. Metadata is materialized lazily for pages referenced by reads, so a large read-only image does not require one in-memory object per NAND page.

Extended behavior is configured under `nand.timing`, `nand.features`, and `nand.reliability`; `configs/hbf_small_test.yaml` enables the timing and command features with zero failure rates. `summary.csv` reports failed requests, program failures, ECC-corrected reads, uncorrectable reads, retry count, and successful-byte goodput. Plane utilization now measures NAND-array occupancy; overlapped cache data transfer is not double-counted as array activity.

Multi-plane grouping requires the same operation, die, block index, and page index on different planes. `multi_plane_setup_ns` is the collection window. Cache program provides one staged page per plane, allowing its Data In transfer to overlap the active program operation.

The supplied baseline is a FLINT-like research configuration, not a claim about a mandatory HBF standard timing or topology.

The v0.2 Host-managed mapping contract and implementation status are documented in [`docs/HOST_MANAGED_STRIPE_MAPPING.md`](docs/HOST_MANAGED_STRIPE_MAPPING.md). Automatic Refresh details are in [`docs/V0.2.4_AUTOMATIC_REFRESH.md`](docs/V0.2.4_AUTOMATIC_REFRESH.md).

Run the supplied four-point reproducible sweep with `python3 tools/experiment_runner.py experiments/example_sweep.json`. See [`docs/V0.2.6_SCALABLE_STATS_EXPERIMENTS.md`](docs/V0.2.6_SCALABLE_STATS_EXPERIMENTS.md) for the manifest schema and output contract.

For an automatic reclamation example, run `./build/hbfsim configs/hbf_host_gc.yaml traces/host_gc_cycle.csv`. The trace fills Host-visible capacity, explicitly trims one stripe, lets `HostGcManager` reclaim it, and then rewrites and reads the range.

For a write/erase/rewrite lifecycle smoke test, use `traces/read_write_erase.csv` with a small configuration. In `host_managed` mode, Erase expands to every lane in the physical stripe; only after all lane blocks finish erasing can that physical stripe be allocated with a newer generation. The automated erase test also enables strict validation and reads back the rewritten page.
