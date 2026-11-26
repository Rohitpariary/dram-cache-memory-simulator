/// @file simulator.cpp
/// @brief Implementation of the top-level Simulator.

#include "memsim/simulator.hpp"
#include "memsim/trace_loader.hpp"
#include "memsim/workload_generator.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace memsim {

Simulator::Simulator(const SimulatorConfig& config)
    : config_(config)
    , hierarchy_(config.cache_configs, config.dram_config, config.dram_scheduling_policy)
{}

SystemStatistics Simulator::run(const std::vector<MemoryRequest>& requests) {
    if (config_.verbose) {
        std::cout << "Running simulation with " << requests.size()
                  << " requests...\n";
    }

    for (size_t i = 0; i < requests.size(); ++i) {
        hierarchy_.access(requests[i]);

        if (config_.verbose && (i + 1) % 10000 == 0) {
            std::cout << "  Processed " << (i + 1) << " / "
                      << requests.size() << " requests\n";
        }
    }

    SystemStatistics stats = hierarchy_.collect_statistics();
    stats.total_requests = requests.size();

    // Write CSV output if configured.
    if (!config_.csv_output.empty()) {
        std::ofstream csv(config_.csv_output);
        if (csv.is_open()) {
            csv << stats.to_csv();
            if (config_.verbose) {
                std::cout << "Results written to " << config_.csv_output << '\n';
            }
        } else {
            std::cerr << "Warning: Could not open CSV output file: "
                      << config_.csv_output << '\n';
        }
    }

    return stats;
}

SystemStatistics Simulator::run() {
    std::vector<MemoryRequest> requests;

    if (!config_.trace_file.empty()) {
        // Load trace from file.
        TraceLoadResult trace = load_trace(config_.trace_file);
        if (!trace.ok()) {
            std::string err_msg = "Trace file errors:\n";
            for (const auto& e : trace.errors) {
                err_msg += "  Line " + std::to_string(e.line_number)
                         + ": " + e.message + "\n";
            }
            throw std::runtime_error(err_msg);
        }
        requests = std::move(trace.requests);
        if (config_.verbose) {
            std::cout << "Loaded " << requests.size() << " requests from "
                      << config_.trace_file << '\n';
        }
    } else if (!config_.workload_type.empty()) {
        // Generate synthetic workload.
        WorkloadConfig wl_cfg;
        wl_cfg.request_count = config_.request_count;
        wl_cfg.address_space_size = config_.address_space_size;
        wl_cfg.stride = config_.stride;
        wl_cfg.read_ratio = config_.read_ratio;
        wl_cfg.random_seed = config_.random_seed;
        wl_cfg.working_set_size = config_.working_set_size;
        wl_cfg.access_size = 4;

        requests = generate_workload(config_.workload_type, wl_cfg);
        if (config_.verbose) {
            std::cout << "Generated " << requests.size() << " "
                      << config_.workload_type << " requests\n";
        }
    } else {
        throw std::runtime_error(
            "No trace file or workload type specified. "
            "Use --trace or --workload.");
    }

    return run(requests);
}

const CacheHierarchy& Simulator::get_hierarchy() const {
    return hierarchy_;
}

void Simulator::reset() {
    hierarchy_.reset();
}

} // namespace memsim
