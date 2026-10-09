#!/usr/bin/env python3
"""
Per-pixel Mean Difference Map + Statistical Significance Testing.

Compares two .ht image files (GPU wavefront vs CPU depth-first)
using per-pixel two-sample Z-tests with Bonferroni and
Benjamini-Hochberg FDR correction for multiple comparisons.

Usage:
    python pixel_consistency_analysis.py \
        --cpu  path/to/cpu.ht \
        --gpu  path/to/gpu.ht \
        --scene cube_IR \
        --outdir img/ \
        --report pixel_consistency_report.md

Outputs:
    {outdir}/{scene}_difference_map.png   -- 2x2 panel (E_cpu, E_gpu, DeltaT, Z)
    {outdir}/{scene}_z_diagnostic.png     -- Z histogram + Q-Q plot
    {report}                               -- Markdown statistical report
"""

import argparse
import os
import sys
import textwrap
from pathlib import Path

import numpy as np

# ---------------------------------------------------------------------------
# Lazy imports — fail fast with clear message
# ---------------------------------------------------------------------------
try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.colors import TwoSlopeNorm
    from matplotlib.gridspec import GridSpec
except ImportError:
    sys.exit("ERROR: matplotlib required. Install via: pip install matplotlib")

try:
    from scipy import stats as sp_stats
except ImportError:
    sys.exit("ERROR: scipy required. Install via: pip install scipy")


# ═══════════════════════════════════════════════════════════════════════════
# Publication-quality defaults (consistent with plot_comparison.py)
# ═══════════════════════════════════════════════════════════════════════════
plt.rcParams.update({
    'font.family': 'serif',
    'font.size': 10,
    'axes.titlesize': 11,
    'axes.labelsize': 10,
    'xtick.labelsize': 9,
    'ytick.labelsize': 9,
    'legend.fontsize': 8.5,
    'figure.dpi': 150,
    'savefig.dpi': 200,
    'savefig.bbox': 'tight',
    'savefig.pad_inches': 0.15,
})

C_GPU = '#D62828'
C_CPU = '#1D3557'


# ═══════════════════════════════════════════════════════════════════════════
# .ht File Parser
# ═══════════════════════════════════════════════════════════════════════════
def load_ht(path):
    """Parse a .ht image file.

    Returns
    -------
    W, H : int
        Image width and height.
    E : ndarray shape (H, W)
        Per-pixel temperature expectation (K).
    SE : ndarray shape (H, W)
        Per-pixel temperature standard error (K).
    """
    path = Path(path)
    if not path.exists():
        raise FileNotFoundError(f".ht file not found: {path}")

    with open(path, 'r') as f:
        # First line: "<W> <H>"
        header = f.readline().strip().split()
        if len(header) < 2:
            raise ValueError(f"Invalid .ht header: expected 'W H', got '{f.readline()}'")
        W, H = int(header[0]), int(header[1])

        E = np.zeros((H, W), dtype=np.float64)
        SE = np.zeros((H, W), dtype=np.float64)

        idx = 0
        for line in f:
            parts = line.split()
            if len(parts) < 2:
                continue
            iy = idx // W
            ix = idx % W
            if iy >= H:
                break
            E[iy, ix] = float(parts[0])
            SE[iy, ix] = float(parts[1])
            idx += 1

    if idx != W * H:
        print(f"WARNING: Expected {W*H} pixels, parsed {idx}", file=sys.stderr)

    return W, H, E, SE


# ═══════════════════════════════════════════════════════════════════════════
# Per-Pixel Z-Test
# ═══════════════════════════════════════════════════════════════════════════
SE_FLOOR = 1e-12   # Below this, pixel is treated as zero-variance


def compute_z_map(E_cpu, SE_cpu, E_gpu, SE_gpu):
    """Compute per-pixel Z statistics and validity mask.

    Returns
    -------
    Z : ndarray
        |E_gpu - E_cpu| / sqrt(SE_gpu^2 + SE_cpu^2).  NaN for invalid pixels.
    delta : ndarray
        E_gpu - E_cpu (signed difference).
    valid : bool ndarray
        True where both pixels have non-trivial SE.
    """
    SE_comb = np.sqrt(SE_gpu**2 + SE_cpu**2)
    delta = E_gpu - E_cpu

    # Validity: at least one side has non-zero SE, and at least one E != 0
    has_signal = (np.abs(E_cpu) > SE_FLOOR) | (np.abs(E_gpu) > SE_FLOOR)
    has_se = SE_comb > SE_FLOOR
    valid = has_signal & has_se

    Z = np.full_like(delta, np.nan)
    Z[valid] = np.abs(delta[valid]) / SE_comb[valid]

    return Z, delta, valid


# ═══════════════════════════════════════════════════════════════════════════
# Multiple Comparison Corrections
# ═══════════════════════════════════════════════════════════════════════════
def multiple_corrections(Z, valid, alpha=0.05):
    """Apply Bonferroni and BH-FDR corrections.

    Returns
    -------
    result : dict with keys:
        n_valid, p_values, n_raw, n_bonf, n_fdr,
        alpha_bonf, z_bonf, fdr_reject_mask, bonf_reject_mask
    """
    z_valid = Z[valid]
    n = len(z_valid)
    if n == 0:
        return {
            'n_valid': 0, 'p_values': np.array([]),
            'n_raw': 0, 'n_bonf': 0, 'n_fdr': 0,
            'alpha_bonf': alpha, 'z_bonf': np.inf,
            'fdr_reject_mask': np.zeros_like(valid),
            'bonf_reject_mask': np.zeros_like(valid),
        }

    # Two-sided p-values from Z
    p_values = 2.0 * (1.0 - sp_stats.norm.cdf(z_valid))

    # --- Bonferroni ---
    alpha_bonf = alpha / n
    z_bonf = sp_stats.norm.ppf(1.0 - alpha_bonf / 2.0)
    bonf_reject = z_valid > z_bonf
    n_bonf = int(np.sum(bonf_reject))

    # --- BH-FDR ---
    sorted_idx = np.argsort(p_values)
    sorted_p = p_values[sorted_idx]
    thresholds = np.arange(1, n + 1) / n * alpha
    # Find largest k where p_(k) <= k/N * alpha
    below = sorted_p <= thresholds
    if np.any(below):
        k_max = np.max(np.where(below)[0])
        fdr_reject_sorted = np.zeros(n, dtype=bool)
        fdr_reject_sorted[:k_max + 1] = True
        fdr_reject = np.zeros(n, dtype=bool)
        fdr_reject[sorted_idx] = fdr_reject_sorted
    else:
        fdr_reject = np.zeros(n, dtype=bool)
    n_fdr = int(np.sum(fdr_reject))

    # Raw uncorrected
    n_raw = int(np.sum(p_values < alpha))

    # Map back to full image masks
    bonf_mask = np.zeros_like(valid)
    fdr_mask = np.zeros_like(valid)
    valid_indices = np.where(valid)
    bonf_mask[valid_indices[0][bonf_reject], valid_indices[1][bonf_reject]] = True
    fdr_mask[valid_indices[0][fdr_reject], valid_indices[1][fdr_reject]] = True

    return {
        'n_valid': n,
        'p_values': p_values,
        'n_raw': n_raw,
        'n_bonf': n_bonf,
        'n_fdr': n_fdr,
        'alpha_bonf': alpha_bonf,
        'z_bonf': z_bonf,
        'fdr_reject_mask': fdr_mask,
        'bonf_reject_mask': bonf_mask,
    }


# ═══════════════════════════════════════════════════════════════════════════
# Visualization: Difference Map (2x2)
# ═══════════════════════════════════════════════════════════════════════════
def plot_difference_map(E_cpu, E_gpu, delta, Z, valid,
                        bonf_mask, fdr_mask, scene, outpath):
    """Generate the 2x2 difference map figure."""
    fig, axes = plt.subplots(2, 2, figsize=(12, 10))
    fig.suptitle(f'Per-Pixel Mean Difference Map — {scene}',
                 fontsize=13, fontweight='bold', y=0.98)

    # Shared temperature range for top row
    vmin_t = min(np.nanmin(E_cpu[valid]), np.nanmin(E_gpu[valid]))
    vmax_t = max(np.nanmax(E_cpu[valid]), np.nanmax(E_gpu[valid]))

    # --- Top-left: CPU temperature ---
    ax = axes[0, 0]
    im0 = ax.imshow(E_cpu, cmap='inferno', vmin=vmin_t, vmax=vmax_t,
                     origin='upper', aspect='equal')
    ax.set_title(f'CPU depth-first  $E_{{cpu}}$', color=C_CPU)
    fig.colorbar(im0, ax=ax, label='T [K]', shrink=0.85)

    # --- Top-right: GPU temperature ---
    ax = axes[0, 1]
    im1 = ax.imshow(E_gpu, cmap='inferno', vmin=vmin_t, vmax=vmax_t,
                     origin='upper', aspect='equal')
    ax.set_title(f'GPU wavefront  $E_{{gpu}}$', color=C_GPU)
    fig.colorbar(im1, ax=ax, label='T [K]', shrink=0.85)

    # --- Bottom-left: Delta T (signed) ---
    ax = axes[1, 0]
    # Use symmetric color scale around 0
    delta_display = np.where(valid, delta, np.nan)
    abs_max = np.nanmax(np.abs(delta_display))
    if abs_max < SE_FLOOR:
        abs_max = 1.0
    norm_delta = TwoSlopeNorm(vmin=-abs_max, vcenter=0, vmax=abs_max)
    im2 = ax.imshow(delta_display, cmap='RdBu_r', norm=norm_delta,
                     origin='upper', aspect='equal')
    ax.set_title(r'$\Delta T = E_{gpu} - E_{cpu}$')
    cbar2 = fig.colorbar(im2, ax=ax, label='$\\Delta T$ [K]', shrink=0.85)
    # Annotate stats
    mean_delta = np.nanmean(delta_display)
    max_abs = np.nanmax(np.abs(delta_display))
    ax.text(0.02, 0.02,
            f'mean={mean_delta:.4f} K\nmax|ΔT|={max_abs:.4f} K',
            transform=ax.transAxes, fontsize=7.5,
            verticalalignment='bottom',
            bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.8))

    # --- Bottom-right: Z-map with significance contours ---
    ax = axes[1, 1]
    Z_display = np.where(valid, Z, np.nan)
    im3 = ax.imshow(Z_display, cmap='viridis', vmin=0,
                     vmax=min(np.nanmax(Z_display), 8.0),
                     origin='upper', aspect='equal')
    ax.set_title(r'$Z(x,y) = |\Delta E| / SE_{combined}$')
    fig.colorbar(im3, ax=ax, label='Z', shrink=0.85)

    # Significance contours
    if np.any(fdr_mask):
        ax.contour(fdr_mask.astype(float), levels=[0.5],
                   colors='orange', linewidths=1.0, linestyles='--')
    if np.any(bonf_mask):
        ax.contour(bonf_mask.astype(float), levels=[0.5],
                   colors='red', linewidths=1.2)

    # Legend for contours
    from matplotlib.lines import Line2D
    legend_elements = [
        Line2D([0], [0], color='orange', ls='--', lw=1.0, label='FDR reject'),
        Line2D([0], [0], color='red', ls='-', lw=1.2, label='Bonferroni reject'),
    ]
    ax.legend(handles=legend_elements, loc='upper right', fontsize=7)

    # Hide ticks for cleaner look
    for ax_row in axes:
        for a in ax_row:
            a.set_xticks([])
            a.set_yticks([])

    fig.tight_layout(rect=[0, 0, 1, 0.96])
    fig.savefig(outpath)
    plt.close(fig)
    print(f"  Saved: {outpath}")


# ═══════════════════════════════════════════════════════════════════════════
# Visualization: Z Diagnostic (histogram + Q-Q)
# ═══════════════════════════════════════════════════════════════════════════
def plot_z_diagnostic(Z, valid, z_bonf, corr_result, scene, outpath):
    """Generate Z distribution diagnostic figure."""
    z_valid = Z[valid]
    n = len(z_valid)
    if n == 0:
        print("  WARNING: No valid pixels for Z diagnostic.")
        return

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
    fig.suptitle(f'Z Distribution Diagnostic — {scene}',
                 fontsize=13, fontweight='bold', y=0.98)

    # --- Left: Histogram ---
    ax1.hist(z_valid, bins=80, density=True, alpha=0.7,
             color='steelblue', edgecolor='white', linewidth=0.3,
             label=f'Observed Z  (n={n:,})')

    # Theoretical half-normal (since Z = |...|, Z >= 0)
    x_th = np.linspace(0, min(z_valid.max(), 8), 300)
    # Z is |standard normal|, so its pdf is the folded normal = 2*phi(z)
    pdf_th = 2.0 * sp_stats.norm.pdf(x_th)
    ax1.plot(x_th, pdf_th, 'k--', lw=1.5,
             label=r'Theoretical $2\varphi(z)$  (folded $\mathcal{N}(0,1)$)')

    # Threshold lines
    ax1.axvline(1.96, color='gray', ls=':', lw=0.8, label='z=1.96 (α=0.05)')
    ax1.axvline(z_bonf, color='red', ls='--', lw=1.0,
                label=f'Bonferroni z={z_bonf:.2f}')

    ax1.set_xlabel('Z')
    ax1.set_ylabel('Density')
    ax1.set_xlim(0, min(z_valid.max() * 1.05, 10))
    ax1.legend(fontsize=7.5)

    # KS test against folded normal (half-normal)
    # |N(0,1)| has CDF = 2*Phi(z) - 1 for z >= 0
    ks_stat, ks_p = sp_stats.kstest(z_valid, lambda z: 2 * sp_stats.norm.cdf(z) - 1)
    ax1.text(0.98, 0.95,
             f'KS stat = {ks_stat:.4f}\nKS p = {ks_p:.4f}\n'
             f'Mean Z = {np.mean(z_valid):.4f}\n'
             f'Median Z = {np.median(z_valid):.4f}',
             transform=ax1.transAxes, fontsize=8,
             verticalalignment='top', horizontalalignment='right',
             bbox=dict(boxstyle='round,pad=0.3', facecolor='lightyellow', alpha=0.9))

    # --- Right: Q-Q plot ---
    # Compare observed Z against theoretical half-normal quantiles
    sorted_z = np.sort(z_valid)
    n_qq = min(len(sorted_z), 5000)  # subsample for large images
    if n_qq < len(sorted_z):
        idx = np.linspace(0, len(sorted_z) - 1, n_qq, dtype=int)
        sorted_z_qq = sorted_z[idx]
    else:
        sorted_z_qq = sorted_z

    theoretical_q = sp_stats.halfnorm.ppf(
        np.linspace(1 / (n_qq + 1), n_qq / (n_qq + 1), n_qq))

    ax2.scatter(theoretical_q, sorted_z_qq, s=1, alpha=0.5, color='steelblue')
    max_val = max(theoretical_q.max(), sorted_z_qq.max())
    ax2.plot([0, max_val], [0, max_val], 'k--', lw=1, label='y = x (perfect match)')
    ax2.set_xlabel('Theoretical Quantiles (Half-Normal)')
    ax2.set_ylabel('Observed Z Quantiles')
    ax2.set_title('Q-Q Plot')
    ax2.legend(fontsize=8)
    ax2.set_aspect('equal', adjustable='box')
    ax2.set_xlim(0, max_val * 1.05)
    ax2.set_ylim(0, max_val * 1.05)

    fig.tight_layout(rect=[0, 0, 1, 0.95])
    fig.savefig(outpath)
    plt.close(fig)
    print(f"  Saved: {outpath}")

    return ks_stat, ks_p


# ═══════════════════════════════════════════════════════════════════════════
# Report Generation
# ═══════════════════════════════════════════════════════════════════════════
def generate_report(scene, W, H, E_cpu, SE_cpu, E_gpu, SE_gpu,
                    Z, delta, valid, corr, ks_stat, ks_p, report_path):
    """Write Markdown statistical report."""
    z_valid = Z[valid]
    n = corr['n_valid']
    total = W * H

    # Determine verdict
    #
    # Theoretical mean of |N(0,1)| = sqrt(2/pi) ~ 0.7979.
    # KS p-value is unreliable at large N (>100K pixels always rejects),
    # so we use KS statistic magnitude + FDR/Bonferroni counts.
    THEORETICAL_MEAN_Z = np.sqrt(2.0 / np.pi)  # 0.7979
    pct_fdr = 100.0 * corr['n_fdr'] / n if n > 0 else 0
    pct_bonf = 100.0 * corr['n_bonf'] / n if n > 0 else 0
    pct_raw = 100.0 * corr['n_raw'] / n if n > 0 else 0
    mean_z = np.mean(z_valid) if n > 0 else 0
    mean_z_deviation = abs(mean_z - THEORETICAL_MEAN_Z) / THEORETICAL_MEAN_Z

    if corr['n_bonf'] == 0 and corr['n_fdr'] == 0 and mean_z_deviation < 0.05:
        verdict = "PASS (strong)"
        verdict_desc = (f"Bonferroni = 0, FDR = 0, "
                        f"mean Z = {mean_z:.4f} (theoretical {THEORETICAL_MEAN_Z:.4f}, "
                        f"deviation {mean_z_deviation:.1%}). "
                        f"Raw reject rate {pct_raw:.2f}% ≈ expected 5%.")
    elif corr['n_bonf'] == 0 and pct_fdr <= 1.0:
        verdict = "PASS"
        verdict_desc = (f"Bonferroni = 0, FDR = {pct_fdr:.2f}% (≤ 1%), "
                        f"mean Z = {mean_z:.4f}.")
    elif pct_fdr <= 5.0:
        verdict = "MARGINAL"
        verdict_desc = f"FDR rejects = {pct_fdr:.2f}% (≤ 5%), review spatial pattern."
    else:
        verdict = "FAIL"
        verdict_desc = f"FDR rejects = {pct_fdr:.2f}% (> 5%), systematic divergence likely."

    # Find worst pixel
    if n > 0:
        worst_idx = np.unravel_index(np.nanargmax(Z), Z.shape)
        worst_z = Z[worst_idx]
        worst_delta = delta[worst_idx]
    else:
        worst_idx = (0, 0)
        worst_z = 0
        worst_delta = 0

    delta_valid = delta[valid]

    report = textwrap.dedent(f"""\
    # Per-Pixel Statistical Consistency Report

    **Scene**: {scene}
    **Resolution**: {W} x {H} ({total:,} pixels)
    **Valid pixels**: {n:,} / {total:,} ({100*n/total:.1f}%)
    **Generated**: pixel_consistency_analysis.py

    ---

    ## Method

    Per-pixel two-sample Z-test:

    $$Z_{{x,y}} = \\frac{{|E_{{gpu}} - E_{{cpu}}|}}{{\\sqrt{{SE_{{gpu}}^2 + SE_{{cpu}}^2}}}}$$

    Invalid pixels (background/zero-variance) excluded.
    Multiple comparison corrections: Bonferroni and Benjamini-Hochberg FDR (α=0.05).

    ---

    ## Z Distribution Statistics

    | Statistic | Value |
    |-----------|-------|
    | Mean Z | {mean_z:.4f} |
    | Median Z | {np.median(z_valid):.4f} |
    | Std Z | {np.std(z_valid):.4f} |
    | Max Z | {worst_z:.4f} @ pixel ({worst_idx[1]}, {worst_idx[0]}) |
    | Min Z | {np.min(z_valid):.4f} |

    ## ΔT Statistics

    | Statistic | Value |
    |-----------|-------|
    | Mean ΔT | {np.mean(delta_valid):.6f} K |
    | Median ΔT | {np.median(delta_valid):.6f} K |
    | Std ΔT | {np.std(delta_valid):.6f} K |
    | Max |ΔT| | {np.max(np.abs(delta_valid)):.6f} K @ pixel ({worst_idx[1]}, {worst_idx[0]}) |

    ---

    ## Multiple Comparison Results

    | Correction | Significant Pixels | Percentage | Threshold |
    |-----------|-------------------|------------|-----------|
    | None (α=0.05) | {corr['n_raw']:,} | {pct_raw:.2f}% | Z > 1.96 |
    | Bonferroni | {corr['n_bonf']:,} | {pct_bonf:.4f}% | Z > {corr['z_bonf']:.2f} (α_B={corr['alpha_bonf']:.2e}) |
    | BH-FDR (α=0.05) | {corr['n_fdr']:,} | {pct_fdr:.2f}% | adaptive |

    **Expected under null (pure MC noise)**: ~5% raw, ~0% Bonferroni, ≤5% FDR.

    ---

    ## Z Distribution Normality

    | Test | Statistic | p-value | Interpretation |
    |------|-----------|---------|---------------|
    | KS vs Half-Normal | {ks_stat:.4f} | {ks_p:.4f} | {'Consistent' if ks_p > 0.05 else 'Deviates'} with |Z| ~ HalfNormal(0,1) |

    ---

    ## Verdict

    **{verdict}**: {verdict_desc}

    ---

    ## Figures

    - `{scene}_difference_map.png` — 2x2 panel: E_cpu, E_gpu, ΔT, Z with significance contours
    - `{scene}_z_diagnostic.png` — Z histogram + Q-Q plot

    ---

    *Generated by pixel_consistency_analysis.py*
    """)

    Path(report_path).write_text(report, encoding='utf-8')
    print(f"  Saved: {report_path}")


# ═══════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════
def main():
    parser = argparse.ArgumentParser(
        description='Per-pixel consistency analysis between GPU and CPU .ht images.')
    parser.add_argument('--cpu', required=True, help='Path to CPU .ht file')
    parser.add_argument('--gpu', required=True, help='Path to GPU .ht file')
    parser.add_argument('--scene', default='scene',
                        help='Scene name for titles and filenames')
    parser.add_argument('--outdir', default='.',
                        help='Directory for output images')
    parser.add_argument('--report', default=None,
                        help='Path for Markdown report (default: {outdir}/{scene}_report.md)')
    parser.add_argument('--alpha', type=float, default=0.05,
                        help='Significance level (default: 0.05)')
    args = parser.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    if args.report is None:
        args.report = str(outdir / f'{args.scene}_report.md')

    print(f"=== Per-Pixel Consistency Analysis: {args.scene} ===")
    print()

    # --- Load ---
    print("[1/5] Loading .ht files...")
    W_cpu, H_cpu, E_cpu, SE_cpu = load_ht(args.cpu)
    W_gpu, H_gpu, E_gpu, SE_gpu = load_ht(args.gpu)
    print(f"  CPU: {W_cpu}x{H_cpu}  GPU: {W_gpu}x{H_gpu}")

    if (W_cpu, H_cpu) != (W_gpu, H_gpu):
        sys.exit(f"ERROR: Resolution mismatch: CPU={W_cpu}x{H_cpu}, GPU={W_gpu}x{H_gpu}")

    W, H = W_cpu, H_cpu

    # --- Z-test ---
    print("[2/5] Computing per-pixel Z statistics...")
    Z, delta, valid = compute_z_map(E_cpu, SE_cpu, E_gpu, SE_gpu)
    n_valid = int(np.sum(valid))
    print(f"  Valid pixels: {n_valid:,} / {W*H:,} ({100*n_valid/(W*H):.1f}%)")
    z_valid = Z[valid]
    if n_valid > 0:
        print(f"  Z: mean={np.mean(z_valid):.4f}, "
              f"median={np.median(z_valid):.4f}, "
              f"max={np.max(z_valid):.4f}")

    # --- Corrections ---
    print("[3/5] Applying multiple comparison corrections...")
    corr = multiple_corrections(Z, valid, alpha=args.alpha)
    print(f"  Raw (p<{args.alpha}):     {corr['n_raw']:,} "
          f"({100*corr['n_raw']/max(n_valid,1):.2f}%)")
    print(f"  Bonferroni:      {corr['n_bonf']:,} "
          f"({100*corr['n_bonf']/max(n_valid,1):.4f}%)")
    print(f"  BH-FDR:          {corr['n_fdr']:,} "
          f"({100*corr['n_fdr']/max(n_valid,1):.2f}%)")

    # --- Plots ---
    print("[4/5] Generating figures...")
    diff_path = outdir / f'{args.scene}_difference_map.png'
    plot_difference_map(E_cpu, E_gpu, delta, Z, valid,
                        corr['bonf_reject_mask'], corr['fdr_reject_mask'],
                        args.scene, diff_path)

    diag_path = outdir / f'{args.scene}_z_diagnostic.png'
    ks_result = plot_z_diagnostic(Z, valid, corr['z_bonf'], corr,
                                  args.scene, diag_path)
    ks_stat = ks_result[0] if ks_result else 0
    ks_p = ks_result[1] if ks_result else 1.0

    # --- Report ---
    print("[5/5] Generating report...")
    generate_report(args.scene, W, H, E_cpu, SE_cpu, E_gpu, SE_gpu,
                    Z, delta, valid, corr, ks_stat, ks_p, args.report)

    # --- Summary ---
    THEORETICAL_MEAN_Z = np.sqrt(2.0 / np.pi)
    pct_fdr = 100.0 * corr['n_fdr'] / max(n_valid, 1)
    mean_z_dev = abs(np.mean(Z[valid]) - THEORETICAL_MEAN_Z) / THEORETICAL_MEAN_Z if n_valid > 0 else 1.0
    if corr['n_bonf'] == 0 and corr['n_fdr'] == 0 and mean_z_dev < 0.05:
        verdict = "PASS (strong)"
    elif corr['n_bonf'] == 0 and pct_fdr <= 1.0:
        verdict = "PASS"
    elif pct_fdr <= 5.0:
        verdict = "MARGINAL"
    else:
        verdict = "FAIL"

    print()
    print(f"  *** VERDICT: {verdict} ***")
    print()


if __name__ == '__main__':
    main()
