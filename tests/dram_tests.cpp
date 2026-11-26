/// @file dram_tests.cpp
/// @brief Comprehensive unit tests for the DRAM model (bank, address mapping,
///        timing, statistics, configuration validation).

#include <gtest/gtest.h>
#include "memsim/dram.hpp"
#include "memsim/dram_config.hpp"
#include "memsim/types.hpp"

using namespace memsim;

// ===========================================================================
// Helpers — small deterministic configs for quick, predictable tests
// ===========================================================================

/// 1 channel, 1 rank, 4 banks, 64 rows, 64 cols, 512-byte rows.
/// Timing: tRCD=10, tCAS=10, tRP=10, tRAS=20, tRC=30, tWR=8
static DramConfig make_simple_dram_config() {
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
    cfg.tRC  = 30;   // tRAS + tRP
    cfg.tRRD = 4;
    cfg.tFAW = 16;
    cfg.tCCD = 4;
    cfg.tWR  = 8;
    cfg.tWTR = 4;
    cfg.tRFC = 100;

    return cfg;
}

// ===========================================================================
// AddressMapping Tests
// ===========================================================================

TEST(AddressMappingTest, MapDecomposesWithDefaultConfig) {
    DramConfig cfg = make_simple_dram_config();
    cfg.validate();  // sanity

    AddressMapping mapper;

    // With this config:
    //   bytes_per_col = row_size_bytes / num_cols = 512 / 64 = 8
    //   offset_bits   = log2(8)  = 3
    //   col_bits      = log2(64) = 6
    //   chan_bits      = log2(1)  = 0
    //   bank_bits     = log2(4)  = 2
    //   rank_bits     = log2(1)  = 0
    //   row  = remaining upper bits
    //
    //   Layout: [row | rank(0) | bank(2) | channel(0) | col(6) | offset(3)]

    // Address 0 → everything zero.
    PhysicalAddress pa0 = mapper.map(0, cfg);
    EXPECT_EQ(pa0.channel, 0u);
    EXPECT_EQ(pa0.rank,    0u);
    EXPECT_EQ(pa0.bank,    0u);
    EXPECT_EQ(pa0.row,     0u);
    EXPECT_EQ(pa0.col,     0u);

    // Address 8 → offset wraps, col = 1.
    PhysicalAddress pa1 = mapper.map(8, cfg);
    EXPECT_EQ(pa1.col, 1u);
    EXPECT_EQ(pa1.bank, 0u);
    EXPECT_EQ(pa1.row,  0u);

    // Address at the start of bank 1:
    // bank bits start at bit 9 (3 offset + 6 col = 9).
    // bank=1 → address has bit 9 set → addr = 512
    PhysicalAddress pa_bank1 = mapper.map(512, cfg);
    EXPECT_EQ(pa_bank1.bank, 1u);
    EXPECT_EQ(pa_bank1.col,  0u);
    EXPECT_EQ(pa_bank1.row,  0u);

    // Address in row 1, bank 0:
    // row bits start at bit 11 (3 + 6 + 0 + 2 + 0 = 11).
    // row=1 → addr = 1 << 11 = 2048
    PhysicalAddress pa_row1 = mapper.map(2048, cfg);
    EXPECT_EQ(pa_row1.row,  1u);
    EXPECT_EQ(pa_row1.bank, 0u);
}

TEST(AddressMappingTest, MapWithMultipleChannels) {
    DramConfig cfg = make_simple_dram_config();
    cfg.num_channels = 2;
    cfg.validate();

    AddressMapping mapper;

    // With 2 channels: chan_bits = 1.
    // Layout: [row | rank(0) | bank(2) | channel(1) | col(6) | offset(3)]
    // Channel bit at bit 9.
    // Channel 0 → addr bit9 = 0, Channel 1 → addr bit9 = 1.

    PhysicalAddress pa_ch0 = mapper.map(0, cfg);
    EXPECT_EQ(pa_ch0.channel, 0u);

    PhysicalAddress pa_ch1 = mapper.map(512, cfg);
    EXPECT_EQ(pa_ch1.channel, 1u);
    EXPECT_EQ(pa_ch1.col,     0u);
}

// ===========================================================================
// DramBank Tests — Row Miss / Hit / Conflict
// ===========================================================================

TEST(DramBankTest, FirstAccessClosedBankIsRowMiss) {
    DramConfig cfg = make_simple_dram_config();
    DramBank bank;

    DramAccessResult r = bank.access(/*row=*/0, AccessType::Read,
                                     /*current_cycle=*/0, cfg);

    EXPECT_EQ(r.access_type, DramAccessType::RowMiss);
    // Expected latency: tRCD + tCAS = 10 + 10 = 20
    EXPECT_EQ(r.latency, cfg.tRCD + cfg.tCAS);
}

TEST(DramBankTest, RowBufferHit) {
    DramConfig cfg = make_simple_dram_config();
    DramBank bank;

    // First access — opens row 5.
    bank.access(/*row=*/5, AccessType::Read, /*current_cycle=*/0, cfg);

    // Second access to same row — should be a row hit.
    DramAccessResult r = bank.access(/*row=*/5, AccessType::Read,
                                     /*current_cycle=*/100, cfg);

    EXPECT_EQ(r.access_type, DramAccessType::RowHit);
    // Expected latency: tCAS = 10
    EXPECT_EQ(r.latency, cfg.tCAS);
}

TEST(DramBankTest, RowConflict) {
    DramConfig cfg = make_simple_dram_config();
    DramBank bank;

    // First access — opens row 3.
    bank.access(/*row=*/3, AccessType::Read, /*current_cycle=*/0, cfg);

    // Access a *different* row while row 3 is open → conflict.
    DramAccessResult r = bank.access(/*row=*/7, AccessType::Read,
                                     /*current_cycle=*/100, cfg);

    EXPECT_EQ(r.access_type, DramAccessType::RowConflict);
    // Expected latency: tRP + tRCD + tCAS = 10 + 10 + 10 = 30
    EXPECT_EQ(r.latency, cfg.tRP + cfg.tRCD + cfg.tCAS);
}

TEST(DramBankTest, WriteLatencyIncludesTWR) {
    DramConfig cfg = make_simple_dram_config();
    DramBank bank;

    // Row miss + write.
    DramAccessResult r_miss = bank.access(/*row=*/0, AccessType::Write,
                                          /*current_cycle=*/0, cfg);
    EXPECT_EQ(r_miss.access_type, DramAccessType::RowMiss);
    EXPECT_EQ(r_miss.latency, cfg.tRCD + cfg.tCAS + cfg.tWR);

    // Row hit + write.
    DramAccessResult r_hit = bank.access(/*row=*/0, AccessType::Write,
                                         /*current_cycle=*/100, cfg);
    EXPECT_EQ(r_hit.access_type, DramAccessType::RowHit);
    EXPECT_EQ(r_hit.latency, cfg.tCAS + cfg.tWR);

    // Row conflict + write.
    DramAccessResult r_conflict = bank.access(/*row=*/1, AccessType::Write,
                                              /*current_cycle=*/200, cfg);
    EXPECT_EQ(r_conflict.access_type, DramAccessType::RowConflict);
    EXPECT_EQ(r_conflict.latency, cfg.tRP + cfg.tRCD + cfg.tCAS + cfg.tWR);
}

// ===========================================================================
// DramBank — is_row_open / close / reset
// ===========================================================================

TEST(DramBankTest, IsRowOpenAfterAccess) {
    DramConfig cfg = make_simple_dram_config();
    DramBank bank;

    EXPECT_FALSE(bank.is_row_open(0));
    bank.access(0, AccessType::Read, 0, cfg);
    EXPECT_TRUE(bank.is_row_open(0));
    EXPECT_FALSE(bank.is_row_open(1));
}

TEST(DramBankTest, CloseRowSetsIdle) {
    DramConfig cfg = make_simple_dram_config();
    DramBank bank;

    bank.access(0, AccessType::Read, 0, cfg);
    EXPECT_TRUE(bank.is_row_open(0));

    bank.close_row();
    EXPECT_FALSE(bank.is_row_open(0));
    EXPECT_EQ(bank.state(), BankState::Idle);
}

TEST(DramBankTest, ResetSetsIdleAndZeroCycles) {
    DramConfig cfg = make_simple_dram_config();
    DramBank bank;

    bank.access(0, AccessType::Read, 100, cfg);
    bank.reset();

    EXPECT_EQ(bank.state(),              BankState::Idle);
    EXPECT_EQ(bank.open_row_index(),     -1);
    EXPECT_EQ(bank.available_at(),       0u);
    EXPECT_EQ(bank.last_activate_time(), 0u);
}

// ===========================================================================
// DramModel — multiple banks, statistics
// ===========================================================================

TEST(DramModelTest, MultipleBanksDoNotInterfere) {
    DramConfig cfg = make_simple_dram_config();
    DramModel model(cfg);

    // Access addresses that map to different banks.
    // Bank 0: address 0
    // Bank 1: address 512  (bank bits start at bit 9 for our config)
    DramAccessResult r0 = model.access(0,   AccessType::Read, 0);
    DramAccessResult r1 = model.access(512, AccessType::Read, 0);

    // Both should be row-misses (first access to their respective banks).
    EXPECT_EQ(r0.access_type, DramAccessType::RowMiss);
    EXPECT_EQ(r1.access_type, DramAccessType::RowMiss);

    // Re-access the same addresses — should be row hits now.
    DramAccessResult r0_hit = model.access(0,   AccessType::Read, 100);
    DramAccessResult r1_hit = model.access(512, AccessType::Read, 100);
    EXPECT_EQ(r0_hit.access_type, DramAccessType::RowHit);
    EXPECT_EQ(r1_hit.access_type, DramAccessType::RowHit);
}

TEST(DramModelTest, StatisticsCountersCorrect) {
    DramConfig cfg = make_simple_dram_config();
    DramModel model(cfg);

    // Row miss.
    model.access(0, AccessType::Read, 0);
    // Row hit (same row, same bank).
    model.access(8, AccessType::Read, 100);
    // Row conflict (different row, same bank).
    model.access(2048, AccessType::Read, 200);

    const auto& stats = model.get_stats();
    EXPECT_EQ(stats.total_accesses, 3u);
    EXPECT_EQ(stats.row_hits + stats.row_misses + stats.row_conflicts,
              stats.total_accesses);
    EXPECT_EQ(stats.row_hits,      1u);
    EXPECT_EQ(stats.row_misses,    1u);
    EXPECT_EQ(stats.row_conflicts, 1u);
}

TEST(DramModelTest, StatisticsReadWriteCounters) {
    DramConfig cfg = make_simple_dram_config();
    DramModel model(cfg);

    model.access(0,    AccessType::Read,  0);
    model.access(512,  AccessType::Write, 100);
    model.access(1024, AccessType::Read,  200);
    model.access(1536, AccessType::Write, 300);
    model.access(0,    AccessType::Read,  400);

    const auto& stats = model.get_stats();
    EXPECT_EQ(stats.total_reads + stats.total_writes, stats.total_accesses);
    EXPECT_EQ(stats.total_reads,  3u);
    EXPECT_EQ(stats.total_writes, 2u);
}

TEST(DramModelTest, RowHitRate) {
    DramConfig cfg = make_simple_dram_config();
    DramModel model(cfg);

    // No accesses → hit rate 0.
    EXPECT_DOUBLE_EQ(model.get_stats().row_hit_rate(), 0.0);

    // One miss.
    model.access(0, AccessType::Read, 0);
    EXPECT_DOUBLE_EQ(model.get_stats().row_hit_rate(), 0.0);

    // One hit.
    model.access(8, AccessType::Read, 100);
    // 1 hit / 2 total = 0.5
    EXPECT_DOUBLE_EQ(model.get_stats().row_hit_rate(), 0.5);
}

// ===========================================================================
// DramModel — reset
// ===========================================================================

TEST(DramModelTest, ResetClearsEverything) {
    DramConfig cfg = make_simple_dram_config();
    DramModel model(cfg);

    // Generate some activity.
    model.access(0,    AccessType::Read,  0);
    model.access(8,    AccessType::Write, 100);
    model.access(2048, AccessType::Read,  200);

    model.reset();

    const auto& stats = model.get_stats();
    EXPECT_EQ(stats.total_accesses,    0u);
    EXPECT_EQ(stats.total_reads,       0u);
    EXPECT_EQ(stats.total_writes,      0u);
    EXPECT_EQ(stats.row_hits,          0u);
    EXPECT_EQ(stats.row_misses,        0u);
    EXPECT_EQ(stats.row_conflicts,     0u);
    EXPECT_EQ(stats.total_activations, 0u);
    EXPECT_EQ(stats.total_read_latency,  0u);
    EXPECT_EQ(stats.total_write_latency, 0u);

    // Banks should be idle — access is a row miss, not a conflict.
    DramAccessResult r = model.access(0, AccessType::Read, 0);
    EXPECT_EQ(r.access_type, DramAccessType::RowMiss);
}

// ===========================================================================
// DramModel — map_address / is_row_open
// ===========================================================================

TEST(DramModelTest, MapAddressConsistency) {
    DramConfig cfg = make_simple_dram_config();
    DramModel model(cfg);

    PhysicalAddress pa = model.map_address(512);
    EXPECT_EQ(pa.bank, 1u);  // based on our bit layout
    EXPECT_EQ(pa.channel, 0u);
    EXPECT_EQ(pa.rank, 0u);
}

TEST(DramModelTest, IsRowOpenTracksState) {
    DramConfig cfg = make_simple_dram_config();
    DramModel model(cfg);

    PhysicalAddress pa = model.map_address(0);
    EXPECT_FALSE(model.is_row_open(pa.channel, pa.rank, pa.bank, pa.row));

    model.access(0, AccessType::Read, 0);
    EXPECT_TRUE(model.is_row_open(pa.channel, pa.rank, pa.bank, pa.row));
}

// ===========================================================================
// DramConfig — validation tests
// ===========================================================================

TEST(DramConfigTest, DefaultConfigValidates) {
    DramConfig cfg;
    EXPECT_NO_THROW(cfg.validate());
}

TEST(DramConfigTest, DDR4FactoryValidates) {
    DramConfig cfg = DramConfig::make_ddr4_default();
    EXPECT_NO_THROW(cfg.validate());
}

TEST(DramConfigTest, DDR5FactoryValidates) {
    DramConfig cfg = DramConfig::make_ddr5_default();
    EXPECT_NO_THROW(cfg.validate());
}

TEST(DramConfigTest, ZeroBanksThrows) {
    DramConfig cfg;
    cfg.num_banks = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(DramConfigTest, ZeroChannelsThrows) {
    DramConfig cfg;
    cfg.num_channels = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(DramConfigTest, ZeroRanksThrows) {
    DramConfig cfg;
    cfg.num_ranks = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(DramConfigTest, NonPowerOfTwoBanksThrows) {
    DramConfig cfg;
    cfg.num_banks = 3;  // not a power of 2
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(DramConfigTest, NonPowerOfTwoRowsThrows) {
    DramConfig cfg;
    cfg.num_rows = 100;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(DramConfigTest, NonPowerOfTwoChannelsThrows) {
    DramConfig cfg;
    cfg.num_channels = 3;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(DramConfigTest, InconsistentTRCThrows) {
    DramConfig cfg;
    cfg.tRAS = 33;
    cfg.tRP  = 14;
    cfg.tRC  = 10;   // < tRAS + tRP = 47 → invalid
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(DramConfigTest, ZeroTRCDThrows) {
    DramConfig cfg;
    cfg.tRCD = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(DramConfigTest, ZeroTCASThrows) {
    DramConfig cfg;
    cfg.tCAS = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(DramConfigTest, ZeroTRPThrows) {
    DramConfig cfg;
    cfg.tRP = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

// ===========================================================================
// DramStatistics — derived metrics edge cases
// ===========================================================================

TEST(DramStatisticsTest, AvgLatencyZeroWhenNoAccesses) {
    DramStatistics stats;
    EXPECT_DOUBLE_EQ(stats.avg_read_latency(),  0.0);
    EXPECT_DOUBLE_EQ(stats.avg_write_latency(), 0.0);
    EXPECT_DOUBLE_EQ(stats.row_hit_rate(),      0.0);
}

TEST(DramStatisticsTest, ResetClearsAllCounters) {
    DramStatistics stats;
    stats.total_accesses = 10;
    stats.total_reads    = 5;
    stats.total_writes   = 5;
    stats.row_hits       = 3;
    stats.row_misses     = 4;
    stats.row_conflicts  = 3;
    stats.total_activations  = 7;
    stats.total_read_latency = 100;
    stats.total_write_latency = 200;

    stats.reset();

    EXPECT_EQ(stats.total_accesses, 0u);
    EXPECT_EQ(stats.total_reads,    0u);
    EXPECT_EQ(stats.total_writes,   0u);
    EXPECT_EQ(stats.row_hits,       0u);
    EXPECT_EQ(stats.row_misses,     0u);
    EXPECT_EQ(stats.row_conflicts,  0u);
    EXPECT_EQ(stats.total_activations,    0u);
    EXPECT_EQ(stats.total_read_latency,   0u);
    EXPECT_EQ(stats.total_write_latency,  0u);
}

// ===========================================================================
// DramModel — latency accumulation in statistics
// ===========================================================================

TEST(DramModelTest, LatencyAccumulatedInStats) {
    DramConfig cfg = make_simple_dram_config();
    DramModel model(cfg);

    DramAccessResult r = model.access(0, AccessType::Read, 0);
    const auto& stats = model.get_stats();

    EXPECT_EQ(stats.total_read_latency, r.latency);
    EXPECT_EQ(stats.total_write_latency, 0u);
    EXPECT_DOUBLE_EQ(stats.avg_read_latency(),
                     static_cast<double>(r.latency));
}

// ===========================================================================
// DramModel construction with DDR4/DDR5 configs (no crash)
// ===========================================================================

TEST(DramModelTest, ConstructWithDDR4) {
    EXPECT_NO_THROW(DramModel model(DramConfig::make_ddr4_default()));
}

TEST(DramModelTest, ConstructWithDDR5) {
    EXPECT_NO_THROW(DramModel model(DramConfig::make_ddr5_default()));
}

TEST(DramModelTest, ConstructWithInvalidConfigThrows) {
    DramConfig cfg;
    cfg.num_banks = 0;
    EXPECT_THROW(DramModel model(cfg), std::invalid_argument);
}

TEST(DramModelTest, EnforcesWriteToReadTurnaroundTWTR) {
    DramConfig cfg = make_simple_dram_config();
    cfg.tWTR = 12;
    DramModel model(cfg);

    // Write at cycle 0 -> completes at (tRCD + tCAS + tWR) = 28
    DramAccessResult w = model.access(0, AccessType::Write, 0);
    // Subsequent Read at cycle 29 must wait until w.completion_cycle + tWTR = 40
    DramAccessResult r = model.access(8, AccessType::Read, w.completion_cycle + 1);
    EXPECT_GE(r.completion_cycle, w.completion_cycle + cfg.tWTR + r.latency);
}

TEST(DramModelTest, ClosedPagePolicyAutoPrecharges) {
    DramConfig cfg = make_simple_dram_config();
    cfg.page_policy = PagePolicy::ClosedPage;
    DramModel model(cfg);

    // First access opens and auto-closes row 0
    DramAccessResult r1 = model.access(0, AccessType::Read, 0);
    EXPECT_EQ(r1.access_type, DramAccessType::RowMiss);

    // Second access to same row is also a RowMiss (row was closed)
    DramAccessResult r2 = model.access(8, AccessType::Read, 100);
    EXPECT_EQ(r2.access_type, DramAccessType::RowMiss);
    EXPECT_EQ(model.get_stats().row_hits, 0u);
    EXPECT_GT(model.get_stats().total_energy_nj, 0.0);
}

TEST(DramModelTest, PeriodicRefreshTriggersAtTREFI) {
    DramConfig cfg = make_simple_dram_config();
    cfg.enable_refresh = true;
    cfg.tREFI = 1000;
    cfg.tRFC  = 200;
    DramModel model(cfg);

    model.access(0, AccessType::Read, 0);
    EXPECT_EQ(model.get_stats().total_refreshes, 0u);

    // Access past tREFI triggers a refresh + tRFC stall
    DramAccessResult r = model.access(8, AccessType::Read, 1200);
    EXPECT_EQ(model.get_stats().total_refreshes, 1u);
    EXPECT_GE(r.completion_cycle, 1200u + cfg.tRFC);
}

TEST(DramConfigTest, HBM3FactoryValidates) {
    DramConfig cfg = DramConfig::make_hbm3_default();
    EXPECT_NO_THROW(cfg.validate());
}
