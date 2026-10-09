#!/usr/bin/env python3
"""
逐像素均值差异图 + 统计显著性检验（汉化版）。

基于同目录 pixel_consistency_analysis.py 的汉化版本，仅翻译图片中
的标题、坐标轴标签、图例与文本注释，不改动数据处理逻辑与命令行
选项。输出 PNG / 报告文件名追加 ``_zh`` 后缀以避免覆盖原文件。

用法：
    python pixel_consistency_analysis_zh.py \\
        --cpu  path/to/cpu.ht \\
        --gpu  path/to/gpu.ht \\
        --scene cube_IR \\
        --outdir img/ \\
        --report pixel_consistency_report.md

输出：
    {outdir}/{scene}_difference_map_zh.png  -- 2x2 面板（E_cpu、E_gpu、ΔT、Z）
    {outdir}/{scene}_z_diagnostic_zh.png    -- Z 直方图 + Q-Q 图
    {report 同名追加 _zh}                   -- Markdown 统计报告
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
# 出版质量默认设置（含中文字体回退，与 plot_comparison_zh.py 一致）
# ═══════════════════════════════════════════════════════════════════════════
_CJK_FONTS = [
    'Microsoft YaHei', 'SimHei', 'Source Han Sans SC',
    'Noto Sans CJK SC', 'PingFang SC', 'Heiti SC', 'WenQuanYi Zen Hei',
]

plt.rcParams.update({
    'font.family': 'sans-serif',
    'font.sans-serif': _CJK_FONTS + ['DejaVu Sans'],
    'axes.unicode_minus': False,
    'font.size': 10,
    'axes.titlesize': 11,
    'axes.labelsize': 10,
    'xtick.labelsize': 9,
    'ytick.labelsize': 9,
    'legend.fontsize': 8.5,
    'mathtext.fontset': 'dejavuserif',
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
SE_FLOOR = 1e-12


def compute_z_map(E_cpu, SE_cpu, E_gpu, SE_gpu):
    SE_comb = np.sqrt(SE_gpu**2 + SE_cpu**2)
    delta = E_gpu - E_cpu

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

    p_values = 2.0 * (1.0 - sp_stats.norm.cdf(z_valid))

    alpha_bonf = alpha / n
    z_bonf = sp_stats.norm.ppf(1.0 - alpha_bonf / 2.0)
    bonf_reject = z_valid > z_bonf
    n_bonf = int(np.sum(bonf_reject))

    sorted_idx = np.argsort(p_values)
    sorted_p = p_values[sorted_idx]
    thresholds = np.arange(1, n + 1) / n * alpha
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

    n_raw = int(np.sum(p_values < alpha))

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
    """生成 2x2 面板差异图。"""
    fig, axes = plt.subplots(2, 2, figsize=(12, 10))
    fig.suptitle(f'逐像素均值差异图 —— {scene}',
                 fontsize=13, fontweight='bold', y=0.98)

    vmin_t = min(np.nanmin(E_cpu[valid]), np.nanmin(E_gpu[valid]))
    vmax_t = max(np.nanmax(E_cpu[valid]), np.nanmax(E_gpu[valid]))

    # --- 左上：CPU 温度 ---
    ax = axes[0, 0]
    im0 = ax.imshow(E_cpu, cmap='inferno', vmin=vmin_t, vmax=vmax_t,
                     origin='upper', aspect='equal')
    ax.set_title(f'CPU 深度优先  $E_{{cpu}}$', color=C_CPU)
    fig.colorbar(im0, ax=ax, label='T [K]', shrink=0.85)

    # --- 右上：GPU 温度 ---
    ax = axes[0, 1]
    im1 = ax.imshow(E_gpu, cmap='inferno', vmin=vmin_t, vmax=vmax_t,
                     origin='upper', aspect='equal')
    ax.set_title(f'GPU 波前  $E_{{gpu}}$', color=C_GPU)
    fig.colorbar(im1, ax=ax, label='T [K]', shrink=0.85)

    # --- 左下：Delta T（有符号） ---
    ax = axes[1, 0]
    delta_display = np.where(valid, delta, np.nan)
    abs_max = np.nanmax(np.abs(delta_display))
    if abs_max < SE_FLOOR:
        abs_max = 1.0
    norm_delta = TwoSlopeNorm(vmin=-abs_max, vcenter=0, vmax=abs_max)
    im2 = ax.imshow(delta_display, cmap='RdBu_r', norm=norm_delta,
                     origin='upper', aspect='equal')
    ax.set_title(r'$\Delta T = E_{gpu} - E_{cpu}$')
    cbar2 = fig.colorbar(im2, ax=ax, label='$\\Delta T$ [K]', shrink=0.85)
    mean_delta = np.nanmean(delta_display)
    max_abs = np.nanmax(np.abs(delta_display))
    ax.text(0.02, 0.02,
            f'均值={mean_delta:.4f} K\n最大|ΔT|={max_abs:.4f} K',
            transform=ax.transAxes, fontsize=7.5,
            verticalalignment='bottom',
            bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.8))

    # --- 右下：Z 图与显著性等高线 ---
    ax = axes[1, 1]
    Z_display = np.where(valid, Z, np.nan)
    im3 = ax.imshow(Z_display, cmap='viridis', vmin=0,
                     vmax=min(np.nanmax(Z_display), 8.0),
                     origin='upper', aspect='equal')
    ax.set_title(r'$Z(x,y) = |\Delta E| / SE_{combined}$')
    fig.colorbar(im3, ax=ax, label='Z', shrink=0.85)

    if np.any(fdr_mask):
        ax.contour(fdr_mask.astype(float), levels=[0.5],
                   colors='orange', linewidths=1.0, linestyles='--')
    if np.any(bonf_mask):
        ax.contour(bonf_mask.astype(float), levels=[0.5],
                   colors='red', linewidths=1.2)

    from matplotlib.lines import Line2D
    legend_elements = [
        Line2D([0], [0], color='orange', ls='--', lw=1.0, label='FDR 拒绝'),
        Line2D([0], [0], color='red', ls='-', lw=1.2, label='Bonferroni 拒绝'),
    ]
    ax.legend(handles=legend_elements, loc='upper right', fontsize=7)

    for ax_row in axes:
        for a in ax_row:
            a.set_xticks([])
            a.set_yticks([])

    fig.tight_layout(rect=[0, 0, 1, 0.96])
    fig.savefig(outpath)
    plt.close(fig)
    print(f"  已保存：{outpath}")


# ═══════════════════════════════════════════════════════════════════════════
# Visualization: Z Diagnostic (histogram + Q-Q)
# ═══════════════════════════════════════════════════════════════════════════
def plot_z_diagnostic(Z, valid, z_bonf, corr_result, scene, outpath):
    """生成 Z 分布诊断图。"""
    z_valid = Z[valid]
    n = len(z_valid)
    if n == 0:
        print("  警告：没有有效像素，跳过 Z 诊断图。")
        return

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
    fig.suptitle(f'Z 分布诊断 —— {scene}',
                 fontsize=13, fontweight='bold', y=0.98)

    # --- 左：直方图 ---
    ax1.hist(z_valid, bins=80, density=True, alpha=0.7,
             color='steelblue', edgecolor='white', linewidth=0.3,
             label=f'观测 Z  (n={n:,})')

    # 理论半正态（因 Z = |...|，故 Z >= 0）
    x_th = np.linspace(0, min(z_valid.max(), 8), 300)
    pdf_th = 2.0 * sp_stats.norm.pdf(x_th)
    ax1.plot(x_th, pdf_th, 'k--', lw=1.5,
             label=r'理论 $2\varphi(z)$  （折叠 $\mathcal{N}(0,1)$）')

    ax1.axvline(1.96, color='gray', ls=':', lw=0.8, label='z=1.96 (α=0.05)')
    ax1.axvline(z_bonf, color='red', ls='--', lw=1.0,
                label=f'Bonferroni z={z_bonf:.2f}')

    ax1.set_xlabel('Z')
    ax1.set_ylabel('密度')
    ax1.set_xlim(0, min(z_valid.max() * 1.05, 10))
    ax1.legend(fontsize=7.5)

    # 与折叠正态（半正态）的 KS 检验
    ks_stat, ks_p = sp_stats.kstest(z_valid, lambda z: 2 * sp_stats.norm.cdf(z) - 1)
    ax1.text(0.98, 0.95,
             f'KS 统计量 = {ks_stat:.4f}\nKS p = {ks_p:.4f}\n'
             f'Z 均值 = {np.mean(z_valid):.4f}\n'
             f'Z 中位数 = {np.median(z_valid):.4f}',
             transform=ax1.transAxes, fontsize=8,
             verticalalignment='top', horizontalalignment='right',
             bbox=dict(boxstyle='round,pad=0.3', facecolor='lightyellow', alpha=0.9))

    # --- 右：Q-Q 图 ---
    sorted_z = np.sort(z_valid)
    n_qq = min(len(sorted_z), 5000)
    if n_qq < len(sorted_z):
        idx = np.linspace(0, len(sorted_z) - 1, n_qq, dtype=int)
        sorted_z_qq = sorted_z[idx]
    else:
        sorted_z_qq = sorted_z

    theoretical_q = sp_stats.halfnorm.ppf(
        np.linspace(1 / (n_qq + 1), n_qq / (n_qq + 1), n_qq))

    ax2.scatter(theoretical_q, sorted_z_qq, s=1, alpha=0.5, color='steelblue')
    max_val = max(theoretical_q.max(), sorted_z_qq.max())
    ax2.plot([0, max_val], [0, max_val], 'k--', lw=1, label='y = x（完美匹配）')
    ax2.set_xlabel('理论分位数（半正态）')
    ax2.set_ylabel('观测 Z 分位数')
    ax2.set_title('Q-Q 图')
    ax2.legend(fontsize=8)
    ax2.set_aspect('equal', adjustable='box')
    ax2.set_xlim(0, max_val * 1.05)
    ax2.set_ylim(0, max_val * 1.05)

    fig.tight_layout(rect=[0, 0, 1, 0.95])
    fig.savefig(outpath)
    plt.close(fig)
    print(f"  已保存：{outpath}")

    return ks_stat, ks_p


# ═══════════════════════════════════════════════════════════════════════════
# Report Generation
# ═══════════════════════════════════════════════════════════════════════════
def generate_report(scene, W, H, E_cpu, SE_cpu, E_gpu, SE_gpu,
                    Z, delta, valid, corr, ks_stat, ks_p, report_path):
    """写出 Markdown 统计报告。"""
    z_valid = Z[valid]
    n = corr['n_valid']
    total = W * H

    THEORETICAL_MEAN_Z = np.sqrt(2.0 / np.pi)  # 0.7979
    pct_fdr = 100.0 * corr['n_fdr'] / n if n > 0 else 0
    pct_bonf = 100.0 * corr['n_bonf'] / n if n > 0 else 0
    pct_raw = 100.0 * corr['n_raw'] / n if n > 0 else 0
    mean_z = np.mean(z_valid) if n > 0 else 0
    mean_z_deviation = abs(mean_z - THEORETICAL_MEAN_Z) / THEORETICAL_MEAN_Z

    if corr['n_bonf'] == 0 and corr['n_fdr'] == 0 and mean_z_deviation < 0.05:
        verdict = "通过（强）"
        verdict_desc = (f"Bonferroni = 0，FDR = 0，"
                        f"Z 均值 = {mean_z:.4f}（理论值 {THEORETICAL_MEAN_Z:.4f}，"
                        f"偏差 {mean_z_deviation:.1%}）。"
                        f"原始拒绝率 {pct_raw:.2f}% ≈ 期望 5%。")
    elif corr['n_bonf'] == 0 and pct_fdr <= 1.0:
        verdict = "通过"
        verdict_desc = (f"Bonferroni = 0，FDR = {pct_fdr:.2f}%（≤ 1%），"
                        f"Z 均值 = {mean_z:.4f}。")
    elif pct_fdr <= 5.0:
        verdict = "边界"
        verdict_desc = f"FDR 拒绝 = {pct_fdr:.2f}%（≤ 5%），需检查空间分布。"
    else:
        verdict = "未通过"
        verdict_desc = f"FDR 拒绝 = {pct_fdr:.2f}%（> 5%），可能存在系统性偏差。"

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
    # 逐像素统计一致性报告

    **场景**：{scene}
    **分辨率**：{W} x {H}（{total:,} 像素）
    **有效像素**：{n:,} / {total:,}（{100*n/total:.1f}%）
    **生成脚本**：pixel_consistency_analysis_zh.py

    ---

    ## 方法

    逐像素双样本 Z 检验：

    $$Z_{{x,y}} = \\frac{{|E_{{gpu}} - E_{{cpu}}|}}{{\\sqrt{{SE_{{gpu}}^2 + SE_{{cpu}}^2}}}}$$

    无效像素（背景 / 零方差）已剔除。
    多重比较校正：Bonferroni 与 Benjamini-Hochberg FDR（α=0.05）。

    ---

    ## Z 分布统计

    | 统计量 | 数值 |
    |--------|------|
    | Z 均值 | {mean_z:.4f} |
    | Z 中位数 | {np.median(z_valid):.4f} |
    | Z 标准差 | {np.std(z_valid):.4f} |
    | Z 最大值 | {worst_z:.4f} @ 像素 ({worst_idx[1]}, {worst_idx[0]}) |
    | Z 最小值 | {np.min(z_valid):.4f} |

    ## ΔT 统计

    | 统计量 | 数值 |
    |--------|------|
    | ΔT 均值 | {np.mean(delta_valid):.6f} K |
    | ΔT 中位数 | {np.median(delta_valid):.6f} K |
    | ΔT 标准差 | {np.std(delta_valid):.6f} K |
    | 最大 \|ΔT\| | {np.max(np.abs(delta_valid)):.6f} K @ 像素 ({worst_idx[1]}, {worst_idx[0]}) |

    ---

    ## 多重比较结果

    | 校正方式 | 显著像素 | 占比 | 阈值 |
    |----------|----------|------|------|
    | 无（α=0.05） | {corr['n_raw']:,} | {pct_raw:.2f}% | Z > 1.96 |
    | Bonferroni | {corr['n_bonf']:,} | {pct_bonf:.4f}% | Z > {corr['z_bonf']:.2f}（α_B={corr['alpha_bonf']:.2e}） |
    | BH-FDR（α=0.05） | {corr['n_fdr']:,} | {pct_fdr:.2f}% | 自适应 |

    **零假设下（纯 MC 噪声）的期望**：原始约 5%，Bonferroni 约 0%，FDR ≤ 5%。

    ---

    ## Z 分布正态性

    | 检验 | 统计量 | p 值 | 解释 |
    |------|--------|------|------|
    | KS vs 半正态 | {ks_stat:.4f} | {ks_p:.4f} | {'符合' if ks_p > 0.05 else '偏离'} \|Z\| ~ HalfNormal(0,1) |

    ---

    ## 结论

    **{verdict}**：{verdict_desc}

    ---

    ## 图表

    - `{scene}_difference_map_zh.png` —— 2x2 面板：E_cpu、E_gpu、ΔT、Z 与显著性等高线
    - `{scene}_z_diagnostic_zh.png` —— Z 直方图 + Q-Q 图

    ---

    *由 pixel_consistency_analysis_zh.py 生成*
    """)

    Path(report_path).write_text(report, encoding='utf-8')
    print(f"  已保存：{report_path}")


# ═══════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════
def _zh_suffix(path_str):
    """在给定路径的扩展名前插入 _zh 后缀。"""
    p = Path(path_str)
    return str(p.with_name(f'{p.stem}_zh{p.suffix}'))


def main():
    parser = argparse.ArgumentParser(
        description='GPU 与 CPU .ht 图像间的逐像素一致性分析（汉化版）。')
    parser.add_argument('--cpu', required=True, help='Path to CPU .ht file')
    parser.add_argument('--gpu', required=True, help='Path to GPU .ht file')
    parser.add_argument('--scene', default='scene',
                        help='Scene name for titles and filenames')
    parser.add_argument('--outdir', default=None,
                        help='Directory for output images '
                             '(default: guide/stat_consistency_test/img/)')
    parser.add_argument('--report', default=None,
                        help='Path for Markdown report (default: {outdir}/{scene}_report.md)')
    parser.add_argument('--alpha', type=float, default=0.05,
                        help='Significance level (default: 0.05)')
    args = parser.parse_args()

    # 汉化版固定输出到 guide/stat_consistency_test/img/ 以免覆盖英文版
    if args.outdir is None:
        outdir = Path(__file__).resolve().parent.parent / 'img'
    else:
        outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    if args.report is None:
        args.report = str(outdir / f'{args.scene}_report.md')

    # 输出文件追加 _zh 后缀以免覆盖英文版
    diff_path = outdir / f'{args.scene}_difference_map_zh.png'
    diag_path = outdir / f'{args.scene}_z_diagnostic_zh.png'
    report_path = _zh_suffix(args.report)

    print(f"=== 逐像素一致性分析：{args.scene} ===")
    print()

    # --- Load ---
    print("[1/5] 加载 .ht 文件...")
    W_cpu, H_cpu, E_cpu, SE_cpu = load_ht(args.cpu)
    W_gpu, H_gpu, E_gpu, SE_gpu = load_ht(args.gpu)
    print(f"  CPU: {W_cpu}x{H_cpu}  GPU: {W_gpu}x{H_gpu}")

    if (W_cpu, H_cpu) != (W_gpu, H_gpu):
        sys.exit(f"ERROR: Resolution mismatch: CPU={W_cpu}x{H_cpu}, GPU={W_gpu}x{H_gpu}")

    W, H = W_cpu, H_cpu

    # --- Z-test ---
    print("[2/5] 计算逐像素 Z 统计...")
    Z, delta, valid = compute_z_map(E_cpu, SE_cpu, E_gpu, SE_gpu)
    n_valid = int(np.sum(valid))
    print(f"  有效像素：{n_valid:,} / {W*H:,}（{100*n_valid/(W*H):.1f}%）")
    z_valid = Z[valid]
    if n_valid > 0:
        print(f"  Z：均值={np.mean(z_valid):.4f}，"
              f"中位数={np.median(z_valid):.4f}，"
              f"最大值={np.max(z_valid):.4f}")

    # --- Corrections ---
    print("[3/5] 应用多重比较校正...")
    corr = multiple_corrections(Z, valid, alpha=args.alpha)
    print(f"  原始 (p<{args.alpha}):  {corr['n_raw']:,} "
          f"({100*corr['n_raw']/max(n_valid,1):.2f}%)")
    print(f"  Bonferroni:      {corr['n_bonf']:,} "
          f"({100*corr['n_bonf']/max(n_valid,1):.4f}%)")
    print(f"  BH-FDR:          {corr['n_fdr']:,} "
          f"({100*corr['n_fdr']/max(n_valid,1):.2f}%)")

    # --- Plots ---
    print("[4/5] 生成图表...")
    plot_difference_map(E_cpu, E_gpu, delta, Z, valid,
                        corr['bonf_reject_mask'], corr['fdr_reject_mask'],
                        args.scene, diff_path)

    ks_result = plot_z_diagnostic(Z, valid, corr['z_bonf'], corr,
                                  args.scene, diag_path)
    ks_stat = ks_result[0] if ks_result else 0
    ks_p = ks_result[1] if ks_result else 1.0

    # --- Report ---
    print("[5/5] 生成报告...")
    generate_report(args.scene, W, H, E_cpu, SE_cpu, E_gpu, SE_gpu,
                    Z, delta, valid, corr, ks_stat, ks_p, report_path)

    # --- Summary ---
    THEORETICAL_MEAN_Z = np.sqrt(2.0 / np.pi)
    pct_fdr = 100.0 * corr['n_fdr'] / max(n_valid, 1)
    mean_z_dev = abs(np.mean(Z[valid]) - THEORETICAL_MEAN_Z) / THEORETICAL_MEAN_Z if n_valid > 0 else 1.0
    if corr['n_bonf'] == 0 and corr['n_fdr'] == 0 and mean_z_dev < 0.05:
        verdict = "通过（强）"
    elif corr['n_bonf'] == 0 and pct_fdr <= 1.0:
        verdict = "通过"
    elif pct_fdr <= 5.0:
        verdict = "边界"
    else:
        verdict = "未通过"

    print()
    print(f"  *** 结论：{verdict} ***")
    print()


if __name__ == '__main__':
    main()
