#!/usr/bin/env python3
"""
Automated experiment runner for the DRAM & Cache Memory Simulator.

Executes reproducible parameter sweeps by invoking the compiled `memory_sim`
binary, captures exit status and errors, parses per-level cache, DRAM, and
AMAT metrics, and writes structured results to CSV alongside a machine-readable
JSON experiment manifest.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional


DEFAULT_SIM_CANDIDATES = [
    Path("build/memory_sim.exe"),
    Path("build/memory_sim"),
    Path("build/Release/memory_sim.exe"),
    Path("build/Debug/memory_sim.exe"),
]


def find_simulator(explicit_path: Optional[str]) -> Path:
    if explicit_path:
        p = Path(explicit_path)
        if not p.is_file():
            raise FileNotFoundError(f"Simulator binary not found at: {p}")
        return p
    for cand in DEFAULT_SIM_CANDIDATES:
        if cand.is_file():
            return cand
    raise FileNotFoundError(
        "Could not locate memory_sim binary. Build the project first or pass --sim <path>."
    )


def parse_sim_output(stdout: str) -> Dict[str, Any]:
    """Parse structured metrics from memory_sim stdout."""
    metrics: Dict[str, Any] = {}

    def extract_section(header: str) -> str:
        pattern = rf"===\s*{re.escape(header)}\s*===(.*?)(?=\n===|\Z)"
        m = re.search(pattern, stdout, re.DOTALL)
        return m.group(1) if m else ""

    def grab_int(text: str, label: str) -> int:
        m = re.search(rf"{re.escape(label)}:\s*(\d+)", text)
        if not m:
            raise ValueError(f"Missing integer field '{label}' in simulator output")
        return int(m.group(1))

    def grab_float(text: str, label: str) -> float:
        m = re.search(rf"{re.escape(label)}:\s*([0-9]+(?:\.[0-9]+)?)", text)
        if not m:
            raise ValueError(f"Missing float field '{label}' in simulator output")
        return float(m.group(1))

    l1_sec = extract_section("L1 Statistics")
    if l1_sec:
        metrics["l1_accesses"] = grab_int(l1_sec, "Total accesses")
        metrics["l1_hits"] = grab_int(l1_sec, "Hits")
        metrics["l1_misses"] = grab_int(l1_sec, "Misses")
        metrics["l1_hit_rate_pct"] = grab_float(l1_sec, "Hit rate")
        metrics["l1_miss_rate_pct"] = grab_float(l1_sec, "Miss rate")
        metrics["l1_evictions"] = grab_int(l1_sec, "Evictions")
        metrics["l1_writebacks"] = grab_int(l1_sec, "Writebacks")
        metrics["l1_bytes_transferred"] = grab_int(l1_sec, "Bytes transferred")
    else:
        for k in (
            "l1_accesses", "l1_hits", "l1_misses", "l1_evictions",
            "l1_writebacks", "l1_bytes_transferred",
        ):
            metrics[k] = 0
        metrics["l1_hit_rate_pct"] = 0.0
        metrics["l1_miss_rate_pct"] = 0.0

    l2_sec = extract_section("L2 Statistics")
    if l2_sec:
        metrics["l2_accesses"] = grab_int(l2_sec, "Total accesses")
        metrics["l2_hits"] = grab_int(l2_sec, "Hits")
        metrics["l2_misses"] = grab_int(l2_sec, "Misses")
        metrics["l2_hit_rate_pct"] = grab_float(l2_sec, "Hit rate")
        metrics["l2_miss_rate_pct"] = grab_float(l2_sec, "Miss rate")
        metrics["l2_evictions"] = grab_int(l2_sec, "Evictions")
        metrics["l2_writebacks"] = grab_int(l2_sec, "Writebacks")
        metrics["l2_bytes_transferred"] = grab_int(l2_sec, "Bytes transferred")
    else:
        for k in (
            "l2_accesses", "l2_hits", "l2_misses", "l2_evictions",
            "l2_writebacks", "l2_bytes_transferred",
        ):
            metrics[k] = 0
        metrics["l2_hit_rate_pct"] = 0.0
        metrics["l2_miss_rate_pct"] = 0.0

    dram_sec = extract_section("DRAM Statistics")
    if not dram_sec:
        raise ValueError("Missing 'DRAM Statistics' section in simulator output")
    metrics["dram_accesses"] = grab_int(dram_sec, "Total accesses")
    metrics["dram_reads"] = grab_int(dram_sec, "Read accesses")
    metrics["dram_writes"] = grab_int(dram_sec, "Write accesses")
    metrics["dram_row_hits"] = grab_int(dram_sec, "Row buffer hits")
    metrics["dram_row_misses"] = grab_int(dram_sec, "Row misses")
    metrics["dram_row_conflicts"] = grab_int(dram_sec, "Row conflicts")
    metrics["dram_row_hit_rate_pct"] = grab_float(dram_sec, "Row hit rate")
    metrics["dram_avg_read_latency"] = grab_float(dram_sec, "Avg read latency")
    metrics["dram_avg_write_latency"] = grab_float(dram_sec, "Avg write latency")
    metrics["dram_activations"] = grab_int(dram_sec, "Total activations")

    sys_sec = extract_section("System Overview")
    if not sys_sec:
        raise ValueError("Missing 'System Overview' section in simulator output")
    metrics["total_requests"] = grab_int(sys_sec, "Total requests")
    metrics["total_simulated_cycles"] = grab_int(sys_sec, "Total simulated cycles")

    perf_sec = extract_section("Performance")
    metrics["amat_cycles"] = grab_float(perf_sec, "AMAT") if perf_sec else 0.0
    metrics["avg_cycles_per_request"] = (
        metrics["total_simulated_cycles"] / metrics["total_requests"]
        if metrics["total_requests"] > 0
        else 0.0
    )
    return metrics


def build_experiment_matrix(request_count: int, seed: int) -> List[Dict[str, Any]]:
    """Build reproducible experiment suites covering all evaluation axes."""
    experiments: List[Dict[str, Any]] = []

    def add_exp(suite: str, name: str, **overrides: Any) -> None:
        cfg = {
            "suite": suite,
            "experiment_name": name,
            "workload": "random",
            "count": request_count,
            "address_space": 262144,  # 256 KB address space to stress 16-128 KB caches
            "stride": 64,
            "read_ratio": 0.8,
            "seed": seed,
            "working_set": 16384,
            "l1_size": 16384,
            "l1_assoc": 4,
            "l1_line_size": 64,
            "l1_latency": 1,
            "l2_size": 262144,
            "l2_assoc": 8,
            "l2_latency": 10,
            "replacement": "lru",
            "write_policy": "wb",
            "alloc_policy": "wa",
            "dram_banks": 8,
            "dram_row_size": 8192,
            "dram_tRCD": 14,
            "dram_tCAS": 14,
            "dram_tRP": 14,
            "prefetch": "none",
            "dram_page_policy": "open",
            "dram_preset": "",
        }
        cfg.update(overrides)
        experiments.append(cfg)

    # 1. Baseline comparison matrix (A, B, C, D) from project specification
    add_exp("baseline_matrix", "ExpA_16K_256K_4way_LRU", l1_size=16384, l2_size=262144, l1_assoc=4, replacement="lru", workload="hotset")
    add_exp("baseline_matrix", "ExpB_32K_256K_4way_LRU", l1_size=32768, l2_size=262144, l1_assoc=4, replacement="lru", workload="hotset")
    add_exp("baseline_matrix", "ExpC_16K_512K_8way_LRU", l1_size=16384, l2_size=524288, l1_assoc=8, replacement="lru", workload="hotset")
    add_exp("baseline_matrix", "ExpD_16K_256K_4way_FIFO", l1_size=16384, l2_size=262144, l1_assoc=4, replacement="fifo", workload="hotset")

    # 2. L1 capacity sweep (also yields latency vs cache size)
    for l1_kb in (2, 4, 8, 16, 32, 64, 128):
        add_exp(
            "l1_capacity_sweep",
            f"L1_{l1_kb}KB",
            l1_size=l1_kb * 1024,
            l2_size=262144,
            l1_assoc=4,
            workload="random",
            address_space=262144,
        )

    # 3. L2 capacity sweep (with smaller L1 so L2 sees misses)
    for l2_kb in (32, 64, 128, 256, 512, 1024):
        add_exp(
            "l2_capacity_sweep",
            f"L2_{l2_kb}KB",
            l1_size=8192,
            l2_size=l2_kb * 1024,
            l2_assoc=8,
            workload="random",
            address_space=1048576,
        )

    # 4. Associativity sweep (1-way to 16-way on strided conflict-prone workload)
    #    32 KB address space with 4 KB stride = 8 unique blocks mapping to set 0
    for assoc in (1, 2, 4, 8, 16):
        add_exp(
            "associativity_sweep",
            f"Assoc_{assoc}way",
            l1_size=16384,
            l1_assoc=assoc,
            workload="strided",
            stride=4096,
            address_space=32768,
        )

    # 5. Replacement policy comparison (LRU vs FIFO vs Random) across L1 sizes
    for policy in ("lru", "fifo", "random"):
        for l1_kb in (4, 8, 16, 32, 64):
            add_exp(
                "replacement_comparison",
                f"{policy.upper()}_{l1_kb}KB",
                l1_size=l1_kb * 1024,
                l1_assoc=4,
                replacement=policy,
                workload="hotset",
                address_space=262144,
            )

    # 6. Workload sweep (row-buffer hit rate & cache behavior across all 10 workloads)
    workloads = [
        "sequential", "random", "strided", "hotset", "working_set",
        "read_heavy", "write_heavy", "mixed", "bank_conflict", "row_locality",
    ]
    for wl in workloads:
        add_exp(
            "workload_comparison",
            f"Workload_{wl}",
            workload=wl,
            l1_size=16384,
            l2_size=65536,
            address_space=1048576,
        )

    # 7. DRAM timing sweep (varying tRCD = tCAS = tRP)
    for label, trcd, tcas, trp in (
        ("Fast_10_10_10", 10, 10, 10),
        ("DDR4_14_14_14", 14, 14, 14),
        ("Mid_22_22_22", 22, 22, 22),
        ("DDR5_40_40_40", 40, 40, 40),
    ):
        add_exp(
            "dram_timing_sweep",
            f"DRAM_{label}",
            workload="random",
            l1_size=8192,
            l2_size=32768,
            address_space=1048576,
            dram_tRCD=trcd,
            dram_tCAS=tcas,
            dram_tRP=trp,
        )

    # 8. Working-set size sweep
    for ws_kb in (1, 2, 4, 8, 16, 32, 64, 128, 256):
        add_exp(
            "working_set_sweep",
            f"WS_{ws_kb}KB",
            workload="working_set",
            working_set=ws_kb * 1024,
            l1_size=16384,
            l2_size=65536,
        )

    # 9. Read/write ratio sweep
    for rr in (0.0, 0.2, 0.4, 0.6, 0.8, 1.0):
        add_exp(
            "rw_ratio_sweep",
            f"ReadRatio_{int(rr * 100)}",
            workload="mixed",
            read_ratio=rr,
            l1_size=16384,
            l2_size=65536,
            address_space=1048576,
        )

    # 10. Prefetching & DRAM Page-Policy / Preset Comparison
    for pf in ("none", "next_line", "stride"):
        add_exp(
            "prefetch_comparison",
            f"Prefetch_{pf}_sequential",
            workload="sequential",
            prefetch=pf,
            l1_size=16384,
            l2_size=65536,
            address_space=1048576,
        )
    for page_pol in ("open", "closed"):
        for wl in ("row_locality", "bank_conflict", "random"):
            add_exp(
                "page_policy_comparison",
                f"Page_{page_pol}_{wl}",
                workload=wl,
                dram_page_policy=page_pol,
                l1_size=8192,
                l2_size=32768,
                address_space=1048576,
            )

    return experiments


def run_single(sim_path: Path, cfg: Dict[str, Any], repeat_idx: int) -> Dict[str, Any]:
    cmd = [
        str(sim_path),
        "--workload", str(cfg["workload"]),
        "--count", str(cfg["count"]),
        "--address-space", str(cfg["address_space"]),
        "--stride", str(cfg["stride"]),
        "--read-ratio", str(cfg["read_ratio"]),
        "--seed", str(cfg["seed"]),
        "--working-set", str(cfg["working_set"]),
        "--l1-size", str(cfg["l1_size"]),
        "--l1-assoc", str(cfg["l1_assoc"]),
        "--l1-line-size", str(cfg["l1_line_size"]),
        "--l1-latency", str(cfg["l1_latency"]),
        "--l2-size", str(cfg["l2_size"]),
        "--l2-assoc", str(cfg["l2_assoc"]),
        "--l2-latency", str(cfg["l2_latency"]),
        "--replacement", str(cfg["replacement"]),
        "--write-policy", str(cfg["write_policy"]),
        "--alloc-policy", str(cfg["alloc_policy"]),
        "--dram-banks", str(cfg["dram_banks"]),
        "--dram-row-size", str(cfg["dram_row_size"]),
        "--dram-tRCD", str(cfg["dram_tRCD"]),
        "--dram-tCAS", str(cfg["dram_tCAS"]),
        "--dram-tRP", str(cfg["dram_tRP"]),
        "--prefetch", str(cfg["prefetch"]),
        "--dram-page-policy", str(cfg["dram_page_policy"]),
    ]
    if cfg.get("dram_preset"):
        cmd.extend(["--dram-preset", str(cfg["dram_preset"])])

    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise RuntimeError(
            f"Experiment '{cfg['experiment_name']}' failed with exit code {proc.returncode}:\n"
            f"CMD: {' '.join(cmd)}\nSTDERR: {proc.stderr.strip()}"
        )

    parsed = parse_sim_output(proc.stdout)
    row = dict(cfg)
    row["repeat"] = repeat_idx
    row["exit_code"] = proc.returncode
    row.update(parsed)
    return row


def resolve_output_path(path: Path, overwrite: bool) -> Path:
    if not path.exists() or overwrite:
        return path
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    return path.with_name(f"{path.stem}_{stamp}{path.suffix}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run automated memory simulator experiments.")
    parser.add_argument("--sim", type=str, default=None, help="Path to memory_sim executable")
    parser.add_argument("--output", type=str, default="results/experiments.csv", help="Output CSV path")
    parser.add_argument("--manifest", type=str, default="results/manifest.json", help="Output JSON manifest path")
    parser.add_argument("--count", type=int, default=20000, help="Requests per experiment")
    parser.add_argument("--seed", type=int, default=42, help="Base random seed")
    parser.add_argument("--repeats", type=int, default=1, help="Number of repetitions per experiment")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing output files")
    args = parser.parse_args()

    sim_path = find_simulator(args.sim)
    out_csv = resolve_output_path(Path(args.output), args.overwrite)
    out_manifest = resolve_output_path(Path(args.manifest), args.overwrite)
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    out_manifest.parent.mkdir(parents=True, exist_ok=True)

    matrix = build_experiment_matrix(args.count, args.seed)
    print(f"Using simulator: {sim_path}")
    print(f"Running {len(matrix)} configurations x {args.repeats} repeat(s)...")

    rows: List[Dict[str, Any]] = []
    failures: List[Dict[str, Any]] = []

    for idx, cfg in enumerate(matrix, start=1):
        for r in range(args.repeats):
            try:
                row = run_single(sim_path, cfg, r)
                rows.append(row)
                print(
                    f"[{idx}/{len(matrix)}] {cfg['suite']} / {cfg['experiment_name']} "
                    f"-> L1 miss={row['l1_miss_rate_pct']:.2f}%, "
                    f"AMAT={row['amat_cycles']:.2f} cyc"
                )
            except Exception as exc:
                failures.append({
                    "suite": cfg["suite"],
                    "experiment_name": cfg["experiment_name"],
                    "repeat": r,
                    "error": str(exc),
                })
                print(f"[ERROR] {cfg['experiment_name']}: {exc}", file=sys.stderr)

    if rows:
        fieldnames = list(rows[0].keys())
        with out_csv.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
        print(f"\nSaved {len(rows)} rows to {out_csv}")

    manifest = {
        "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "simulator_binary": str(sim_path),
        "request_count": args.count,
        "base_seed": args.seed,
        "repeats": args.repeats,
        "total_planned": len(matrix) * args.repeats,
        "total_succeeded": len(rows),
        "total_failed": len(failures),
        "csv_output": str(out_csv),
        "failures": failures,
    }
    with out_manifest.open("w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    print(f"Saved manifest to {out_manifest}")

    if failures:
        print(f"\nWARNING: {len(failures)} experiment(s) failed!", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
