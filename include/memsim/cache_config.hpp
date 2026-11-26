#pragma once

/// @file cache_config.hpp
/// @brief Configuration structure for a cache level in the memory hierarchy.
///
/// A cache is characterized by its capacity, line (block) size, associativity,
/// and various policies (replacement, write-handling, write-allocation).
/// The CacheConfig struct encapsulates all of these parameters and provides
/// validation to ensure architectural consistency:
///
///     capacity = num_sets × associativity × line_size
///
/// This identity must hold for a valid cache geometry. The line size must
/// also be a power of two so that address decomposition can use simple
/// bit-shift / mask operations rather than expensive division.

#include <cstdint>
#include <string>
#include <stdexcept>
#include <sstream>

#include "memsim/types.hpp"

namespace memsim {

// ---------------------------------------------------------------------------
// CacheConfig — fully describes one cache level
// ---------------------------------------------------------------------------

struct CacheConfig {
    /// Total capacity of the cache in bytes (e.g. 32768 for a 32 KiB L1).
    uint32_t capacity_bytes{16384};

    /// Size of a single cache line / block in bytes.  Must be a power of 2.
    uint32_t line_size_bytes{64};

    /// Set associativity.  1 = direct-mapped, N = N-way set-associative,
    /// capacity/(line_size) = fully-associative.
    uint32_t associativity{4};

    /// Number of sets.  Must satisfy:
    ///     num_sets = capacity_bytes / (associativity * line_size_bytes)
    uint32_t num_sets{0};

    /// Latency (in clock cycles) for a cache hit.
    cycle_t hit_latency_cycles{1};

    /// Block replacement strategy used when eviction is necessary.
    ReplacementPolicy replacement_policy{ReplacementPolicy::LRU};

    /// Determines whether dirty lines are written to the next level
    /// immediately (WriteThrough) or only on eviction (WriteBack).
    WritePolicy write_policy{WritePolicy::WriteBack};

    /// Controls whether a write-miss brings the target line into the cache.
    WriteAllocatePolicy write_allocate_policy{WriteAllocatePolicy::WriteAllocate};

    /// Hardware prefetcher policy for this cache level.
    PrefetchPolicy prefetch_policy{PrefetchPolicy::None};

    /// Number of cache lines to prefetch per trigger (1–4 typical).
    uint32_t prefetch_degree{1};

    /// Human-readable name for this cache level (e.g. "L1", "L2").
    std::string name{"Cache"};

    // -----------------------------------------------------------------
    // Validation
    // -----------------------------------------------------------------

    /// Validate all configuration parameters.
    ///
    /// Checks:
    ///   1. All size / count values are strictly positive.
    ///   2. line_size_bytes is a power of two.
    ///   3. The fundamental cache identity holds:
    ///          capacity = num_sets × associativity × line_size
    ///
    /// If num_sets is zero on entry it is *computed* from the other three
    /// values (capacity, associativity, line_size) before the check.
    ///
    /// @throws std::invalid_argument on any validation failure.
    void validate() {
        // --- positivity checks ---
        if (capacity_bytes == 0) {
            throw std::invalid_argument(
                name + ": capacity_bytes must be > 0");
        }
        if (line_size_bytes == 0) {
            throw std::invalid_argument(
                name + ": line_size_bytes must be > 0");
        }
        if (associativity == 0) {
            throw std::invalid_argument(
                name + ": associativity must be > 0");
        }
        if (hit_latency_cycles == 0) {
            throw std::invalid_argument(
                name + ": hit_latency_cycles must be > 0");
        }

        // --- power-of-two check for line size ---
        // A positive integer n is a power of two iff (n & (n-1)) == 0.
        if ((line_size_bytes & (line_size_bytes - 1)) != 0) {
            throw std::invalid_argument(
                name + ": line_size_bytes (" +
                std::to_string(line_size_bytes) +
                ") must be a power of 2");
        }

        // --- derive num_sets if the caller left it at zero ---
        if (num_sets == 0) {
            const uint32_t denom = associativity * line_size_bytes;
            if (capacity_bytes % denom != 0) {
                std::ostringstream oss;
                oss << name
                    << ": capacity_bytes (" << capacity_bytes
                    << ") is not evenly divisible by associativity ("
                    << associativity << ") * line_size_bytes ("
                    << line_size_bytes << ")";
                throw std::invalid_argument(oss.str());
            }
            num_sets = capacity_bytes / denom;
        }

        // --- fundamental identity ---
        const uint64_t expected =
            static_cast<uint64_t>(num_sets) *
            static_cast<uint64_t>(associativity) *
            static_cast<uint64_t>(line_size_bytes);

        if (expected != capacity_bytes) {
            std::ostringstream oss;
            oss << name
                << ": capacity mismatch — num_sets (" << num_sets
                << ") * associativity (" << associativity
                << ") * line_size_bytes (" << line_size_bytes
                << ") = " << expected
                << ", but capacity_bytes = " << capacity_bytes;
            throw std::invalid_argument(oss.str());
        }

        // --- num_sets should also be a power of two for fast indexing ---
        if ((num_sets & (num_sets - 1)) != 0) {
            throw std::invalid_argument(
                name + ": num_sets (" + std::to_string(num_sets) +
                ") must be a power of 2 for efficient address decoding");
        }
    }

    // -----------------------------------------------------------------
    // Convenience factory methods
    // -----------------------------------------------------------------

    /// Create a typical L1 cache configuration.
    ///
    /// 32 KiB, 64-byte lines, 8-way set-associative, 1-cycle hit latency,
    /// LRU replacement, write-back with write-allocate.
    static CacheConfig make_l1_default() {
        CacheConfig cfg;
        cfg.name               = "L1";
        cfg.capacity_bytes     = 32768;       // 32 KiB
        cfg.line_size_bytes    = 64;
        cfg.associativity      = 8;
        cfg.num_sets           = 0;           // let validate() compute
        cfg.hit_latency_cycles = 1;
        cfg.replacement_policy   = ReplacementPolicy::LRU;
        cfg.write_policy         = WritePolicy::WriteBack;
        cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
        cfg.validate();
        return cfg;
    }

    /// Create a typical L2 cache configuration.
    ///
    /// 256 KiB, 64-byte lines, 8-way, 10-cycle hit latency.
    static CacheConfig make_l2_default() {
        CacheConfig cfg;
        cfg.name               = "L2";
        cfg.capacity_bytes     = 262144;      // 256 KiB
        cfg.line_size_bytes    = 64;
        cfg.associativity      = 8;
        cfg.num_sets           = 0;
        cfg.hit_latency_cycles = 10;
        cfg.replacement_policy   = ReplacementPolicy::LRU;
        cfg.write_policy         = WritePolicy::WriteBack;
        cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
        cfg.validate();
        return cfg;
    }

    /// Create a typical L3 cache configuration.
    ///
    /// 8 MiB, 64-byte lines, 16-way, 40-cycle hit latency.
    static CacheConfig make_l3_default() {
        CacheConfig cfg;
        cfg.name               = "L3";
        cfg.capacity_bytes     = 8388608;     // 8 MiB
        cfg.line_size_bytes    = 64;
        cfg.associativity      = 16;
        cfg.num_sets           = 0;
        cfg.hit_latency_cycles = 40;
        cfg.replacement_policy   = ReplacementPolicy::LRU;
        cfg.write_policy         = WritePolicy::WriteBack;
        cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
        cfg.validate();
        return cfg;
    }
};

} // namespace memsim
