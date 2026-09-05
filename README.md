# HBFSim v0.1.1

HBFSim is a trace-driven, single-threaded discrete-event simulator for HBF-style NAND stacks. It models performance-relevant resources rather than packet- or bit-level hardware details.

## Included in v0.1

- Host commands and write/read payloads use separate staged events; requests are split into page-sized subrequests.
- Stack → Die → Plane topology, with per-die and per-stack array-concurrency caps.
- Host routing and NAND placement are independent: logical-address striping selects a Host Channel, while media placement selects DataPort/Die/Plane.
- Host command, H2D, and D2H resources support configurable full-duplex operation; per-stack DataFabric models both explicit per-port bandwidth and aggregate bandwidth.
- NAND read, program, erase, and refresh operation types; read/program/erase timing is configurable.
- `linear`, `fine_stripe`, `burst_stripe`, and sparse-L2P `host_managed` mapping policies.
- Read-priority scheduling with non-read aging, round-robin plane dispatch, separate user/erase/refresh queues, sequential-program validation, deterministic event ordering, CSV trace replay, and per-operation CSV statistics.
- NAND block lifecycle: `FREE → OPEN → CLOSED → ERASING → FREE`, program-frontier and valid-page tracking, erase-count updates, and optional strict read validation.
- Page lifecycle with stable `ERASED / VALID / INVALID / FAILED` states and sparse `READING / PROGRAMMING` transient states. Stable states use lazy bitmaps so untouched blocks consume no per-page storage.
- Independent Block, Plane, and Die readiness timestamps, plus configurable `tCCS`, `tADL`, `tWHR`, suspend, resume, multi-plane setup, cache-program setup, and read-retry delays.
- Program/erase suspend and resume for queued reads, compatible same-die multi-plane batches, and one-page cache-program overlap per plane.
- Deterministic seeded program-failure injection and Poisson raw-bit-error sampling, with configurable ECC strength, retry count, retry latency, and retry BER reduction.
- Host-managed writes allocate and commit their physical page at program time; overwrites invalidate the previous page only after successful program completion.
- CSV traces stream one record at a time. The production path enforces `INITIALIZE → WARMUP → MEASURE → DRAIN`; warm-up is fully drained before measured requests are admitted.
- `empty`, `image_loaded`, and `preconditioned` initialization modes allow strict validation for read-only traces without allocating metadata for the full device.
- Fixed-memory latency histograms report p50/p95/p99/p99.9. Additional CSVs expose transaction latency breakdown, queue depth, Stack array/fabric overlap, and Host Channel/DataPort/Die utilization.

Refresh is accepted as an operation and has a timing path. Automatic refresh policy, GC/wear-leveling policy, HBM overlap, thermal behavior, detailed voltage-threshold distributions, and packet-level UCIe remain outside v0.1.

The reliability model is command-level rather than bit-level: each read samples a raw error count from a Poisson distribution, ECC corrects counts within `ecc_correctable_bits`, and each retry multiplies BER by `retry_ber_multiplier`. A failed program consumes its sequential-program position but does not replace the previous L2P mapping.

## Source layout

The implementation is split by responsibility so that NAND behavior and experiment plumbing can evolve independently:

```text
src/config.cpp      YAML subset and unit parsing
src/trace.cpp       streaming IRequestSource and CSV trace input
src/event_queue.cpp deterministic event queue
src/mapper.cpp      logical-to-physical placement and host L2P
src/link.cpp        HostRouter, full-duplex HostInterface, and DataFabric
src/reliability.cpp seeded program-failure, raw-error, ECC, and retry model
src/scheduler.cpp   queues, readiness checks, batching, cache, suspend/resume
src/events.cpp      command completion and NAND state transitions
src/stats.cpp       fixed-memory latency and resource-occupancy metrics
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

The trace columns are `timestamp_ns,op,address,size,stream`; timestamps must be nondecreasing. Sizes may use `KiB`, `MiB`, `GiB`, or `TiB`; addresses accept decimal or `0x` hexadecimal notation. Results are emitted under `statistics.output_dir`. Besides `summary.csv` and `plane_utilization.csv`, v0.1.1 writes `latency_breakdown.csv`, `resource_utilization.csv`, `queue_depth.csv`, `data_port_utilization.csv`, `die_utilization.csv`, and `host_channel_utilization.csv`.

The baseline uses `initialization.mode: image_loaded` with strict validation. Metadata is materialized lazily for pages referenced by reads, so a large read-only image does not require one in-memory object per NAND page.

Extended behavior is configured under `nand.timing`, `nand.features`, and `nand.reliability`; `configs/hbf_small_test.yaml` enables the timing and command features with zero failure rates. `summary.csv` reports failed requests, program failures, ECC-corrected reads, uncorrectable reads, retry count, and successful-byte goodput. Plane utilization now measures NAND-array occupancy; overlapped cache data transfer is not double-counted as array activity.

Multi-plane grouping requires the same operation, die, block index, and page index on different planes. `multi_plane_setup_ns` is the collection window. Cache program provides one staged page per plane, allowing its Data In transfer to overlap the active program operation.

The supplied baseline is a FLINT-like research configuration, not a claim about a mandatory HBF standard timing or topology.

For a write/erase/rewrite lifecycle smoke test, use `traces/read_write_erase.csv` with a small configuration. In `host_managed` mode, a completed erase releases old L2P entries and resets the frontier when the erased block is the current allocation block. The automated erase test also enables strict validation and reads back the rewritten page.
