#pragma once

/// @file workload_generator.hpp
/// @brief Synthetic workload generators for benchmarking the memory hierarchy.
///
/// Each generator produces a vector of MemoryRequest objects with
/// configurable parameters.  Fixed random seeds ensure reproducibility.
///
/// Workload patterns:
///   1. Sequential    — unit-stride through the address space.
///   2. Random        — uniform random addresses.
///   3. Strided       — fixed stride between accesses.
///   4. Hot-set       — repeated access to a small working set.
///   5. Working-set   — sweep through a configurable working-set size.
///   6. Read-heavy    — mostly reads with occasional writes.
///   7. Write-heavy   — mostly writes with occasional reads.
///   8. Mixed         — configurable read/write ratio.
///   9. Bank-conflict — addresses that map to the same DRAM bank.
///  10. Row-locality  — addresses that map to the same DRAM row.

#include <cstdint>
#include <string>
#include <vector>

#include "memsim/request.hpp"
#include "memsim/types.hpp"

namespace memsim {

/// @brief Configuration for workload generation.
struct WorkloadConfig {
    uint64_t request_count{10000};      ///< Number of requests to generate.
    uint64_t address_space_size{1 << 20}; ///< Address space in bytes (1 MB default).
    uint64_t stride{64};                ///< Stride for strided access patterns.
    double   read_ratio{0.8};           ///< Fraction of reads (0.0–1.0).
    uint64_t random_seed{42};           ///< Seed for reproducible random generation.
    uint64_t working_set_size{4096};    ///< Working set size in bytes.
    uint32_t access_size{4};            ///< Bytes per access.
    uint64_t hot_set_size{256};         ///< Size of the hot region in bytes.
    double   hot_set_probability{0.9};  ///< Probability of accessing the hot set.
    uint32_t num_banks{8};              ///< For bank-conflict workloads.
    uint32_t row_size{8192};            ///< For row-locality workloads.
};

/// @brief Generate a sequential access workload.
///
/// Accesses addresses 0, stride, 2*stride, ... wrapping around the
/// address space.  Good for testing spatial locality and cache prefetch.
std::vector<MemoryRequest> generate_sequential(const WorkloadConfig& cfg);

/// @brief Generate a random access workload.
///
/// Uniformly random addresses within the address space.
/// Tests worst-case cache behavior and DRAM row-buffer efficiency.
std::vector<MemoryRequest> generate_random(const WorkloadConfig& cfg);

/// @brief Generate a strided access workload.
///
/// Accesses at a fixed stride, demonstrating cache set conflicts
/// when stride is a multiple of cache size.
std::vector<MemoryRequest> generate_strided(const WorkloadConfig& cfg);

/// @brief Generate a hot-set workload.
///
/// Most accesses (hot_set_probability) go to a small region,
/// the rest are uniformly random.  Tests temporal locality.
std::vector<MemoryRequest> generate_hotset(const WorkloadConfig& cfg);

/// @brief Generate a working-set sweep workload.
///
/// Sequentially accesses through a region of working_set_size bytes,
/// then repeats.  Demonstrates the effect of working-set size vs cache size.
std::vector<MemoryRequest> generate_working_set(const WorkloadConfig& cfg);

/// @brief Generate a read-heavy workload (90% reads).
std::vector<MemoryRequest> generate_read_heavy(const WorkloadConfig& cfg);

/// @brief Generate a write-heavy workload (90% writes).
std::vector<MemoryRequest> generate_write_heavy(const WorkloadConfig& cfg);

/// @brief Generate a mixed read/write workload with configurable ratio.
std::vector<MemoryRequest> generate_mixed(const WorkloadConfig& cfg);

/// @brief Generate a bank-conflict-heavy workload.
///
/// Addresses are chosen to target the same DRAM bank repeatedly,
/// forcing row conflicts.
std::vector<MemoryRequest> generate_bank_conflict(const WorkloadConfig& cfg);

/// @brief Generate a row-locality workload.
///
/// Addresses are chosen to stay within the same DRAM row,
/// maximising row-buffer hit rate.
std::vector<MemoryRequest> generate_row_locality(const WorkloadConfig& cfg);

/// @brief Generate a workload by name.
/// @param name  One of: sequential, random, strided, hotset, working_set,
///              read_heavy, write_heavy, mixed, bank_conflict, row_locality.
/// @param cfg   Workload configuration.
/// @throws std::invalid_argument if name is not recognised.
std::vector<MemoryRequest> generate_workload(const std::string& name,
                                              const WorkloadConfig& cfg);

/// @brief Save requests to a trace file.
void save_trace(const std::vector<MemoryRequest>& requests,
                const std::string& filepath,
                const std::string& comment = "");

} // namespace memsim
