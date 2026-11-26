#pragma once

/// @file system_statistics.hpp
/// @brief Aggregated system-wide statistics combining cache and DRAM data.

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "memsim/dram.hpp"
#include "memsim/memory_controller.hpp"
#include "memsim/statistics.hpp"
#include "memsim/types.hpp"

namespace memsim {

/// Aggregates per-level cache statistics and DRAM statistics for the whole
/// simulated memory hierarchy.
struct SystemStatistics {
    /// One CacheStatistics per level (index 0 = L1, 1 = L2, …).
    std::vector<CacheStatistics> cache_level_stats;

    /// DRAM / main-memory statistics.
    DramStatistics dram_stats;

    /// Memory-controller statistics.
    ControllerStatistics controller_stats;

    /// Total cycles the simulator ran for.
    cycle_t total_simulated_cycles{0};

    /// Total number of CPU requests processed.
    uint64_t total_requests{0};

    // ---- AMAT computation -----------------------------------------------

    /// Compute the Average Memory Access Time (AMAT) using the classic
    /// recursive formula:
    ///
    ///   AMAT = hit_time_L1 + miss_rate_L1 × (hit_time_L2 + miss_rate_L2 × …)
    ///
    /// @param hit_times  Per-level hit times (cycles), one per cache level.
    /// @param memory_access_time  DRAM access time (cycles). If 0, uses
    ///                            avg latency from dram_stats.
    [[nodiscard]] double compute_amat(
            const std::vector<double>& hit_times,
            double memory_access_time = 0.0) const {

        if (cache_level_stats.empty() || hit_times.empty()) {
            return 0.0;
        }

        const double total_lat = static_cast<double>(
            dram_stats.total_read_latency + dram_stats.total_write_latency);
        const double total_acc = static_cast<double>(dram_stats.total_accesses);
        const double avg_dram = (total_acc > 0) ? (total_lat / total_acc) : 0.0;

        const double mem_time =
            (memory_access_time > 0.0) ? memory_access_time : avg_dram;

        const auto levels = static_cast<int>(
            std::min(cache_level_stats.size(), hit_times.size()));

        double penalty = mem_time;
        for (int i = levels - 1; i >= 0; --i) {
            const double mr = cache_level_stats[static_cast<size_t>(i)].miss_rate();
            penalty = hit_times[static_cast<size_t>(i)] + mr * penalty;
        }
        return penalty;
    }

    // ---- reporting ------------------------------------------------------

    void report(std::ostream& os) const {
        os << "================================================================\n"
           << "       Memory Hierarchy Simulation Report\n"
           << "================================================================\n\n";

        for (size_t i = 0; i < cache_level_stats.size(); ++i) {
            cache_level_stats[i].report(os, "L" + std::to_string(i + 1));
            os << '\n';
        }

        // DRAM report
        os << "=== DRAM Statistics ===\n"
           << "  Total accesses:      " << dram_stats.total_accesses  << '\n'
           << "    Read accesses:     " << dram_stats.total_reads     << '\n'
           << "    Write accesses:    " << dram_stats.total_writes    << '\n'
           << "  Row buffer hits:     " << dram_stats.row_hits        << '\n'
           << "  Row misses:          " << dram_stats.row_misses      << '\n'
           << "  Row conflicts:       " << dram_stats.row_conflicts   << '\n'
           << "  Row hit rate:        " << std::fixed << std::setprecision(4)
                                        << (dram_stats.row_hit_rate() * 100.0) << " %\n"
           << "  Avg read latency:    " << std::fixed << std::setprecision(2)
                                        << dram_stats.avg_read_latency() << " cycles\n"
           << "  Avg write latency:   " << std::fixed << std::setprecision(2)
                                        << dram_stats.avg_write_latency() << " cycles\n"
           << "  Total activations:   " << dram_stats.total_activations << '\n'
           << "  Total refreshes:     " << dram_stats.total_refreshes   << '\n'
           << "  Est. DRAM energy:    " << std::fixed << std::setprecision(2)
                                        << dram_stats.total_energy_nj << " nJ\n";
        os << '\n';

        os << "=== System Overview ===\n"
           << "  Total requests:         " << total_requests << '\n'
           << "  Total simulated cycles: " << total_simulated_cycles << '\n'
           << "  Controller requests:    " << controller_stats.total_requests << '\n'
           << "  Controller reorderings: " << controller_stats.reorderings << '\n';
    }

    /// Export all statistics as a multi-section CSV string.
    [[nodiscard]] std::string to_csv() const {
        std::ostringstream ss;

        for (size_t i = 0; i < cache_level_stats.size(); ++i) {
            ss << "# L" << (i + 1) << '\n'
               << CacheStatistics::to_csv_header() << '\n'
               << cache_level_stats[i].to_csv_row() << '\n';
        }

        ss << "# DRAM\n"
           << "total_accesses,total_reads,total_writes,"
              "row_hits,row_misses,row_conflicts,"
              "total_activations,total_read_latency,total_write_latency,"
              "avg_read_latency,avg_write_latency,row_hit_rate\n"
           << dram_stats.total_accesses << ','
           << dram_stats.total_reads << ','
           << dram_stats.total_writes << ','
           << dram_stats.row_hits << ','
           << dram_stats.row_misses << ','
           << dram_stats.row_conflicts << ','
           << dram_stats.total_activations << ','
           << dram_stats.total_read_latency << ','
           << dram_stats.total_write_latency << ','
           << std::fixed << std::setprecision(6)
           << dram_stats.avg_read_latency() << ','
           << dram_stats.avg_write_latency() << ','
           << dram_stats.row_hit_rate() << '\n';

        ss << "# System\n"
           << "total_requests,total_simulated_cycles\n"
           << total_requests << ',' << total_simulated_cycles << '\n';

        return ss.str();
    }

    void reset() {
        for (auto& cs : cache_level_stats) {
            cs.reset();
        }
        dram_stats.reset();
        controller_stats.reset();
        total_simulated_cycles = 0;
        total_requests = 0;
    }
};

} // namespace memsim
