# Experiments & Performance Metrics

## 1. Metric Definitions

- **Cache Hit Rate / Miss Rate**: `hits / total_accesses` and `misses / total_accesses` per level (local miss rate for L2/L3).
- **Dirty Evictions / Writebacks**: Number of evicted valid lines with `dirty == true` under Write-Back.
- **Bytes Transferred**: Bytes written to lower memory levels via dirty evictions (`line_size_bytes`) and write-throughs (`req.size`).
- **DRAM Row-Buffer Hit Rate**: `row_hits / dram_total_accesses`.
- **Total Simulated Cycles**: Cumulative cycles elapsed across all simulated requests.
- **Simulated Avg Cycles / Request**: `total_simulated_cycles / total_requests` (includes dirty-eviction and write-through effects).
- **AMAT (Analytical)**: Recursive `hit_time + miss_rate * lower_level_penalty` using observed per-level miss rates and average DRAM latency.

## 2. Reproducing the Experiment Suite

1. Build the release binary:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```
2. Run the automated parameter sweeps (`scripts/run_experiments.py`):
   ```bash
   python scripts/run_experiments.py --count 20000 --seed 42 --output results/experiments.csv --overwrite
   ```
   This executes 9 experiment suites (baseline matrix A/B/C/D, L1 capacity sweep, L2 capacity sweep, associativity sweep, LRU vs. FIFO vs. Random, 10-workload comparison, DRAM timing sweep, working-set sweep, and read/write ratio sweep) and records a `results/manifest.json` alongside `results/experiments.csv`.
3. Generate all Matplotlib figures (`scripts/plot_results.py`):
   ```bash
   python scripts/plot_results.py --input results/experiments.csv --out-dir results/plots
   ```
