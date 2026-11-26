# System Architecture

## 1. Module Overview

The simulator is organized under `include/memsim/` and `src/` with clean separation between configuration, state models, orchestration, and reporting:

| Module | Files | Responsibility |
|---|---|---|
| Core Types | `types.hpp` | `addr_t`, `cycle_t`, `byte_count_t`, policy enums & parsers |
| Request | `request.hpp` | `MemoryRequest` with block alignment & latency helpers |
| Cache | `cache_config.hpp`, `cache.hpp`, `cache.cpp` | `CacheConfig`, `CacheLine`, `CacheSet`, `Cache` |
| Hierarchy | `cache_hierarchy.hpp`, `cache_hierarchy.cpp` | Multi-level `CacheHierarchy` (L1 -> L2 -> L3 -> DRAM) |
| DRAM | `dram_config.hpp`, `dram.hpp`, `dram.cpp` | `DramConfig`, `AddressMapping`, `DramBank`, `DramModel` |
| Controller | `memory_controller.hpp`, `memory_controller.cpp` | `MemoryController` with FCFS and FR-FCFS |
| Workloads | `trace_loader.*`, `workload_generator.*` | Trace file parser and 10 synthetic generators |
| Statistics | `statistics.hpp`, `system_statistics.hpp` | Per-level `CacheStatistics`, `DramStatistics`, `SystemStatistics`, AMAT |
| Orchestration | `simulator.hpp`, `simulator.cpp`, `main.cpp` | `Simulator` runner and CLI frontend |

## 2. Cache Address Decomposition

All caches require power-of-two `line_size_bytes` and `num_sets`:

```text
 63                              offset_bits + index_bits       offset_bits         0
┌────────────────────────────────────────┬──────────────────────┬────────────────────┐
│                  Tag                   │      Set Index       │    Block Offset    │
└────────────────────────────────────────┴──────────────────────┴────────────────────┘
```

- `offset_bits = log2(line_size_bytes)`
- `index_bits = log2(num_sets)` (0 for fully associative where `num_sets == 1`)
- `set_index = (address >> offset_bits) & (num_sets - 1)`
- `tag = address >> (offset_bits + index_bits)`

On eviction, the victim's block-aligned address is reconstructed as:
`reconstructed = (tag << (offset_bits + index_bits)) | (set_index << offset_bits)`

## 3. DRAM Physical Address Mapping

`AddressMapping::map` implements low-order row-interleaved mapping:

```text
 MSB                                                                              LSB
┌──────────┬──────────┬──────────┬───────────┬────────────┬──────────────────────────┐
│   Row    │   Rank   │   Bank   │  Channel  │   Column   │  Byte Offset in Column   │
└──────────┴──────────┴──────────┴───────────┴────────────┴──────────────────────────┘
```

- `bytes_per_col = row_size_bytes / num_cols`
- Placing `channel` and `bank` bits just above `column` distributes consecutive cache-line accesses across channels and banks for bank-level parallelism, while keeping accesses within `row_size_bytes` in the same open row.

## 4. DRAM Bank State Machine

Each `DramBank` tracks `state_` (`Idle`, `Active`, `Precharging`, `Refreshing`), `open_row_`, `available_at_`, and `last_activate_time_`:

- **Row Hit** (`Active` and `open_row_ == row`): latency = `tCAS` (+ `tWR` for writes); next command after `tCCD`.
- **Row Miss** (`Idle` or `Precharging`): issues `ACTIVATE` then `CAS`; latency = `tRCD + tCAS` (+ `tWR` for writes).
- **Row Conflict** (`Active` and `open_row_ != row`): enforces `tRAS` since `last_activate_time_`, issues `PRECHARGE` (`tRP`), `ACTIVATE` (`tRCD`), and `CAS` (`tCAS`) (+ `tWR` for writes).

`DramModel` enforces rank-level `tRRD` (minimum gap between successive `ACTIVATE`s on a rank) and `tFAW` (at most 4 `ACTIVATE`s in any `tFAW`-cycle window).
