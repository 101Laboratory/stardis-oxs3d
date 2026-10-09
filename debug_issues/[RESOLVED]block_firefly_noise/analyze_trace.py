#!/usr/bin/env python3
"""
GPU vs CPU per-path temperature trace analyzer.

Usage:
    python analyze_trace.py gpu_trace.csv [cpu_trace.csv] [--pool-size 4096]

Produces:
  1. Per-batch (fill/refill round) statistics: mean, std, min, max, failure rate
  2. Heatmap of per-pixel mean temperature (GPU)
  3. Batch-vs-batch anomaly detection
  4. If CPU trace provided: per-pixel GPU-CPU delta heatmap
  5. Scatter plot: path_id vs T_value (colored by anomaly)
"""

import sys
import os
import argparse
import numpy as np

def parse_args():
    p = argparse.ArgumentParser(description="Analyze GPU/CPU pixel trace CSVs")
    p.add_argument("gpu_csv", help="GPU trace CSV (path_id,px,py,spp,T_value,...)")
    p.add_argument("cpu_csv", nargs="?", default=None, help="CPU trace CSV (optional)")
    p.add_argument("--pool-size", type=int, default=4096, help="Wavefront pool size")
    p.add_argument("--no-plot", action="store_true", help="Skip matplotlib plots")
    return p.parse_args()


def load_gpu_trace(path):
    """Load GPU trace CSV. Handles large files efficiently."""
    print(f"Loading GPU trace: {path} ...")
    # Use numpy for speed on large files
    # Columns: path_id,px,py,spp,T_value,T_done,done_reason,steps,phase
    dtype = np.dtype([
        ('path_id', np.uint32),
        ('px', np.uint32),
        ('py', np.uint32),
        ('spp', np.uint32),
        ('T_value', np.float64),
        ('T_done', np.int32),
        ('done_reason', np.int32),
        ('steps', np.uint64),
        ('phase', np.int32),
    ])
    data = np.loadtxt(path, delimiter=',', skiprows=1, dtype=dtype)
    print(f"  Loaded {len(data)} paths")
    return data


def load_cpu_trace(path):
    """Load CPU trace CSV."""
    print(f"Loading CPU trace: {path} ...")
    dtype = np.dtype([
        ('px', np.uint32),
        ('py', np.uint32),
        ('spp', np.uint32),
        ('T_value', np.float64),
        ('T_done', np.int32),
        ('res', np.int32),
    ])
    data = np.loadtxt(path, delimiter=',', skiprows=1, dtype=dtype)
    print(f"  Loaded {len(data)} paths")
    return data


def batch_statistics(gpu, pool_size):
    """Compute per-batch statistics based on path_id // pool_size."""
    path_ids = gpu['path_id']
    T = gpu['T_value']
    T_done = gpu['T_done']
    steps = gpu['steps']
    done_reason = gpu['done_reason']

    batch_ids = path_ids // pool_size
    max_batch = int(batch_ids.max())

    print(f"\n{'='*80}")
    print(f"BATCH STATISTICS (pool_size={pool_size}, {max_batch+1} batches)")
    print(f"{'='*80}")
    print(f"{'Batch':>6} {'Count':>7} {'Mean T':>14} {'Std T':>14} "
          f"{'Min T':>14} {'Max T':>14} {'Fail%':>7} {'AvgSteps':>10}")
    print(f"{'-'*6:>6} {'-'*7:>7} {'-'*14:>14} {'-'*14:>14} "
          f"{'-'*14:>14} {'-'*14:>14} {'-'*7:>7} {'-'*10:>10}")

    batch_means = []
    batch_stds = []
    batch_fail_rates = []

    for b in range(max_batch + 1):
        mask = batch_ids == b
        count = int(mask.sum())
        if count == 0:
            continue
        t_batch = T[mask]
        done_batch = T_done[mask]
        steps_batch = steps[mask]
        dr_batch = done_reason[mask]

        mean_t = float(np.mean(t_batch))
        std_t = float(np.std(t_batch))
        min_t = float(np.min(t_batch))
        max_t = float(np.max(t_batch))
        fail_rate = float(np.sum(dr_batch == -1)) / count * 100
        avg_steps = float(np.mean(steps_batch))

        batch_means.append(mean_t)
        batch_stds.append(std_t)
        batch_fail_rates.append(fail_rate)

        print(f"{b:>6} {count:>7} {mean_t:>14.6f} {std_t:>14.6f} "
              f"{min_t:>14.6f} {max_t:>14.6f} {fail_rate:>6.2f}% {avg_steps:>10.1f}")

    batch_means = np.array(batch_means)
    batch_stds = np.array(batch_stds)
    batch_fail_rates = np.array(batch_fail_rates)

    # Detect anomalous batches (mean deviates > 2 sigma from global mean)
    global_mean = np.mean(batch_means)
    global_std = np.std(batch_means)
    print(f"\nGlobal batch-mean: {global_mean:.6f} +/- {global_std:.6f}")

    if global_std > 0:
        anomalies = np.where(np.abs(batch_means - global_mean) > 2 * global_std)[0]
        if len(anomalies) > 0:
            print(f"ANOMALOUS BATCHES (|mean - global| > 2*sigma): {anomalies.tolist()}")
        else:
            print("No anomalous batches detected (all within 2*sigma)")
    
    # Trend: is there a monotonic drift?
    if len(batch_means) > 2:
        correlation = np.corrcoef(np.arange(len(batch_means)), batch_means)[0, 1]
        print(f"Batch-index vs mean-T correlation: {correlation:.4f}")
        if abs(correlation) > 0.5:
            print(f"  WARNING: Strong {'positive' if correlation > 0 else 'negative'} "
                  f"trend detected — later batches have "
                  f"{'higher' if correlation > 0 else 'lower'} temperatures")

    return batch_means, batch_stds, batch_fail_rates


def pixel_heatmap(gpu, label="GPU"):
    """Compute per-pixel mean temperature."""
    px = gpu['px']
    py = gpu['py']
    T = gpu['T_value']
    T_done = gpu['T_done']

    w = int(px.max()) + 1
    h = int(py.max()) + 1

    sum_img = np.zeros((h, w), dtype=np.float64)
    cnt_img = np.zeros((h, w), dtype=np.int32)

    # Only count done paths
    mask = T_done == 1
    np.add.at(sum_img, (py[mask], px[mask]), T[mask])
    np.add.at(cnt_img, (py[mask], px[mask]), 1)

    mean_img = np.where(cnt_img > 0, sum_img / cnt_img, np.nan)

    print(f"\n{label} per-pixel mean T: shape={mean_img.shape}, "
          f"range=[{np.nanmin(mean_img):.4f}, {np.nanmax(mean_img):.4f}], "
          f"global mean={np.nanmean(mean_img):.4f}")

    return mean_img, cnt_img


def gpu_cpu_comparison(gpu, cpu):
    """Compare GPU vs CPU per-pixel mean temperature."""
    gpu_img, gpu_cnt = pixel_heatmap(gpu, "GPU")
    cpu_img, cpu_cnt = pixel_heatmap(cpu, "CPU")

    # Ensure same size
    h = min(gpu_img.shape[0], cpu_img.shape[0])
    w = min(gpu_img.shape[1], cpu_img.shape[1])
    gpu_img = gpu_img[:h, :w]
    cpu_img = cpu_img[:h, :w]

    delta = gpu_img - cpu_img
    valid = ~(np.isnan(gpu_img) | np.isnan(cpu_img))

    print(f"\nGPU - CPU delta:")
    print(f"  Valid pixels: {valid.sum()}")
    print(f"  Mean delta: {np.mean(delta[valid]):.6f}")
    print(f"  Std delta:  {np.std(delta[valid]):.6f}")
    print(f"  Max |delta|: {np.max(np.abs(delta[valid])):.6f}")
    print(f"  Median |delta|: {np.median(np.abs(delta[valid])):.6f}")

    # Find top-10 worst pixels
    abs_delta = np.abs(delta)
    abs_delta[~valid] = 0
    flat_idx = np.argsort(abs_delta.ravel())[::-1][:10]
    print(f"\n  Top-10 worst pixels (GPU-CPU):")
    for idx in flat_idx:
        y, x = divmod(int(idx), w)
        print(f"    ({x:>4}, {y:>4}): GPU={gpu_img[y,x]:.6f}  "
              f"CPU={cpu_img[y,x]:.6f}  delta={delta[y,x]:+.6f}")

    return delta, gpu_img, cpu_img


def done_reason_statistics(gpu, pool_size):
    """Per-batch done_reason distribution."""
    path_ids = gpu['path_id']
    done_reason = gpu['done_reason']
    batch_ids = path_ids // pool_size
    max_batch = int(batch_ids.max())

    reasons = sorted(set(done_reason.tolist()))
    print(f"\n{'='*80}")
    print(f"DONE_REASON DISTRIBUTION PER BATCH")
    print(f"{'='*80}")
    header = f"{'Batch':>6}"
    for r in reasons:
        header += f" {'r='+str(r):>8}"
    print(header)

    batch_reason_counts = []
    for b in range(max_batch + 1):
        mask = batch_ids == b
        dr = done_reason[mask]
        row = [b]
        line = f"{b:>6}"
        for r in reasons:
            c = int(np.sum(dr == r))
            row.append(c)
            line += f" {c:>8}"
        batch_reason_counts.append(row)
        print(line)

    return batch_reason_counts


def steps_distribution(gpu, pool_size):
    """Per-batch steps_taken distribution (median, p90, p99, max)."""
    path_ids = gpu['path_id']
    steps = gpu['steps'].astype(np.float64)
    batch_ids = path_ids // pool_size
    max_batch = int(batch_ids.max())

    print(f"\n{'='*80}")
    print(f"STEPS_TAKEN DISTRIBUTION PER BATCH")
    print(f"{'='*80}")
    print(f"{'Batch':>6} {'Median':>10} {'P90':>10} {'P99':>10} {'Max':>10}")

    for b in range(max_batch + 1):
        mask = batch_ids == b
        s = steps[mask]
        if len(s) == 0:
            continue
        med = np.median(s)
        p90 = np.percentile(s, 90)
        p99 = np.percentile(s, 99)
        mx = np.max(s)
        print(f"{b:>6} {med:>10.0f} {p90:>10.0f} {p99:>10.0f} {mx:>10.0f}")


def plot_results(gpu, pool_size, batch_means, delta=None, gpu_img=None, cpu_img=None):
    """Generate diagnostic plots."""
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print("\nmatplotlib not available, skipping plots")
        return

    outdir = os.path.dirname(os.path.abspath(__file__))

    # 1. Batch mean temperature trend
    fig, axes = plt.subplots(2, 2, figsize=(16, 12))

    ax = axes[0, 0]
    ax.plot(batch_means, 'b.-', markersize=3)
    ax.axhline(np.mean(batch_means), color='r', linestyle='--', label='global mean')
    ax.set_xlabel('Batch index (path_id // pool_size)')
    ax.set_ylabel('Mean T_value')
    ax.set_title('Per-batch mean temperature')
    ax.legend()

    # 2. Scatter: path_id vs T_value (subsample for speed)
    ax = axes[0, 1]
    n = len(gpu)
    step = max(1, n // 50000)  # subsample to ~50k points
    idx = np.arange(0, n, step)
    ax.scatter(gpu['path_id'][idx], gpu['T_value'][idx], s=0.3, alpha=0.3, c='blue')
    ax.set_xlabel('path_id (task queue order)')
    ax.set_ylabel('T_value')
    ax.set_title(f'path_id vs T_value (1/{step} subsample)')

    # 3. GPU per-pixel mean T heatmap
    ax = axes[1, 0]
    if gpu_img is not None:
        im = ax.imshow(gpu_img, origin='lower', cmap='hot', aspect='equal')
        plt.colorbar(im, ax=ax)
        ax.set_title('GPU per-pixel mean T')
    else:
        img, _ = pixel_heatmap(gpu, "GPU (for plot)")
        im = ax.imshow(img, origin='lower', cmap='hot', aspect='equal')
        plt.colorbar(im, ax=ax)
        ax.set_title('GPU per-pixel mean T')

    # 4. GPU-CPU delta or GPU std heatmap
    ax = axes[1, 1]
    if delta is not None:
        vmax = np.nanpercentile(np.abs(delta), 99)
        im = ax.imshow(delta, origin='lower', cmap='RdBu_r', aspect='equal',
                       vmin=-vmax, vmax=vmax)
        plt.colorbar(im, ax=ax)
        ax.set_title('GPU - CPU delta')
    else:
        # Show per-pixel std instead
        px = gpu['px']
        py = gpu['py']
        T = gpu['T_value']
        T_done = gpu['T_done']
        w = int(px.max()) + 1
        h = int(py.max()) + 1
        sum_img = np.zeros((h, w))
        sum2_img = np.zeros((h, w))
        cnt_img = np.zeros((h, w))
        mask = T_done == 1
        np.add.at(sum_img, (py[mask], px[mask]), T[mask])
        np.add.at(sum2_img, (py[mask], px[mask]), T[mask]**2)
        np.add.at(cnt_img, (py[mask], px[mask]), 1)
        var_img = np.where(cnt_img > 1,
                           (sum2_img - sum_img**2 / cnt_img) / (cnt_img - 1),
                           np.nan)
        std_img = np.sqrt(np.maximum(var_img, 0))
        im = ax.imshow(std_img, origin='lower', cmap='viridis', aspect='equal')
        plt.colorbar(im, ax=ax)
        ax.set_title('GPU per-pixel std T')

    plt.tight_layout()
    outpath = os.path.join(outdir, 'trace_analysis.png')
    plt.savefig(outpath, dpi=150)
    print(f"\nPlots saved to {outpath}")
    plt.close()

    # 5. Batch mean + fail rate dual axis
    fig, ax1 = plt.subplots(figsize=(12, 5))
    ax1.plot(batch_means, 'b.-', label='batch mean T')
    ax1.set_xlabel('Batch index')
    ax1.set_ylabel('Mean T_value', color='b')
    ax1.tick_params(axis='y', labelcolor='b')
    outpath2 = os.path.join(outdir, 'batch_trend.png')
    plt.tight_layout()
    plt.savefig(outpath2, dpi=150)
    print(f"Batch trend saved to {outpath2}")
    plt.close()


def main():
    args = parse_args()

    # Load GPU trace
    gpu = load_gpu_trace(args.gpu_csv)

    # Basic sanity
    print(f"\npath_id range: [{gpu['path_id'].min()}, {gpu['path_id'].max()}]")
    print(f"Unique pixels: {len(set(zip(gpu['px'].tolist(), gpu['py'].tolist())))}")
    print(f"T_value range: [{gpu['T_value'].min():.6f}, {gpu['T_value'].max():.6f}]")
    print(f"T_done==1: {(gpu['T_done']==1).sum()} / {len(gpu)}")
    print(f"done_reason==-1 (failed): {(gpu['done_reason']==-1).sum()}")
    nan_count = np.sum(np.isnan(gpu['T_value']))
    inf_count = np.sum(np.isinf(gpu['T_value']))
    print(f"NaN T_value: {nan_count}, Inf T_value: {inf_count}")

    # Batch statistics
    batch_means, batch_stds, batch_fail_rates = batch_statistics(gpu, args.pool_size)

    # Done reason distribution
    done_reason_statistics(gpu, args.pool_size)

    # Steps distribution
    steps_distribution(gpu, args.pool_size)

    # CPU comparison
    delta = None
    gpu_img = None
    cpu_img = None
    if args.cpu_csv:
        cpu = load_cpu_trace(args.cpu_csv)
        delta, gpu_img, cpu_img = gpu_cpu_comparison(gpu, cpu)

    # Plots
    if not args.no_plot:
        plot_results(gpu, args.pool_size, batch_means, delta, gpu_img, cpu_img)

    print("\n" + "="*80)
    print("ANALYSIS COMPLETE")
    print("="*80)


if __name__ == "__main__":
    main()
