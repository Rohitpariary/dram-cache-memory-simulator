#pragma once

/// @file types.hpp
/// @brief Core type aliases and enumerations for the DRAM & Cache Memory Simulator.
///
/// This header establishes the fundamental vocabulary types used throughout the
/// simulator.  Every subsystem—cache hierarchy, DRAM controller, statistics
/// engine—speaks in terms of these types so that width mismatches and implicit
/// conversions are caught at compile time rather than at debug time.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace memsim {

// ---------------------------------------------------------------------------
// Type aliases
// ---------------------------------------------------------------------------

/// A 64-bit physical (or virtual) memory address.
/// 64 bits is wide enough to model any realistic address space while keeping
/// alignment arithmetic simple (all bit-masks fit in a single register).
using addr_t = uint64_t;

/// Cycle counter — unsigned 64-bit to avoid overflow even in multi-billion-
/// cycle simulations.
using cycle_t = uint64_t;

/// Byte-granularity size / count type.  Used for block sizes, transfer sizes,
/// and cumulative byte-transfer statistics.
using byte_count_t = uint64_t;

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/// Whether a memory operation is a load (Read) or a store (Write).
enum class AccessType : uint8_t {
    Read,   ///< Load / fetch
    Write   ///< Store
};

/// Cache-line replacement policy.
///
/// * **LRU** – Least Recently Used.  Evicts the line whose last access
///   timestamp is the oldest.  Good locality exploitation but requires
///   per-line bookkeeping.
/// * **FIFO** – First-In, First-Out.  Evicts the line that was *installed*
///   earliest, regardless of later re-references.  Simpler than LRU but can
///   suffer from Bélády's anomaly in some workloads.
/// * **Random** – Picks a victim uniformly at random.  Extremely cheap in
///   hardware and surprisingly competitive for highly-associative caches.
enum class ReplacementPolicy : uint8_t {
    LRU,
    FIFO,
    Random
};

/// What happens on a store that hits in the cache.
///
/// * **WriteThrough** – The store is forwarded to the next level immediately,
///   so every level always has a consistent copy.  Generates more downstream
///   traffic but simplifies coherence.
/// * **WriteBack** – The store is absorbed by the cache; the dirty line is
///   written back only on eviction.  Reduces traffic at the cost of needing
///   a dirty bit per line.
enum class WritePolicy : uint8_t {
    WriteThrough,
    WriteBack
};

/// What happens on a store that *misses* in the cache.
///
/// * **WriteAllocate** – Fetch the block into the cache, then perform the
///   write (typical pairing with WriteBack).
/// * **NoWriteAllocate** – Forward the write to the next level without
///   loading the block (typical pairing with WriteThrough).
enum class WriteAllocatePolicy : uint8_t {
    WriteAllocate,
    NoWriteAllocate
};

/// DRAM scheduling policy for the memory controller's request queue.
///
/// * **FCFS** – First-Come, First-Served.  Simple and fair but ignores row-
///   buffer locality, leading to many costly row activations.
/// * **FR_FCFS** – First-Ready, First-Come-First-Served.  Prioritises
///   requests that hit an already-open row (row-buffer hits) over those that
///   would require a precharge + activate.  Among equally "ready" requests,
///   the oldest is chosen.  This is the de-facto baseline in academic DRAM
///   research.
enum class SchedulingPolicy : uint8_t {
    FCFS,
    FR_FCFS
};

/// DRAM row-buffer management policy.
enum class PagePolicy : uint8_t {
    OpenPage,    ///< Keep row open after access (exploits row locality)
    ClosedPage   ///< Auto-precharge after access (reduces conflict penalty on random traffic)
};

/// Hardware cache prefetching policy.
enum class PrefetchPolicy : uint8_t {
    None,
    NextLine,    ///< Prefetch block + 1 on miss
    Stride       ///< Detect constant address stride and prefetch ahead
};

// ---------------------------------------------------------------------------
// Enum ↔ string conversion helpers
// ---------------------------------------------------------------------------

/// Convert an AccessType value to a human-readable string_view.
[[nodiscard]] constexpr std::string_view to_string(AccessType t) noexcept {
    switch (t) {
        case AccessType::Read:  return "Read";
        case AccessType::Write: return "Write";
    }
    return "Unknown";  // unreachable, silences compiler warnings
}

/// Convert a ReplacementPolicy value to a human-readable string_view.
[[nodiscard]] constexpr std::string_view to_string(ReplacementPolicy p) noexcept {
    switch (p) {
        case ReplacementPolicy::LRU:    return "LRU";
        case ReplacementPolicy::FIFO:   return "FIFO";
        case ReplacementPolicy::Random: return "Random";
    }
    return "Unknown";
}

/// Convert a WritePolicy value to a human-readable string_view.
[[nodiscard]] constexpr std::string_view to_string(WritePolicy p) noexcept {
    switch (p) {
        case WritePolicy::WriteThrough: return "WriteThrough";
        case WritePolicy::WriteBack:    return "WriteBack";
    }
    return "Unknown";
}

/// Convert a WriteAllocatePolicy value to a human-readable string_view.
[[nodiscard]] constexpr std::string_view to_string(WriteAllocatePolicy p) noexcept {
    switch (p) {
        case WriteAllocatePolicy::WriteAllocate:   return "WriteAllocate";
        case WriteAllocatePolicy::NoWriteAllocate:  return "NoWriteAllocate";
    }
    return "Unknown";
}

/// Convert a SchedulingPolicy value to a human-readable string_view.
[[nodiscard]] constexpr std::string_view to_string(SchedulingPolicy p) noexcept {
    switch (p) {
        case SchedulingPolicy::FCFS:    return "FCFS";
        case SchedulingPolicy::FR_FCFS: return "FR-FCFS";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string(PagePolicy p) noexcept {
    switch (p) {
        case PagePolicy::OpenPage:   return "OpenPage";
        case PagePolicy::ClosedPage: return "ClosedPage";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string(PrefetchPolicy p) noexcept {
    switch (p) {
        case PrefetchPolicy::None:     return "None";
        case PrefetchPolicy::NextLine: return "NextLine";
        case PrefetchPolicy::Stride:   return "Stride";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// String → enum parsing  (throws std::invalid_argument on bad input)
// ---------------------------------------------------------------------------

/// Parse a string into an AccessType.
/// @throws std::invalid_argument if the string is not recognised.
[[nodiscard]] inline AccessType access_type_from_string(std::string_view s) {
    if (s == "Read"  || s == "read"  || s == "R" || s == "r") return AccessType::Read;
    if (s == "Write" || s == "write" || s == "W" || s == "w") return AccessType::Write;
    throw std::invalid_argument(std::string("Unknown AccessType: ") + std::string(s));
}

/// Parse a string into a ReplacementPolicy.
/// @throws std::invalid_argument if the string is not recognised.
[[nodiscard]] inline ReplacementPolicy replacement_policy_from_string(std::string_view s) {
    if (s == "LRU"    || s == "lru")    return ReplacementPolicy::LRU;
    if (s == "FIFO"   || s == "fifo")   return ReplacementPolicy::FIFO;
    if (s == "Random" || s == "random") return ReplacementPolicy::Random;
    throw std::invalid_argument(std::string("Unknown ReplacementPolicy: ") + std::string(s));
}

/// Parse a string into a WritePolicy.
/// @throws std::invalid_argument if the string is not recognised.
[[nodiscard]] inline WritePolicy write_policy_from_string(std::string_view s) {
    if (s == "WriteThrough" || s == "write_through" || s == "wt") return WritePolicy::WriteThrough;
    if (s == "WriteBack"    || s == "write_back"    || s == "wb") return WritePolicy::WriteBack;
    throw std::invalid_argument(std::string("Unknown WritePolicy: ") + std::string(s));
}

/// Parse a string into a WriteAllocatePolicy.
/// @throws std::invalid_argument if the string is not recognised.
[[nodiscard]] inline WriteAllocatePolicy write_allocate_policy_from_string(std::string_view s) {
    if (s == "WriteAllocate"   || s == "write_allocate"    || s == "wa")  return WriteAllocatePolicy::WriteAllocate;
    if (s == "NoWriteAllocate" || s == "no_write_allocate" || s == "nwa") return WriteAllocatePolicy::NoWriteAllocate;
    throw std::invalid_argument(std::string("Unknown WriteAllocatePolicy: ") + std::string(s));
}

/// Parse a string into a SchedulingPolicy.
/// @throws std::invalid_argument if the string is not recognised.
[[nodiscard]] inline SchedulingPolicy scheduling_policy_from_string(std::string_view s) {
    if (s == "FCFS"    || s == "fcfs")                        return SchedulingPolicy::FCFS;
    if (s == "FR-FCFS" || s == "FR_FCFS" || s == "fr_fcfs")   return SchedulingPolicy::FR_FCFS;
    throw std::invalid_argument(std::string("Unknown SchedulingPolicy: ") + std::string(s));
}

[[nodiscard]] inline PagePolicy page_policy_from_string(std::string_view s) {
    if (s == "OpenPage"   || s == "open_page"   || s == "open")   return PagePolicy::OpenPage;
    if (s == "ClosedPage" || s == "closed_page" || s == "closed") return PagePolicy::ClosedPage;
    throw std::invalid_argument(std::string("Unknown PagePolicy: ") + std::string(s));
}

[[nodiscard]] inline PrefetchPolicy prefetch_policy_from_string(std::string_view s) {
    if (s == "None"     || s == "none")                          return PrefetchPolicy::None;
    if (s == "NextLine" || s == "next_line" || s == "nextline")  return PrefetchPolicy::NextLine;
    if (s == "Stride"   || s == "stride")                        return PrefetchPolicy::Stride;
    throw std::invalid_argument(std::string("Unknown PrefetchPolicy: ") + std::string(s));
}

}  // namespace memsim
