#pragma once

/// @file dram_config.hpp
/// @brief Configuration parameters for the DRAM timing and organization model.
///
/// This file encapsulates the structural and timing parameters of a modern
/// DRAM subsystem (DDR4 / DDR5).  Every timing value is expressed in
/// memory-controller clock cycles so that the simulator remains
/// clock-frequency-agnostic.
///
/// Terminology refresher (active on every access path):
///   ┌──────────┐  tRCD  ┌──────────┐  tCAS  ┌──────┐
///   │ ACTIVATE │──────▶│  COLUMN   │──────▶│ DATA │
///   │ (row)    │       │  ACCESS   │       │  OUT │
///   └──────────┘       └──────────┘       └──────┘
///
///   On a *row conflict* the bank must first PRECHARGE (tRP) before the
///   sequence above can start.

#include <cstdint>
#include <stdexcept>
#include <string>

#include "memsim/types.hpp"

namespace memsim {

// ---------------------------------------------------------------------------
// DramConfig
// ---------------------------------------------------------------------------

/// @brief Complete structural + timing configuration for a DRAM subsystem.
///
/// All timing parameters are in *memory-controller clock cycles*.
/// The `validate()` method performs sanity checks; call it after any
/// manual modification.  Two convenience factories create realistic
/// DDR4-2400 and DDR5-4800 defaults.
struct DramConfig {
    // ---- Organisation ----
    uint32_t num_channels    = 1;       ///< Independent memory channels.
    uint32_t num_ranks       = 1;       ///< Ranks per channel.
    uint32_t num_banks       = 8;       ///< Banks per rank.
    uint32_t num_rows        = 32768;   ///< Rows (pages) per bank.
    uint32_t num_cols        = 1024;    ///< Columns per row.
    uint32_t row_size_bytes  = 8192;    ///< Row-buffer size in bytes (8 KB).

    // ---- Timing (all in cycles) ----

    /// Row-activate → column-access delay (tRCD).
    /// The time between issuing an ACTIVATE command and being allowed to
    /// issue a READ or WRITE command to the now-open row.
    cycle_t tRCD = 14;

    /// Column-access strobe latency (CAS Latency, CL / tCAS).
    /// The time from issuing a READ command until data begins appearing on
    /// the data bus.
    cycle_t tCAS = 14;

    /// Precharge latency (tRP).
    /// The time required to deactivate (close) the currently open row in a
    /// bank before a new row can be activated.
    cycle_t tRP = 14;

    /// Minimum row-active time (tRAS).
    /// The minimum duration a row must remain active after an ACTIVATE
    /// command before a PRECHARGE may be issued.
    cycle_t tRAS = 33;

    /// Row-cycle time (tRC = tRAS + tRP).
    /// Minimum time between successive ACTIVATE commands to the *same* bank.
    cycle_t tRC = 47;

    /// Row-to-row activation delay (tRRD).
    /// Minimum time between ACTIVATE commands to *different* banks in the
    /// same rank.
    cycle_t tRRD = 6;

    /// Four-Activation Window (tFAW).
    /// At most four ACTIVATE commands may be issued within any tFAW-cycle
    /// window to limit peak current draw.
    cycle_t tFAW = 25;

    /// Column-to-column command delay (tCCD).
    /// Minimum gap between consecutive column (READ/WRITE) commands to the
    /// same rank.
    cycle_t tCCD = 4;

    /// Write-recovery time (tWR).
    /// Time after the last data-in before a PRECHARGE may be issued
    /// following a WRITE command.
    cycle_t tWR = 16;

    /// Write-to-read turnaround time (tWTR).
    /// Minimum delay between a WRITE completing and a subsequent READ
    /// command to the same rank.
    cycle_t tWTR = 8;

    /// Refresh cycle time (tRFC).
    /// The time consumed by a single REFRESH command; the bank (or rank) is
    /// unavailable for this duration.
    cycle_t tRFC = 350;

    /// Refresh interval (tREFI) in cycles between periodic auto-refresh commands.
    cycle_t tREFI = 7800;

    /// Enable periodic DRAM refresh modeling (tREFI / tRFC).
    bool enable_refresh = false;

    /// Row-buffer management policy (OpenPage vs ClosedPage).
    PagePolicy page_policy = PagePolicy::OpenPage;

    // ---- Analytical Energy Parameters (pJ per operation, educational DDR4-like) ----
    double energy_activate_pj  = 1200.0;  ///< Energy per ACTIVATE command (pJ)
    double energy_precharge_pj = 800.0;   ///< Energy per PRECHARGE command (pJ)
    double energy_read_pj      = 1500.0;  ///< Energy per column READ (pJ)
    double energy_write_pj     = 1600.0;  ///< Energy per column WRITE (pJ)
    double energy_refresh_pj   = 5000.0;  ///< Energy per REFRESH command (pJ)

    // ---- Validation ----

    /// @brief Validate configuration consistency.
    /// @throws std::invalid_argument on any inconsistency.
    void validate() const {
        if (num_channels == 0)
            throw std::invalid_argument("DramConfig: num_channels must be > 0");
        if (num_ranks == 0)
            throw std::invalid_argument("DramConfig: num_ranks must be > 0");
        if (num_banks == 0)
            throw std::invalid_argument("DramConfig: num_banks must be > 0");
        if (num_rows == 0)
            throw std::invalid_argument("DramConfig: num_rows must be > 0");
        if (num_cols == 0)
            throw std::invalid_argument("DramConfig: num_cols must be > 0");
        if (row_size_bytes == 0)
            throw std::invalid_argument("DramConfig: row_size_bytes must be > 0");

        // Power-of-two checks — needed so bit-field extraction works.
        auto is_power_of_two = [](uint32_t v) { return v != 0 && (v & (v - 1)) == 0; };
        if (!is_power_of_two(num_channels))
            throw std::invalid_argument("DramConfig: num_channels must be a power of 2");
        if (!is_power_of_two(num_ranks))
            throw std::invalid_argument("DramConfig: num_ranks must be a power of 2");
        if (!is_power_of_two(num_banks))
            throw std::invalid_argument("DramConfig: num_banks must be a power of 2");
        if (!is_power_of_two(num_rows))
            throw std::invalid_argument("DramConfig: num_rows must be a power of 2");
        if (!is_power_of_two(num_cols))
            throw std::invalid_argument("DramConfig: num_cols must be a power of 2");
        if (!is_power_of_two(row_size_bytes))
            throw std::invalid_argument("DramConfig: row_size_bytes must be a power of 2");

        // tRC consistency
        if (tRC < tRAS + tRP)
            throw std::invalid_argument(
                "DramConfig: tRC (" + std::to_string(tRC) +
                ") must be >= tRAS + tRP (" + std::to_string(tRAS + tRP) + ")");

        // Basic non-zero checks for critical timings.
        if (tRCD == 0) throw std::invalid_argument("DramConfig: tRCD must be > 0");
        if (tCAS == 0) throw std::invalid_argument("DramConfig: tCAS must be > 0");
        if (tRP  == 0) throw std::invalid_argument("DramConfig: tRP must be > 0");
    }

    // ---- Factories ----

    /// @brief Create a DDR4-2400 (CL17) configuration with sensible defaults.
    ///
    /// Timings correspond roughly to a DDR4-2400 DIMM operating at
    /// 1200 MHz (effective 2400 MT/s).  All values in controller cycles.
    static DramConfig make_ddr4_default() {
        DramConfig cfg;
        cfg.num_channels   = 1;
        cfg.num_ranks      = 2;
        cfg.num_banks      = 16;    // 4 bank groups × 4 banks
        cfg.num_rows       = 32768;
        cfg.num_cols       = 1024;
        cfg.row_size_bytes = 8192;

        cfg.tRCD = 14;
        cfg.tCAS = 14;
        cfg.tRP  = 14;
        cfg.tRAS = 33;
        cfg.tRC  = 47;   // tRAS + tRP
        cfg.tRRD = 6;
        cfg.tFAW = 25;
        cfg.tCCD = 4;
        cfg.tWR  = 16;
        cfg.tWTR = 8;
        cfg.tRFC = 350;

        return cfg;
    }

    /// @brief Create a DDR5-4800 (CL40) configuration with sensible defaults.
    ///
    /// DDR5 roughly doubles bank count and introduces sub-channels.
    /// Timings correspond to a DDR5-4800 DIMM at 2400 MHz base clock.
    static DramConfig make_ddr5_default() {
        DramConfig cfg;
        cfg.num_channels   = 2;     // Two 32-bit sub-channels per DIMM
        cfg.num_ranks      = 1;
        cfg.num_banks      = 32;    // 8 bank groups × 4 banks
        cfg.num_rows       = 65536;
        cfg.num_cols       = 1024;
        cfg.row_size_bytes = 8192;

        cfg.tRCD = 40;
        cfg.tCAS = 40;
        cfg.tRP  = 40;
        cfg.tRAS = 77;
        cfg.tRC  = 117;  // tRAS + tRP
        cfg.tRRD = 8;
        cfg.tFAW = 32;
        cfg.tCCD = 8;
        cfg.tWR  = 48;
        cfg.tWTR = 12;
        cfg.tRFC = 410;

        return cfg;
    }

    /// @brief Create an HBM3-like high-bandwidth stacked DRAM configuration.
    ///
    /// Features many independent channels and banks with lower row-activation
    /// energy per bit compared to off-package DDR4/DDR5 DIMMs.
    static DramConfig make_hbm3_default() {
        DramConfig cfg;
        cfg.num_channels   = 8;
        cfg.num_ranks      = 1;
        cfg.num_banks      = 16;
        cfg.num_rows       = 16384;
        cfg.num_cols       = 1024;
        cfg.row_size_bytes = 2048;

        cfg.tRCD = 16;
        cfg.tCAS = 16;
        cfg.tRP  = 16;
        cfg.tRAS = 32;
        cfg.tRC  = 48;
        cfg.tRRD = 4;
        cfg.tFAW = 16;
        cfg.tCCD = 2;
        cfg.tWR  = 16;
        cfg.tWTR = 6;
        cfg.tRFC = 260;

        cfg.energy_activate_pj  = 450.0;
        cfg.energy_precharge_pj = 300.0;
        cfg.energy_read_pj      = 550.0;
        cfg.energy_write_pj     = 600.0;
        cfg.energy_refresh_pj   = 2200.0;

        return cfg;
    }
};

} // namespace memsim
