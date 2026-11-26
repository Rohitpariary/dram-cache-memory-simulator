/// @file memory_controller.cpp
/// @brief Implementation of the memory controller with FCFS and FR-FCFS.
///
/// FR-FCFS queries actual bank row-buffer state via DramModel::is_row_open()
/// to prioritize requests that would hit an already-open row.

#include "memsim/memory_controller.hpp"

#include <algorithm>
#include <cassert>
#include <limits>

namespace memsim {

MemoryController::MemoryController(const DramConfig& dram_config,
                                   SchedulingPolicy policy,
                                   uint32_t max_queue_size)
    : dram_(dram_config)
    , policy_(policy)
    , max_queue_size_(max_queue_size)
{}

cycle_t MemoryController::submit(const MemoryRequest& req, cycle_t current_cycle) {
    // Map address to physical DRAM location.
    PhysicalAddress pa = addr_map_.map(req.address, dram_.get_config());

    // Create entry and add to queue.
    ControllerEntry entry;
    entry.request = req;
    entry.phys_addr = pa;
    entry.enqueue_time = current_cycle;

    queue_.push_back(entry);

    // Track max queue depth.
    if (queue_.size() > stats_.max_queue_depth) {
        stats_.max_queue_depth = queue_.size();
    }

    // Select which request to dispatch.
    size_t idx = select_next_request(current_cycle);

    // Dispatch selected request.
    auto& selected = queue_[idx];
    DramAccessResult result = dram_.access(
        selected.request.address,
        selected.request.type,
        current_cycle);

    selected.completed = true;
    selected.completion_time = result.completion_cycle;

    cycle_t latency = result.latency;

    // Track if FR-FCFS reordered (selected something other than the oldest).
    if (idx > 0 && policy_ == SchedulingPolicy::FR_FCFS) {
        ++stats_.reorderings;
    }

    // Remove completed entry.
    queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(idx));

    // Update stats.
    ++stats_.total_requests;
    if (req.type == AccessType::Read) {
        ++stats_.total_reads;
    } else {
        ++stats_.total_writes;
    }
    stats_.total_latency_cycles += latency;

    return latency;
}

void MemoryController::enqueue(const MemoryRequest& req, cycle_t current_cycle) {
    PhysicalAddress pa = addr_map_.map(req.address, dram_.get_config());

    ControllerEntry entry;
    entry.request = req;
    entry.phys_addr = pa;
    entry.enqueue_time = current_cycle;

    queue_.push_back(entry);
    if (queue_.size() > stats_.max_queue_depth) {
        stats_.max_queue_depth = queue_.size();
    }
}

void MemoryController::drain(cycle_t current_cycle) {
    while (!queue_.empty()) {
        size_t idx = select_next_request(current_cycle);
        auto& selected = queue_[idx];

        DramAccessResult result = dram_.access(
            selected.request.address,
            selected.request.type,
            current_cycle);

        if (idx > 0 && policy_ == SchedulingPolicy::FR_FCFS) {
            ++stats_.reorderings;
        }

        ++stats_.total_requests;
        if (selected.request.type == AccessType::Read) {
            ++stats_.total_reads;
        } else {
            ++stats_.total_writes;
        }
        stats_.total_latency_cycles += result.latency;

        current_cycle = result.completion_cycle;
        queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(idx));
    }
}

size_t MemoryController::select_next_request(cycle_t /*current_cycle*/) {
    if (queue_.empty()) return 0;

    switch (policy_) {
    case SchedulingPolicy::FCFS:
        // Simply return the oldest request (front of queue).
        return 0;

    case SchedulingPolicy::FR_FCFS: {
        // First-Ready, First-Come-First-Served:
        // 1. Prioritise requests that hit an open row buffer (row-buffer hits).
        // 2. Among row-buffer hits, pick the oldest.
        // 3. If no row-buffer hits, fall back to FCFS (oldest request).

        // Find the oldest request that would be a row-buffer hit
        // by querying actual bank state via DramModel::is_row_open().
        for (size_t i = 0; i < queue_.size(); ++i) {
            const auto& entry = queue_[i];
            const auto& pa = entry.phys_addr;

            if (dram_.is_row_open(pa.channel, pa.rank, pa.bank, pa.row)) {
                return i;  // This is a row-buffer hit — dispatch it first
            }
        }

        // No row-buffer hit found — fall back to FCFS (oldest request).
        return 0;
    }

    default:
        return 0;
    }
}

const ControllerStatistics& MemoryController::get_stats() const noexcept {
    return stats_;
}

const DramModel& MemoryController::get_dram() const noexcept {
    return dram_;
}

const DramStatistics& MemoryController::get_dram_stats() const noexcept {
    return dram_.get_stats();
}

SchedulingPolicy MemoryController::get_policy() const noexcept {
    return policy_;
}

void MemoryController::reset() {
    dram_.reset();
    queue_.clear();
    stats_.reset();
}

} // namespace memsim
