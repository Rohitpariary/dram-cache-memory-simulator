# Validation Strategy & Test Suite

## 1. Unit & Integration Test Suites

The test suite uses **GoogleTest** (`v1.14.0`) integrated with **CTest** across four targets:

1. **`tests/cache_tests.cpp`**
   - Compulsory misses, repeated hits, and same-block different-offset hits.
   - Address decomposition (`extract_offset`, `extract_set_index`, `extract_tag`).
   - Direct-mapped conflict misses, N-way set-associative capacity, fully associative behavior.
   - Targeted victim-selection tests for **LRU** and **FIFO**.
   - **Write-Back**, **Write-Through**, **Write-Allocate**, and **No-Write-Allocate** semantics, including dirty-bit tracking and dirty evictions.
   - `CacheConfig` validation (zero associativity, non-power-of-two line size, inconsistent capacity).

2. **`tests/dram_tests.cpp`**
   - `AddressMapping` bit-field decomposition across channels and banks.
   - `DramBank` state transitions: `RowMiss` (`tRCD + tCAS`), `RowHit` (`tCAS`), `RowConflict` (`tRP + tRCD + tCAS`), and write latency (`+ tWR`).
   - Multi-bank independence, `reset()`, `is_row_open()`, and `DramConfig` validation (including DDR4/DDR5 factories).

3. **`tests/hierarchy_tests.cpp`**
   - L1 hit, L1 miss -> DRAM, DRAM-only (`0` cache levels), and 3-level (`L1+L2+L3`) hierarchies.
   - Dirty-eviction and write-through propagation from L1 to L2.
   - Independent per-level statistics and `reset()`.

4. **`tests/integration_tests.cpp`**
   - Trace parser (`R`/`W`, hex/decimal, comments, blank lines, optional size, malformed input errors).
   - All 10 synthetic workload generators, seed reproducibility, and read/write ratios.
   - Full `Simulator::run()`, `SystemStatistics::compute_amat()`, CSV export, and `MemoryController` (`FCFS` / `FR-FCFS`).

## 2. Structural Invariants Checked

Across randomized and deterministic tests, the suite verifies:
- `hits + misses == total_accesses` at every cache level
- `read_accesses + write_accesses == total_accesses`
- `read_hits + write_hits == hits` and `read_misses + write_misses == misses`
- `row_hits + row_misses + row_conflicts == dram_total_accesses`
- `total_reads + total_writes == dram_total_accesses`
- Zero-access workloads never divide by zero (rates return `0.0`).
