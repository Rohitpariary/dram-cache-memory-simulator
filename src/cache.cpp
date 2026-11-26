/// @file cache.cpp
/// @brief Implementation of the set-associative cache simulator.
///
/// See cache.hpp for the full design rationale and address decomposition
/// diagram.  This file implements the three classes declared there:
///   - CacheSet   (set-level lookup, eviction, insertion)
///   - Cache      (top-level access logic, address math, statistics)

#include "memsim/cache.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace memsim {

// =====================================================================
// Utility — integer log2 for positive powers of two
// =====================================================================

namespace {

/// Compute floor(log2(n)) via bit-counting.
/// Precondition: n > 0 and n is a power of two.
[[nodiscard]] constexpr uint32_t log2_of_power_of_two(uint32_t n) noexcept {
    uint32_t result = 0;
    while (n > 1) {
        n >>= 1;
        ++result;
    }
    return result;
}

} // anonymous namespace

// =====================================================================
// CacheSet
// =====================================================================

CacheSet::CacheSet(uint32_t ways)
    : lines_(ways) // value-initialised → all CacheLine fields are zero/false
{}

std::optional<uint32_t> CacheSet::find(addr_t tag) const {
    for (uint32_t i = 0; i < static_cast<uint32_t>(lines_.size()); ++i) {
        if (lines_[i].valid && lines_[i].tag == tag) {
            return i;
        }
    }
    return std::nullopt;
}

uint32_t CacheSet::find_victim(ReplacementPolicy policy,
                                std::mt19937& rng) const {
    // --- Prefer the first invalid (empty) way ---
    for (uint32_t i = 0; i < static_cast<uint32_t>(lines_.size()); ++i) {
        if (!lines_[i].valid) {
            return i;
        }
    }

    // --- All ways are valid; apply the replacement policy ---
    switch (policy) {

    case ReplacementPolicy::LRU: {
        // Evict the line with the *smallest* last_access_time (least recent).
        uint32_t victim = 0;
        uint64_t oldest = std::numeric_limits<uint64_t>::max();
        for (uint32_t i = 0; i < static_cast<uint32_t>(lines_.size()); ++i) {
            if (lines_[i].last_access_time < oldest) {
                oldest = lines_[i].last_access_time;
                victim = i;
            }
        }
        return victim;
    }

    case ReplacementPolicy::FIFO: {
        // Evict the line that was *inserted* earliest (smallest insertion_time).
        uint32_t victim = 0;
        uint64_t earliest = std::numeric_limits<uint64_t>::max();
        for (uint32_t i = 0; i < static_cast<uint32_t>(lines_.size()); ++i) {
            if (lines_[i].insertion_time < earliest) {
                earliest = lines_[i].insertion_time;
                victim   = i;
            }
        }
        return victim;
    }

    case ReplacementPolicy::Random: {
        // Uniform random selection among all valid ways.
        std::uniform_int_distribution<uint32_t> dist(
            0, static_cast<uint32_t>(lines_.size()) - 1);
        return dist(rng);
    }

    default:
        // Should never happen if the enum is well-formed.
        return 0;
    }
}

void CacheSet::insert(uint32_t way, addr_t tag, bool dirty,
                       uint64_t current_time) {
    assert(way < lines_.size());
    CacheLine& line      = lines_[way];
    line.valid            = true;
    line.dirty            = dirty;
    line.tag              = tag;
    line.last_access_time = current_time;
    line.insertion_time   = current_time;
}

void CacheSet::update_access(uint32_t way, uint64_t current_time) {
    assert(way < lines_.size());
    lines_[way].last_access_time = current_time;
}

bool CacheSet::is_full() const {
    return std::all_of(lines_.cbegin(), lines_.cend(),
                       [](const CacheLine& l) { return l.valid; });
}

uint32_t CacheSet::get_way_count() const {
    return static_cast<uint32_t>(lines_.size());
}

const CacheLine& CacheSet::get_line(uint32_t way) const {
    assert(way < lines_.size());
    return lines_[way];
}

CacheLine& CacheSet::get_line_mut(uint32_t way) {
    assert(way < lines_.size());
    return lines_[way];
}

// =====================================================================
// Cache — construction & reset
// =====================================================================

Cache::Cache(CacheConfig config)
    : config_(std::move(config))
{
    // Validate (and possibly derive num_sets).
    config_.validate();

    // Pre-compute bit-field widths for address decomposition.
    offset_bits_ = log2_of_power_of_two(config_.line_size_bytes);
    index_bits_  = log2_of_power_of_two(config_.num_sets);

    // Initialise the statistics tracker (all counters start at zero).
    stats_ = CacheStatistics{};

    // Allocate the set array.  Each set has `associativity` ways.
    sets_.reserve(config_.num_sets);
    for (uint32_t i = 0; i < config_.num_sets; ++i) {
        sets_.emplace_back(config_.associativity);
    }
}

void Cache::reset() {
    access_counter_ = 0;
    stats_ = CacheStatistics{};
    seen_blocks_.clear();
    last_miss_block_ = 0;
    last_stride_ = 0;
    has_last_miss_ = false;
    sets_.clear();
    sets_.reserve(config_.num_sets);
    for (uint32_t i = 0; i < config_.num_sets; ++i) {
        sets_.emplace_back(config_.associativity);
    }
}

// =====================================================================
// Cache — address decomposition
// =====================================================================

addr_t Cache::extract_tag(addr_t address) const {
    return address >> (offset_bits_ + index_bits_);
}

uint32_t Cache::extract_set_index(addr_t address) const {
    return static_cast<uint32_t>(
        (address >> offset_bits_) & (config_.num_sets - 1));
}

uint32_t Cache::extract_offset(addr_t address) const {
    return static_cast<uint32_t>(
        address & ((static_cast<addr_t>(1) << offset_bits_) - 1));
}

addr_t Cache::reconstruct_address(addr_t tag, uint32_t set_index) const {
    return (tag << (offset_bits_ + index_bits_))
         | (static_cast<addr_t>(set_index) << offset_bits_);
}

// =====================================================================
// Cache — access  (the heart of the simulator)
// =====================================================================

CacheResult Cache::access(const MemoryRequest& req) {
    const uint64_t current_time = access_counter_++;
    const addr_t   tag          = extract_tag(req.address);
    const uint32_t set_index    = extract_set_index(req.address);
    const bool     is_write     = (req.type == AccessType::Write);

    CacheSet& set = sets_[set_index];
    CacheResult result;
    result.latency = config_.hit_latency_cycles;

    // Record the access in statistics (regardless of hit/miss).
    stats_.record_access(req.type);

    // ------------------------------------------------------------------
    // Probe the set
    // ------------------------------------------------------------------
    auto way_opt = set.find(tag);

    if (way_opt.has_value()) {
        // ============================================================
        // HIT
        // ============================================================
        const uint32_t way = *way_opt;
        result.hit = true;

        // Touch the line for LRU bookkeeping.
        set.update_access(way, current_time);

        // Handle write-specific logic.
        if (is_write) {
            if (config_.write_policy == WritePolicy::WriteBack) {
                // Mark dirty — will be written back only on eviction.
                set.get_line_mut(way).dirty = true;
            } else {
                // WriteThrough — propagate every write to the next level.
                result.write_through = true;
                stats_.record_transfer(req.size);
            }
        }

        stats_.record_hit(req.type);
        return result;
    }

    // ============================================================
    // MISS
    // ============================================================
    result.hit = false;
    stats_.record_miss(req.type);

    const addr_t block_addr =
        req.address & ~(static_cast<addr_t>(config_.line_size_bytes) - 1);
    if (seen_blocks_.insert(block_addr).second) {
        ++stats_.compulsory_misses;
    } else {
        ++stats_.non_compulsory_misses;
    }

    if (is_write &&
        config_.write_allocate_policy == WriteAllocatePolicy::NoWriteAllocate) {
        // ---------------------------------------------------------
        // Write miss + NoWriteAllocate
        // ---------------------------------------------------------
        // The write is forwarded directly to the next level; we do
        // NOT bring the line into this cache.
        result.write_through = true;
        stats_.record_transfer(req.size);
        return result;
    }

    // ---------------------------------------------------------
    // Read miss  OR  Write miss + WriteAllocate
    // ---------------------------------------------------------
    // We must bring the block into the cache.
    handle_miss_with_allocate(set_index, tag, is_write, current_time, result);

    // For WriteThrough writes that *also* allocate (write-allocate +
    // write-through), the write must still propagate.
    if (is_write && config_.write_policy == WritePolicy::WriteThrough) {
        result.write_through = true;
        stats_.record_transfer(req.size);
    }

    // Hardware prefetcher (triggered on miss)
    if (config_.prefetch_policy == PrefetchPolicy::NextLine) {
        for (uint32_t d = 1; d <= config_.prefetch_degree; ++d) {
            prefetch_line(block_addr + static_cast<addr_t>(d) * config_.line_size_bytes);
        }
    } else if (config_.prefetch_policy == PrefetchPolicy::Stride) {
        const int64_t stride = has_last_miss_
            ? (static_cast<int64_t>(block_addr) - static_cast<int64_t>(last_miss_block_))
            : 0;
        if (has_last_miss_ && stride != 0 && stride == last_stride_) {
            for (uint32_t d = 1; d <= config_.prefetch_degree; ++d) {
                const int64_t target = static_cast<int64_t>(block_addr) +
                                       static_cast<int64_t>(d) * stride;
                if (target >= 0) {
                    prefetch_line(static_cast<addr_t>(target));
                }
            }
        }
        last_stride_ = stride;
        last_miss_block_ = block_addr;
        has_last_miss_ = true;
    }

    return result;
}

// =====================================================================
// Cache — miss handling (allocate path)
// =====================================================================

void Cache::handle_miss_with_allocate(uint32_t set_index, addr_t tag,
                                       bool is_write, uint64_t current_time,
                                       CacheResult& result) {
    CacheSet& set = sets_[set_index];

    // Choose a victim way (prefers invalid / empty slots).
    const uint32_t victim_way = set.find_victim(
        config_.replacement_policy, rng_);

    const CacheLine& victim = set.get_line(victim_way);

    // If the victim way holds a valid line we are evicting it.
    if (victim.valid) {
        result.eviction = true;
        result.evicted_address = reconstruct_address(victim.tag, set_index);

        if (victim.dirty) {
            // The evicted line is dirty under WriteBack — it must be
            // written to the next level.
            result.dirty_eviction = true;
            stats_.record_transfer(config_.line_size_bytes);
        }

        stats_.record_eviction(victim.dirty);
    }

    // Determine the initial dirty state of the new line:
    //   • If this is a write AND the policy is WriteBack, the line is
    //     dirty immediately (the store is captured in the cache).
    //   • Otherwise the line starts clean.
    const bool new_dirty =
        is_write && (config_.write_policy == WritePolicy::WriteBack);

    set.insert(victim_way, tag, new_dirty, current_time);
}

// =====================================================================
// Cache — invalidation
// =====================================================================

void Cache::invalidate(addr_t address) {
    const addr_t   tag       = extract_tag(address);
    const uint32_t set_index = extract_set_index(address);

    CacheSet& set = sets_[set_index];
    auto way_opt  = set.find(tag);

    if (way_opt.has_value()) {
        CacheLine& line = set.get_line_mut(*way_opt);
        line.valid = false;
        line.dirty = false;
        line.tag   = 0;
    }
}

bool Cache::prefetch_line(addr_t address) {
    const addr_t   tag       = extract_tag(address);
    const uint32_t set_index = extract_set_index(address);
    CacheSet& set = sets_[set_index];

    if (set.find(tag).has_value()) {
        return false;  // already cached
    }

    const uint32_t victim_way = set.find_victim(config_.replacement_policy, rng_);
    const CacheLine& victim = set.get_line(victim_way);
    if (victim.valid) {
        if (victim.dirty) {
            stats_.record_transfer(config_.line_size_bytes);
        }
        stats_.record_eviction(victim.dirty);
    }

    set.insert(victim_way, tag, false, access_counter_++);
    ++stats_.prefetches_issued;
    return true;
}

// =====================================================================
// Cache — accessors
// =====================================================================

const CacheStatistics& Cache::get_stats() const {
    return stats_;
}

const CacheConfig& Cache::get_config() const {
    return config_;
}

} // namespace memsim
