/// @file cache_hierarchy.cpp
/// @brief Implementation of the multi-level cache hierarchy.

#include "memsim/cache_hierarchy.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace memsim {

CacheHierarchy::CacheHierarchy(const std::vector<CacheConfig>& cache_configs,
                               const DramConfig& dram_config,
                               SchedulingPolicy dram_policy)
    : controller_(dram_config, dram_policy)
{
    caches_.reserve(cache_configs.size());
    for (const auto& cfg : cache_configs) {
        caches_.emplace_back(cfg);
    }
}

HierarchyAccessResult CacheHierarchy::access(const MemoryRequest& req) {
    HierarchyAccessResult hier_result;
    cycle_t accumulated_latency = 0;

    // Walk down the hierarchy: L1 → L2 → L3 → …
    for (uint32_t level = 0; level < static_cast<uint32_t>(caches_.size()); ++level) {
        Cache& cache = caches_[level];
        CacheResult cr = cache.access(req);

        accumulated_latency += cr.latency;

        // Handle dirty evictions — write back to the next level.
        if (cr.eviction && cr.dirty_eviction) {
            handle_dirty_eviction(level, cr.evicted_address, current_cycle_);
        }

        if (cr.hit) {
            // Data found at this level.
            hier_result.total_latency = accumulated_latency;
            hier_result.served_by_level = level + 1;
            hier_result.dram_accessed = false;

            // For write-through, propagate to next level.
            if (cr.write_through && level + 1 < static_cast<uint32_t>(caches_.size())) {
                MemoryRequest wb_req(req.address, AccessType::Write, req.size);
                caches_[level + 1].access(wb_req);
            } else if (cr.write_through) {
                // Write-through to DRAM via MemoryController
                MemoryRequest dram_wt(req.address, AccessType::Write, req.size, current_cycle_);
                controller_.submit(dram_wt, current_cycle_);
            }

            current_cycle_ += accumulated_latency;
            return hier_result;
        }

        // Miss — for write-through writes, propagate now
        if (cr.write_through && req.type == AccessType::Write) {
            // This was a no-write-allocate miss with write-through;
            // the write will be sent to the next level in the loop.
        }
    }

    // All cache levels missed — access DRAM via MemoryController.
    MemoryRequest dram_req(req.address, req.type, req.size,
                           current_cycle_ + accumulated_latency);
    cycle_t dram_latency = controller_.submit(
        dram_req, current_cycle_ + accumulated_latency);

    accumulated_latency += dram_latency;
    hier_result.total_latency = accumulated_latency;
    hier_result.served_by_level = 0;  // DRAM
    hier_result.dram_accessed = true;

    current_cycle_ += accumulated_latency;
    return hier_result;
}

uint32_t CacheHierarchy::num_levels() const {
    return static_cast<uint32_t>(caches_.size());
}

const Cache& CacheHierarchy::get_cache(uint32_t level) const {
    if (level >= caches_.size()) {
        throw std::out_of_range("Cache level out of range");
    }
    return caches_[level];
}

const DramModel& CacheHierarchy::get_dram() const {
    return controller_.get_dram();
}

const MemoryController& CacheHierarchy::get_controller() const {
    return controller_;
}

SystemStatistics CacheHierarchy::collect_statistics() const {
    SystemStatistics sys;
    sys.cache_level_stats.reserve(caches_.size());
    for (const auto& cache : caches_) {
        sys.cache_level_stats.push_back(cache.get_stats());
    }
    sys.dram_stats = controller_.get_dram_stats();
    sys.controller_stats = controller_.get_stats();
    sys.total_simulated_cycles = current_cycle_;
    return sys;
}

void CacheHierarchy::reset() {
    for (auto& cache : caches_) {
        cache.reset();
    }
    controller_.reset();
    current_cycle_ = 0;
}

void CacheHierarchy::fill_cache_level(uint32_t level, addr_t address,
                                       cycle_t /*cycle*/) {
    // Create a read request to bring the line into the cache.
    // This is an internal fill, not a CPU request.
    MemoryRequest fill_req(address, AccessType::Read);
    caches_[level].access(fill_req);
}

void CacheHierarchy::handle_dirty_eviction(uint32_t level,
                                            addr_t evicted_address,
                                            cycle_t cycle) {
    // Write the dirty line to the next level.
    if (level + 1 < static_cast<uint32_t>(caches_.size())) {
        MemoryRequest wb_req(evicted_address, AccessType::Write);
        caches_[level + 1].access(wb_req);
    } else {
        // Last cache level — write to DRAM via MemoryController.
        MemoryRequest wb_dram(evicted_address, AccessType::Write, 4, cycle);
        controller_.submit(wb_dram, cycle);
    }
}

} // namespace memsim
