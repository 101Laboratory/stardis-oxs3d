#!/usr/bin/env python3
"""
CPU vs GPU Path Depth Comparison Analysis

Reads CSV trace files from both CPU and GPU solvers and produces:
  1. Statistical summary tables
  2. Distribution comparisons
  3. Consistency checks
  4. Comparison report (stdout + optional markdown)

Usage:
  python analyze_path_depth.py \
    --cpu cpu_path_depth.csv \
    --gpu gpu_pixel_trace.csv \
    [--out comparison_report.md]
"""

import argparse
import sys
from pathlib import Path

try:
    import numpy as np
    import pandas as pd
    HAS_PANDAS = True
except ImportError:
    HAS_PANDAS = False


def load_cpu_csv(path: str) -> "pd.DataFrame":
    """Load CPU path depth CSV."""
    df = pd.read_csv(path)
    # Compute derived columns
    df["total_physical_iters"] = (
        df["ds_steps"] + df["wos_steps"] + df["rad_bounces"] + df["cnv_steps"]
    )
    return df


def load_gpu_csv(path: str) -> "pd.DataFrame":
    """Load GPU pixel trace CSV (extended with L1 fields)."""
    df = pd.read_csv(path)

    # Rename GPU-specific columns to match CPU naming
    rename_map = {}
    if "ds_steps" not in df.columns and "ds_steps_L1" in df.columns:
        rename_map = {
            "ds_steps_L1": "ds_steps",
            "wos_steps_L1": "wos_steps",
            "rad_bounces_L1": "rad_bounces",
            "cnv_steps_L1": "cnv_steps",
            "func_calls_L0": "func_calls",
        }
    elif "ds_steps" not in df.columns:
        # GPU CSV without L1 fields — can only compare steps_taken
        print("WARNING: GPU CSV does not have L1 physical iteration columns.")
        print("         Only steps_taken comparison is possible.")
        df["ds_steps"] = 0
        df["wos_steps"] = 0
        df["rad_bounces"] = 0
        df["cnv_steps"] = 0

    if rename_map:
        df = df.rename(columns=rename_map)

    df["total_physical_iters"] = (
        df.get("ds_steps", 0) + df.get("wos_steps", 0)
        + df.get("rad_bounces", 0) + df.get("cnv_steps", 0)
    )
    return df


def summary_stats(df: "pd.DataFrame", label: str) -> dict:
    """Compute summary statistics for a path depth DataFrame."""
    stats = {}
    stats["label"] = label
    stats["total_paths"] = len(df)
    stats["completed"] = int((df["T_done"] == 1).sum()) if "T_done" in df.columns else len(df)
    stats["failed"] = int((df["done_reason"] == -1).sum()) if "done_reason" in df.columns else 0

    for col in ["ds_steps", "wos_steps", "rad_bounces", "cnv_steps",
                "total_physical_iters", "func_calls"]:
        if col in df.columns:
            s = df[col]
            stats[f"{col}_mean"] = s.mean()
            stats[f"{col}_max"] = int(s.max())
            stats[f"{col}_min"] = int(s.min())
            stats[f"{col}_median"] = s.median()
            stats[f"{col}_p95"] = s.quantile(0.95)
            stats[f"{col}_p99"] = s.quantile(0.99)
            stats[f"{col}_p999"] = s.quantile(0.999)

    if "steps" in df.columns:
        s = df["steps"]
        stats["steps_taken_mean"] = s.mean()
        stats["steps_taken_max"] = int(s.max())
        stats["steps_taken_p99"] = s.quantile(0.99)

    return stats


def done_reason_distribution(df: "pd.DataFrame") -> dict:
    """Compute done_reason distribution."""
    if "done_reason" not in df.columns:
        return {}
    counts = df["done_reason"].value_counts().to_dict()
    total = len(df)
    return {k: {"count": v, "pct": 100.0 * v / total} for k, v in counts.items()}


def log2_histogram(series: "pd.Series", max_bucket: int = 24) -> list:
    """Compute log2 bucket histogram."""
    buckets = []
    for i in range(max_bucket):
        lo = 0 if i == 0 else (1 << i)
        hi = 1 << (i + 1)
        count = int(((series >= lo) & (series < hi)).sum())
        if count > 0:
            buckets.append((lo, hi, count))
    # Overflow bucket
    overflow = int((series >= (1 << max_bucket)).sum())
    if overflow > 0:
        buckets.append((1 << max_bucket, float("inf"), overflow))
    return buckets


def print_comparison(cpu_stats: dict, gpu_stats: dict):
    """Print side-by-side comparison."""
    print("\n" + "=" * 72)
    print("  CPU vs GPU Path Depth Comparison")
    print("=" * 72)

    print(f"\n  {'Metric':<30} {'CPU':>15} {'GPU':>15} {'Ratio':>10}")
    print(f"  {'-'*30} {'-'*15} {'-'*15} {'-'*10}")

    print(f"  {'Total paths':<30} {cpu_stats['total_paths']:>15,} {gpu_stats['total_paths']:>15,}")
    print(f"  {'Failed paths':<30} {cpu_stats['failed']:>15,} {gpu_stats['failed']:>15,}")

    for col in ["ds_steps", "wos_steps", "rad_bounces", "cnv_steps",
                "total_physical_iters", "func_calls"]:
        for suffix, label in [("_max", "max"), ("_mean", "mean"),
                               ("_p99", "p99"), ("_p999", "p999")]:
            key = f"{col}{suffix}"
            cpu_val = cpu_stats.get(key)
            gpu_val = gpu_stats.get(key)
            if cpu_val is not None and gpu_val is not None:
                ratio = gpu_val / cpu_val if cpu_val != 0 else float("inf")
                name = f"{col} ({label})"
                if isinstance(cpu_val, float):
                    print(f"  {name:<30} {cpu_val:>15,.1f} {gpu_val:>15,.1f} {ratio:>10.2f}x")
                else:
                    print(f"  {name:<30} {cpu_val:>15,} {gpu_val:>15,} {ratio:>10.2f}x")


def print_done_reason_comparison(cpu_dr, gpu_dr):
    """Print done_reason distribution comparison."""
    print(f"\n  {'Done Reason':<25} {'CPU %':>10} {'GPU %':>10}")
    print(f"  {'-'*25} {'-'*10} {'-'*10}")
    reasons = {1: "rad_miss", 2: "temp_known", 3: "boundary", 4: "time_rewind", -1: "failed"}
    all_keys = set(list(cpu_dr.keys()) + list(gpu_dr.keys()))
    for k in sorted(all_keys, key=lambda x: (x < 0, abs(x))):
        name = reasons.get(k, f"unknown({k})")
        cpu_pct = cpu_dr.get(k, {}).get("pct", 0)
        gpu_pct = gpu_dr.get(k, {}).get("pct", 0)
        print(f"  {name:<25} {cpu_pct:>9.2f}% {gpu_pct:>9.2f}%")


def print_histogram(cpu_hist, gpu_hist, label="total_physical_iters"):
    """Print side-by-side log2 histograms."""
    print(f"\n  Log2 Histogram: {label}")
    print(f"  {'Bucket':<22} {'CPU paths':>12} {'GPU paths':>12}")
    print(f"  {'-'*22} {'-'*12} {'-'*12}")

    # Merge buckets
    all_buckets = {}
    for lo, hi, cnt in cpu_hist:
        all_buckets.setdefault((lo, hi), [0, 0])[0] = cnt
    for lo, hi, cnt in gpu_hist:
        all_buckets.setdefault((lo, hi), [0, 0])[1] = cnt

    for (lo, hi), (cc, gc) in sorted(all_buckets.items()):
        hi_str = f"{hi:>7,}" if hi != float("inf") else "     ∞"
        print(f"  [{lo:>7,}, {hi_str}) {cc:>12,} {gc:>12,}")


def generate_markdown_report(
    cpu_stats, gpu_stats, cpu_dr, gpu_dr, cpu_hist, gpu_hist
) -> str:
    """Generate a markdown comparison report."""
    lines = []
    lines.append("# CPU vs GPU 路径深度对比报告\n")
    lines.append(f"**生成时间**: (auto-generated)\n")

    lines.append("## 1. 总览\n")
    lines.append(f"| 指标 | CPU | GPU |")
    lines.append(f"|------|-----|-----|")
    lines.append(f"| 总路径数 | {cpu_stats['total_paths']:,} | {gpu_stats['total_paths']:,} |")
    lines.append(f"| 失败路径 | {cpu_stats['failed']:,} | {gpu_stats['failed']:,} |")

    lines.append("\n## 2. 深度统计\n")
    lines.append(f"| 指标 | CPU | GPU | 比值 |")
    lines.append(f"|------|-----|-----|------|")

    for col in ["ds_steps", "total_physical_iters", "func_calls"]:
        for suffix, label in [("_max", "max"), ("_mean", "avg"),
                               ("_p99", "p99"), ("_p999", "p999")]:
            key = f"{col}{suffix}"
            cv = cpu_stats.get(key, 0)
            gv = gpu_stats.get(key, 0)
            ratio = gv / cv if cv != 0 else 0
            if isinstance(cv, float):
                lines.append(f"| {col} ({label}) | {cv:,.1f} | {gv:,.1f} | {ratio:.2f}× |")
            else:
                lines.append(f"| {col} ({label}) | {cv:,} | {gv:,} | {ratio:.2f}× |")

    lines.append("\n## 3. 终止原因分布\n")
    lines.append(f"| 原因 | CPU % | GPU % |")
    lines.append(f"|------|-------|-------|")
    reasons = {1: "rad_miss", 2: "temp_known", 3: "boundary", 4: "time_rewind", -1: "failed"}
    all_keys = set(list(cpu_dr.keys()) + list(gpu_dr.keys()))
    for k in sorted(all_keys, key=lambda x: (x < 0, abs(x))):
        name = reasons.get(k, f"unknown({k})")
        cp = cpu_dr.get(k, {}).get("pct", 0)
        gp = gpu_dr.get(k, {}).get("pct", 0)
        lines.append(f"| {name} | {cp:.2f}% | {gp:.2f}% |")

    lines.append("\n## 4. 判定\n")
    cpu_max = cpu_stats.get("ds_steps_max", 0)
    gpu_max = gpu_stats.get("ds_steps_max", 0)
    if cpu_max > 0:
        ratio = gpu_max / cpu_max
        if ratio < 2:
            lines.append(f"**✅ 一致**: CPU max ds_steps={cpu_max:,}, GPU max ds_steps={gpu_max:,}, 比值 {ratio:.2f}×")
        elif ratio < 10:
            lines.append(f"**⚠️ 可疑**: CPU max ds_steps={cpu_max:,}, GPU max ds_steps={gpu_max:,}, 比值 {ratio:.2f}×")
        else:
            lines.append(f"**❌ 异常**: CPU max ds_steps={cpu_max:,}, GPU max ds_steps={gpu_max:,}, 比值 {ratio:.2f}× — 需排查 GPU 实现")
    else:
        lines.append("⚠️ CPU 无 delta-sphere 数据，无法判定")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="CPU vs GPU Path Depth Comparison")
    parser.add_argument("--cpu", required=True, help="CPU path depth CSV file")
    parser.add_argument("--gpu", required=True, help="GPU pixel trace CSV file")
    parser.add_argument("--out", default=None, help="Output markdown report file")
    args = parser.parse_args()

    if not HAS_PANDAS:
        print("ERROR: pandas and numpy are required. Install with:")
        print("  pip install pandas numpy")
        sys.exit(1)

    if not Path(args.cpu).exists():
        print(f"ERROR: CPU CSV not found: {args.cpu}")
        sys.exit(1)
    if not Path(args.gpu).exists():
        print(f"ERROR: GPU CSV not found: {args.gpu}")
        sys.exit(1)

    print(f"Loading CPU data from {args.cpu}...")
    cpu_df = load_cpu_csv(args.cpu)
    print(f"  {len(cpu_df):,} paths loaded")

    print(f"Loading GPU data from {args.gpu}...")
    gpu_df = load_gpu_csv(args.gpu)
    print(f"  {len(gpu_df):,} paths loaded")

    # Compute statistics
    cpu_stats = summary_stats(cpu_df, "CPU")
    gpu_stats = summary_stats(gpu_df, "GPU")

    cpu_dr = done_reason_distribution(cpu_df)
    gpu_dr = done_reason_distribution(gpu_df)

    cpu_hist = log2_histogram(cpu_df["total_physical_iters"])
    gpu_hist = log2_histogram(gpu_df["total_physical_iters"])

    # Print comparison
    print_comparison(cpu_stats, gpu_stats)
    print_done_reason_comparison(cpu_dr, gpu_dr)
    print_histogram(cpu_hist, gpu_hist)

    # GPU steps_taken vs total_physical_iters ratio
    if "steps" in gpu_df.columns:
        valid = gpu_df["total_physical_iters"] > 0
        if valid.any():
            ratios = gpu_df.loc[valid, "steps"] / gpu_df.loc[valid, "total_physical_iters"]
            print(f"\n  GPU steps_taken / total_physical_iters ratio:")
            print(f"    mean={ratios.mean():.2f}  median={ratios.median():.2f}"
                  f"  p5={ratios.quantile(0.05):.2f}  p95={ratios.quantile(0.95):.2f}")

    # Generate markdown report
    if args.out:
        report = generate_markdown_report(
            cpu_stats, gpu_stats, cpu_dr, gpu_dr, cpu_hist, gpu_hist
        )
        Path(args.out).write_text(report, encoding="utf-8")
        print(f"\nReport written to {args.out}")

    print("\nDone.")


if __name__ == "__main__":
    main()
