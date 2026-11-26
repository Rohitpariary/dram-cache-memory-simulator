/// @file trace_loader.cpp
/// @brief Implementation of the trace file parser.

#include "memsim/trace_loader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace memsim {

namespace {

/// Trim leading and trailing whitespace from a string.
std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/// Parse a single line into a MemoryRequest.
/// Returns true on success, fills error message on failure.
bool parse_line(const std::string& line, [[maybe_unused]] uint32_t line_num,
                MemoryRequest& req, std::string& error_msg) {
    std::istringstream iss(line);

    // Read access type.
    std::string type_str;
    if (!(iss >> type_str)) {
        error_msg = "Expected access type (R/W)";
        return false;
    }

    if (type_str == "R" || type_str == "r" || type_str == "Read" || type_str == "read") {
        req.type = AccessType::Read;
    } else if (type_str == "W" || type_str == "w" || type_str == "Write" || type_str == "write") {
        req.type = AccessType::Write;
    } else {
        error_msg = "Unknown access type '" + type_str + "' (expected R or W)";
        return false;
    }

    // Read address.
    std::string addr_str;
    if (!(iss >> addr_str)) {
        error_msg = "Expected address after access type";
        return false;
    }

    try {
        // Detect hex prefix.
        if (addr_str.size() >= 2 &&
            addr_str[0] == '0' &&
            (addr_str[1] == 'x' || addr_str[1] == 'X')) {
            req.address = std::stoull(addr_str, nullptr, 16);
        } else {
            req.address = std::stoull(addr_str, nullptr, 10);
        }
    } catch (const std::exception&) {
        error_msg = "Invalid address '" + addr_str + "'";
        return false;
    }

    // Optionally read size.
    uint32_t size = 4;  // default
    if (iss >> size) {
        if (size == 0) {
            error_msg = "Access size must be > 0";
            return false;
        }
        req.size = size;
    } else {
        req.size = 4;
    }

    req.arrival_time = 0;
    req.completion_time = 0;
    return true;
}

} // anonymous namespace

TraceLoadResult parse_trace(const std::string& content) {
    TraceLoadResult result;
    std::istringstream stream(content);
    std::string line;
    uint32_t line_num = 0;

    while (std::getline(stream, line)) {
        ++line_num;
        ++result.lines_processed;

        std::string trimmed = trim(line);

        // Skip blank lines.
        if (trimmed.empty()) {
            ++result.blank_lines_skipped;
            continue;
        }

        // Skip comments.
        if (trimmed[0] == '#') {
            ++result.comments_skipped;
            continue;
        }

        MemoryRequest req;
        std::string error_msg;

        if (parse_line(trimmed, line_num, req, error_msg)) {
            result.requests.push_back(req);
        } else {
            result.errors.push_back({line_num, error_msg});
        }
    }

    return result;
}

TraceLoadResult load_trace(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        TraceLoadResult result;
        result.errors.push_back({0, "Failed to open file: " + filepath});
        return result;
    }

    // Read entire file into string.
    std::ostringstream ss;
    ss << file.rdbuf();
    return parse_trace(ss.str());
}

} // namespace memsim
