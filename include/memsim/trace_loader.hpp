#pragma once

/// @file trace_loader.hpp
/// @brief Parses human-readable memory-trace files into MemoryRequest vectors.
///
/// Supported trace format:
///   - One request per line.
///   - Format: `R|W <hex_or_decimal_address> [size]`
///   - Lines starting with '#' are comments.
///   - Blank lines are ignored.
///   - Hex addresses are prefixed with "0x" or "0X".
///
/// Example trace:
///   # Sequential reads
///   R 0x00000000
///   R 0x00000004
///   W 0x00000040
///   R 0x00002000 8

#include <cstdint>
#include <string>
#include <vector>

#include "memsim/request.hpp"

namespace memsim {

/// @brief Parse error information.
struct TraceError {
    uint32_t line_number{0};
    std::string message;
};

/// @brief Result of loading a trace file.
struct TraceLoadResult {
    std::vector<MemoryRequest> requests;
    std::vector<TraceError> errors;
    uint32_t lines_processed{0};
    uint32_t comments_skipped{0};
    uint32_t blank_lines_skipped{0};

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

/// @brief Load a trace file from disk.
/// @param filepath  Path to the trace file.
/// @return TraceLoadResult with parsed requests and any errors.
TraceLoadResult load_trace(const std::string& filepath);

/// @brief Parse trace content from a string (useful for testing).
/// @param content  Trace content as a string.
/// @return TraceLoadResult with parsed requests and any errors.
TraceLoadResult parse_trace(const std::string& content);

} // namespace memsim
