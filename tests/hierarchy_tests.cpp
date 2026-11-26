/// @file hierarchy_tests.cpp
/// @brief Unit tests for the multi-level cache hierarchy (CacheHierarchy).
///
/// Tests cover:
///   - L1 hit serves at level 1
///   - L1 miss / L2 hit serves at level 2
///   - All-miss goes to DRAM
///   - Fills propagate back to L1 on lower-level hit
///   - Dirty eviction propagates through levels
///   - Hierarchy with no cache levels (DRAM-only)
///   - Three-level hierarchy
///   - Per-level statistics independence
///   - collect_statistics() aggregation
///   - reset() clears all state

#include <gtest/gtest.h>
#include "memsim/cache_hierarchy.hpp"
#include "memsim/cache_config.hpp"
#include "memsim/dram_config.hpp"
#include "memsim/request.hpp"
#include "memsim/types.hpp"

using namespace memsim;

// ===========================================================================
// Helpers — small deterministic configs for fast tests
// ===========================================================================

/// L1: 256B, 64-byte lines, 2-way → 2 sets, 1-cycle hit
static CacheConfig make_small_l1() {
    CacheConfig cfg;
    cfg.name               = "L1";
    cfg.capacity_bytes     = 256;
    cfg.line_size_bytes    = 64;
    cfg.associativity      = 2;
    cfg.num_sets           = 0;
    cfg.hit_latency_cycles = 1;
    cfg.replacement_policy   = ReplacementPolicy::LRU;
    cfg.write_policy         = WritePolicy::WriteBack;
    cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
    cfg.validate();
    return cfg;
}

/// L2: 512B, 64-byte lines, 4-way → 2 sets, 10-cycle hit
static CacheConfig make_small_l2() {
    CacheConfig cfg;
    cfg.name               = "L2";
    cfg.capacity_bytes     = 512;
    cfg.line_size_bytes    = 64;
    cfg.associativity      = 4;
    cfg.num_sets           = 0;
    cfg.hit_latency_cycles = 10;
    cfg.replacement_policy   = ReplacementPolicy::LRU;
    cfg.write_policy         = WritePolicy::WriteBack;
    cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
    cfg.validate();
    return cfg;
}

/// L3: 1024B, 64-byte lines, 8-way → 2 sets, 40-cycle hit
static CacheConfig make_small_l3() {
    CacheConfig cfg;
    cfg.name               = "L3";
    cfg.capacity_bytes     = 1024;
    cfg.line_size_bytes    = 64;
    cfg.associativity      = 8;
    cfg.num_sets           = 0;
    cfg.hit_latency_cycles = 40;
    cfg.replacement_policy   = ReplacementPolicy::LRU;
    cfg.write_policy         = WritePolicy::WriteBack;
    cfg.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;
    cfg.validate();
    return cfg;
}

/// Simple DRAM config for testing
static DramConfig make_simple_dram() {
    DramConfig cfg;
    cfg.num_channels   = 1;
    cfg.num_ranks      = 1;
    cfg.num_banks      = 4;
    cfg.num_rows       = 64;
    cfg.num_cols       = 64;
    cfg.row_size_bytes = 512;
    cfg.tRCD = 10;
    cfg.tCAS = 10;
    cfg.tRP  = 10;
    cfg.tRAS = 20;
    cfg.tRC  = 30;
    cfg.tRRD = 4;
    cfg.tFAW = 16;
    cfg.tCCD = 4;
    cfg.tWR  = 8;
    cfg.tWTR = 4;
    cfg.tRFC = 100;
    return cfg;
}

// ===========================================================================
// L1 Hit Tests
// ===========================================================================

TEST(HierarchyTest, L1HitServesAtLevel1) {
    std::vector<CacheConfig> configs = {make_small_l1(), make_small_l2()};
    CacheHierarchy hier(configs, make_simple_dram());

    MemoryRequest req(0x100, AccessType::Read);
    hier.access(req);  // compulsory miss

    HierarchyAccessResult r = hier.access(req);  // should hit L1
    EXPECT_EQ(r.served_by_level, 1u);
    EXPECT_FALSE(r.dram_accessed);
}

TEST(HierarchyTest, L1HitLatencyIsL1HitTime) {
    std::vector<CacheConfig> configs = {make_small_l1(), make_small_l2()};
    CacheHierarchy hier(configs, make_simple_dram());

    MemoryRequest req(0x100, AccessType::Read);
    hier.access(req);  // miss

    HierarchyAccessResult r = hier.access(req);  // hit
    EXPECT_EQ(r.total_latency, 1u);  // L1 hit latency
}

// ===========================================================================
// L1 Miss + L2 Hit Tests
// ===========================================================================

TEST(HierarchyTest, L1MissL2HitServesAtLevel2) {
    std::vector<CacheConfig> configs = {make_small_l1(), make_small_l2()};
    CacheHierarchy hier(configs, make_simple_dram());

    MemoryRequest req(0x100, AccessType::Read);

    // First access: compulsory miss everywhere, fills both L1 and L2
    hier.access(req);

    // Evict from L1 but keep in L2.
    // L1 is 2-way, 2 sets. Address 0x100 maps to some set.
    // Fill that set with two other addresses to evict 0x100 from L1.
    uint32_t set_idx = hier.get_cache(0).extract_set_index(0x100);

    // We need addresses that map to the same L1 set but are different tags.
    // L1: 2 sets, 64-byte lines → index from bit 6
    // Set 0: addr bit 6 = 0, Set 1: addr bit 6 = 1
    // Addresses mapping to same set as 0x100 are 0x100+k*128 for k=0,1,2...
    // (stride of 128 = 2 sets * 64 bytes)
    addr_t base = 0x100;
    addr_t stride = 128;  // 2 sets * 64 line_size
    MemoryRequest evict1(base + stride, AccessType::Read);
    MemoryRequest evict2(base + 2 * stride, AccessType::Read);

    hier.access(evict1);  // fills L1 and L2
    hier.access(evict2);  // evicts original 0x100 from L1 (2-way full), L2 (4-way) keeps it

    // Now 0x100 should miss L1 and hit L2
    HierarchyAccessResult r = hier.access(req);
    EXPECT_EQ(r.served_by_level, 2u);
    EXPECT_FALSE(r.dram_accessed);
    EXPECT_EQ(r.total_latency, 1u + 10u);
}

// ===========================================================================
// All-Miss Goes to DRAM
// ===========================================================================

TEST(HierarchyTest, AllCacheMissGoesToDram) {
    std::vector<CacheConfig> configs = {make_small_l1(), make_small_l2()};
    CacheHierarchy hier(configs, make_simple_dram());

    // First access to a new address — compulsory miss at all levels
    MemoryRequest req(0x2000, AccessType::Read);
    HierarchyAccessResult r = hier.access(req);

    EXPECT_EQ(r.served_by_level, 0u);  // DRAM
    EXPECT_TRUE(r.dram_accessed);
}

TEST(HierarchyTest, DramAccessLatencyIncluded) {
    std::vector<CacheConfig> configs = {make_small_l1(), make_small_l2()};
    DramConfig dram_cfg = make_simple_dram();
    CacheHierarchy hier(configs, dram_cfg);

    MemoryRequest req(0x2000, AccessType::Read);
    HierarchyAccessResult r = hier.access(req);

    // Total latency = L1 miss (1) + L2 miss (10) + DRAM (tRCD+tCAS = 20)
    EXPECT_EQ(r.total_latency, 1u + 10u + (dram_cfg.tRCD + dram_cfg.tCAS));
}

// ===========================================================================
// Fill behavior — repeat access should hit L1
// ===========================================================================

TEST(HierarchyTest, L1MissFillsL1OnNextAccess) {
    std::vector<CacheConfig> configs = {make_small_l1(), make_small_l2()};
    CacheHierarchy hier(configs, make_simple_dram());

    MemoryRequest req(0x2000, AccessType::Read);
    hier.access(req);  // compulsory miss, fills L1

    HierarchyAccessResult r = hier.access(req);  // should now hit L1
    EXPECT_EQ(r.served_by_level, 1u);
    EXPECT_FALSE(r.dram_accessed);
}

// ===========================================================================
// Dirty Eviction Propagation
// ===========================================================================

TEST(HierarchyTest, DirtyEvictionPropagates) {
    std::vector<CacheConfig> configs = {make_small_l1(), make_small_l2()};
    CacheHierarchy hier(configs, make_simple_dram());

    // Write to address (creates dirty line in L1)
    addr_t base = 0x000;
    MemoryRequest write_req(base, AccessType::Write);
    hier.access(write_req);

    // Fill L1 set with more addresses to force eviction
    addr_t stride = 128;  // same-set stride for 2-set L1
    hier.access(MemoryRequest(base + stride, AccessType::Write));
    hier.access(MemoryRequest(base + 2 * stride, AccessType::Write));
    // Third write to same set evicts the first dirty line

    // After eviction, the dirty line should have been written to L2
    // L2 stats should show some write accesses from the dirty eviction
    const auto& l2_stats = hier.get_cache(1).get_stats();
    EXPECT_GT(l2_stats.write_accesses, 0u);
}

// ===========================================================================
// No Cache Levels — DRAM only
// ===========================================================================

TEST(HierarchyTest, NoCacheLevelsAccessesDramDirectly) {
    std::vector<CacheConfig> configs;  // empty
    CacheHierarchy hier(configs, make_simple_dram());

    EXPECT_EQ(hier.num_levels(), 0u);

    MemoryRequest req(0x100, AccessType::Read);
    HierarchyAccessResult r = hier.access(req);

    EXPECT_EQ(r.served_by_level, 0u);
    EXPECT_TRUE(r.dram_accessed);
}

// ===========================================================================
// Three-Level Hierarchy
// ===========================================================================

TEST(HierarchyTest, ThreeLevelHierarchyConstructs) {
    std::vector<CacheConfig> configs = {
        make_small_l1(), make_small_l2(), make_small_l3()
    };
    CacheHierarchy hier(configs, make_simple_dram());
    EXPECT_EQ(hier.num_levels(), 3u);
}

TEST(HierarchyTest, ThreeLevelCompulsoryMissGoesToDram) {
    std::vector<CacheConfig> configs = {
        make_small_l1(), make_small_l2(), make_small_l3()
    };
    CacheHierarchy hier(configs, make_simple_dram());

    MemoryRequest req(0x3000, AccessType::Read);
    HierarchyAccessResult r = hier.access(req);
    EXPECT_TRUE(r.dram_accessed);
    EXPECT_EQ(r.served_by_level, 0u);
}

// ===========================================================================
// Per-Level Statistics
// ===========================================================================

TEST(HierarchyTest, StatisticsPerLevel) {
    std::vector<CacheConfig> configs = {make_small_l1(), make_small_l2()};
    CacheHierarchy hier(configs, make_simple_dram());

    MemoryRequest req(0x100, AccessType::Read);
    hier.access(req);  // miss both levels
    hier.access(req);  // hit L1

    const auto& l1_stats = hier.get_cache(0).get_stats();
    EXPECT_EQ(l1_stats.total_accesses, 2u);
    EXPECT_EQ(l1_stats.hits, 1u);
    EXPECT_EQ(l1_stats.misses, 1u);
}

TEST(HierarchyTest, StatisticsInvariant) {
    std::vector<CacheConfig> configs = {make_small_l1(), make_small_l2()};
    CacheHierarchy hier(configs, make_simple_dram());

    // Run some accesses
    for (uint32_t i = 0; i < 50; ++i) {
        addr_t addr = (i % 10) * 64;
        MemoryRequest req(addr, (i % 3 == 0) ? AccessType::Write : AccessType::Read);
        hier.access(req);
    }

    // Check invariants at each level
    for (uint32_t level = 0; level < hier.num_levels(); ++level) {
        const auto& stats = hier.get_cache(level).get_stats();
        EXPECT_EQ(stats.hits + stats.misses, stats.total_accesses)
            << "Level " << level << " invariant violated";
        EXPECT_EQ(stats.read_accesses + stats.write_accesses, stats.total_accesses)
            << "Level " << level << " r/w sum violated";
    }
}

// ===========================================================================
// collect_statistics() Aggregation
// ===========================================================================

TEST(HierarchyTest, CollectStatisticsAggregates) {
    std::vector<CacheConfig> configs = {make_small_l1(), make_small_l2()};
    CacheHierarchy hier(configs, make_simple_dram());

    hier.access(MemoryRequest(0x000, AccessType::Read));
    hier.access(MemoryRequest(0x100, AccessType::Read));
    hier.access(MemoryRequest(0x000, AccessType::Read));  // L1 hit

    SystemStatistics sys = hier.collect_statistics();

    EXPECT_EQ(sys.cache_level_stats.size(), 2u);
    EXPECT_GT(sys.cache_level_stats[0].total_accesses, 0u);
    EXPECT_GT(sys.total_simulated_cycles, 0u);
}

// ===========================================================================
// Reset
// ===========================================================================

TEST(HierarchyTest, ResetClearsAll) {
    std::vector<CacheConfig> configs = {make_small_l1(), make_small_l2()};
    CacheHierarchy hier(configs, make_simple_dram());

    hier.access(MemoryRequest(0x000, AccessType::Read));
    hier.access(MemoryRequest(0x100, AccessType::Write));

    hier.reset();

    // After reset, L1 stats should be zero
    EXPECT_EQ(hier.get_cache(0).get_stats().total_accesses, 0u);
    EXPECT_EQ(hier.get_cache(1).get_stats().total_accesses, 0u);
    EXPECT_EQ(hier.get_dram().get_stats().total_accesses, 0u);
    EXPECT_EQ(hier.get_current_cycle(), 0u);

    // Accesses after reset should be compulsory misses
    HierarchyAccessResult r = hier.access(MemoryRequest(0x000, AccessType::Read));
    EXPECT_TRUE(r.dram_accessed);
}

// ===========================================================================
// Write-Through Hierarchy
// ===========================================================================

TEST(HierarchyTest, WriteThroughPropagatesOnHit) {
    CacheConfig l1 = make_small_l1();
    l1.write_policy = WritePolicy::WriteThrough;
    l1.write_allocate_policy = WriteAllocatePolicy::WriteAllocate;

    std::vector<CacheConfig> configs = {l1, make_small_l2()};
    CacheHierarchy hier(configs, make_simple_dram());

    // Load a line into L1
    hier.access(MemoryRequest(0x100, AccessType::Read));

    // Write to it — write-through should propagate to L2
    hier.access(MemoryRequest(0x100, AccessType::Write));

    const auto& l2_stats = hier.get_cache(1).get_stats();
    EXPECT_GT(l2_stats.write_accesses, 0u)
        << "Write-through should propagate writes to L2";
}

// ===========================================================================
// Access to Cache Level Out of Range
// ===========================================================================

TEST(HierarchyTest, GetCacheOutOfRangeThrows) {
    std::vector<CacheConfig> configs = {make_small_l1()};
    CacheHierarchy hier(configs, make_simple_dram());

    EXPECT_THROW(hier.get_cache(5), std::out_of_range);
}

TEST(HierarchyTest, ControllerTracksDramRequestsAndPolicy) {
    std::vector<CacheConfig> configs = {make_small_l1()};
    CacheHierarchy hier(configs, make_simple_dram(), SchedulingPolicy::FR_FCFS);

    EXPECT_EQ(hier.get_controller().get_policy(), SchedulingPolicy::FR_FCFS);

    hier.access(MemoryRequest(0x100, AccessType::Read));
    hier.access(MemoryRequest(0x200, AccessType::Read));

    SystemStatistics sys = hier.collect_statistics();
    EXPECT_EQ(sys.controller_stats.total_requests, 2u);
    EXPECT_EQ(sys.dram_stats.total_accesses, 2u);
}
