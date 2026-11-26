/// @file dram.cpp
/// @brief Implementation of the DRAM bank, address-mapping, and timing model.
///
/// See dram.hpp for the architectural overview and class documentation.

#include "memsim/dram.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace memsim {

// ===========================================================================
// AddressMapping
// ===========================================================================

PhysicalAddress AddressMapping::map(addr_t address,
                                    const DramConfig& config) const noexcept {
    // -----------------------------------------------------------------------
    //  Row-interleaved mapping (low-order interleaving)
    //
    //  Bit-field layout (LSB → MSB):
    //
    //    [byte-offset | column | channel | bank | rank | row]
    //
    //  * byte_offset_bits = log2(row_size_bytes / num_cols)
    //        → selects a byte within a single column word.
    //  * col_bits   = log2(num_cols)
    //  * chan_bits   = log2(num_channels)
    //  * bank_bits  = log2(num_banks)
    //  * rank_bits  = log2(num_ranks)
    //  * row_bits   = log2(num_rows)   (remaining upper bits)
    //
    //  Placing channel and bank bits close to the LSB ensures that
    //  sequential cache-line addresses land in different banks/channels,
    //  maximising bank-level parallelism for streaming workloads.
    // -----------------------------------------------------------------------

    const uint32_t bytes_per_col    = config.row_size_bytes / config.num_cols;
    const uint32_t offset_bits      = log2u(bytes_per_col);
    const uint32_t col_bits         = log2u(config.num_cols);
    const uint32_t chan_bits         = log2u(config.num_channels);
    const uint32_t bank_bits        = log2u(config.num_banks);
    const uint32_t rank_bits        = log2u(config.num_ranks);
    // row_bits is implicitly whatever remains.

    addr_t a = address;

    // Shift out the byte-offset within a column word.
    a >>= offset_bits;

    PhysicalAddress pa{};

    // Column index.
    pa.col = static_cast<uint32_t>(a & ((1u << col_bits) - 1));
    a >>= col_bits;

    // Channel index.
    pa.channel = static_cast<uint32_t>(a & ((1u << chan_bits) - 1));
    a >>= chan_bits;

    // Bank index.
    pa.bank = static_cast<uint32_t>(a & ((1u << bank_bits) - 1));
    a >>= bank_bits;

    // Rank index.
    pa.rank = static_cast<uint32_t>(a & ((1u << rank_bits) - 1));
    a >>= rank_bits;

    // Row index — all remaining upper bits.
    pa.row = static_cast<uint32_t>(a & (config.num_rows - 1));

    return pa;
}

// ===========================================================================
// DramBank
// ===========================================================================

DramAccessResult DramBank::access(uint32_t row,
                                  AccessType type,
                                  cycle_t current_cycle,
                                  const DramConfig& config) {
    // The effective start cycle is the later of "now" and "when the bank
    // is actually free" — commands cannot be issued to a busy bank.
    cycle_t start = std::max(current_cycle, available_at_);

    DramAccessResult result{};

    if (state_ == BankState::Active && open_row_ == static_cast<int32_t>(row)) {
        // ----- ROW HIT -----
        // The requested row is already in the row buffer.  We only need
        // the column-access (CAS) latency.
        result.access_type = DramAccessType::RowHit;
        result.latency     = config.tCAS;

        if (type == AccessType::Write) {
            // Writes additionally incur tWR (write-recovery) before the
            // bank can accept another command.
            result.latency += config.tWR;
        }

        result.completion_cycle = start + result.latency;

        // Bank remains Active with the same row open.
        available_at_ = start + config.tCCD; // enforce tCCD for back-to-back column cmds.

    } else if (state_ == BankState::Idle || state_ == BankState::Precharging) {
        // ----- ROW MISS (closed page) -----
        // No row is open (or precharge just completed).  We must issue an
        // ACTIVATE followed by a CAS.
        //
        // If the bank is still precharging we wait until it finishes.
        result.access_type = DramAccessType::RowMiss;

        // Latency: ACTIVATE (tRCD) + CAS (tCAS).
        result.latency = config.tRCD + config.tCAS;

        if (type == AccessType::Write) {
            result.latency += config.tWR;
        }

        result.completion_cycle = start + result.latency;

        // Bank is now Active with the new row open.
        state_              = BankState::Active;
        open_row_           = static_cast<int32_t>(row);
        last_activate_time_ = start;
        available_at_       = start + config.tRCD; // column cmd only after tRCD

    } else {
        // ----- ROW CONFLICT -----
        // A *different* row is open.  We must PRECHARGE the current row,
        // then ACTIVATE the new row, then issue the CAS.
        assert(state_ == BankState::Active || state_ == BankState::Refreshing);

        result.access_type = DramAccessType::RowConflict;

        // Enforce tRAS: the open row must have been active for at least
        // tRAS cycles before we may precharge.
        const cycle_t earliest_precharge = last_activate_time_ + config.tRAS;
        start = std::max(start, earliest_precharge);

        // Latency: PRECHARGE (tRP) + ACTIVATE (tRCD) + CAS (tCAS).
        result.latency = config.tRP + config.tRCD + config.tCAS;

        if (type == AccessType::Write) {
            result.latency += config.tWR;
        }

        result.completion_cycle = start + result.latency;

        // Bank transitions through Precharging → Active.
        state_              = BankState::Active;
        open_row_           = static_cast<int32_t>(row);
        last_activate_time_ = start + config.tRP; // ACTIVATE fires after precharge
        available_at_       = start + config.tRP + config.tRCD;
    }

    // Closed-page policy auto-precharges the row immediately after the column access.
    if (config.page_policy == PagePolicy::ClosedPage) {
        state_    = BankState::Idle;
        open_row_ = -1;
        available_at_ = std::max(available_at_, result.completion_cycle + config.tRP);
    }

    return result;
}

bool DramBank::is_row_open(uint32_t row) const noexcept {
    return (state_ == BankState::Active) &&
           (open_row_ == static_cast<int32_t>(row));
}

void DramBank::close_row() noexcept {
    state_    = BankState::Idle;
    open_row_ = -1;
}

void DramBank::reset() noexcept {
    state_              = BankState::Idle;
    open_row_           = -1;
    available_at_       = 0;
    last_activate_time_ = 0;
}

// ===========================================================================
// DramModel
// ===========================================================================

DramModel::DramModel(const DramConfig& config)
    : config_(config)
{
    config_.validate();

    // Allocate the bank array: channels × ranks × banks.
    banks_.resize(config_.num_channels);
    for (auto& rank_vec : banks_) {
        rank_vec.resize(config_.num_ranks);
        for (auto& bank_vec : rank_vec) {
            bank_vec.resize(config_.num_banks);
        }
    }

    // Allocate per-rank activation-tracking and write-to-read turnaround structures.
    activation_history_.resize(config_.num_channels);
    last_activate_cycle_.resize(config_.num_channels);
    last_write_cycle_.resize(config_.num_channels);
    has_written_.resize(config_.num_channels);
    for (uint32_t ch = 0; ch < config_.num_channels; ++ch) {
        activation_history_[ch].resize(config_.num_ranks);
        last_activate_cycle_[ch].resize(config_.num_ranks, 0);
        last_write_cycle_[ch].resize(config_.num_ranks, 0);
        has_written_[ch].resize(config_.num_ranks, false);
    }
}

DramAccessResult DramModel::access(addr_t address,
                                   AccessType type,
                                   cycle_t current_cycle) {
    // 1. Map address to physical location.
    const PhysicalAddress pa = address_mapping_.map(address, config_);

    // 2. Get the target bank.
    DramBank& bank = get_bank(pa);

    // 3. Determine whether this will be a row hit, miss, or conflict
    //    *before* modifying state so we can enforce inter-bank constraints
    //    only when an ACTIVATE is needed.
    const bool needs_activate =
        !(bank.state() == BankState::Active &&
          bank.open_row_index() == static_cast<int32_t>(pa.row));

    // 4. Enforce tRRD / tFAW for new activations, tWTR for Read-after-Write,
    //    and periodic refresh (tREFI / tRFC) when enabled.
    cycle_t effective_cycle = current_cycle;
    if (config_.enable_refresh && config_.tREFI > 0 &&
        current_cycle >= last_refresh_cycle_ + config_.tREFI) {
        last_refresh_cycle_ = current_cycle;
        effective_cycle = std::max(effective_cycle, current_cycle + config_.tRFC);
        ++stats_.total_refreshes;
        stats_.total_energy_nj += config_.energy_refresh_pj / 1000.0;
    }
    if (needs_activate) {
        effective_cycle = apply_activation_constraints(
            pa.channel, pa.rank, effective_cycle);
    }
    if (type == AccessType::Read && has_written_[pa.channel][pa.rank]) {
        effective_cycle = std::max(
            effective_cycle,
            last_write_cycle_[pa.channel][pa.rank] + config_.tWTR);
    }

    // 5. Perform the bank-level access (computes latency, updates bank).
    DramAccessResult result = bank.access(pa.row, type, effective_cycle, config_);

    if (type == AccessType::Write) {
        last_write_cycle_[pa.channel][pa.rank] = result.completion_cycle;
        has_written_[pa.channel][pa.rank] = true;
    }

    // 6. If an ACTIVATE was issued, record it for tFAW tracking and
    //    update the last-activate time for tRRD.
    if (needs_activate) {
        const cycle_t act_time = bank.last_activate_time();
        record_activation(pa.channel, pa.rank, act_time);
        last_activate_cycle_[pa.channel][pa.rank] = act_time;
        ++stats_.total_activations;
    }

    // 7. Record statistics.
    ++stats_.total_accesses;

    switch (result.access_type) {
        case DramAccessType::RowHit:
            ++stats_.row_hits;
            break;
        case DramAccessType::RowMiss:
            ++stats_.row_misses;
            stats_.total_energy_nj += config_.energy_activate_pj / 1000.0;
            break;
        case DramAccessType::RowConflict:
            ++stats_.row_conflicts;
            stats_.total_energy_nj +=
                (config_.energy_precharge_pj + config_.energy_activate_pj) / 1000.0;
            break;
    }

    if (config_.page_policy == PagePolicy::ClosedPage) {
        stats_.total_energy_nj += config_.energy_precharge_pj / 1000.0;
    }

    if (type == AccessType::Read) {
        ++stats_.total_reads;
        stats_.total_read_latency += result.latency;
        stats_.total_energy_nj += config_.energy_read_pj / 1000.0;
    } else {
        ++stats_.total_writes;
        stats_.total_write_latency += result.latency;
        stats_.total_energy_nj += config_.energy_write_pj / 1000.0;
    }

    return result;
}

const DramStatistics& DramModel::get_stats() const noexcept {
    return stats_;
}

const DramConfig& DramModel::get_config() const noexcept {
    return config_;
}

bool DramModel::is_row_open(uint32_t channel, uint32_t rank,
                             uint32_t bank, uint32_t row) const {
    if (channel >= config_.num_channels || rank >= config_.num_ranks ||
        bank >= config_.num_banks) {
        return false;
    }
    return banks_[channel][rank][bank].is_row_open(row);
}

PhysicalAddress DramModel::map_address(addr_t address) const {
    return address_mapping_.map(address, config_);
}

void DramModel::reset() {
    stats_.reset();
    for (auto& rank_vec : banks_)
        for (auto& bank_vec : rank_vec)
            for (auto& bank : bank_vec)
                bank.reset();

    for (auto& rank_vec : activation_history_)
        for (auto& deq : rank_vec)
            deq.clear();

    for (auto& rank_vec : last_activate_cycle_)
        std::fill(rank_vec.begin(), rank_vec.end(), cycle_t{0});

    for (auto& rank_vec : last_write_cycle_)
        std::fill(rank_vec.begin(), rank_vec.end(), cycle_t{0});

    for (auto& rank_vec : has_written_)
        std::fill(rank_vec.begin(), rank_vec.end(), false);

    last_refresh_cycle_ = 0;
}

// ---- Private helpers --------------------------------------------------------

DramBank& DramModel::get_bank(const PhysicalAddress& pa) {
    assert(pa.channel < config_.num_channels);
    assert(pa.rank    < config_.num_ranks);
    assert(pa.bank    < config_.num_banks);
    return banks_[pa.channel][pa.rank][pa.bank];
}

const DramBank& DramModel::get_bank_const(const PhysicalAddress& pa) const {
    assert(pa.channel < config_.num_channels);
    assert(pa.rank    < config_.num_ranks);
    assert(pa.bank    < config_.num_banks);
    return banks_[pa.channel][pa.rank][pa.bank];
}

cycle_t DramModel::apply_activation_constraints(uint32_t channel,
                                                 uint32_t rank,
                                                 cycle_t desired_cycle) {
    cycle_t earliest = desired_cycle;

    // ---- tRRD: row-to-row activation delay (same rank) ----
    // An ACTIVATE to any bank in this rank must be at least tRRD cycles
    // after the previous ACTIVATE to the same rank.
    const cycle_t last_act = last_activate_cycle_[channel][rank];
    if (last_act > 0) {
        earliest = std::max(earliest, last_act + config_.tRRD);
    }

    // ---- tFAW: four-activation window ----
    // At most 4 ACTIVATEs within any tFAW-cycle window.
    auto& history = activation_history_[channel][rank];

    // Evict activations that have fallen outside the tFAW window.
    while (!history.empty() && history.front() + config_.tFAW <= desired_cycle) {
        history.pop_front();
    }

    if (history.size() >= 4) {
        // The 5th ACTIVATE must wait until the oldest one exits the window.
        earliest = std::max(earliest, history.front() + config_.tFAW);
    }

    return earliest;
}

void DramModel::record_activation(uint32_t channel, uint32_t rank,
                                  cycle_t cycle) {
    auto& history = activation_history_[channel][rank];
    history.push_back(cycle);

    // Keep the deque bounded — we only ever need the last 4.
    while (history.size() > 4) {
        history.pop_front();
    }
}

} // namespace memsim
