#pragma once

/// @file dram.hpp
/// @brief DRAM bank, address-mapping, and timing model.
///
/// The hierarchy modelled is:
///
///     DramModel
///       └── channels[]
///             └── ranks[]
///                   └── banks[]  (each a DramBank)
///
/// Each DramBank tracks its row-buffer state and the earliest cycle at
/// which it can accept a new command.  The DramModel coordinates
/// inter-bank timing constraints (tRRD, tFAW) and exposes a single
/// `access()` entry-point consumed by the memory controller / cache.
///
/// Address mapping (row-interleaved):
///
///   MSB ──────────────────────────────────────────── LSB
///   [ row | rank | bank | channel | column | offset ]
///
/// This interleaving distributes consecutive cache-line accesses across
/// channels and banks, maximising bank-level parallelism.

#include "memsim/dram_config.hpp"
#include "memsim/types.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace memsim {

// ---- DRAM-specific enumerations ---------------------------------------------

/// @brief State of an individual DRAM bank.
enum class BankState : uint8_t {
    Idle,          ///< No row is open; bank is precharged and ready.
    Active,        ///< A row is open in the row buffer.
    Precharging,   ///< Bank is closing a row (PRECHARGE in progress).
    Refreshing     ///< Bank is undergoing a refresh cycle.
};

/// @brief Outcome category of a DRAM access relative to the row buffer.
///
/// These three categories drive the latency calculation:
///   - RowHit     — requested row is already open  → fastest
///   - RowMiss    — no row is open, activate needed → moderate
///   - RowConflict — *wrong* row is open, precharge + activate → slowest
enum class DramAccessType : uint8_t {
    RowHit,
    RowMiss,
    RowConflict
};

// ---- Result struct ----------------------------------------------------------

/// @brief Result returned from every DRAM access.
struct DramAccessResult {
    DramAccessType access_type;    ///< Hit / miss / conflict category.
    cycle_t        latency;        ///< Total access latency in cycles.
    cycle_t        completion_cycle; ///< Absolute cycle at which data is ready.
};

// ---- Statistics -------------------------------------------------------------

/// @brief Aggregate statistics collected by the DRAM model.
struct DramStatistics {
    uint64_t total_accesses    = 0;
    uint64_t total_reads       = 0;
    uint64_t total_writes      = 0;
    uint64_t row_hits          = 0;
    uint64_t row_misses        = 0;   ///< Bank was idle (closed-page).
    uint64_t row_conflicts     = 0;   ///< Wrong row open → precharge first.
    uint64_t total_activations = 0;   ///< Number of ACTIVATE commands issued.
    uint64_t total_refreshes   = 0;   ///< Number of periodic REFRESH cycles.

    // Latency accumulators (for computing averages).
    uint64_t total_read_latency  = 0;
    uint64_t total_write_latency = 0;

    /// Analytical energy estimate in nanojoules (nJ).
    double total_energy_nj = 0.0;

    /// @brief Reset all counters to zero.
    void reset() noexcept {
        total_accesses = total_reads = total_writes = 0;
        row_hits = row_misses = row_conflicts = 0;
        total_activations = total_refreshes = 0;
        total_read_latency = total_write_latency = 0;
        total_energy_nj = 0.0;
    }

    /// @brief Average read latency (0 if no reads).
    [[nodiscard]] double avg_read_latency() const noexcept {
        return total_reads > 0
            ? static_cast<double>(total_read_latency) / static_cast<double>(total_reads)
            : 0.0;
    }

    /// @brief Average write latency (0 if no writes).
    [[nodiscard]] double avg_write_latency() const noexcept {
        return total_writes > 0
            ? static_cast<double>(total_write_latency) / static_cast<double>(total_writes)
            : 0.0;
    }

    /// @brief Row-buffer hit rate in [0, 1] (0 if no accesses).
    [[nodiscard]] double row_hit_rate() const noexcept {
        return total_accesses > 0
            ? static_cast<double>(row_hits) / static_cast<double>(total_accesses)
            : 0.0;
    }
};

// ---- Physical address -------------------------------------------------------

/// @brief Decomposed physical address within the DRAM hierarchy.
struct PhysicalAddress {
    uint32_t channel = 0;
    uint32_t rank    = 0;
    uint32_t bank    = 0;
    uint32_t row     = 0;
    uint32_t col     = 0;
};

// ---- AddressMapping ---------------------------------------------------------

/// @brief Maps a flat byte address to { channel, rank, bank, row, column }.
///
/// Mapping scheme (row-interleaved, low-order interleaving):
///
///   MSB ──────────────────────────────────────────────── LSB
///   [ row | rank | bank | channel | column | byte-offset ]
///
/// The byte-offset width is log2(row_size_bytes / num_cols) — i.e. the
/// number of bytes per column "word".  Placing channel and bank bits just
/// above the column bits distributes contiguous cache-line accesses across
/// as many banks / channels as possible, maximising parallelism.
class AddressMapping {
public:
    AddressMapping() = default;

    /// @brief Decompose a byte address into its physical DRAM components.
    ///
    /// @param address  Byte address.
    /// @param config   DRAM organisation (must be validated).
    /// @return Decomposed PhysicalAddress.
    [[nodiscard]] PhysicalAddress map(addr_t address, const DramConfig& config) const noexcept;

private:
    /// Return floor(log2(v)) for power-of-two v.  Undefined for v == 0.
    static constexpr uint32_t log2u(uint32_t v) noexcept {
        uint32_t r = 0;
        while (v >>= 1) ++r;
        return r;
    }
};

// ---- DramBank ---------------------------------------------------------------

/// @brief Models the state of a single DRAM bank and its row buffer.
///
/// The bank tracks:
///   * which row (if any) is currently open,
///   * the earliest cycle at which it can accept the next command,
///   * the time of its last ACTIVATE (used for tRRD / tFAW by DramModel).
class DramBank {
public:
    /// @brief Access this bank for the given row.
    ///
    /// Determines hit / miss / conflict, computes the resulting latency,
    /// and transitions the bank state.  The caller is responsible for
    /// enforcing inter-bank constraints (tRRD, tFAW) *before* calling this.
    ///
    /// @param row            Target row within the bank.
    /// @param type           Read or Write.
    /// @param current_cycle  Current simulator cycle.
    /// @param config         DRAM timing configuration.
    /// @return Access result with latency and completion cycle.
    DramAccessResult access(uint32_t row, AccessType type,
                            cycle_t current_cycle,
                            const DramConfig& config);

    /// @brief Is the given row currently open in the row buffer?
    [[nodiscard]] bool is_row_open(uint32_t row) const noexcept;

    /// @brief Forcibly close the open row (precharge).
    void close_row() noexcept;

    /// @brief Reset bank to its power-on state.
    void reset() noexcept;

    // ---- Accessors (for DramModel inter-bank timing) ----

    [[nodiscard]] BankState state()              const noexcept { return state_; }
    [[nodiscard]] int32_t   open_row_index()     const noexcept { return open_row_; }
    [[nodiscard]] cycle_t   available_at()       const noexcept { return available_at_; }
    [[nodiscard]] cycle_t   last_activate_time() const noexcept { return last_activate_time_; }

private:
    BankState state_              = BankState::Idle;
    int32_t   open_row_           = -1;   ///< -1 ⇒ no row open.
    cycle_t   available_at_       = 0;    ///< Earliest cycle for next command.
    cycle_t   last_activate_time_ = 0;    ///< Cycle of last ACTIVATE issued.
};

// ---- DramModel --------------------------------------------------------------

/// @brief Top-level DRAM timing model.
///
/// Owns the full bank array (channels × ranks × banks) and enforces both
/// per-bank row-buffer semantics and inter-bank timing constraints
/// (tRRD, tFAW).  Provides an `access()` entry-point that accepts a flat
/// byte address and returns the total access latency.
class DramModel {
public:
    /// @brief Construct a DramModel from the given configuration.
    /// @throws std::invalid_argument if config fails validation.
    explicit DramModel(const DramConfig& config);

    /// @brief Process a memory access and return the timing result.
    ///
    /// Steps:
    ///   1. Map address → { channel, rank, bank, row, column }.
    ///   2. Look up the target DramBank.
    ///   3. Determine row hit / miss / conflict.
    ///   4. Enforce tRRD and tFAW constraints for new activations.
    ///   5. Compute latency and update bank state.
    ///   6. Record statistics.
    ///
    /// @param address       Byte address to access.
    /// @param type          Read or Write.
    /// @param current_cycle Current simulator cycle.
    /// @return DramAccessResult with latency and completion cycle.
    DramAccessResult access(addr_t address, AccessType type, cycle_t current_cycle);

    /// @brief Read-only access to collected statistics.
    [[nodiscard]] const DramStatistics& get_stats() const noexcept;

    /// @brief Read-only access to the configuration.
    [[nodiscard]] const DramConfig& get_config() const noexcept;

    /// @brief Check if a specific row is currently open in a given bank.
    /// @param channel  Channel index.
    /// @param rank     Rank index.
    /// @param bank     Bank index.
    /// @param row      Row to check.
    /// @return True if the given row is open in the specified bank.
    [[nodiscard]] bool is_row_open(uint32_t channel, uint32_t rank,
                                    uint32_t bank, uint32_t row) const;

    /// @brief Map a byte address to its physical DRAM components.
    [[nodiscard]] PhysicalAddress map_address(addr_t address) const;

    /// @brief Reset all bank state and statistics.
    void reset();

private:
    /// @brief Retrieve a reference to the bank identified by physical address.
    DramBank& get_bank(const PhysicalAddress& pa);

    /// @brief Enforce tRRD (row-to-row delay) and tFAW (four-activation
    ///        window) constraints.  Returns the earliest cycle at which
    ///        a new ACTIVATE may be issued to the target rank.
    cycle_t apply_activation_constraints(uint32_t channel, uint32_t rank,
                                         cycle_t desired_cycle);

    /// @brief Record an activation event for tFAW tracking.
    void record_activation(uint32_t channel, uint32_t rank, cycle_t cycle);

    DramConfig  config_;
    AddressMapping address_mapping_;
    DramStatistics stats_;

    /// @brief Retrieve a const reference to the bank identified by physical address.
    [[nodiscard]] const DramBank& get_bank_const(const PhysicalAddress& pa) const;

    /// banks_[channel][rank][bank]
    std::vector<std::vector<std::vector<DramBank>>> banks_;

    /// Per-rank recent activation timestamps for tFAW enforcement.
    /// Indexed: activation_history_[channel][rank] → deque of recent cycles.
    std::vector<std::vector<std::deque<cycle_t>>> activation_history_;

    /// Cycle of the last ACTIVATE issued per rank (for tRRD).
    /// last_activate_cycle_[channel][rank]
    std::vector<std::vector<cycle_t>> last_activate_cycle_;

    /// Completion cycle of the last WRITE per rank (for tWTR enforcement).
    std::vector<std::vector<cycle_t>> last_write_cycle_;
    std::vector<std::vector<bool>>    has_written_;

    /// Cycle at which the last periodic REFRESH occurred.
    cycle_t last_refresh_cycle_{0};
};

} // namespace memsim
