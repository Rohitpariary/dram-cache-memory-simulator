# Design Decisions & Simplifications

## 1. Non-Inclusive, Non-Exclusive (NINE) Cache Hierarchy
`CacheHierarchy` models a NINE hierarchy: a line may reside in L1, L2, L3, or any subset independently. On a cache miss, `Cache::access` allocates the block in that level (for reads, or writes under `WriteAllocate`). There is no back-invalidation on lower-level evictions. This keeps per-level replacement policies independent and easy to reason about.

## 2. Serialized Timing Model vs. Out-of-Order Concurrency
`CacheHierarchy::access()` processes one CPU request to completion before starting the next (`current_cycle_ += accumulated_latency`). While this makes latency accounting and AMAT comparison exact and deterministic, it does not model overlapping Miss Status Holding Registers (MSHRs), non-blocking caches, or DRAM bus bandwidth saturation under concurrent misses.

## 3. MemoryController Integration
`MemoryController` implements `FCFS` and `FR_FCFS` scheduling over `DramModel`, querying live bank state via `DramModel::is_row_open()`. `CacheHierarchy` routes all last-level misses, write-throughs, and dirty evictions through `MemoryController::submit()`. Because `CacheHierarchy` processes CPU requests serially, the controller queue holds one request at a time during full-hierarchy runs, while `MemoryController::submit()` / `drain()` can also be exercised directly on queued batches.

## 4. Power-of-Two Geometry Requirement
Both `CacheConfig` and `DramConfig` enforce power-of-two sizes (`line_size_bytes`, `num_sets`, `num_channels`, `num_ranks`, `num_banks`, `num_rows`, `num_cols`, `row_size_bytes`). This allows bit-shift/mask address decomposition (`O(1)`) without modulo division and matches standard hardware decoders.

## 5. DRAM Timing Simplifications
- Modeled on active path: `tRCD`, `tCAS`, `tRP`, `tRAS`, `tCCD`, `tRRD`, `tFAW`, `tWR`.
- Validated in `DramConfig` but not modeled as active bus stalls: `tWTR` (write-to-read turnaround) and `tRFC` (periodic DRAM refresh).
- `tRC` is validated (`tRC >= tRAS + tRP`).

## 6. AMAT vs. Simulated Latency
The classic recursive AMAT formula:
`AMAT = L1_Hit + L1_MissRate * (L2_Hit + L2_MissRate * DRAM_AvgLatency)`
assumes serial lookups and uniform miss penalties. In the presence of writebacks, dirty evictions, and variable DRAM row-hit/miss/conflict penalties, observed `total_simulated_cycles / total_requests` can differ slightly from recursive AMAT because dirty evictions generate extra lower-level traffic not captured in CPU read/write miss rates alone.
