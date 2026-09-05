# HBFSim v0.1

HBFSim is a trace-driven, single-threaded discrete-event simulator for HBF-style NAND stacks. It models performance-relevant resources rather than packet- or bit-level hardware details.

## Included in v0.1

- Host commands and write/read payloads use separate staged events; requests are split into page-sized subrequests.
- Stack → Die → Plane topology, with per-die and per-stack array-concurrency caps.
- Per-stack, multi-channel Host Interface resources and per-stack, multi-port Base-Die fabrics with aggregate bandwidth limits.
- NAND read, program, erase, and refresh operation types; read/program/erase timing is configurable.
- `linear`, `fine_stripe`, `burst_stripe`, and sparse-L2P `host_managed` mapping policies.
- Read-priority scheduling with non-read aging, round-robin plane dispatch, separate user/erase/refresh queues, sequential-program validation, deterministic event ordering, CSV trace replay, and per-operation CSV statistics.
- NAND block lifecycle: `FREE → OPEN → CLOSED → ERASING → FREE`, program-frontier and valid-page tracking, erase-count updates, and optional strict read validation.
- Page lifecycle with stable `ERASED / VALID / INVALID / FAILED` states and sparse `READING / PROGRAMMING` transient states. Stable states use lazy bitmaps so untouched blocks consume no per-page storage.
- Independent Block, Plane, and Die readiness timestamps, plus configurable `tCCS`, `tADL`, `tWHR`, suspend, resume, multi-plane setup, cache-program setup, and read-retry delays.
- Program/erase suspend and resume for queued reads, compatible same-die multi-plane batches, and one-page cache-program overlap per plane.
- Deterministic seeded program-failure injection and Poisson raw-bit-error sampling, with configurable ECC strength, retry count, retry latency, and retry BER reduction.
- Host-managed writes allocate and commit their physical page at program time; overwrites invalidate the previous page only after successful program completion.
- Warm-up requests are executed but excluded from latency, bandwidth, and utilization measurements. Measurement time runs from the first measured arrival to the last measured completion.

Refresh is accepted as an operation and has a timing path. Automatic refresh policy, GC/wear-leveling policy, HBM overlap, thermal behavior, detailed voltage-threshold distributions, and packet-level UCIe remain outside v0.1.

The reliability model is command-level rather than bit-level: each read samples a raw error count from a Poisson distribution, ECC corrects counts within `ecc_correctable_bits`, and each retry multiplies BER by `retry_ber_multiplier`. A failed program consumes its sequential-program position but does not replace the previous L2P mapping.

## Source layout

The implementation is split by responsibility so that NAND behavior and experiment plumbing can evolve independently:

```text
src/config.cpp      YAML subset and unit parsing
src/trace.cpp       CSV trace replay input
src/mapper.cpp      logical-to-physical placement and host L2P
src/link.cpp        pipelined host-link and multi-port fabric resources
src/reliability.cpp seeded program-failure, raw-error, ECC, and retry model
src/scheduler.cpp   queues, readiness checks, batching, cache, suspend/resume
src/events.cpp      command completion and NAND state transitions
src/stats.cpp       aggregate and per-operation metrics
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

The trace columns are `timestamp_ns,op,address,size,stream`. Sizes may use `KiB`, `MiB`, `GiB`, or `TiB`; addresses accept decimal or `0x` hexadecimal notation. Results are emitted to the configuration's `statistics.output_dir` as `summary.csv` and `plane_utilization.csv`. The summary includes the measured time window and per-operation counts, bytes, bandwidth, mean latency, and p99 latency; the plane file includes idle planes as zero-utilization rows.

Extended behavior is configured under `nand.timing`, `nand.features`, and `nand.reliability`; `configs/hbf_small_test.yaml` enables the timing and command features with zero failure rates. `summary.csv` reports failed requests, program failures, ECC-corrected reads, uncorrectable reads, retry count, and successful-byte goodput. Plane utilization now measures NAND-array occupancy; overlapped cache data transfer is not double-counted as array activity.

Multi-plane grouping requires the same operation, die, block index, and page index on different planes. `multi_plane_setup_ns` is the collection window. Cache program provides one staged page per plane, allowing its Data In transfer to overlap the active program operation.

The supplied baseline is a FLINT-like research configuration, not a claim about a mandatory HBF standard timing or topology.

For a write/erase/rewrite lifecycle smoke test, use `traces/read_write_erase.csv` with a small configuration. In `host_managed` mode, a completed erase releases old L2P entries and resets the frontier when the erased block is the current allocation block. The automated erase test also enables strict validation and reads back the rewritten page.
