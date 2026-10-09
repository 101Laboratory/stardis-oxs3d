#!/usr/bin/env python3
"""
数值一致性证明图（汉化版）—— 物理复杂度阶梯。

基于 plot_comparison_v2.py 的最终版本，对图片中的标题、坐标轴标签、
图例和文本注释进行汉化，便于中文报告使用。

输出图（与英文版同名）：
  1. A2 —— 含体积源的稳态导热（抛物线）
  2. A3 —— 接触热阻分段线性温度族
  3. D1 —— 对流瞬态（指数衰减）
  4. E3 —— sin × exp 非稳态解析解（双子图）
  5. E5 —— 多域双探针瞬态
  6. 汇总 —— σ 直方图 + 对等图
"""

import argparse
import os
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.gridspec import GridSpec
import numpy as np
import pandas as pd

# ---------------------------------------------------------------------------
# 出版质量默认设置（含中文字体回退）
# ---------------------------------------------------------------------------
# 在 Windows 上 Microsoft YaHei / SimHei 通常可用；macOS 用 PingFang/Heiti；
# Linux 可安装 Noto Sans CJK SC。matplotlib 会在列表中按顺序回退。
_CJK_FONTS = [
    'Microsoft YaHei', 'SimHei', 'Source Han Sans SC',
    'Noto Sans CJK SC', 'PingFang SC', 'Heiti SC', 'WenQuanYi Zen Hei',
]

plt.rcParams.update({
    'font.family': 'sans-serif',
    'font.sans-serif': _CJK_FONTS + ['DejaVu Sans'],
    'axes.unicode_minus': False,   # 负号显示为 ASCII '-'
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

# ---------------------------------------------------------------------------
# 视觉常量
# ---------------------------------------------------------------------------
C_THEORY = '#555555'
C_GPU    = '#D62828'
C_CPU    = '#1D3557'

LS_THEORY = '--'
LW_THEORY = 1.5

MK_GPU  = 's'   # 方形
MK_CPU  = 'o'   # 圆形
MS_GPU  = 5
MS_CPU  = 5
CAPSIZE = 3

# 通用中文标签
LBL_ANALYTIC = '解析解'
LBL_OURS     = '本方法'
LBL_BASE     = '基线'

# ---------------------------------------------------------------------------
# 辅助函数
# ---------------------------------------------------------------------------
def load_csv(path):
    df = pd.read_csv(path)
    df.columns = df.columns.str.strip()
    for c in ['E', 'SE', 'ref', 'sigma',
              'probe_x', 'probe_y', 'probe_z', 'time']:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors='coerce')
    return df


def add_grid(ax):
    ax.grid(True, ls=':', lw=0.4, alpha=0.6)


def sigma_errbar(df, scale=3):
    """根据 SE 列返回 3σ 误差棒数组。"""
    return scale * df['SE'].values


def save_fig(fig, name, img_dir):
    path = img_dir / f'{name}.png'
    fig.savefig(str(path))
    plt.close(fig)
    print(f'  已保存 {path}')


def add_residual_ax(ax_r, datasets, x_key='probe_x'):
    """在给定坐标轴上绘制残差 σ = (E - ref) / SE 散点。

    参数
    ----
    datasets : list of (DataFrame, color, marker) 元组
    x_key    : 横轴对应的列名
    """
    for df, color, marker in datasets:
        v = df.dropna(subset=['E', 'ref', 'SE']).copy()
        v = v[v['SE'] > 1e-15]
        if len(v) == 0:
            continue
        sigma_v = (v['E'] - v['ref']) / v['SE']
        ax_r.scatter(v[x_key], sigma_v, marker=marker, c=color,
                     s=25, zorder=3, alpha=0.8,
                     edgecolors='white', linewidth=0.3)
    ax_r.axhspan(-3, 3, color='#2a9d8f', alpha=0.08, zorder=0)
    ax_r.axhline(0, color='gray', ls='-', lw=0.5)
    for y in [3, -3]:
        ax_r.axhline(y, color='red', ls='--', lw=0.7, alpha=0.5)
    ax_r.set_ylabel(r'$\sigma$', fontsize=9)
    add_grid(ax_r)


# =============================================================================
# 图 1：A2 —— 体积源抛物线（带残差面板）
# =============================================================================
def fig_a2(gpu_dir, cpu_dir, img_dir):
    """带体积源的稳态导热 → 抛物线温度分布 T(x)。"""
    print('图 1：A2 体积源抛物线')

    P0 = 10.0       # W/m3
    LAMBDA = 0.1     # W/(m K)
    T0 = 320.0       # K

    x_th = np.linspace(0.0, 1.0, 300)
    x_off = x_th - 0.5
    T_th = P0 / (2 * LAMBDA) * (0.25 - x_off**2) + T0

    gpu = load_csv(gpu_dir / 'A2.csv')
    cpu = load_csv(cpu_dir / 'A2.csv')

    fig = plt.figure(figsize=(6.5, 5.0), layout='constrained')
    gs = GridSpec(2, 1, height_ratios=[3, 1], hspace=0.08, figure=fig)
    ax = fig.add_subplot(gs[0])
    ax_r = fig.add_subplot(gs[1], sharex=ax)

    # --- 主图 ---
    ax.plot(x_th, T_th, color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY,
            label=LBL_ANALYTIC, zorder=1)

    ax.errorbar(gpu['probe_x'], gpu['E'], yerr=sigma_errbar(gpu),
                fmt=MK_GPU, color=C_GPU, ms=MS_GPU, capsize=CAPSIZE,
                label=LBL_OURS, zorder=3)

    ax.errorbar(cpu['probe_x'], cpu['E'], yerr=sigma_errbar(cpu),
                fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                label=LBL_BASE, zorder=2)

    T_max = P0 / (8 * LAMBDA) + T0
    ax.annotate(f'$T_{{max}} = {T_max:.1f}$ K',
                xy=(0.5, T_max), xytext=(0.70, T_max + 0.5),
                fontsize=8.5, color=C_THEORY,
                arrowprops=dict(arrowstyle='->', color=C_THEORY, lw=0.8))

    ax.set_ylabel('温度 $T$ [K]')
    ax.set_title('含体积源的稳态导热',
                 fontweight='bold', pad=12)
    ax.legend(loc='lower center', ncol=3, framealpha=0.9)
    add_grid(ax)
    plt.setp(ax.get_xticklabels(), visible=False)

    # --- 残差面板 ---
    add_residual_ax(ax_r,
                    [(gpu, C_GPU, MK_GPU), (cpu, C_CPU, MK_CPU)])
    ax_r.set_xlabel('位置 $x$ [m]')

    save_fig(fig, 'fig1_a2_parabola', img_dir)


# =============================================================================
# 图 2：A3 —— 接触热阻分段线性温度族
# =============================================================================
def fig_a3(gpu_dir, cpu_dir, img_dir):
    """带接触热阻的多材料界面。"""
    print('图 2：A3 接触热阻族')

    LAMBDA1 = 0.1
    LAMBDA2 = 0.2
    L = 4.0
    x0 = 3.0
    T_left = 0.0
    T_right = 100.0

    def analytical_a3(x_arr, R):
        q = (T_right - T_left) / (x0 / LAMBDA1 + R + (L - x0) / LAMBDA2)
        return np.where(x_arr <= x0,
                        T_left + q / LAMBDA1 * x_arr,
                        T_right - q / LAMBDA2 * (L - x_arr))

    R_vals_gpu = [0.01, 0.1, 1.0, 10.0, 100.0]
    cmap = plt.cm.viridis_r
    colors_r = [cmap(i / (len(R_vals_gpu) - 1))
                for i in range(len(R_vals_gpu))]

    gpu = load_csv(gpu_dir / 'A3.csv')
    cpu = load_csv(cpu_dir / 'A3.csv')

    fig, ax = plt.subplots(figsize=(7, 4.5))
    x_th = np.linspace(0, L, 500)

    # 5 个 GPU R 值对应的解析曲线（按颜色编码）
    for i, R in enumerate(R_vals_gpu):
        T_th = analytical_a3(x_th, R)
        ax.plot(x_th, T_th, color=colors_r[i], ls=LS_THEORY, lw=LW_THEORY,
                alpha=0.8, zorder=1)

    # GPU 数据 —— 所有 R 组使用统一红色标记
    gpu_plotted = False
    for i, R in enumerate(R_vals_gpu):
        candidates = [f'R={R}', f'R={R:g}', f'R={R:.0e}']
        if R == int(R):
            candidates.append(f'R={int(R)}')
        mask = gpu['sub_id'].isin(candidates)
        g = gpu[mask]
        if len(g) > 0:
            lbl = LBL_OURS if not gpu_plotted else None
            ax.errorbar(g['probe_x'], g['E'], yerr=sigma_errbar(g),
                        fmt=MK_GPU, color=C_GPU, ms=MS_GPU,
                        capsize=CAPSIZE, zorder=3, label=lbl)
            gpu_plotted = True

    # CPU 数据 —— 全部 R 值（2D + 3D），统一蓝色标记
    if len(cpu) > 0:
        ax.errorbar(cpu['probe_x'], cpu['E'], yerr=sigma_errbar(cpu),
                    fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                    label=LBL_BASE, zorder=2, alpha=0.7)

    # 界面位置标记
    ax.axvline(x=x0, color='gray', ls=':', lw=0.8, alpha=0.5)
    ax.text(x0 + 0.05, 5, '界面', fontsize=7.5, color='gray',
            rotation=90, va='bottom')

    # 图例
    handles = [
        Line2D([0], [0], color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY,
               label=LBL_ANALYTIC),
        Line2D([0], [0], marker=MK_GPU, color='w', markerfacecolor=C_GPU,
               ms=MS_GPU + 1, label=LBL_OURS),
        Line2D([0], [0], marker=MK_CPU, color='w', markerfacecolor=C_CPU,
               ms=MS_CPU + 1, label=LBL_BASE),
    ]
    for i, R in enumerate(R_vals_gpu):
        handles.append(Line2D([0], [0], color=colors_r[i], ls=LS_THEORY,
                              lw=2, label=f'$R = {R:g}$'))
    ax.legend(handles=handles, loc='upper left', fontsize=7.5, ncol=2,
              framealpha=0.9)

    ax.set_xlabel('位置 $x$ [m]')
    ax.set_ylabel('温度 $T$ [K]')
    ax.set_title('接触热阻：分段线性温度族',
                 fontweight='bold', pad=12)
    ax.set_xlim(-0.1, L + 0.1)
    ax.set_ylim(-5, 110)
    add_grid(ax)
    fig.tight_layout()
    save_fig(fig, 'fig2_a3_contact_resistance', img_dir)


# =============================================================================
# 图 3：D1 —— 对流瞬态（指数衰减）
# =============================================================================
def fig_d1(gpu_dir, cpu_dir, img_dir):
    """对流瞬态 —— 指数趋近 T_inf。"""
    print('图 3：D1 对流瞬态')

    T_init = 280.0   # K
    T_inf = 325.0    # K
    nu = 1.2         # 1/s

    t_th = np.linspace(0, 5, 300)
    T_th = T_init * np.exp(-nu * t_th) + T_inf * (1 - np.exp(-nu * t_th))

    cpu = load_csv(cpu_dir / 'D1.csv')
    cpu_3d = cpu[cpu['sub_id'] == '3D'].copy()

    gpu = load_csv(gpu_dir / 'D1.csv')

    # 区分 GPU 瞬态点（sub_id='transient'）与稳态点
    gpu_transient = gpu[gpu['sub_id'] == 'transient'].copy()
    gpu_steady = gpu[gpu['sub_id'] != 'transient'].copy()

    fig, ax = plt.subplots(figsize=(6.5, 4.2))

    ax.plot(t_th, T_th, color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY,
            label=LBL_ANALYTIC, zorder=1)

    # T_inf 渐近线
    ax.axhline(y=T_inf, color=C_THEORY, ls=':', lw=0.7, alpha=0.4)
    ax.text(4.5, T_inf + 0.5, f'$T_\\infty = {T_inf:.0f}$ K',
            fontsize=8, color=C_THEORY, ha='right')

    # T_0 标注
    ax.annotate(f'$T_0 = {T_init:.0f}$ K',
                xy=(0, T_init), xytext=(0.5, T_init + 2),
                fontsize=8, color=C_THEORY,
                arrowprops=dict(arrowstyle='->', color=C_THEORY, lw=0.7))

    # CPU 瞬态点
    cpu_finite = cpu_3d[np.isfinite(cpu_3d['time'])]
    cpu_trans = cpu_finite[cpu_finite['time'] < 1e10]
    if len(cpu_trans) > 0:
        ax.errorbar(cpu_trans['time'], cpu_trans['E'],
                    yerr=sigma_errbar(cpu_trans),
                    fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                    label=LBL_BASE, zorder=2)

    # CPU 稳态
    cpu_ss = cpu_3d[cpu_3d['time'] == np.inf]
    if len(cpu_ss) > 0:
        ax.errorbar([4.5], cpu_ss['E'].values[:1],
                    yerr=sigma_errbar(cpu_ss.iloc[:1]),
                    fmt=MK_CPU, color=C_CPU, ms=MS_CPU + 1, capsize=CAPSIZE,
                    zorder=2, markeredgewidth=1.2)
        ax.annotate('$t \\to \\infty$',
                    xy=(4.5, cpu_ss['E'].values[0]),
                    xytext=(4.8, cpu_ss['E'].values[0] - 3),
                    fontsize=7.5, color=C_CPU,
                    arrowprops=dict(arrowstyle='->', color=C_CPU, lw=0.6))

    # GPU 瞬态点
    if len(gpu_transient) > 0:
        ax.errorbar(gpu_transient['time'], gpu_transient['E'],
                    yerr=sigma_errbar(gpu_transient),
                    fmt=MK_GPU, color=C_GPU, ms=MS_GPU, capsize=CAPSIZE,
                    label=LBL_OURS, zorder=3)

    # GPU 稳态：聚合为 t->inf 处一个点
    if len(gpu_steady) > 0:
        gpu_mean = gpu_steady['E'].mean()
        gpu_se_combined = np.sqrt((gpu_steady['SE']**2).sum()) / len(gpu_steady)
        t_ss_gpu = 5.0 if len(gpu_transient) == 0 else 5.3
        ax.errorbar([t_ss_gpu], [gpu_mean], yerr=[3 * gpu_se_combined],
                    fmt=MK_GPU, color=C_GPU, ms=MS_GPU + 1, capsize=CAPSIZE,
                    zorder=3, markeredgewidth=1.2,
                    label=(f'{LBL_OURS}（$t \\to \\infty$）'
                           if len(gpu_transient) == 0 else None))
        ax.annotate('$t \\to \\infty$',
                    xy=(t_ss_gpu, gpu_mean),
                    xytext=(t_ss_gpu + 0.3, gpu_mean + 3),
                    fontsize=7.5, color=C_GPU,
                    arrowprops=dict(arrowstyle='->', color=C_GPU, lw=0.6))

    ax.set_xlabel('时间 $t$ [s]')
    ax.set_ylabel('温度 $T$ [K]')
    ax.set_title('对流冷却瞬态（牛顿冷却定律）',
                 fontweight='bold', pad=12)
    ax.set_xlim(-0.2, 6.0)
    ax.legend(loc='center right', framealpha=0.9)
    add_grid(ax)
    fig.tight_layout()
    save_fig(fig, 'fig3_d1_convection', img_dir)


# =============================================================================
# 图 4：E3 —— sin × exp 非稳态解析解（双子图 + 残差）
# =============================================================================
def fig_e3(gpu_dir, cpu_dir, img_dir):
    """扩散方程的精确解，含 sin × exp 模态。"""
    print('图 4：E3 非稳态解析解')

    # 物理常数（来自测试源码）
    LAMBDA_E3 = 0.1
    RHO = 25.0
    CP = 2.0
    alpha_diff = LAMBDA_E3 / (RHO * CP)

    B1 = 10.0
    B2 = 1000.0
    kx = np.pi / 4.0
    ky = np.pi / 4.0
    kz = np.pi / 4.0
    k2 = kx**2 + ky**2 + kz**2
    tau = 1.0 / (alpha_diff * k2)  # ~270 s

    def T_analytic(x, y, z, t):
        poly = B1 * (x**3 * z - 3 * x * y**2 * z)
        sinusoidal = B2 * np.sin(kx * x) * np.sin(ky * y) * np.sin(kz * z)
        return (poly + sinusoidal * np.exp(-alpha_diff * k2 * t)) / LAMBDA_E3

    gpu = load_csv(gpu_dir / 'E3.csv')
    cpu = load_csv(cpu_dir / 'E3.csv')

    gpu_t = gpu[gpu['sub_id'].str.startswith('t=')].copy()
    gpu_x = gpu[gpu['sub_id'].str.startswith('x=')].copy()
    cpu_t = cpu[cpu['sub_id'].str.startswith('t=')].copy()
    cpu_x = cpu[cpu['sub_id'].str.startswith('x=')].copy()

    fig = plt.figure(figsize=(12, 6.0), layout='constrained')
    gs = GridSpec(2, 2, height_ratios=[3, 1], hspace=0.08, wspace=0.3,
                  figure=fig)
    ax1 = fig.add_subplot(gs[0, 0])
    ax1_r = fig.add_subplot(gs[1, 0], sharex=ax1)
    ax2 = fig.add_subplot(gs[0, 1])
    ax2_r = fig.add_subplot(gs[1, 1], sharex=ax2)

    # --- 左：固定位置时间衰减（延伸至 1500 s） ---
    x_fix, y_fix, z_fix = 0.2, 0.3, 0.4
    t_th = np.logspace(np.log10(0.3), np.log10(1500), 500)
    T_th_t = T_analytic(x_fix, y_fix, z_fix, t_th)

    ax1.plot(t_th, T_th_t, color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY,
             label=LBL_ANALYTIC, zorder=1)

    if len(gpu_t) > 0:
        ax1.errorbar(gpu_t['time'], gpu_t['E'], yerr=sigma_errbar(gpu_t),
                     fmt=MK_GPU, color=C_GPU, ms=MS_GPU, capsize=CAPSIZE,
                     label=LBL_OURS, zorder=3)

    if len(cpu_t) > 0:
        ax1.errorbar(cpu_t['time'], cpu_t['E'], yerr=sigma_errbar(cpu_t),
                     fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                     label=LBL_BASE, zorder=2)

    # 稳态渐近线
    T_ss = T_analytic(x_fix, y_fix, z_fix, 1e6)
    ax1.axhline(y=T_ss, color=C_THEORY, ls=':', lw=0.7, alpha=0.4)
    ax1.text(1200, T_ss + 5, f'$T_{{ss}} = {T_ss:.1f}$ K',
             fontsize=7.5, color=C_THEORY, ha='right')

    # 时间常数标注
    T_at_tau = T_analytic(x_fix, y_fix, z_fix, tau)
    ax1.axvline(x=tau, color='#2a9d8f', ls=':', lw=0.8, alpha=0.5)
    ax1.annotate(f'$\\tau = {tau:.0f}$ s',
                 xy=(tau, T_at_tau), xytext=(tau * 2, T_at_tau + 15),
                 fontsize=8, color='#2a9d8f',
                 arrowprops=dict(arrowstyle='->', color='#2a9d8f', lw=0.7))

    ax1.set_xscale('log')
    ax1.set_ylabel('温度 $T$ [K]')
    ax1.set_title('在 $(0.2,\\, 0.3,\\, 0.4)$ 处的指数衰减', fontsize=10)
    ax1.legend(fontsize=8, framealpha=0.9, loc='upper right')
    add_grid(ax1)
    plt.setp(ax1.get_xticklabels(), visible=False)

    # 时间扫描的残差
    add_residual_ax(ax1_r,
                    [(gpu_t, C_GPU, MK_GPU), (cpu_t, C_CPU, MK_CPU)],
                    x_key='time')
    ax1_r.set_xscale('log')
    ax1_r.set_xlabel('时间 $t$ [s]')

    # --- 右：固定时刻的空间分布 ---
    t_fix = 5.0
    x_th_s = np.linspace(-0.50, 0.50, 400)
    T_th_x = T_analytic(x_th_s, y_fix, z_fix, t_fix)

    ax2.plot(x_th_s, T_th_x, color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY,
             label=LBL_ANALYTIC, zorder=1)

    if len(gpu_x) > 0:
        ax2.errorbar(gpu_x['probe_x'], gpu_x['E'], yerr=sigma_errbar(gpu_x),
                     fmt=MK_GPU, color=C_GPU, ms=MS_GPU, capsize=CAPSIZE,
                     label=LBL_OURS, zorder=3)

    if len(cpu_x) > 0:
        ax2.errorbar(cpu_x['probe_x'], cpu_x['E'], yerr=sigma_errbar(cpu_x),
                     fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                     label=LBL_BASE, zorder=2)

    ax2.axhline(y=0, color='gray', ls='-', lw=0.3)
    ax2.set_ylabel('温度 $T$ [K]')
    ax2.set_title(f'$t = {t_fix}$ s 时的空间分布', fontsize=10)
    ax2.legend(fontsize=8, framealpha=0.9)
    add_grid(ax2)
    plt.setp(ax2.get_xticklabels(), visible=False)

    # 空间扫描的残差
    add_residual_ax(ax2_r,
                    [(gpu_x, C_GPU, MK_GPU), (cpu_x, C_CPU, MK_CPU)])
    ax2_r.set_xlabel('位置 $x$ [m]')

    fig.suptitle(r'扩散方程：$\sin \times \exp$ 解析解',
                 fontweight='bold', y=1.02)
    save_fig(fig, 'fig4_e3_sin_exp', img_dir)


# =============================================================================
# 图 5：E5 —— 多域双探针瞬态（带残差面板）
# =============================================================================
def fig_e5(gpu_dir, cpu_dir, img_dir):
    """多域（流体 + 固体 + 大气）耦合瞬态。"""
    print('图 5：E5 多域瞬态')

    gpu = load_csv(gpu_dir / 'E5.csv')
    cpu = load_csv(cpu_dir / 'E5.csv')

    def split_probes(df):
        fluid = df[df['sub_id'].str.contains('fluid')].copy()
        solid = df[df['sub_id'].str.contains('solid')].copy()
        for d in [fluid, solid]:
            d.loc[:, 'time_val'] = d['time'].values
        return fluid.sort_values('time_val'), solid.sort_values('time_val')

    gpu_fluid, gpu_solid = split_probes(gpu)
    cpu_fluid, cpu_solid = split_probes(cpu)

    # 删除 NaN 行（GPU 在 t=0 处常出现 -nan）
    gpu_fluid = gpu_fluid.dropna(subset=['E'])
    gpu_solid = gpu_solid.dropna(subset=['E'])

    fig = plt.figure(figsize=(7, 5.5), layout='constrained')
    gs = GridSpec(2, 1, height_ratios=[3, 1], hspace=0.08, figure=fig)
    ax = fig.add_subplot(gs[0])
    ax_r = fig.add_subplot(gs[1], sharex=ax)

    # --- 主图 ---
    # 来自 CPU ref 列的参考曲线
    for probe_data, lstyle in [(cpu_fluid, '-'), (cpu_solid, '-')]:
        valid = probe_data.dropna(subset=['ref'])
        if len(valid) > 0:
            curve = valid.drop_duplicates(subset=['time_val']).sort_values('time_val')
            ax.plot(curve['time_val'], curve['ref'],
                    color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY, zorder=1)

    # CPU
    if len(cpu_fluid) > 0:
        ax.errorbar(cpu_fluid['time_val'], cpu_fluid['E'],
                    yerr=sigma_errbar(cpu_fluid),
                    fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                    label=f'{LBL_BASE}-流体', zorder=2)
    if len(cpu_solid) > 0:
        ax.errorbar(cpu_solid['time_val'], cpu_solid['E'],
                    yerr=sigma_errbar(cpu_solid),
                    fmt='^', color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                    label=f'{LBL_BASE}-固体', zorder=2, alpha=0.8)

    # GPU
    if len(gpu_fluid) > 0:
        ax.errorbar(gpu_fluid['time_val'], gpu_fluid['E'],
                    yerr=sigma_errbar(gpu_fluid),
                    fmt=MK_GPU, color=C_GPU, ms=MS_GPU, capsize=CAPSIZE,
                    label=f'{LBL_OURS}-流体', zorder=3)
    if len(gpu_solid) > 0:
        ax.errorbar(gpu_solid['time_val'], gpu_solid['E'],
                    yerr=sigma_errbar(gpu_solid),
                    fmt='D', color=C_GPU, ms=MS_GPU - 1, capsize=CAPSIZE,
                    label=f'{LBL_OURS}-固体', zorder=3, alpha=0.8)

    ax.set_ylabel('温度 $T$ [K]')
    ax.set_title('多域耦合瞬态（流体 + 固体 + 大气）',
                 fontweight='bold', pad=12)
    ax.legend(loc='lower right', fontsize=8, framealpha=0.9)
    add_grid(ax)
    plt.setp(ax.get_xticklabels(), visible=False)

    # --- 残差面板 ---
    res_sets = []
    for df, color, mk in [(gpu_fluid, C_GPU, MK_GPU),
                           (gpu_solid, C_GPU, 'D'),
                           (cpu_fluid, C_CPU, MK_CPU),
                           (cpu_solid, C_CPU, '^')]:
        if len(df) > 0:
            res_sets.append((df, color, mk))
    add_residual_ax(ax_r, res_sets, x_key='time_val')
    ax_r.set_xlabel('时间 $t$ [s]')

    save_fig(fig, 'fig5_e5_multidomain', img_dir)


# =============================================================================
# 图 6：汇总 —— σ 直方图 + 对等图
# =============================================================================
def fig_summary(gpu_dir, cpu_dir, img_dir):
    """全部测试的统计汇总。"""
    print('图 6：汇总（σ 直方图 + 对等图）')

    all_frames = []
    for d, tag in [(gpu_dir, 'GPU'), (cpu_dir, 'CPU')]:
        for f in sorted(d.glob('*.csv')):
            try:
                df = load_csv(f)
                df = df.dropna(subset=['E', 'ref', 'SE'])
                df['arch'] = tag
                df['test'] = f.stem
                all_frames.append(df)
            except Exception:
                pass

    if not all_frames:
        print('  无数据 —— 跳过。')
        return

    all_data = pd.concat(all_frames, ignore_index=True)
    mask = ((all_data['SE'] > 1e-15)
            & np.isfinite(all_data['E'])
            & np.isfinite(all_data['ref']))
    valid = all_data[mask].copy()
    valid['sigma_calc'] = np.abs(valid['E'] - valid['ref']) / valid['SE']

    gpu_v = valid[valid['arch'] == 'GPU']
    cpu_v = valid[valid['arch'] == 'CPU']

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.5))

    # --- 左：σ 分布 ---
    bins = np.linspace(0, 5, 30)
    if len(gpu_v) > 0:
        ax1.hist(gpu_v['sigma_calc'], bins=bins, color=C_GPU, alpha=0.55,
                 label=f'{LBL_OURS}（$n={len(gpu_v)}$）',
                 edgecolor='white', linewidth=0.5)
    if len(cpu_v) > 0:
        ax1.hist(cpu_v['sigma_calc'], bins=bins, color=C_CPU, alpha=0.55,
                 label=f'{LBL_BASE}（$n={len(cpu_v)}$）',
                 edgecolor='white', linewidth=0.5)

    ax1.axvline(x=3.0, color='red', ls='--', lw=1.2, alpha=0.7)
    ax1.text(3.15, ax1.get_ylim()[1] * 0.85, r'$3\sigma$ 阈值',
             fontsize=8, color='red', va='top')

    ax1.set_xlabel(r'$\sigma = |E - T_{\mathrm{ref}}| \;/\; SE$')
    ax1.set_ylabel('计数')
    ax1.set_title('蒙特卡洛偏差分布', fontsize=10)
    ax1.legend(fontsize=8, framealpha=0.9)
    add_grid(ax1)

    # --- 右：对等图 ---
    if len(gpu_v) > 0:
        ax2.scatter(gpu_v['ref'], gpu_v['E'],
                    marker=MK_GPU, c=C_GPU, s=MS_GPU**2, alpha=0.6,
                    label=LBL_OURS, zorder=2,
                    edgecolors='white', linewidth=0.3)
    if len(cpu_v) > 0:
        ax2.scatter(cpu_v['ref'], cpu_v['E'],
                    marker=MK_CPU, c=C_CPU, s=MS_CPU**2, alpha=0.6,
                    label=LBL_BASE, zorder=2,
                    edgecolors='white', linewidth=0.3)

    all_vals = np.concatenate([valid['ref'].values, valid['E'].values])
    lo, hi = np.nanmin(all_vals), np.nanmax(all_vals)
    margin = (hi - lo) * 0.05
    diag = [lo - margin, hi + margin]
    ax2.plot(diag, diag, color=C_THEORY, ls='-', lw=1, zorder=1)

    ax2.set_xlabel(r'解析参考 $T_{\mathrm{ref}}$ [K]')
    ax2.set_ylabel('蒙特卡洛估计 $E$ [K]')
    ax2.set_title(r'对等图：$E$ 对 $T_{\mathrm{ref}}$', fontsize=10)
    ax2.set_aspect('equal', adjustable='datalim')
    ax2.legend(fontsize=8, framealpha=0.9)
    add_grid(ax2)

    fig.suptitle('统计汇总：全部测试',
                 fontweight='bold', y=1.02)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    save_fig(fig, 'fig6_summary', img_dir)


# =============================================================================
# 主入口
# =============================================================================
def main():
    parser = argparse.ArgumentParser(
        description='数值一致性证明图（汉化版）')
    parser.add_argument(
        '--csv-dir', type=str,
        default=os.path.dirname(os.path.abspath(__file__)),
        help='包含 gpu/ 和 cpu/ 子目录的根路径')
    args = parser.parse_args()

    csv_root = Path(args.csv_dir)
    gpu_dir = csv_root / 'gpu'
    cpu_dir = csv_root / 'cpu'
    img_dir = csv_root / 'img'
    img_dir.mkdir(exist_ok=True)

    print(f'CSV 根目录: {csv_root}')
    print(f'GPU 目录:   {gpu_dir}  (存在={gpu_dir.exists()})')
    print(f'CPU 目录:   {cpu_dir}  (存在={cpu_dir.exists()})')
    print(f'输出目录:   {img_dir}')
    print()

    fig_a2(gpu_dir, cpu_dir, img_dir)
    fig_a3(gpu_dir, cpu_dir, img_dir)
    fig_d1(gpu_dir, cpu_dir, img_dir)
    fig_e3(gpu_dir, cpu_dir, img_dir)
    fig_e5(gpu_dir, cpu_dir, img_dir)
    fig_summary(gpu_dir, cpu_dir, img_dir)

    print('\n全部图表生成完毕。')


if __name__ == '__main__':
    main()
