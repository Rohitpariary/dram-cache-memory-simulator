#pragma once

/// @file statistics.hpp
/// @brief Performance-counter class for cache levels.
///
/// Every cache level updates its local CacheStatistics object on each
/// access.  Rates and averages return `double` and are guarded against
/// division by zero (returning 0.0 in degenerate cases).
///
/// All counters are `uint64_t` to avoid overflow in long-running simulations.

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "memsim/types.hpp"

namespace memsim {

// =========================================================================
// CacheStatistics
// =========================================================================

/// Accumulates hit / miss / eviction counters for a single cache level.
class CacheStatistics {
public:
    // ---- raw counters (public for direct inspection in tests) ------------

    uint64_t total_accesses{0};
    uint64_t read_accesses{0};
    uint64_t write_accesses{0};

    uint64_t hits{0};
    uint64_t misses{0};

    uint64_t read_hits{0};
    uint64_t read_misses{0};
    uint64_t write_hits{0};
    uint64_t write_misses{0};

    uint64_t evictions{0};
    uint64_t dirty_evictions{0};
    uint64_t writebacks{0};

    uint64_t compulsory_misses{0};
    uint64_t non_compulsory_misses{0};
    uint64_t prefetches_issued{0};

    byte_count_t bytes_transferred{0};

    // ---- derived metrics ------------------------------------------------

    [[nodiscard]] double hit_rate() const noexcept {
        if (total_accesses == 0) return 0.0;
        return static_cast<double>(hits) / static_cast<double>(total_accesses);
    }

    [[nodiscard]] double miss_rate() const noexcept {
        if (total_accesses == 0) return 0.0;
        return static_cast<double>(misses) / static_cast<double>(total_accesses);
    }

    // ---- recording helpers ----------------------------------------------

    void record_access(AccessType type) noexcept {
        ++total_accesses;
        if (type == AccessType::Read) {
            ++read_accesses;
        } else {
            ++write_accesses;
        }
    }

    void record_hit(AccessType type) noexcept {
        ++hits;
        if (type == AccessType::Read) {
            ++read_hits;
        } else {
            ++write_hits;
        }
    }

    void record_miss(AccessType type) noexcept {
        ++misses;
        if (type == AccessType::Read) {
            ++read_misses;
        } else {
            ++write_misses;
        }
    }

    void record_eviction(bool dirty) noexcept {
        ++evictions;
        if (dirty) {
            ++dirty_evictions;
            ++writebacks;
        }
    }

    void record_transfer(byte_count_t bytes) noexcept {
        bytes_transferred += bytes;
    }

    void reset() noexcept {
        total_accesses = read_accesses = write_accesses = 0;
        hits = misses = 0;
        read_hits = read_misses = write_hits = write_misses = 0;
        evictions = dirty_evictions = writebacks = 0;
        compulsory_misses = non_compulsory_misses = prefetches_issued = 0;
        bytes_transferred = 0;
    }

    // ---- reporting ------------------------------------------------------

    void report(std::ostream& os,
                const std::string& level_name = "Cache") const {
        os << "=== " << level_name << " Statistics ===\n"
           << "  Total accesses:    " << total_accesses  << '\n'
           << "    Read accesses:   " << read_accesses   << '\n'
           << "    Write accesses:  " << write_accesses  << '\n'
           << "  Hits:              " << hits             << '\n'
           << "    Read hits:       " << read_hits        << '\n'
           << "    Write hits:      " << write_hits       << '\n'
           << "  Misses:            " << misses           << '\n'
           << "    Compulsory:      " << compulsory_misses << '\n'
           << "    Conflict/Cap:    " << non_compulsory_misses << '\n'
           << "    Read misses:     " << read_misses      << '\n'
           << "    Write misses:    " << write_misses     << '\n'
           << "  Hit rate:          " << std::fixed << std::setprecision(4)
                                      << (hit_rate() * 100.0) << " %\n"
           << "  Miss rate:         " << std::fixed << std::setprecision(4)
                                      << (miss_rate() * 100.0) << " %\n"
           << "  Evictions:         " << evictions        << '\n'
           << "    Dirty evictions: " << dirty_evictions  << '\n'
           << "  Writebacks:        " << writebacks       << '\n'
           << "  Prefetches issued: " << prefetches_issued << '\n'
           << "  Bytes transferred: " << bytes_transferred << '\n';
    }

    [[nodiscard]] static std::string to_csv_header() {
        return "total_accesses,read_accesses,write_accesses,"
               "hits,misses,read_hits,read_misses,write_hits,write_misses,"
               "evictions,dirty_evictions,writebacks,bytes_transferred,"
               "hit_rate,miss_rate";
    }

    [[nodiscard]] std::string to_csv_row() const {
        std::ostringstream ss;
        ss << total_accesses  << ',' << read_accesses  << ',' << write_accesses  << ','
           << hits            << ',' << misses          << ','
           << read_hits       << ',' << read_misses     << ','
           << write_hits      << ',' << write_misses    << ','
           << evictions       << ',' << dirty_evictions << ','
           << writebacks      << ',' << bytes_transferred << ','
           << std::fixed << std::setprecision(6) << hit_rate() << ','
           << std::fixed << std::setprecision(6) << miss_rate();
        return ss.str();
    }
};

} // namespace memsim
