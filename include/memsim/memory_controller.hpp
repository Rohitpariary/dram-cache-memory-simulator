#pragma once

/// @file memory_controller.hpp
/// @brief Memory controller with FCFS and FR-FCFS scheduling policies.
///
/// The memory controller sits between the cache hierarchy and the DRAM
/// model.  It accepts read/write requests, queues them, and dispatches
/// them to the DRAM model according to the selected scheduling policy.
///
/// - **FCFS** (First-Come, First-Served): Requests are dispatched in
///   arrival order.  Simple and fair, but ignores row-buffer locality.
///
/// - **FR-FCFS** (First-Ready, First-Come-First-Served): Prioritises
///   requests that would hit an already-open row buffer.  Among equally
///   ready requests, the oldest is chosen.  This is the standard baseline
///   in academic DRAM scheduling research.

#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

#include "memsim/dram.hpp"
#include "memsim/request.hpp"
#include "memsim/types.hpp"

namespace memsim {

/// Statistics collected by the memory controller.
struct ControllerStatistics {
    uint64_t total_requests{0};
    uint64_t total_reads{0};
    uint64_t total_writes{0};
    uint64_t total_latency_cycles{0};
    uint64_t max_queue_depth{0};
    uint64_t reorderings{0};  ///< Number of times FR-FCFS reordered vs FCFS.

    [[nodiscard]] double avg_latency() const noexcept {
        if (total_requests == 0) return 0.0;
        return static_cast<double>(total_latency_cycles) /
               static_cast<double>(total_requests);
    }

    void reset() noexcept {
        total_requests = total_reads = total_writes = 0;
        total_latency_cycles = 0;
        max_queue_depth = 0;
        reorderings = 0;
    }
};

/// Entry in the memory controller's request queue.
struct ControllerEntry {
    MemoryRequest request;
    PhysicalAddress phys_addr;
    cycle_t enqueue_time{0};
    bool completed{false};
    cycle_t completion_time{0};
};

/// @brief Memory controller with configurable scheduling policy.
class MemoryController {
public:
    /// @param dram_config  DRAM configuration.
    /// @param policy       Scheduling policy to use.
    /// @param max_queue_size  Maximum pending requests (0 = unlimited).
    MemoryController(const DramConfig& dram_config,
                     SchedulingPolicy policy = SchedulingPolicy::FCFS,
                     uint32_t max_queue_size = 64);

    /// @brief Submit a memory request to the controller and dispatch immediately.
    /// @return Total latency (cycles) for this access.
    cycle_t submit(const MemoryRequest& req, cycle_t current_cycle);

    /// @brief Enqueue a memory request without dispatching immediately.
    ///        Use drain() to schedule and execute all queued requests.
    void enqueue(const MemoryRequest& req, cycle_t current_cycle);

    /// @brief Process all queued requests using the active scheduling policy.
    void drain(cycle_t current_cycle);

    // ---- Accessors ----

    [[nodiscard]] const ControllerStatistics& get_stats() const noexcept;
    [[nodiscard]] const DramModel& get_dram() const noexcept;
    [[nodiscard]] const DramStatistics& get_dram_stats() const noexcept;
    [[nodiscard]] SchedulingPolicy get_policy() const noexcept;

    void reset();

private:
    /// Select the next request to dispatch according to the policy.
    size_t select_next_request(cycle_t current_cycle);

    DramModel dram_;
    AddressMapping addr_map_;
    SchedulingPolicy policy_;
    uint32_t max_queue_size_;
    std::deque<ControllerEntry> queue_;
    ControllerStatistics stats_;
};

} // namespace memsim
