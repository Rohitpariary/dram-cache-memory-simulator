#pragma once

/// @file cache_hierarchy.hpp
/// @brief Multi-level cache hierarchy (L1 → L2 → L3 → DRAM).
///
/// The CacheHierarchy owns one or more Cache objects arranged in a serial
/// hierarchy.  An access enters at L1; on a miss it falls through to L2,
/// then L3, then DRAM.  Data-movement (fills, writebacks, dirty evictions)
/// propagates according to each level's write and allocation policies.
///
/// **Inclusion / Exclusion**: This simulator implements a *non-inclusive,
/// non-exclusive* hierarchy (NINE).  A line can exist at any subset of
/// levels.  Dirty evictions propagate downward but there is no back-
/// invalidation on fills.

#include <cstdint>
#include <memory>
#include <vector>

#include "memsim/cache.hpp"
#include "memsim/cache_config.hpp"
#include "memsim/dram.hpp"
#include "memsim/memory_controller.hpp"
#include "memsim/request.hpp"
#include "memsim/system_statistics.hpp"
#include "memsim/types.hpp"

namespace memsim {

/// Result of a full hierarchy access (all levels + DRAM).
struct HierarchyAccessResult {
    /// Total latency (cycles) accumulated across all levels.
    cycle_t total_latency{0};

    /// Which cache level served the data (1 = L1, 2 = L2, …).
    /// 0 means the access was served by DRAM.
    uint32_t served_by_level{0};

    /// True if DRAM was accessed.
    bool dram_accessed{false};
};

/// @brief Configurable multi-level cache hierarchy with DRAM backend.
class CacheHierarchy {
public:
    /// @brief Construct a hierarchy from per-level cache configurations
    ///        and a DRAM configuration.
    ///
    /// @param cache_configs  Ordered list of cache-level configs (index 0 = L1).
    ///                       May be empty (all accesses go to DRAM).
    /// @param dram_config    DRAM timing / organisation.
    CacheHierarchy(const std::vector<CacheConfig>& cache_configs,
                   const DramConfig& dram_config,
                   SchedulingPolicy dram_policy = SchedulingPolicy::FCFS);

    /// @brief Process a memory request through the full hierarchy.
    ///
    /// Flow:
    ///   1. Try L1 — if hit, done.
    ///   2. Try L2 — if hit, fill L1, done.
    ///   …
    ///   N. On last-level miss, access DRAM, fill back up through all
    ///      missed levels.
    ///
    /// Dirty evictions are written to the next level at each stage.
    ///
    /// @param req  The memory request to process.
    /// @return     HierarchyAccessResult describing the outcome.
    HierarchyAccessResult access(const MemoryRequest& req);

    // ---- Accessors ----

    /// Number of cache levels.
    [[nodiscard]] uint32_t num_levels() const;

    /// Read-only access to a specific cache level (0 = L1).
    [[nodiscard]] const Cache& get_cache(uint32_t level) const;

    /// Read-only access to the DRAM model.
    [[nodiscard]] const DramModel& get_dram() const;

    /// Read-only access to the memory controller.
    [[nodiscard]] const MemoryController& get_controller() const;

    /// Collect statistics from all levels and DRAM into a SystemStatistics.
    [[nodiscard]] SystemStatistics collect_statistics() const;

    /// Reset all caches and DRAM.
    void reset();

    /// Get the current simulated cycle.
    [[nodiscard]] cycle_t get_current_cycle() const { return current_cycle_; }

private:
    /// Issue a fill (read-miss allocation) into the given cache level.
    void fill_cache_level(uint32_t level, addr_t address, cycle_t cycle);

    /// Handle a dirty eviction by writing back to the next level.
    void handle_dirty_eviction(uint32_t level, addr_t evicted_address,
                                cycle_t cycle);

    std::vector<Cache> caches_;
    MemoryController controller_;
    cycle_t current_cycle_{0};
};

} // namespace memsim
