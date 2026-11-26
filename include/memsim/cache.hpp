#pragma once

/// @file cache.hpp
/// @brief Set-associative cache simulator with configurable policies.
///
/// This header declares the three core types that model a cache:
///
///   CacheLine  — metadata for a single cache block (valid, dirty, tag, …)
///   CacheSet   — one set of the cache; contains `associativity` lines
///   Cache      — the top-level cache; owns all sets and orchestrates lookups
///
/// ## Address Decomposition (power-of-two geometry)
///
///   |<---- tag ---->|<-- set index -->|<-- offset -->|
///
///   offset_bits = log2(line_size_bytes)
///   index_bits  = log2(num_sets)
///   tag         = address >> (offset_bits + index_bits)
///   set_index   = (address >> offset_bits) & (num_sets − 1)
///
/// ## Access Flow
///
///   1. Decompose the address into tag + set index.
///   2. Search the target set for a matching, valid line.
///   3. On HIT  → update access metadata (LRU timestamp), record stats.
///   4. On MISS →
///      a. Write-miss with NoWriteAllocate → do NOT bring line into cache.
///      b. Otherwise → pick a victim (LRU / FIFO / Random), evict it if
///         necessary, and install the new line.
///   5. For WriteThrough writes → always flag that data must propagate
///      to the next level in the hierarchy.
///   6. For WriteBack → mark the line dirty; writeback happens on eviction.

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "memsim/cache_config.hpp"
#include "memsim/request.hpp"
#include "memsim/statistics.hpp"
#include "memsim/types.hpp"

namespace memsim {

// =====================================================================
// CacheResult — returned by Cache::access()
// =====================================================================

/// Outcome of a single cache access.  Gives the caller enough information
/// to drive the next-level memory (e.g. DRAM controller) if necessary.
struct CacheResult {
    /// True if the access was a cache hit.
    bool hit{false};

    /// True if a line was evicted to make room for the new block.
    bool eviction{false};

    /// True if the evicted line was dirty (needs write-back).
    bool dirty_eviction{false};

    /// The full byte-address of the evicted line (only meaningful when
    /// `eviction` is true).  Reconstructed from the evicted tag + set index.
    addr_t evicted_address{0};

    /// Access latency contributed by *this* cache level alone (cycles).
    cycle_t latency{0};

    /// True if a write-through write occurred, meaning the write must
    /// also propagate to the next level regardless of hit/miss.
    bool write_through{false};
};

// =====================================================================
// CacheLine — one slot inside a set
// =====================================================================

/// Metadata for a single cache block.  The actual data payload is not
/// stored because the simulator only models *timing and hit/miss
/// behaviour*, not data contents.
struct CacheLine {
    /// Is this line currently holding a valid block?
    bool valid{false};

    /// Has this line been written to since it was fetched?
    /// Only meaningful under a WriteBack policy.
    bool dirty{false};

    /// The tag portion of the block address stored in this line.
    addr_t tag{0};

    /// Monotonically increasing timestamp of the most recent access.
    /// Used by the LRU replacement policy.
    uint64_t last_access_time{0};

    /// Timestamp of the moment this line was first brought into the set.
    /// Used by the FIFO replacement policy.
    uint64_t insertion_time{0};
};

// =====================================================================
// CacheSet — one associativity-group
// =====================================================================

/// Represents a single set inside the cache.  Each set contains
/// `associativity` lines (ways).  All lookup, insertion and eviction
/// operations are scoped to a set.
class CacheSet {
public:
    /// Construct a set with the given number of ways (associativity).
    explicit CacheSet(uint32_t ways);

    // ---- Lookup ----

    /// Search for a valid line whose tag matches `tag`.
    /// @return The way index if found, or std::nullopt on miss.
    [[nodiscard]] std::optional<uint32_t> find(addr_t tag) const;

    // ---- Eviction ----

    /// Select a victim way according to the given replacement policy.
    /// Prefers invalid (empty) ways first; if all ways are valid the
    /// policy selects among them.
    /// @param policy       The replacement algorithm.
    /// @param rng          A Mersenne Twister RNG, used only for Random.
    /// @return Way index of the chosen victim.
    [[nodiscard]] uint32_t find_victim(ReplacementPolicy policy,
                                       std::mt19937& rng) const;

    // ---- Insertion / Update ----

    /// Install a new block in the given way, overwriting whatever was there.
    /// @param way          Way index (from find_victim).
    /// @param tag          Tag of the new block.
    /// @param dirty        Initial dirty state.
    /// @param current_time Monotonic access counter for LRU / FIFO.
    void insert(uint32_t way, addr_t tag, bool dirty, uint64_t current_time);

    /// Update the access timestamp of an existing line (LRU touch).
    void update_access(uint32_t way, uint64_t current_time);

    // ---- Queries ----

    /// @return True if every way in the set holds a valid line.
    [[nodiscard]] bool is_full() const;

    /// @return The number of ways (associativity) in this set.
    [[nodiscard]] uint32_t get_way_count() const;

    /// Direct (const) access to a line — used by Cache for eviction info.
    [[nodiscard]] const CacheLine& get_line(uint32_t way) const;

    /// Direct (mutable) access to a line — used by Cache to set dirty bits, etc.
    CacheLine& get_line_mut(uint32_t way);

private:
    std::vector<CacheLine> lines_;
};

// =====================================================================
// Cache — the top-level cache simulator
// =====================================================================

class Cache {
public:
    /// Construct a cache from a (validated) configuration.
    /// @throws std::invalid_argument if the config fails validation.
    explicit Cache(CacheConfig config);

    // ---- Core operation ----

    /// Perform a read or write access.
    ///
    /// This is the main entry point.  It decomposes the address, probes
    /// the appropriate set, handles hit/miss logic including replacement
    /// and write-policy semantics, records statistics, and returns a
    /// CacheResult describing what happened.
    CacheResult access(const MemoryRequest& req);

    // ---- Address decomposition ----

    /// Extract the tag field from a byte address.
    [[nodiscard]] addr_t   extract_tag(addr_t address) const;

    /// Extract the set index from a byte address.
    [[nodiscard]] uint32_t extract_set_index(addr_t address) const;

    /// Extract the block offset from a byte address.
    [[nodiscard]] uint32_t extract_offset(addr_t address) const;

    // ---- Accessors ----

    /// Read-only view of accumulated statistics.
    [[nodiscard]] const CacheStatistics& get_stats() const;

    /// Read-only view of the configuration.
    [[nodiscard]] const CacheConfig& get_config() const;

    // ---- Maintenance ----

    /// Clear all lines and reset statistics to zero.
    void reset();

    /// Invalidate any line that maps to the given address.
    /// Useful for coherence protocols or explicit flush operations.
    void invalidate(addr_t address);

    /// Install a prefetched line without recording a CPU access/miss.
    /// Returns true if a prefetch line was actually installed.
    bool prefetch_line(addr_t address);

private:
    // ---- Helpers ----

    /// Reconstruct a full byte-address from a tag and set index.
    [[nodiscard]] addr_t reconstruct_address(addr_t tag,
                                             uint32_t set_index) const;

    /// Handle a cache miss (read-miss or write-miss with WriteAllocate).
    /// Selects a victim, evicts if necessary, and inserts the new line.
    /// Populates the relevant fields of `result`.
    void handle_miss_with_allocate(uint32_t set_index, addr_t tag,
                                   bool is_write, uint64_t current_time,
                                   CacheResult& result);

    CacheConfig          config_;
    std::vector<CacheSet> sets_;
    CacheStatistics      stats_;

    /// Monotonically increasing counter used as a logical clock for
    /// replacement-policy timestamps.
    uint64_t access_counter_{0};

    /// Pre-computed bit-widths for address decomposition.
    uint32_t offset_bits_{0};
    uint32_t index_bits_{0};

    /// Random number generator for the Random replacement policy.
    std::mt19937 rng_{42};  // deterministic seed for reproducibility

    /// Block-aligned addresses ever seen by this cache (for 3C compulsory miss tracking).
    std::unordered_set<addr_t> seen_blocks_;

    /// Stride-prefetcher tracking state.
    addr_t last_miss_block_{0};
    int64_t last_stride_{0};
    bool has_last_miss_{false};
};

} // namespace memsim
