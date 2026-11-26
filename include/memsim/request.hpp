#pragma once

/// @file request.hpp
/// @brief Defines the MemoryRequest structure that flows through every stage of
///        the simulated memory hierarchy.
///
/// A MemoryRequest is the atomic unit of work in the simulator.  The trace
/// reader produces requests; the cache hierarchy and DRAM controller consume
/// them.  Each request carries its address, access type, size, and timing
/// metadata that is filled in as the request progresses through the pipeline.

#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>

#include "memsim/types.hpp"

namespace memsim {

/// Represents a single memory access flowing through the hierarchy.
///
/// **Block-address helpers**
///
/// In a cache, every address maps to a *block* (also called *line*).  The
/// block address is obtained by stripping the low-order offset bits:
///
///     block_address = address >> log2(block_size)
///
/// For efficiency the helpers here use a simple division + modulo instead of
/// requiring the caller to precompute log2, but the block_size **must** be a
/// power of two (which is validated at runtime).
struct MemoryRequest {

    // ----- data members ---------------------------------------------------

    /// Target physical address of the access.
    addr_t address{0};

    /// Read or Write.
    AccessType type{AccessType::Read};

    /// Number of bytes accessed.  Defaults to 4 (a single 32-bit word), the
    /// most common unit in many trace formats.
    uint32_t size{4};

    /// Simulation cycle at which this request entered the memory hierarchy.
    cycle_t arrival_time{0};

    /// Simulation cycle at which this request was fully serviced.  Filled in
    /// by the hierarchy as the request completes.
    cycle_t completion_time{0};

    // ----- constructors ---------------------------------------------------

    MemoryRequest() = default;

    /// Convenience constructor for the most common usage pattern.
    MemoryRequest(addr_t addr, AccessType acc, uint32_t sz = 4,
                  cycle_t arrive = 0) noexcept
        : address{addr}, type{acc}, size{sz}, arrival_time{arrive} {}

    // ----- helper methods -------------------------------------------------

    /// Return the cache-block-aligned address for a given @p block_size.
    ///
    /// The block size **must** be a power of two; this is validated at
    /// runtime.  The block address is simply:
    ///     address & ~(block_size - 1)
    ///
    /// @param block_size  Cache line size in bytes (must be a power of 2).
    /// @return            The address with offset bits zeroed out.
    /// @throws std::invalid_argument if @p block_size is zero or not a
    ///                               power of two.
    [[nodiscard]] addr_t get_block_address(uint32_t block_size) const {
        validate_block_size(block_size);
        return address & ~(static_cast<addr_t>(block_size) - 1);
    }

    /// Return the byte offset within the cache block.
    ///
    /// @param block_size  Cache line size in bytes (must be a power of 2).
    /// @return            `address % block_size`, i.e. the low-order bits.
    /// @throws std::invalid_argument if @p block_size is zero or not a
    ///                               power of two.
    [[nodiscard]] uint32_t get_offset(uint32_t block_size) const {
        validate_block_size(block_size);
        return static_cast<uint32_t>(address & (static_cast<addr_t>(block_size) - 1));
    }

    /// Compute the latency (in cycles) that this request spent inside the
    /// memory hierarchy.  Returns 0 if completion_time has not been set yet
    /// or if completion_time < arrival_time (should not happen in a correct
    /// simulation but we guard defensively).
    [[nodiscard]] cycle_t latency() const noexcept {
        if (completion_time >= arrival_time) {
            return completion_time - arrival_time;
        }
        return 0;
    }

    /// Equality comparison — useful in unit tests.
    [[nodiscard]] bool operator==(const MemoryRequest& other) const noexcept {
        return address         == other.address
            && type            == other.type
            && size            == other.size
            && arrival_time    == other.arrival_time
            && completion_time == other.completion_time;
    }

    [[nodiscard]] bool operator!=(const MemoryRequest& other) const noexcept {
        return !(*this == other);
    }

private:
    /// Validate that a block size is a non-zero power of two.
    static void validate_block_size(uint32_t block_size) {
        if (block_size == 0) {
            throw std::invalid_argument("block_size must be non-zero");
        }
        // A power of two has exactly one bit set: (n & (n-1)) == 0.
        if ((block_size & (block_size - 1)) != 0) {
            throw std::invalid_argument(
                "block_size must be a power of two, got " +
                std::to_string(block_size));
        }
    }
};

// ---------------------------------------------------------------------------
// Stream insertion — handy for logging and debugging
// ---------------------------------------------------------------------------

/// Pretty-print a MemoryRequest to an output stream.
inline std::ostream& operator<<(std::ostream& os, const MemoryRequest& req) {
    os << "MemoryRequest{"
       << "addr=0x" << std::hex << req.address << std::dec
       << ", type=" << to_string(req.type)
       << ", size=" << req.size
       << ", arrive=" << req.arrival_time
       << ", complete=" << req.completion_time
       << '}';
    return os;
}

}  // namespace memsim
