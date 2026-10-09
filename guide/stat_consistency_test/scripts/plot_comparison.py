#!/usr/bin/env python3
"""
Numerical consistency proof plots — Physics Complexity Ladder.

Generates 6 publication-quality figures comparing GPU wavefront vs
CPU depth-first Monte Carlo solvers against analytical references.

Figures:
  1. A2 — Steady conduction with volumetric source (parabola)
  2. A3 — Contact resistance piecewise-linear family
  3. D1 — Convection transient (exponential decay)
  4. E3 — sin x exp unsteady analytic (dual subplot)
  5. E5 — Multi-domain dual-probe transient
  6. Summary — sigma histogram + parity plot
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
# Publication-quality defaults
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# Visual constants
# ---------------------------------------------------------------------------
C_THEORY = '#555555'
C_GPU    = '#D62828'
C_CPU    = '#1D3557'

LS_THEORY = '--'
LW_THEORY = 1.5

MK_GPU  = 's'   # square
MK_CPU  = 'o'   # circle
MS_GPU  = 5
MS_CPU  = 5
CAPSIZE = 3

# ---------------------------------------------------------------------------
# Helpers
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
    """Return 3-sigma error bar array from SE column."""
    return scale * df['SE'].values


def save_fig(fig, name, img_dir):
    path = img_dir / f'{name}.png'
    fig.savefig(str(path))
    plt.close(fig)
    print(f'  Saved {path}')


def add_residual_ax(ax_r, datasets, x_key='probe_x'):
    """Draw residual sigma = (E-ref)/SE scatter on a given axis.

    Parameters
    ----------
    datasets : list of (DataFrame, color, marker) tuples
    x_key    : column name for horizontal axis
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
# Figure 1: A2 — Volumetric Power Parabola  (+ residual panel)
# =============================================================================
def fig_a2(gpu_dir, cpu_dir, img_dir):
    """Steady-state conduction with volumetric source -> parabolic T(x)."""
    print('Figure 1: A2 Volumetric Power Parabola')

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

    # --- Main plot ---
    ax.plot(x_th, T_th, color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY,
            label='Analytical', zorder=1)

    ax.errorbar(gpu['probe_x'], gpu['E'], yerr=sigma_errbar(gpu),
                fmt=MK_GPU, color=C_GPU, ms=MS_GPU, capsize=CAPSIZE,
                label='GPU wavefront', zorder=3)

    ax.errorbar(cpu['probe_x'], cpu['E'], yerr=sigma_errbar(cpu),
                fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                label='CPU depth-first', zorder=2)

    T_max = P0 / (8 * LAMBDA) + T0
    ax.annotate(f'$T_{{max}} = {T_max:.1f}$ K',
                xy=(0.5, T_max), xytext=(0.70, T_max + 0.5),
                fontsize=8.5, color=C_THEORY,
                arrowprops=dict(arrowstyle='->', color=C_THEORY, lw=0.8))

    ax.set_ylabel('Temperature $T$ [K]')
    ax.set_title('Steady Conduction with Volumetric Source',
                 fontweight='bold', pad=12)
    ax.text(0.5, 1.02,
            r'$T(x) = \frac{P}{2\lambda}\!\left(\frac{1}{4}'
            r' - \left(x - \frac{1}{2}\right)^2\right) + T_0$',
            transform=ax.transAxes, ha='center', fontsize=9, color=C_THEORY)
    ax.legend(loc='lower center', ncol=3, framealpha=0.9)
    add_grid(ax)
    plt.setp(ax.get_xticklabels(), visible=False)

    # --- Residual panel ---
    add_residual_ax(ax_r,
                    [(gpu, C_GPU, MK_GPU), (cpu, C_CPU, MK_CPU)])
    ax_r.set_xlabel('Position $x$ [m]')

    save_fig(fig, 'fig1_a2_parabola', img_dir)


# =============================================================================
# Figure 2: A3 — Contact Resistance Piecewise-Linear Family
# =============================================================================
def fig_a3(gpu_dir, cpu_dir, img_dir):
    """Multi-material interface with thermal contact resistance."""
    print('Figure 2: A3 Contact Resistance Family')

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

    # Analytical curves for the 5 GPU R values (colour-coded)
    for i, R in enumerate(R_vals_gpu):
        T_th = analytical_a3(x_th, R)
        ax.plot(x_th, T_th, color=colors_r[i], ls=LS_THEORY, lw=LW_THEORY,
                alpha=0.8, zorder=1)

    # GPU data — uniform RED markers for ALL R groups
    gpu_plotted = False
    for i, R in enumerate(R_vals_gpu):
        candidates = [f'R={R}', f'R={R:g}', f'R={R:.0e}']
        if R == int(R):
            candidates.append(f'R={int(R)}')
        mask = gpu['sub_id'].isin(candidates)
        g = gpu[mask]
        if len(g) > 0:
            lbl = 'GPU wavefront' if not gpu_plotted else None
            ax.errorbar(g['probe_x'], g['E'], yerr=sigma_errbar(g),
                        fmt=MK_GPU, color=C_GPU, ms=MS_GPU,
                        capsize=CAPSIZE, zorder=3, label=lbl)
            gpu_plotted = True

    # CPU data — ALL R values (2D + 3D), uniform BLUE markers
    if len(cpu) > 0:
        ax.errorbar(cpu['probe_x'], cpu['E'], yerr=sigma_errbar(cpu),
                    fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                    label='CPU depth-first', zorder=2, alpha=0.7)

    # Interface marker
    ax.axvline(x=x0, color='gray', ls=':', lw=0.8, alpha=0.5)
    ax.text(x0 + 0.05, 5, 'Interface', fontsize=7.5, color='gray',
            rotation=90, va='bottom')

    # Legend
    handles = [
        Line2D([0], [0], color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY,
               label='Analytical'),
        Line2D([0], [0], marker=MK_GPU, color='w', markerfacecolor=C_GPU,
               ms=MS_GPU + 1, label='GPU wavefront'),
        Line2D([0], [0], marker=MK_CPU, color='w', markerfacecolor=C_CPU,
               ms=MS_CPU + 1, label='CPU depth-first'),
    ]
    for i, R in enumerate(R_vals_gpu):
        handles.append(Line2D([0], [0], color=colors_r[i], ls=LS_THEORY,
                              lw=2, label=f'$R = {R:g}$'))
    ax.legend(handles=handles, loc='upper left', fontsize=7.5, ncol=2,
              framealpha=0.9)

    ax.set_xlabel('Position $x$ [m]')
    ax.set_ylabel('Temperature $T$ [K]')
    ax.set_title('Contact Resistance: Piecewise-Linear Temperature Family',
                 fontweight='bold', pad=12)
    ax.text(0.5, 1.02,
            r'$q = \frac{T_R - T_L}{x_0/\lambda_1 + R + (L-x_0)/\lambda_2}$',
            transform=ax.transAxes, ha='center', fontsize=9, color=C_THEORY)
    ax.set_xlim(-0.1, L + 0.1)
    ax.set_ylim(-5, 110)
    add_grid(ax)
    fig.tight_layout()
    save_fig(fig, 'fig2_a3_contact_resistance', img_dir)


# =============================================================================
# Figure 3: D1 — Convection Transient (Exponential Decay)
# =============================================================================
def fig_d1(gpu_dir, cpu_dir, img_dir):
    """Convection transient -- exponential approach to T_inf."""
    print('Figure 3: D1 Convection Transient')

    T_init = 280.0   # K
    T_inf = 325.0    # K
    nu = 1.2         # 1/s

    t_th = np.linspace(0, 5, 300)
    T_th = T_init * np.exp(-nu * t_th) + T_inf * (1 - np.exp(-nu * t_th))

    cpu = load_csv(cpu_dir / 'D1.csv')
    cpu_3d = cpu[cpu['sub_id'] == '3D'].copy()

    gpu = load_csv(gpu_dir / 'D1.csv')

    # Separate GPU transient (sub_id='transient') from steady-state
    gpu_transient = gpu[gpu['sub_id'] == 'transient'].copy()
    gpu_steady = gpu[gpu['sub_id'] != 'transient'].copy()

    fig, ax = plt.subplots(figsize=(6.5, 4.2))

    ax.plot(t_th, T_th, color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY,
            label='Analytical', zorder=1)

    # T_inf asymptote
    ax.axhline(y=T_inf, color=C_THEORY, ls=':', lw=0.7, alpha=0.4)
    ax.text(4.5, T_inf + 0.5, f'$T_\\infty = {T_inf:.0f}$ K',
            fontsize=8, color=C_THEORY, ha='right')

    # T_0 label
    ax.annotate(f'$T_0 = {T_init:.0f}$ K',
                xy=(0, T_init), xytext=(0.5, T_init + 2),
                fontsize=8, color=C_THEORY,
                arrowprops=dict(arrowstyle='->', color=C_THEORY, lw=0.7))

    # CPU transient points
    cpu_finite = cpu_3d[np.isfinite(cpu_3d['time'])]
    cpu_trans = cpu_finite[cpu_finite['time'] < 1e10]
    if len(cpu_trans) > 0:
        ax.errorbar(cpu_trans['time'], cpu_trans['E'],
                    yerr=sigma_errbar(cpu_trans),
                    fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                    label='CPU depth-first', zorder=2)

    # CPU steady-state
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

    # GPU transient points (available after D1 test rebuild)
    if len(gpu_transient) > 0:
        ax.errorbar(gpu_transient['time'], gpu_transient['E'],
                    yerr=sigma_errbar(gpu_transient),
                    fmt=MK_GPU, color=C_GPU, ms=MS_GPU, capsize=CAPSIZE,
                    label='GPU wavefront', zorder=3)

    # GPU steady-state: aggregate into a single point at t->inf
    if len(gpu_steady) > 0:
        gpu_mean = gpu_steady['E'].mean()
        gpu_se_combined = np.sqrt((gpu_steady['SE']**2).sum()) / len(gpu_steady)
        t_ss_gpu = 5.0 if len(gpu_transient) == 0 else 5.3
        ax.errorbar([t_ss_gpu], [gpu_mean], yerr=[3 * gpu_se_combined],
                    fmt=MK_GPU, color=C_GPU, ms=MS_GPU + 1, capsize=CAPSIZE,
                    zorder=3, markeredgewidth=1.2,
                    label=('GPU wavefront ($t \\to \\infty$)'
                           if len(gpu_transient) == 0 else None))
        ax.annotate('$t \\to \\infty$',
                    xy=(t_ss_gpu, gpu_mean),
                    xytext=(t_ss_gpu + 0.3, gpu_mean + 3),
                    fontsize=7.5, color=C_GPU,
                    arrowprops=dict(arrowstyle='->', color=C_GPU, lw=0.6))

    ax.set_xlabel('Time $t$ [s]')
    ax.set_ylabel('Temperature $T$ [K]')
    ax.set_title("Convective Cooling Transient (Newton's Law)",
                 fontweight='bold', pad=12)
    ax.text(0.5, 1.02,
            r'$T(t) = T_0 \, e^{-\nu t} + T_\infty(1 - e^{-\nu t})$'
            r'$, \;\; \nu = 1.2 \; \mathrm{s}^{-1}$',
            transform=ax.transAxes, ha='center', fontsize=9, color=C_THEORY)
    ax.set_xlim(-0.2, 6.0)
    ax.legend(loc='center right', framealpha=0.9)
    add_grid(ax)
    fig.tight_layout()
    save_fig(fig, 'fig3_d1_convection', img_dir)


# =============================================================================
# Figure 4: E3 — sin x exp Unsteady Analytic (Dual Subplot + Residuals)
# =============================================================================
def fig_e3(gpu_dir, cpu_dir, img_dir):
    """Diffusion equation exact solution with sin x exp modes."""
    print('Figure 4: E3 Unsteady Analytic Profile')

    # Physical constants (from test source code)
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

    # --- Left: Time decay at fixed position (extended to 1500s) ---
    x_fix, y_fix, z_fix = 0.2, 0.3, 0.4
    t_th = np.logspace(np.log10(0.3), np.log10(1500), 500)
    T_th_t = T_analytic(x_fix, y_fix, z_fix, t_th)

    ax1.plot(t_th, T_th_t, color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY,
             label='Analytical', zorder=1)

    if len(gpu_t) > 0:
        ax1.errorbar(gpu_t['time'], gpu_t['E'], yerr=sigma_errbar(gpu_t),
                     fmt=MK_GPU, color=C_GPU, ms=MS_GPU, capsize=CAPSIZE,
                     label='GPU wavefront', zorder=3)

    if len(cpu_t) > 0:
        ax1.errorbar(cpu_t['time'], cpu_t['E'], yerr=sigma_errbar(cpu_t),
                     fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                     label='CPU depth-first', zorder=2)

    # Steady-state asymptote
    T_ss = T_analytic(x_fix, y_fix, z_fix, 1e6)
    ax1.axhline(y=T_ss, color=C_THEORY, ls=':', lw=0.7, alpha=0.4)
    ax1.text(1200, T_ss + 5, f'$T_{{ss}} = {T_ss:.1f}$ K',
             fontsize=7.5, color=C_THEORY, ha='right')

    # Annotate time constant
    T_at_tau = T_analytic(x_fix, y_fix, z_fix, tau)
    ax1.axvline(x=tau, color='#2a9d8f', ls=':', lw=0.8, alpha=0.5)
    ax1.annotate(f'$\\tau = {tau:.0f}$ s',
                 xy=(tau, T_at_tau), xytext=(tau * 2, T_at_tau + 15),
                 fontsize=8, color='#2a9d8f',
                 arrowprops=dict(arrowstyle='->', color='#2a9d8f', lw=0.7))

    ax1.set_xscale('log')
    ax1.set_ylabel('Temperature $T$ [K]')
    ax1.set_title('Exponential Decay at $(0.2,\\, 0.3,\\, 0.4)$', fontsize=10)
    ax1.legend(fontsize=8, framealpha=0.9, loc='upper right')
    add_grid(ax1)
    plt.setp(ax1.get_xticklabels(), visible=False)

    # Residual for time sweep
    add_residual_ax(ax1_r,
                    [(gpu_t, C_GPU, MK_GPU), (cpu_t, C_CPU, MK_CPU)],
                    x_key='time')
    ax1_r.set_xscale('log')
    ax1_r.set_xlabel('Time $t$ [s]')

    # --- Right: Spatial profile at fixed time ---
    t_fix = 5.0
    x_th_s = np.linspace(-0.50, 0.50, 400)
    T_th_x = T_analytic(x_th_s, y_fix, z_fix, t_fix)

    ax2.plot(x_th_s, T_th_x, color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY,
             label='Analytical', zorder=1)

    if len(gpu_x) > 0:
        ax2.errorbar(gpu_x['probe_x'], gpu_x['E'], yerr=sigma_errbar(gpu_x),
                     fmt=MK_GPU, color=C_GPU, ms=MS_GPU, capsize=CAPSIZE,
                     label='GPU wavefront', zorder=3)

    if len(cpu_x) > 0:
        ax2.errorbar(cpu_x['probe_x'], cpu_x['E'], yerr=sigma_errbar(cpu_x),
                     fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                     label='CPU depth-first', zorder=2)

    ax2.axhline(y=0, color='gray', ls='-', lw=0.3)
    ax2.set_ylabel('Temperature $T$ [K]')
    ax2.set_title(f'Spatial Profile at $t = {t_fix}$ s', fontsize=10)
    ax2.legend(fontsize=8, framealpha=0.9)
    add_grid(ax2)
    plt.setp(ax2.get_xticklabels(), visible=False)

    # Residual for spatial sweep
    add_residual_ax(ax2_r,
                    [(gpu_x, C_GPU, MK_GPU), (cpu_x, C_CPU, MK_CPU)])
    ax2_r.set_xlabel('Position $x$ [m]')

    fig.suptitle(r'Diffusion Equation: $\sin \times \exp$ Analytical Solution',
                 fontweight='bold', y=1.02)
    fig.text(0.5, 0.98,
             r'$T = \frac{1}{\lambda}\left[B_1(x^3 z - 3xy^2 z)'
             r' + B_2 \sin \frac{\pi}{4} x \, \sin \frac{\pi}{4} y'
             r' \, \sin \frac{\pi}{4} z'
             r' \cdot e^{-\alpha k^2 t}\right]$'
             r'$, \;\; \tau = 270 \; \mathrm{s}$',
             ha='center', fontsize=9, color=C_THEORY)
    save_fig(fig, 'fig4_e3_sin_exp', img_dir)


# =============================================================================
# Figure 5: E5 — Multi-Domain Dual-Probe Transient  (+ residual panel)
# =============================================================================
def fig_e5(gpu_dir, cpu_dir, img_dir):
    """Multi-domain (fluid + solid + atmosphere) transient."""
    print('Figure 5: E5 Multi-Domain Transient')

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

    # Drop NaN rows (GPU t=0 often has -nan)
    gpu_fluid = gpu_fluid.dropna(subset=['E'])
    gpu_solid = gpu_solid.dropna(subset=['E'])

    fig = plt.figure(figsize=(7, 5.5), layout='constrained')
    gs = GridSpec(2, 1, height_ratios=[3, 1], hspace=0.08, figure=fig)
    ax = fig.add_subplot(gs[0])
    ax_r = fig.add_subplot(gs[1], sharex=ax)

    # --- Main plot ---
    # Reference curves from CPU ref column
    for probe_data, lstyle in [(cpu_fluid, '-'), (cpu_solid, '-')]:
        valid = probe_data.dropna(subset=['ref'])
        if len(valid) > 0:
            # Use first occurrence of each time for a clean curve
            curve = valid.drop_duplicates(subset=['time_val']).sort_values('time_val')
            ax.plot(curve['time_val'], curve['ref'],
                    color=C_THEORY, ls=LS_THEORY, lw=LW_THEORY, zorder=1)

    # CPU
    if len(cpu_fluid) > 0:
        ax.errorbar(cpu_fluid['time_val'], cpu_fluid['E'],
                    yerr=sigma_errbar(cpu_fluid),
                    fmt=MK_CPU, color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                    label='CPU fluid', zorder=2)
    if len(cpu_solid) > 0:
        ax.errorbar(cpu_solid['time_val'], cpu_solid['E'],
                    yerr=sigma_errbar(cpu_solid),
                    fmt='^', color=C_CPU, ms=MS_CPU, capsize=CAPSIZE,
                    label='CPU solid', zorder=2, alpha=0.8)

    # GPU
    if len(gpu_fluid) > 0:
        ax.errorbar(gpu_fluid['time_val'], gpu_fluid['E'],
                    yerr=sigma_errbar(gpu_fluid),
                    fmt=MK_GPU, color=C_GPU, ms=MS_GPU, capsize=CAPSIZE,
                    label='GPU fluid', zorder=3)
    if len(gpu_solid) > 0:
        ax.errorbar(gpu_solid['time_val'], gpu_solid['E'],
                    yerr=sigma_errbar(gpu_solid),
                    fmt='D', color=C_GPU, ms=MS_GPU - 1, capsize=CAPSIZE,
                    label='GPU solid', zorder=3, alpha=0.8)

    ax.set_ylabel('Temperature $T$ [K]')
    ax.set_title('Multi-Domain Coupled Transient (Fluid + Solid + Atmosphere)',
                 fontweight='bold', pad=12)
    ax.text(0.5, 1.02,
            'Reference: high-precision numerical solution (hardcoded)',
            transform=ax.transAxes, ha='center', fontsize=8, color=C_THEORY,
            style='italic')
    ax.legend(loc='center right', fontsize=8, framealpha=0.9)
    add_grid(ax)
    plt.setp(ax.get_xticklabels(), visible=False)

    # --- Residual panel ---
    res_sets = []
    for df, color, mk in [(gpu_fluid, C_GPU, MK_GPU),
                           (gpu_solid, C_GPU, 'D'),
                           (cpu_fluid, C_CPU, MK_CPU),
                           (cpu_solid, C_CPU, '^')]:
        if len(df) > 0:
            res_sets.append((df, color, mk))
    add_residual_ax(ax_r, res_sets, x_key='time_val')
    ax_r.set_xlabel('Time $t$ [s]')

    save_fig(fig, 'fig5_e5_multidomain', img_dir)


# =============================================================================
# Figure 6: Summary — sigma Histogram + Parity Plot
# =============================================================================
def fig_summary(gpu_dir, cpu_dir, img_dir):
    """Statistical summary across all tests."""
    print('Figure 6: Summary (sigma histogram + parity plot)')

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
        print('  No data — skipping.')
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

    # --- Left: sigma distribution ---
    bins = np.linspace(0, 5, 30)
    if len(gpu_v) > 0:
        ax1.hist(gpu_v['sigma_calc'], bins=bins, color=C_GPU, alpha=0.55,
                 label=f'GPU wavefront ($n={len(gpu_v)}$)',
                 edgecolor='white', linewidth=0.5)
    if len(cpu_v) > 0:
        ax1.hist(cpu_v['sigma_calc'], bins=bins, color=C_CPU, alpha=0.55,
                 label=f'CPU depth-first ($n={len(cpu_v)}$)',
                 edgecolor='white', linewidth=0.5)

    ax1.axvline(x=3.0, color='red', ls='--', lw=1.2, alpha=0.7)
    ax1.text(3.15, ax1.get_ylim()[1] * 0.85, r'$3\sigma$ threshold',
             fontsize=8, color='red', va='top')

    total = len(valid)
    passed = int((valid['sigma_calc'] <= 3.0).sum())
    rate = 100 * passed / total if total > 0 else 0
    ax1.text(0.97, 0.95, f'Pass: {passed}/{total} ({rate:.1f}%)',
             transform=ax1.transAxes, ha='right', va='top', fontsize=9,
             bbox=dict(boxstyle='round,pad=0.3', facecolor='white',
                       edgecolor='gray', alpha=0.8))

    ax1.set_xlabel(r'$\sigma = |E - T_{\mathrm{ref}}| \;/\; SE$')
    ax1.set_ylabel('Count')
    ax1.set_title('Monte Carlo Deviation Distribution', fontsize=10)
    ax1.legend(fontsize=8, framealpha=0.9)
    add_grid(ax1)

    # --- Right: Parity plot ---
    if len(gpu_v) > 0:
        ax2.scatter(gpu_v['ref'], gpu_v['E'],
                    marker=MK_GPU, c=C_GPU, s=MS_GPU**2, alpha=0.6,
                    label='GPU wavefront', zorder=2,
                    edgecolors='white', linewidth=0.3)
    if len(cpu_v) > 0:
        ax2.scatter(cpu_v['ref'], cpu_v['E'],
                    marker=MK_CPU, c=C_CPU, s=MS_CPU**2, alpha=0.6,
                    label='CPU depth-first', zorder=2,
                    edgecolors='white', linewidth=0.3)

    all_vals = np.concatenate([valid['ref'].values, valid['E'].values])
    lo, hi = np.nanmin(all_vals), np.nanmax(all_vals)
    margin = (hi - lo) * 0.05
    diag = [lo - margin, hi + margin]
    ax2.plot(diag, diag, color=C_THEORY, ls='-', lw=1, zorder=1)

    ax2.set_xlabel(r'Analytical Reference $T_{\mathrm{ref}}$ [K]')
    ax2.set_ylabel('Monte Carlo Estimate $E$ [K]')
    ax2.set_title('Parity Plot: $E$ vs $T_{\\mathrm{ref}}$', fontsize=10)
    ax2.set_aspect('equal', adjustable='datalim')
    ax2.legend(fontsize=8, framealpha=0.9)
    add_grid(ax2)

    fig.suptitle('Statistical Summary: All Tests',
                 fontweight='bold', y=1.02)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    save_fig(fig, 'fig6_summary', img_dir)


# =============================================================================
# Main
# =============================================================================
def main():
    parser = argparse.ArgumentParser(
        description='Numerical consistency proof plots')
    parser.add_argument(
        '--csv-dir', type=str,
        default=os.path.dirname(os.path.abspath(__file__)),
        help='Root directory containing gpu/ and cpu/ subdirs')
    args = parser.parse_args()

    csv_root = Path(args.csv_dir)
    gpu_dir = csv_root / 'gpu'
    cpu_dir = csv_root / 'cpu'
    img_dir = csv_root / 'img'
    img_dir.mkdir(exist_ok=True)

    print(f'CSV root: {csv_root}')
    print(f'GPU dir:  {gpu_dir}  (exists={gpu_dir.exists()})')
    print(f'CPU dir:  {cpu_dir}  (exists={cpu_dir.exists()})')
    print(f'Output:   {img_dir}')
    print()

    fig_a2(gpu_dir, cpu_dir, img_dir)
    fig_a3(gpu_dir, cpu_dir, img_dir)
    fig_d1(gpu_dir, cpu_dir, img_dir)
    fig_e3(gpu_dir, cpu_dir, img_dir)
    fig_e5(gpu_dir, cpu_dir, img_dir)
    fig_summary(gpu_dir, cpu_dir, img_dir)

    print('\nAll figures generated successfully.')


if __name__ == '__main__':
    main()
