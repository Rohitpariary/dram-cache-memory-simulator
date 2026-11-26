/// @file workload_generator.cpp
/// @brief Implementation of synthetic workload generators.

#include "memsim/workload_generator.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
#include <stdexcept>

namespace memsim {

namespace {

/// Determine access type based on read ratio and RNG.
AccessType pick_type(double read_ratio, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return (dist(rng) < read_ratio) ? AccessType::Read : AccessType::Write;
}

/// Align address down to access_size boundary.
addr_t align(addr_t addr, uint32_t access_size) {
    return addr & ~(static_cast<addr_t>(access_size) - 1);
}

} // anonymous namespace

std::vector<MemoryRequest> generate_sequential(const WorkloadConfig& cfg) {
    std::vector<MemoryRequest> reqs;
    reqs.reserve(static_cast<size_t>(cfg.request_count));

    std::mt19937_64 rng(cfg.random_seed);
    addr_t addr = 0;

    for (uint64_t i = 0; i < cfg.request_count; ++i) {
        AccessType type = pick_type(cfg.read_ratio, rng);
        reqs.emplace_back(align(addr, cfg.access_size), type, cfg.access_size);
        addr += cfg.stride;
        if (addr >= cfg.address_space_size) {
            addr = 0;
        }
    }
    return reqs;
}

std::vector<MemoryRequest> generate_random(const WorkloadConfig& cfg) {
    std::vector<MemoryRequest> reqs;
    reqs.reserve(static_cast<size_t>(cfg.request_count));

    std::mt19937_64 rng(cfg.random_seed);
    std::uniform_int_distribution<addr_t> addr_dist(0, cfg.address_space_size - 1);

    for (uint64_t i = 0; i < cfg.request_count; ++i) {
        AccessType type = pick_type(cfg.read_ratio, rng);
        addr_t addr = align(addr_dist(rng), cfg.access_size);
        reqs.emplace_back(addr, type, cfg.access_size);
    }
    return reqs;
}

std::vector<MemoryRequest> generate_strided(const WorkloadConfig& cfg) {
    std::vector<MemoryRequest> reqs;
    reqs.reserve(static_cast<size_t>(cfg.request_count));

    std::mt19937_64 rng(cfg.random_seed);
    addr_t addr = 0;

    for (uint64_t i = 0; i < cfg.request_count; ++i) {
        AccessType type = pick_type(cfg.read_ratio, rng);
        reqs.emplace_back(align(addr, cfg.access_size), type, cfg.access_size);
        addr = (addr + cfg.stride) % cfg.address_space_size;
    }
    return reqs;
}

std::vector<MemoryRequest> generate_hotset(const WorkloadConfig& cfg) {
    std::vector<MemoryRequest> reqs;
    reqs.reserve(static_cast<size_t>(cfg.request_count));

    std::mt19937_64 rng(cfg.random_seed);
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    std::uniform_int_distribution<addr_t> hot_dist(0, cfg.hot_set_size - 1);
    std::uniform_int_distribution<addr_t> cold_dist(cfg.hot_set_size,
                                                      cfg.address_space_size - 1);

    for (uint64_t i = 0; i < cfg.request_count; ++i) {
        AccessType type = pick_type(cfg.read_ratio, rng);
        addr_t addr;
        if (prob_dist(rng) < cfg.hot_set_probability) {
            addr = align(hot_dist(rng), cfg.access_size);
        } else {
            addr = align(cold_dist(rng), cfg.access_size);
        }
        reqs.emplace_back(addr, type, cfg.access_size);
    }
    return reqs;
}

std::vector<MemoryRequest> generate_working_set(const WorkloadConfig& cfg) {
    std::vector<MemoryRequest> reqs;
    reqs.reserve(static_cast<size_t>(cfg.request_count));

    std::mt19937_64 rng(cfg.random_seed);
    addr_t addr = 0;

    for (uint64_t i = 0; i < cfg.request_count; ++i) {
        AccessType type = pick_type(cfg.read_ratio, rng);
        reqs.emplace_back(align(addr, cfg.access_size), type, cfg.access_size);
        addr += cfg.access_size;
        if (addr >= cfg.working_set_size) {
            addr = 0;
        }
    }
    return reqs;
}

std::vector<MemoryRequest> generate_read_heavy(const WorkloadConfig& cfg) {
    WorkloadConfig modified = cfg;
    modified.read_ratio = 0.9;
    return generate_random(modified);
}

std::vector<MemoryRequest> generate_write_heavy(const WorkloadConfig& cfg) {
    WorkloadConfig modified = cfg;
    modified.read_ratio = 0.1;
    return generate_random(modified);
}

std::vector<MemoryRequest> generate_mixed(const WorkloadConfig& cfg) {
    return generate_random(cfg);  // Uses cfg.read_ratio directly.
}

std::vector<MemoryRequest> generate_bank_conflict(const WorkloadConfig& cfg) {
    // Generate addresses that all map to bank 0.
    // Address mapping: [row | rank | bank | channel | column | offset]
    // To target the same bank, we vary the row bits while keeping
    // bank bits constant.
    std::vector<MemoryRequest> reqs;
    reqs.reserve(static_cast<size_t>(cfg.request_count));

    std::mt19937_64 rng(cfg.random_seed);

    // Calculate the stride that changes only the row bits.
    // bank_stride = num_banks * row_size (to jump to different rows in same bank).
    const uint64_t bank_stride = static_cast<uint64_t>(cfg.num_banks) * cfg.row_size;

    for (uint64_t i = 0; i < cfg.request_count; ++i) {
        AccessType type = pick_type(cfg.read_ratio, rng);
        // Different row, same bank: addr = i * bank_stride + small_offset
        addr_t addr = (i % 256) * bank_stride;
        addr = align(addr, cfg.access_size);
        reqs.emplace_back(addr, type, cfg.access_size);
    }
    return reqs;
}

std::vector<MemoryRequest> generate_row_locality(const WorkloadConfig& cfg) {
    // Generate addresses that all map to the same row in the same bank.
    // This maximises row-buffer hits.
    std::vector<MemoryRequest> reqs;
    reqs.reserve(static_cast<size_t>(cfg.request_count));

    std::mt19937_64 rng(cfg.random_seed);
    std::uniform_int_distribution<uint32_t> col_dist(0, cfg.row_size - 1);

    for (uint64_t i = 0; i < cfg.request_count; ++i) {
        AccessType type = pick_type(cfg.read_ratio, rng);
        // Stay within the first row of bank 0: vary only column bits.
        addr_t addr = align(static_cast<addr_t>(col_dist(rng)), cfg.access_size);
        reqs.emplace_back(addr, type, cfg.access_size);
    }
    return reqs;
}

std::vector<MemoryRequest> generate_workload(const std::string& name,
                                              const WorkloadConfig& cfg) {
    if (name == "sequential")     return generate_sequential(cfg);
    if (name == "random")         return generate_random(cfg);
    if (name == "strided")        return generate_strided(cfg);
    if (name == "hotset")         return generate_hotset(cfg);
    if (name == "working_set")    return generate_working_set(cfg);
    if (name == "read_heavy")     return generate_read_heavy(cfg);
    if (name == "write_heavy")    return generate_write_heavy(cfg);
    if (name == "mixed")          return generate_mixed(cfg);
    if (name == "bank_conflict")  return generate_bank_conflict(cfg);
    if (name == "row_locality")   return generate_row_locality(cfg);
    throw std::invalid_argument("Unknown workload type: " + name);
}

void save_trace(const std::vector<MemoryRequest>& requests,
                const std::string& filepath,
                const std::string& comment) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filepath);
    }

    if (!comment.empty()) {
        file << "# " << comment << '\n';
    }
    file << "# Generated trace with " << requests.size() << " requests\n";

    for (const auto& req : requests) {
        file << (req.type == AccessType::Read ? 'R' : 'W')
             << " 0x" << std::hex << std::setfill('0') << std::setw(8)
             << req.address << std::dec;
        if (req.size != 4) {
            file << ' ' << req.size;
        }
        file << '\n';
    }
}

} // namespace memsim
