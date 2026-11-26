/// @file cache_tests.cpp
/// @brief Comprehensive unit tests for the cache simulator.
///
/// Tests cover:
///   - First access produces a miss
///   - Repeated access produces a hit
///   - Direct-mapped conflict misses
///   - Set-associative mapping
///   - Fully associative behavior
///   - LRU eviction
///   - FIFO eviction
///   - Dirty-bit handling
///   - Write-through propagation
///   - Write-back eviction
///   - Write-allocate vs no-write-allocate
///   - Statistics invariants (hits + misses = accesses)

#include <gtest/gtest.h>
#include "memsim/cache.hpp"
#include "memsim/cache_config.hpp"
#include "memsim/request.hpp"
#include "memsim/types.hpp"

using namespace memsim;

// =========================================================================
// Helper: create a small cache for testing
// =========================================================================

/// 256-byte, 2-way set-associative, 64-byte lines → 2 sets
static CacheConfig make_test_config_2way() {
    CacheConfig cfg;
    cfg.name = "Test-L1";
    cfg.capacity_bytes = 256;
    cfg.line_size_bytes = 64;
    cfg.associativity = 2;
    cfg.num_sets = 0;  // auto-compute → 2
    cfg.hit_latency_cycles = 1;
    cfg.replacement_policy = ReplacementPolicy::LRU;
    cfg.write_policy = WritePolicy::WriteBack;
    cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
    cfg.validate();
    return cfg;
}

/// Direct-mapped cache: 256 bytes, 64-byte lines → 4 sets, 1 way
static CacheConfig make_test_config_direct_mapped() {
    CacheConfig cfg;
    cfg.name = "Test-DM";
    cfg.capacity_bytes = 256;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.num_sets = 0;
    cfg.hit_latency_cycles = 1;
    cfg.replacement_policy = ReplacementPolicy::LRU;
    cfg.write_policy = WritePolicy::WriteBack;
    cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
    cfg.validate();
    return cfg;
}

/// Fully associative: 256 bytes, 64-byte lines → 1 set, 4 ways
static CacheConfig make_test_config_fully_assoc() {
    CacheConfig cfg;
    cfg.name = "Test-FA";
    cfg.capacity_bytes = 256;
    cfg.line_size_bytes = 64;
    cfg.associativity = 4;
    cfg.num_sets = 0;
    cfg.hit_latency_cycles = 1;
    cfg.replacement_policy = ReplacementPolicy::LRU;
    cfg.write_policy = WritePolicy::WriteBack;
    cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
    cfg.validate();
    return cfg;
}

// =========================================================================
// Basic Hit/Miss Tests
// =========================================================================

TEST(CacheTest, FirstAccessIsMiss) {
    Cache cache(make_test_config_2way());
    MemoryRequest req(0x100, AccessType::Read);
    CacheResult result = cache.access(req);
    EXPECT_FALSE(result.hit);
}

TEST(CacheTest, RepeatedAccessIsHit) {
    Cache cache(make_test_config_2way());
    MemoryRequest req(0x100, AccessType::Read);
    cache.access(req);  // miss
    CacheResult result = cache.access(req);  // should hit
    EXPECT_TRUE(result.hit);
}

TEST(CacheTest, SameBlockDifferentOffsetIsHit) {
    Cache cache(make_test_config_2way());
    // Line size = 64 bytes, so 0x100 and 0x110 are in the same block
    MemoryRequest req1(0x100, AccessType::Read);
    MemoryRequest req2(0x110, AccessType::Read);
    cache.access(req1);  // miss, loads block
    CacheResult result = cache.access(req2);  // same block → hit
    EXPECT_TRUE(result.hit);
}

// =========================================================================
// Address Decomposition Tests
// =========================================================================

TEST(CacheTest, AddressDecomposition) {
    Cache cache(make_test_config_2way());
    // 64-byte lines → 6 offset bits
    // 2 sets → 1 index bit
    // Tag = remaining bits

    EXPECT_EQ(cache.extract_offset(0x00), 0u);
    EXPECT_EQ(cache.extract_offset(0x3F), 63u);
    EXPECT_EQ(cache.extract_set_index(0x00), 0u);
    EXPECT_EQ(cache.extract_set_index(0x40), 1u);  // bit 6 = 1
    EXPECT_EQ(cache.extract_set_index(0x80), 0u);  // bit 6 = 0
    EXPECT_EQ(cache.extract_tag(0x00), 0u);
    EXPECT_EQ(cache.extract_tag(0x80), 1u);         // bits 7+ = 1
}

// =========================================================================
// Direct-Mapped Cache Tests
// =========================================================================

TEST(CacheTest, DirectMappedConflictMiss) {
    Cache cache(make_test_config_direct_mapped());
    // 4 sets, 64-byte lines → index from bits [7:6]
    // Addresses 0x000 and 0x100 map to same set (index 0)

    MemoryRequest reqA(0x000, AccessType::Read);
    MemoryRequest reqB(0x100, AccessType::Read);

    cache.access(reqA);  // miss, fills set 0
    CacheResult r1 = cache.access(reqA);
    EXPECT_TRUE(r1.hit);

    cache.access(reqB);  // conflict miss — evicts A
    CacheResult r2 = cache.access(reqA);  // A was evicted → miss again
    EXPECT_FALSE(r2.hit);
}

// =========================================================================
// Set-Associative Tests
// =========================================================================

TEST(CacheTest, SetAssociativeNoConflictWithinAssociativity) {
    Cache cache(make_test_config_2way());
    // 2-way, 2 sets. Set 0 can hold 2 lines.
    // 0x000 and 0x080 both map to set 0 (different tags)

    MemoryRequest reqA(0x000, AccessType::Read);
    MemoryRequest reqB(0x080, AccessType::Read);

    cache.access(reqA);  // miss
    cache.access(reqB);  // miss, but set 0 still has room

    CacheResult r1 = cache.access(reqA);
    EXPECT_TRUE(r1.hit);  // A still in set 0
    CacheResult r2 = cache.access(reqB);
    EXPECT_TRUE(r2.hit);  // B still in set 0
}

TEST(CacheTest, SetAssociativeEvictionWhenFull) {
    Cache cache(make_test_config_2way());
    // 2-way, 2 sets. Filling set 0 with 3 different tags causes eviction.
    // Set 0 addresses (stride 0x80): 0x000, 0x080, 0x100

    MemoryRequest reqA(0x000, AccessType::Read);
    MemoryRequest reqB(0x080, AccessType::Read);
    MemoryRequest reqC(0x100, AccessType::Read);

    cache.access(reqA);  // miss
    cache.access(reqB);  // miss
    cache.access(reqC);  // miss — evicts LRU (A)

    CacheResult r = cache.access(reqA);
    EXPECT_FALSE(r.hit);  // A was evicted
}

// =========================================================================
// Fully Associative Tests
// =========================================================================

TEST(CacheTest, FullyAssociativeHoldsAllWays) {
    Cache cache(make_test_config_fully_assoc());
    // 1 set, 4 ways. Can hold 4 different 64-byte blocks.

    for (uint32_t i = 0; i < 4; ++i) {
        MemoryRequest req(static_cast<addr_t>(i) * 64, AccessType::Read);
        cache.access(req);
    }

    for (uint32_t i = 0; i < 4; ++i) {
        MemoryRequest req(static_cast<addr_t>(i) * 64, AccessType::Read);
        CacheResult r = cache.access(req);
        EXPECT_TRUE(r.hit) << "Expected hit for block " << i;
    }
}

// =========================================================================
// LRU Eviction Tests
// =========================================================================

TEST(CacheTest, LRUEvictsLeastRecentlyUsed) {
    CacheConfig cfg = make_test_config_2way();
    cfg.replacement_policy = ReplacementPolicy::LRU;
    Cache cache(cfg);

    // 2-way set-assoc, 2 sets.
    // Fill set 0 with A(0x000) and B(0x080).
    MemoryRequest reqA(0x000, AccessType::Read);
    MemoryRequest reqB(0x080, AccessType::Read);

    cache.access(reqA);  // A loaded (time 0)
    cache.access(reqB);  // B loaded (time 1)

    // Touch A again (now A is MRU, B is LRU).
    cache.access(reqA);  // hit (time 2)

    // Insert C — should evict B (LRU), not A.
    MemoryRequest reqC(0x100, AccessType::Read);
    cache.access(reqC);  // evicts B

    CacheResult rA = cache.access(reqA);
    EXPECT_TRUE(rA.hit) << "A should still be in cache (was MRU)";

    CacheResult rB = cache.access(reqB);
    EXPECT_FALSE(rB.hit) << "B should have been evicted (was LRU)";
}

// =========================================================================
// FIFO Eviction Tests
// =========================================================================

TEST(CacheTest, FIFOEvictsOldestInserted) {
    CacheConfig cfg = make_test_config_2way();
    cfg.replacement_policy = ReplacementPolicy::FIFO;
    Cache cache(cfg);

    MemoryRequest reqA(0x000, AccessType::Read);
    MemoryRequest reqB(0x080, AccessType::Read);

    cache.access(reqA);  // A inserted first
    cache.access(reqB);  // B inserted second

    // Touch A again — under FIFO, this does NOT change eviction order.
    cache.access(reqA);  // hit, but A is still oldest

    // Insert C — should evict A (first inserted), NOT B.
    MemoryRequest reqC(0x100, AccessType::Read);
    cache.access(reqC);  // evicts A (FIFO)

    // Check B first (hit does not evict anything), then A (miss).
    CacheResult rB = cache.access(reqB);
    EXPECT_TRUE(rB.hit) << "B should still be in cache";

    CacheResult rA = cache.access(reqA);
    EXPECT_FALSE(rA.hit) << "A should have been evicted (FIFO oldest)";
}

// =========================================================================
// Write Policy Tests
// =========================================================================

TEST(CacheTest, WriteBackSetsDirtyBit) {
    CacheConfig cfg = make_test_config_2way();
    cfg.write_policy = WritePolicy::WriteBack;
    cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
    Cache cache(cfg);

    MemoryRequest req(0x100, AccessType::Write);
    CacheResult r = cache.access(req);
    EXPECT_FALSE(r.hit);         // first access is a miss
    EXPECT_FALSE(r.write_through);  // WriteBack doesn't propagate

    // Second write should be a hit.
    CacheResult r2 = cache.access(req);
    EXPECT_TRUE(r2.hit);
    EXPECT_FALSE(r2.write_through);
}

TEST(CacheTest, WriteThroughAlwaysPropagates) {
    CacheConfig cfg = make_test_config_2way();
    cfg.write_policy = WritePolicy::WriteThrough;
    cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
    Cache cache(cfg);

    MemoryRequest req(0x100, AccessType::Write);
    CacheResult r = cache.access(req);
    EXPECT_TRUE(r.write_through);  // Must propagate

    CacheResult r2 = cache.access(req);
    EXPECT_TRUE(r2.hit);
    EXPECT_TRUE(r2.write_through);  // Even on hit, must propagate
}

TEST(CacheTest, NoWriteAllocateDoesNotFill) {
    CacheConfig cfg = make_test_config_2way();
    cfg.write_policy = WritePolicy::WriteThrough;
    cfg.write_allocate_policy = WriteAllocatePolicy::NoWriteAllocate;
    Cache cache(cfg);

    MemoryRequest write_req(0x100, AccessType::Write);
    cache.access(write_req);  // miss — should NOT allocate

    MemoryRequest read_req(0x100, AccessType::Read);
    CacheResult r = cache.access(read_req);
    EXPECT_FALSE(r.hit) << "Line should NOT be in cache after no-write-allocate";
}

TEST(CacheTest, WriteAllocateFillsOnWriteMiss) {
    CacheConfig cfg = make_test_config_2way();
    cfg.write_policy = WritePolicy::WriteBack;
    cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
    Cache cache(cfg);

    MemoryRequest write_req(0x100, AccessType::Write);
    cache.access(write_req);  // miss → allocate + write

    MemoryRequest read_req(0x100, AccessType::Read);
    CacheResult r = cache.access(read_req);
    EXPECT_TRUE(r.hit) << "Line should be in cache after write-allocate";
}

TEST(CacheTest, DirtyEvictionOnWriteBack) {
    CacheConfig cfg = make_test_config_2way();
    cfg.write_policy = WritePolicy::WriteBack;
    cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
    Cache cache(cfg);

    // Fill set 0 with dirty lines.
    MemoryRequest reqA(0x000, AccessType::Write);  // dirty
    MemoryRequest reqB(0x080, AccessType::Write);  // dirty

    cache.access(reqA);
    cache.access(reqB);

    // Insert C into set 0 — evicts A (LRU), which is dirty.
    MemoryRequest reqC(0x100, AccessType::Write);
    CacheResult r = cache.access(reqC);

    EXPECT_TRUE(r.eviction);
    EXPECT_TRUE(r.dirty_eviction);
}

// =========================================================================
// Statistics Invariant Tests
// =========================================================================

TEST(CacheTest, StatsInvariantHitsPlusMissesEqualsAccesses) {
    Cache cache(make_test_config_2way());

    // Mix of hits and misses.
    for (uint32_t i = 0; i < 100; ++i) {
        addr_t addr = (i % 5) * 64;  // 5 unique blocks, some will conflict
        MemoryRequest req(addr, (i % 3 == 0) ? AccessType::Write : AccessType::Read);
        cache.access(req);
    }

    const auto& stats = cache.get_stats();
    EXPECT_EQ(stats.hits + stats.misses, stats.total_accesses);
    EXPECT_EQ(stats.read_accesses + stats.write_accesses, stats.total_accesses);
    EXPECT_EQ(stats.read_hits + stats.write_hits, stats.hits);
    EXPECT_EQ(stats.read_misses + stats.write_misses, stats.misses);
}

TEST(CacheTest, StatsZeroAccessesSafe) {
    Cache cache(make_test_config_2way());
    const auto& stats = cache.get_stats();
    EXPECT_DOUBLE_EQ(stats.hit_rate(), 0.0);
    EXPECT_DOUBLE_EQ(stats.miss_rate(), 0.0);
}

// =========================================================================
// Configuration Validation Tests
// =========================================================================

TEST(CacheConfigTest, ValidConfigDoesNotThrow) {
    EXPECT_NO_THROW(make_test_config_2way());
    EXPECT_NO_THROW(make_test_config_direct_mapped());
    EXPECT_NO_THROW(make_test_config_fully_assoc());
}

TEST(CacheConfigTest, ZeroAssociativityThrows) {
    CacheConfig cfg;
    cfg.capacity_bytes = 256;
    cfg.line_size_bytes = 64;
    cfg.associativity = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(CacheConfigTest, NonPowerOfTwoLineSizeThrows) {
    CacheConfig cfg;
    cfg.capacity_bytes = 256;
    cfg.line_size_bytes = 48;  // not power of 2
    cfg.associativity = 4;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(CacheConfigTest, InconsistentCapacityThrows) {
    CacheConfig cfg;
    cfg.capacity_bytes = 300;  // not divisible evenly
    cfg.line_size_bytes = 64;
    cfg.associativity = 4;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

// =========================================================================
// 3C Miss Classification & Hardware Prefetching Tests
// =========================================================================

TEST(CacheTest, TracksCompulsoryVsNonCompulsoryMisses) {
    Cache cache(make_test_config_direct_mapped());
    // 0x000 and 0x100 conflict on set 0
    cache.access(MemoryRequest(0x000, AccessType::Read));  // compulsory miss #1
    cache.access(MemoryRequest(0x100, AccessType::Read));  // compulsory miss #2 (evicts 0x000)
    cache.access(MemoryRequest(0x000, AccessType::Read));  // conflict miss (seen before)

    const auto& stats = cache.get_stats();
    EXPECT_EQ(stats.compulsory_misses, 2u);
    EXPECT_EQ(stats.non_compulsory_misses, 1u);
    EXPECT_EQ(stats.compulsory_misses + stats.non_compulsory_misses, stats.misses);
}

TEST(CacheTest, NextLinePrefetcherEliminatesSequentialMiss) {
    CacheConfig cfg = make_test_config_2way();
    cfg.prefetch_policy = PrefetchPolicy::NextLine;
    cfg.prefetch_degree = 1;
    Cache cache(cfg);

    // Miss on 0x000 triggers prefetch of 0x040 (64-byte line)
    CacheResult r1 = cache.access(MemoryRequest(0x000, AccessType::Read));
    EXPECT_FALSE(r1.hit);
    EXPECT_EQ(cache.get_stats().prefetches_issued, 1u);

    // Access to 0x040 should now hit!
    CacheResult r2 = cache.access(MemoryRequest(0x040, AccessType::Read));
    EXPECT_TRUE(r2.hit);
}

TEST(CacheTest, StridePrefetcherDetectsConstantStride) {
    CacheConfig cfg = make_test_config_fully_assoc();
    cfg.prefetch_policy = PrefetchPolicy::Stride;
    cfg.prefetch_degree = 1;
    Cache cache(cfg);

    // Two misses separated by stride 128 -> establishes stride
    cache.access(MemoryRequest(0x000, AccessType::Read));
    cache.access(MemoryRequest(0x080, AccessType::Read));
    // Third miss at 0x100 confirms stride 128 and prefetches 0x180
    cache.access(MemoryRequest(0x100, AccessType::Read));
    EXPECT_GE(cache.get_stats().prefetches_issued, 1u);

    CacheResult r = cache.access(MemoryRequest(0x180, AccessType::Read));
    EXPECT_TRUE(r.hit);
}
