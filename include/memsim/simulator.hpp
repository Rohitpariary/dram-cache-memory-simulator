#pragma once

/// @file simulator.hpp
/// @brief Top-level simulator that orchestrates the memory hierarchy.
///
/// The Simulator ties together the cache hierarchy (or memory controller),
/// trace loader, workload generator, and statistics collection into a
/// single run-able entity.

#include <cstdint>
#include <string>
#include <vector>

#include "memsim/cache_config.hpp"
#include "memsim/cache_hierarchy.hpp"
#include "memsim/dram_config.hpp"
#include "memsim/request.hpp"
#include "memsim/system_statistics.hpp"
#include "memsim/types.hpp"

namespace memsim {

/// @brief Complete simulation configuration.
struct SimulatorConfig {
    std::vector<CacheConfig> cache_configs;
    DramConfig dram_config;
    SchedulingPolicy dram_scheduling_policy{SchedulingPolicy::FCFS};

    /// Path to a trace file (empty = use synthetic workload).
    std::string trace_file;

    /// Synthetic workload type (empty = use trace file).
    std::string workload_type;

    /// Number of requests for synthetic workloads.
    uint64_t request_count{10000};

    /// Address space size for synthetic workloads.
    uint64_t address_space_size{1 << 20};

    /// Stride for strided workloads.
    uint64_t stride{64};

    /// Read/write ratio for synthetic workloads.
    double read_ratio{0.8};

    /// Random seed for reproducibility.
    uint64_t random_seed{42};

    /// Working set size for working_set workloads.
    uint64_t working_set_size{4096};

    /// Output CSV file path (empty = no CSV output).
    std::string csv_output;

    /// Verbose logging.
    bool verbose{false};
};

/// @brief Top-level simulator orchestrating the full memory hierarchy.
class Simulator {
public:
    /// @brief Construct the simulator from a configuration.
    explicit Simulator(const SimulatorConfig& config);

    /// @brief Run the simulation on a set of requests.
    /// @param requests  Memory requests to process.
    /// @return System statistics after the run.
    SystemStatistics run(const std::vector<MemoryRequest>& requests);

    /// @brief Run the simulation using the configured trace or workload.
    /// @return System statistics after the run.
    SystemStatistics run();

    /// @brief Get the cache hierarchy (read-only).
    [[nodiscard]] const CacheHierarchy& get_hierarchy() const;

    /// @brief Reset the simulator state.
    void reset();

private:
    SimulatorConfig config_;
    CacheHierarchy hierarchy_;
};

} // namespace memsim
