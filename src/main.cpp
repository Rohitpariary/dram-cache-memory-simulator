/// @file main.cpp
/// @brief Command-line interface for the DRAM & Cache Memory Simulator.
///
/// Usage examples:
///   memory_sim --trace workloads/sequential.trace --l1-size 16384 --l1-assoc 4
///   memory_sim --workload random --count 50000 --l1-size 32768 --csv results/out.csv
///   memory_sim --help

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "memsim/cache_config.hpp"
#include "memsim/dram_config.hpp"
#include "memsim/simulator.hpp"
#include "memsim/system_statistics.hpp"
#include "memsim/types.hpp"

namespace {

// =========================================================================
// Minimal command-line parser (no external dependencies)
// =========================================================================

struct CLIArgs {
    // ---- Trace / Workload ----
    std::string trace_file;
    std::string workload_type;
    uint64_t request_count     = 10000;
    uint64_t address_space     = 1 << 20;
    uint64_t stride            = 64;
    double   read_ratio        = 0.8;
    uint64_t random_seed       = 42;
    uint64_t working_set_size  = 4096;

    // ---- L1 Cache ----
    uint32_t l1_size           = 32768;   // 32 KB
    uint32_t l1_assoc          = 8;
    uint32_t l1_line_size      = 64;
    uint64_t l1_hit_latency    = 1;
    std::string l1_replacement = "lru";
    std::string l1_write       = "wb";
    std::string l1_alloc       = "wa";
    std::string prefetch       = "none";
    uint32_t prefetch_degree   = 1;

    // ---- L2 Cache ----
    uint32_t l2_size           = 262144;  // 256 KB
    uint32_t l2_assoc          = 8;
    uint64_t l2_hit_latency    = 10;
    std::string l2_replacement = "lru";
    std::string l2_write       = "wb";
    std::string l2_alloc       = "wa";
    bool     l2_enabled        = true;

    // ---- L3 Cache ----
    uint32_t l3_size           = 0;       // 0 = disabled
    uint32_t l3_assoc          = 16;
    uint64_t l3_hit_latency    = 40;

    // ---- DRAM ----
    uint32_t dram_banks        = 8;
    uint32_t dram_rows         = 32768;
    uint32_t dram_row_size     = 8192;
    uint64_t dram_tRCD         = 14;
    uint64_t dram_tCAS         = 14;
    uint64_t dram_tRP          = 14;
    std::string dram_policy    = "fcfs";
    std::string dram_page      = "open";
    std::string dram_preset    = "";
    bool     dram_refresh      = false;

    // ---- Output ----
    std::string csv_output;
    bool verbose               = false;
    bool help                  = false;
};

void print_help() {
    std::cout << R"(
DRAM & Cache Memory Simulator v1.0
===================================

Usage: memory_sim [options]

Workload Options:
  --trace <file>          Load trace from file
  --workload <type>       Synthetic workload: sequential, random, strided,
                          hotset, working_set, read_heavy, write_heavy,
                          mixed, bank_conflict, row_locality
  --count <n>             Number of requests (default: 10000)
  --address-space <n>     Address space size in bytes (default: 1048576)
  --stride <n>            Access stride (default: 64)
  --read-ratio <f>        Read fraction 0.0-1.0 (default: 0.8)
  --seed <n>              Random seed (default: 42)
  --working-set <n>       Working set size in bytes (default: 4096)

L1 Cache Options:
  --l1-size <n>           L1 capacity in bytes (default: 32768)
  --l1-assoc <n>          L1 associativity (default: 8)
  --l1-line-size <n>      Cache line size in bytes (default: 64)
  --l1-latency <n>        L1 hit latency in cycles (default: 1)
  --replacement <policy>  Replacement: lru, fifo, random (default: lru)
  --write-policy <p>      Write policy: wb (write-back), wt (write-through)
  --alloc-policy <p>      Allocate: wa (write-allocate), nwa (no-write-allocate)
  --prefetch <p>          Prefetcher: none, next_line, stride (default: none)
  --prefetch-degree <n>   Lines to prefetch per trigger (default: 1)

L2 Cache Options:
  --l2-size <n>           L2 capacity in bytes (default: 262144)
  --l2-assoc <n>          L2 associativity (default: 8)
  --l2-latency <n>        L2 hit latency in cycles (default: 10)
  --no-l2                 Disable L2 cache

L3 Cache Options:
  --l3-size <n>           L3 capacity in bytes (default: disabled)
  --l3-assoc <n>          L3 associativity (default: 16)
  --l3-latency <n>        L3 hit latency in cycles (default: 40)

DRAM Options:
  --dram-banks <n>        Number of banks (default: 8)
  --dram-rows <n>         Rows per bank (default: 32768)
  --dram-row-size <n>     Row buffer size in bytes (default: 8192)
  --dram-tRCD <n>         Row activate to column access (default: 14)
  --dram-tCAS <n>         Column access latency (default: 14)
  --dram-tRP <n>          Precharge latency (default: 14)
  --dram-policy <p>       Controller scheduling: fcfs, fr_fcfs (default: fcfs)
  --dram-page-policy <p>  Row-buffer policy: open, closed (default: open)
  --dram-preset <p>       Preset: ddr4, ddr5, hbm3 (overrides DRAM defaults)
  --dram-refresh          Enable periodic DRAM refresh (tREFI/tRFC)

Output Options:
  --csv <file>            Write results to CSV file
  --verbose               Enable verbose output
  --help                  Show this help message
)";
}

bool parse_args(int argc, char* argv[], CLIArgs& args) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto next_str = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a value\n";
                return "";
            }
            return argv[++i];
        };
        auto next_uint32 = [&]() -> uint32_t {
            return static_cast<uint32_t>(std::stoul(next_str()));
        };
        auto next_uint64 = [&]() -> uint64_t {
            return std::stoull(next_str());
        };
        auto next_double = [&]() -> double {
            return std::stod(next_str());
        };

        try {
            if (arg == "--help" || arg == "-h") {
                args.help = true;
            } else if (arg == "--trace") {
                args.trace_file = next_str();
            } else if (arg == "--workload") {
                args.workload_type = next_str();
            } else if (arg == "--count") {
                args.request_count = next_uint64();
            } else if (arg == "--address-space") {
                args.address_space = next_uint64();
            } else if (arg == "--stride") {
                args.stride = next_uint64();
            } else if (arg == "--read-ratio") {
                args.read_ratio = next_double();
            } else if (arg == "--seed") {
                args.random_seed = next_uint64();
            } else if (arg == "--working-set") {
                args.working_set_size = next_uint64();
            } else if (arg == "--l1-size") {
                args.l1_size = next_uint32();
            } else if (arg == "--l1-assoc") {
                args.l1_assoc = next_uint32();
            } else if (arg == "--l1-line-size") {
                args.l1_line_size = next_uint32();
            } else if (arg == "--l1-latency") {
                args.l1_hit_latency = next_uint64();
            } else if (arg == "--replacement") {
                args.l1_replacement = next_str();
                args.l2_replacement = args.l1_replacement;
            } else if (arg == "--write-policy") {
                args.l1_write = next_str();
                args.l2_write = args.l1_write;
            } else if (arg == "--alloc-policy") {
                args.l1_alloc = next_str();
                args.l2_alloc = args.l1_alloc;
            } else if (arg == "--prefetch") {
                args.prefetch = next_str();
            } else if (arg == "--prefetch-degree") {
                args.prefetch_degree = next_uint32();
            } else if (arg == "--l2-size") {
                args.l2_size = next_uint32();
            } else if (arg == "--l2-assoc") {
                args.l2_assoc = next_uint32();
            } else if (arg == "--l2-latency") {
                args.l2_hit_latency = next_uint64();
            } else if (arg == "--no-l2") {
                args.l2_enabled = false;
            } else if (arg == "--l3-size") {
                args.l3_size = next_uint32();
            } else if (arg == "--l3-assoc") {
                args.l3_assoc = next_uint32();
            } else if (arg == "--l3-latency") {
                args.l3_hit_latency = next_uint64();
            } else if (arg == "--dram-banks") {
                args.dram_banks = next_uint32();
            } else if (arg == "--dram-rows") {
                args.dram_rows = next_uint32();
            } else if (arg == "--dram-row-size") {
                args.dram_row_size = next_uint32();
            } else if (arg == "--dram-tRCD") {
                args.dram_tRCD = next_uint64();
            } else if (arg == "--dram-tCAS") {
                args.dram_tCAS = next_uint64();
            } else if (arg == "--dram-tRP") {
                args.dram_tRP = next_uint64();
            } else if (arg == "--dram-policy") {
                args.dram_policy = next_str();
            } else if (arg == "--dram-page-policy") {
                args.dram_page = next_str();
            } else if (arg == "--dram-preset") {
                args.dram_preset = next_str();
            } else if (arg == "--dram-refresh") {
                args.dram_refresh = true;
            } else if (arg == "--csv") {
                args.csv_output = next_str();
            } else if (arg == "--verbose" || arg == "-v") {
                args.verbose = true;
            } else {
                std::cerr << "Unknown argument: " << arg << '\n';
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing " << arg << ": " << e.what() << '\n';
            return false;
        }
    }
    return true;
}

memsim::SimulatorConfig build_config(const CLIArgs& args) {
    memsim::SimulatorConfig cfg;

    // L1 Cache
    memsim::CacheConfig l1;
    l1.name               = "L1";
    l1.capacity_bytes     = args.l1_size;
    l1.line_size_bytes    = args.l1_line_size;
    l1.associativity      = args.l1_assoc;
    l1.num_sets           = 0;  // auto-compute
    l1.hit_latency_cycles = args.l1_hit_latency;
    l1.replacement_policy   = memsim::replacement_policy_from_string(args.l1_replacement);
    l1.write_policy         = memsim::write_policy_from_string(args.l1_write);
    l1.write_allocate_policy = memsim::write_allocate_policy_from_string(args.l1_alloc);
    l1.prefetch_policy      = memsim::prefetch_policy_from_string(args.prefetch);
    l1.prefetch_degree      = args.prefetch_degree;
    l1.validate();
    cfg.cache_configs.push_back(l1);

    // L2 Cache (optional)
    if (args.l2_enabled && args.l2_size > 0) {
        memsim::CacheConfig l2;
        l2.name               = "L2";
        l2.capacity_bytes     = args.l2_size;
        l2.line_size_bytes    = args.l1_line_size;  // same line size
        l2.associativity      = args.l2_assoc;
        l2.num_sets           = 0;
        l2.hit_latency_cycles = args.l2_hit_latency;
        l2.replacement_policy   = memsim::replacement_policy_from_string(args.l2_replacement);
        l2.write_policy         = memsim::write_policy_from_string(args.l2_write);
        l2.write_allocate_policy = memsim::write_allocate_policy_from_string(args.l2_alloc);
        l2.validate();
        cfg.cache_configs.push_back(l2);
    }

    // L3 Cache (optional)
    if (args.l3_size > 0) {
        memsim::CacheConfig l3;
        l3.name               = "L3";
        l3.capacity_bytes     = args.l3_size;
        l3.line_size_bytes    = args.l1_line_size;
        l3.associativity      = args.l3_assoc;
        l3.num_sets           = 0;
        l3.hit_latency_cycles = args.l3_hit_latency;
        l3.replacement_policy   = memsim::ReplacementPolicy::LRU;
        l3.write_policy         = memsim::WritePolicy::WriteBack;
        l3.write_allocate_policy = memsim::WriteAllocatePolicy::WriteAllocate;
        l3.validate();
        cfg.cache_configs.push_back(l3);
    }

    // DRAM
    if (args.dram_preset == "ddr4") {
        cfg.dram_config = memsim::DramConfig::make_ddr4_default();
    } else if (args.dram_preset == "ddr5") {
        cfg.dram_config = memsim::DramConfig::make_ddr5_default();
    } else if (args.dram_preset == "hbm3") {
        cfg.dram_config = memsim::DramConfig::make_hbm3_default();
    } else {
        cfg.dram_config.num_banks      = args.dram_banks;
        cfg.dram_config.num_rows       = args.dram_rows;
        cfg.dram_config.row_size_bytes = args.dram_row_size;
        cfg.dram_config.tRCD           = args.dram_tRCD;
        cfg.dram_config.tCAS           = args.dram_tCAS;
        cfg.dram_config.tRP            = args.dram_tRP;
        cfg.dram_config.tRAS           = args.dram_tRCD + args.dram_tRP + 5;
        cfg.dram_config.tRC            = cfg.dram_config.tRAS + args.dram_tRP;
    }
    cfg.dram_config.page_policy    = memsim::page_policy_from_string(args.dram_page);
    cfg.dram_config.enable_refresh = args.dram_refresh;
    cfg.dram_scheduling_policy     = memsim::scheduling_policy_from_string(args.dram_policy);

    // Workload
    cfg.trace_file        = args.trace_file;
    cfg.workload_type     = args.workload_type;
    cfg.request_count     = args.request_count;
    cfg.address_space_size = args.address_space;
    cfg.stride            = args.stride;
    cfg.read_ratio        = args.read_ratio;
    cfg.random_seed       = args.random_seed;
    cfg.working_set_size  = args.working_set_size;

    // Output
    cfg.csv_output = args.csv_output;
    cfg.verbose    = args.verbose;

    return cfg;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    CLIArgs args;

    if (!parse_args(argc, argv, args)) {
        std::cerr << "Use --help for usage information.\n";
        return 1;
    }

    if (args.help) {
        print_help();
        return 0;
    }

    // Default to sequential workload if neither trace nor workload specified.
    if (args.trace_file.empty() && args.workload_type.empty()) {
        args.workload_type = "sequential";
    }

    try {
        memsim::SimulatorConfig config = build_config(args);
        memsim::Simulator sim(config);

        memsim::SystemStatistics stats = sim.run();

        // Print report.
        stats.report(std::cout);

        // Compute and print AMAT.
        std::vector<double> hit_times;
        for (const auto& cc : config.cache_configs) {
            hit_times.push_back(static_cast<double>(cc.hit_latency_cycles));
        }
        double amat = stats.compute_amat(hit_times);
        std::cout << "\n=== Performance ===\n"
                  << "  AMAT: " << std::fixed << std::setprecision(2)
                  << amat << " cycles\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
