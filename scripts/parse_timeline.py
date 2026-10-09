#!/usr/bin/env python3
"""
Parse [TIMELINE] logs from persistent wavefront and generate average timing Gantt chart.

Usage:
    python parse_timeline.py <timeline_file>
"""

import sys
import re
from statistics import mean, stdev

def parse_timeline_file(filepath):
    """Parse all [TIMELINE] and [TIMELINE_TS] entries from log file."""
    entries = []
    pattern = r'\[TIMELINE\] step=(\d+) \|syncKA=([\d.]+)\|startDA=([\d.]+)\|launchB=([\d.]+)\|waitDA=([\d.]+)\|cpuA=([\d.]+)\|syncKB=([\d.]+)\|startDB=([\d.]+)\|launchA=([\d.]+)\|waitDB=([\d.]+)\|cpuB=([\d.]+)\|cycle=([\d.]+)ms raysA=(\d+) raysB=(\d+)'
    pattern_ts = r'\[TIMELINE_TS\] step=(\d+) \|t0=([\d.]+)\|t1=([\d.]+)\|t2=([\d.]+)\|t3=([\d.]+)\|t4=([\d.]+)\|t5=([\d.]+)\|t6=([\d.]+)\|t7=([\d.]+)\|t8=([\d.]+)\|t9=([\d.]+)\|t10=([\d.]+)'
    
    current_entry = None
    
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            # Parse TIMELINE (durations)
            match = re.search(pattern, line)
            if match:
                current_entry = {
                    'step': int(match.group(1)),
                    'syncKA': float(match.group(2)),
                    'startDA': float(match.group(3)),
                    'launchB': float(match.group(4)),
                    'waitDA': float(match.group(5)),
                    'cpuA': float(match.group(6)),
                    'syncKB': float(match.group(7)),
                    'startDB': float(match.group(8)),
                    'launchA': float(match.group(9)),
                    'waitDB': float(match.group(10)),
                    'cpuB': float(match.group(11)),
                    'cycle': float(match.group(12)),
                    'raysA': int(match.group(13)),
                    'raysB': int(match.group(14)),
                    'has_timestamps': False
                }
                entries.append(current_entry)
            
            # Parse TIMELINE_TS (timestamps)
            match_ts = re.search(pattern_ts, line)
            if match_ts and current_entry and current_entry['step'] == int(match_ts.group(1)):
                current_entry['t0'] = float(match_ts.group(2))
                current_entry['t1'] = float(match_ts.group(3))
                current_entry['t2'] = float(match_ts.group(4))
                current_entry['t3'] = float(match_ts.group(5))
                current_entry['t4'] = float(match_ts.group(6))
                current_entry['t5'] = float(match_ts.group(7))
                current_entry['t6'] = float(match_ts.group(8))
                current_entry['t7'] = float(match_ts.group(9))
                current_entry['t8'] = float(match_ts.group(10))
                current_entry['t9'] = float(match_ts.group(11))
                current_entry['t10'] = float(match_ts.group(12))
                current_entry['has_timestamps'] = True
    
    return entries

def compute_statistics(entries):
    """Compute mean and stdev for all timing fields."""
    if not entries:
        return None
    
    fields = ['syncKA', 'startDA', 'launchB', 'waitDA', 'cpuA',
              'syncKB', 'startDB', 'launchA', 'waitDB', 'cpuB', 'cycle']
    
    stats = {}
    for field in fields:
        values = [e[field] for e in entries]
        stats[field] = {
            'mean': mean(values),
            'stdev': stdev(values) if len(values) > 1 else 0,
            'min': min(values),
            'max': max(values)
        }
    
    # Ray counts
    rays_a = [e['raysA'] for e in entries]
    rays_b = [e['raysB'] for e in entries]
    stats['raysA'] = {
        'mean': mean(rays_a),
        'min': min(rays_a),
        'max': max(rays_a)
    }
    stats['raysB'] = {
        'mean': mean(rays_b),
        'min': min(rays_b),
        'max': max(rays_b)
    }
    
    return stats

def generate_mermaid_gantt(stats):
    """Generate accurate Mermaid Gantt chart based on mean timing values."""
    
    # Extract mean values (in ms)
    syncKA = stats['syncKA']['mean']
    startDA = stats['startDA']['mean']
    waitDA = stats['waitDA']['mean']
    cpuA = stats['cpuA']['mean']
    launchB = stats['launchB']['mean']
    syncKB = stats['syncKB']['mean']
    startDB = stats['startDB']['mean']
    waitDB = stats['waitDB']['mean']
    cpuB = stats['cpuB']['mean']
    launchA = stats['launchA']['mean']
    cycle = stats['cycle']['mean']
    
    # Scale to microseconds for better Mermaid display
    scale = 1000  # ms to µs
    
    # Build timeline based on actual code execution order (from timestamps)
    # t0: cycle start
    # t1: after syncKA
    # t2: after startDA  
    # t3: after launchB
    # t4: after waitDA
    # t5: after cpuA
    # t6: after syncKB
    # t7: after startDB
    # t8: after launchA
    # t9: after waitDB
    # t10: after cpuB
    
    t0 = 0
    t1 = t0 + syncKA * scale
    t2 = t1 + startDA * scale
    t3 = t2 + launchB * scale
    t4 = t3 + waitDA * scale
    t5 = t4 + cpuA * scale
    t6 = t5 + syncKB * scale
    t7 = t6 + startDB * scale
    t8 = t7 + launchA * scale
    t9 = t8 + waitDB * scale
    t10 = t9 + cpuB * scale
    
    # Generate Mermaid gantt
    gantt = []
    gantt.append("```mermaid")
    gantt.append("gantt")
    gantt.append(f"    title PWF Dual-Buffer Average Timing (Cycle = {cycle:.3f} ms)")
    gantt.append("    dateFormat X")
    gantt.append("    axisFormat %s µs")
    gantt.append("")
    
    # CPU Pipeline (sequential execution)
    gantt.append("    section CPU Pipeline")
    if syncKA * scale >= 1:
        gantt.append(f"    Sync Kernel A        :active, {int(t0)}, {int(t1)}")
    gantt.append(f"    Start D2H A          :active, {int(t1)}, {int(t2)}")
    gantt.append(f"    Launch GPU B         :active, {int(t2)}, {int(t3)}")
    gantt.append(f"    Wait D2H A           :active, {int(t3)}, {int(t4)}")
    gantt.append(f"    Merged Pass A        :active, {int(t4)}, {int(t5)}")
    gantt.append(f"    Sync Kernel B        :active, {int(t5)}, {int(t6)}")
    gantt.append(f"    Start D2H B          :active, {int(t6)}, {int(t7)}")
    gantt.append(f"    Launch GPU A         :active, {int(t7)}, {int(t8)}")
    gantt.append(f"    Wait D2H B           :active, {int(t8)}, {int(t9)}")
    gantt.append(f"    Merged Pass B        :active, {int(t9)}, {int(t10)}")
    
    gantt.append("")
    
    # GPU Pipeline (estimated async execution)
    gantt.append("    section GPU Pipeline")
    # GPU B kernel: starts after launch (t3), runs during cpuA (until ~t5)
    gpu_b_start = t3
    gpu_b_end = t5  # overlap with cpuA
    gantt.append(f"    GPU B Kernel (est)   :done, {int(gpu_b_start)}, {int(gpu_b_end)}")
    
    # GPU A kernel: starts after launch (t8), runs during cpuB (until ~t10)  
    gpu_a_start = t8
    gpu_a_end = t10  # overlap with cpuB
    gantt.append(f"    GPU A Kernel (est)   :done, {int(gpu_a_start)}, {int(gpu_a_end)}")
    
    gantt.append("```")
    
    return '\n'.join(gantt)


def generate_timestamp_gantt(entry):
    """Generate Gantt chart from absolute timestamps of a single cycle."""
    if not entry['has_timestamps']:
        return None
    
    # Extract timestamps (in ms from program start)
    t0 = entry['t0']
    t1 = entry['t1']
    t2 = entry['t2']
    t3 = entry['t3']
    t4 = entry['t4']
    t5 = entry['t5']
    t6 = entry['t6']
    t7 = entry['t7']
    t8 = entry['t8']
    t9 = entry['t9']
    t10 = entry['t10']
    
    # Normalize to start from 0 (relative to t0)
    base = t0
    ts = [t - base for t in [t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10]]
    
    # Convert to microseconds
    ts_us = [t * 1000 for t in ts]
    
    gantt = []
    gantt.append("```mermaid")
    gantt.append("gantt")
    gantt.append(f"    title PWF Cycle {entry['step']} Absolute Timing (Wall time: {ts[10]:.3f} ms)")
    gantt.append("    dateFormat X")
    gantt.append("    axisFormat %s µs")
    gantt.append("")
    
    gantt.append("    section CPU Pipeline")
    gantt.append(f"    Sync Kernel A        :active, {int(ts_us[0])}, {int(ts_us[1])}")
    gantt.append(f"    Start D2H A          :active, {int(ts_us[1])}, {int(ts_us[2])}")
    gantt.append(f"    Wait D2H A           :active, {int(ts_us[3])}, {int(ts_us[4])}")  # note: t3 is after launch B
    gantt.append(f"    Merged Pass A        :active, {int(ts_us[4])}, {int(ts_us[5])}")
    gantt.append(f"    Sync Kernel B        :active, {int(ts_us[5])}, {int(ts_us[6])}")
    gantt.append(f"    Start D2H B          :active, {int(ts_us[6])}, {int(ts_us[7])}")
    gantt.append(f"    Wait D2H B           :active, {int(ts_us[8])}, {int(ts_us[9])}")  # note: t8 is after launch A
    gantt.append(f"    Merged Pass B        :active, {int(ts_us[9])}, {int(ts_us[10])}")
    
    gantt.append("")
    gantt.append("    section GPU Pipeline")
    gantt.append(f"    Launch B             :done, {int(ts_us[2])}, {int(ts_us[3])}")
    # GPU B kernel: estimate overlap during cpuA (t4 to t5)
    gantt.append(f"    Kernel B (est)       :done, {int(ts_us[3])}, {int(ts_us[5])}")
    gantt.append(f"    Launch A             :done, {int(ts_us[7])}, {int(ts_us[8])}")
    # GPU A kernel: estimate starts after launch (next cycle)
    gantt.append(f"    Kernel A (est)       :done, {int(ts_us[8])}, {int(ts_us[10])}")
    
    gantt.append("```")
    
    return '\n'.join(gantt)

def generate_summary_report(stats, num_entries):
    """Generate text summary report."""
    report = []
    report.append("=" * 80)
    report.append("PWF TIMELINE STATISTICS")
    report.append("=" * 80)
    report.append(f"Total samples: {num_entries}")
    report.append("")
    
    report.append("TIMING BREAKDOWN (ms):")
    report.append("-" * 80)
    report.append(f"{'Field':<15} {'Mean':>8} {'StdDev':>8} {'Min':>8} {'Max':>8}")
    report.append("-" * 80)
    
    fields = ['syncKA', 'startDA', 'waitDA', 'cpuA', 'launchB',
              'syncKB', 'startDB', 'waitDB', 'cpuB', 'launchA', 'cycle']
    
    for field in fields:
        s = stats[field]
        report.append(f"{field:<15} {s['mean']:>8.3f} {s['stdev']:>8.3f} {s['min']:>8.3f} {s['max']:>8.3f}")
    
    report.append("-" * 80)
    
    # Compute derived metrics
    cpu_total = stats['syncKA']['mean'] + stats['startDA']['mean'] + stats['waitDA']['mean'] + stats['cpuA']['mean'] + \
                stats['syncKB']['mean'] + stats['startDB']['mean'] + stats['waitDB']['mean'] + stats['cpuB']['mean']
    launch_total = stats['launchB']['mean'] + stats['launchA']['mean']
    
    gpu_kernel_est = stats['cycle']['mean'] - cpu_total - launch_total
    
    report.append("")
    report.append("DERIVED METRICS:")
    report.append("-" * 80)
    report.append(f"CPU Total (sync+d2h+merge): {cpu_total:.3f} ms ({cpu_total/stats['cycle']['mean']*100:.1f}%)")
    report.append(f"Launch Overhead:             {launch_total:.3f} ms ({launch_total/stats['cycle']['mean']*100:.1f}%)")
    report.append(f"GPU Kernel (estimated):      {gpu_kernel_est:.3f} ms ({gpu_kernel_est/stats['cycle']['mean']*100:.1f}%)")
    report.append(f"Cycle Time:                  {stats['cycle']['mean']:.3f} ms")
    report.append("")
    
    # Breakdown per view
    view_a_cpu = stats['syncKA']['mean'] + stats['startDA']['mean'] + stats['waitDA']['mean'] + stats['cpuA']['mean']
    view_b_cpu = stats['syncKB']['mean'] + stats['startDB']['mean'] + stats['waitDB']['mean'] + stats['cpuB']['mean']
    
    report.append("PER-VIEW BREAKDOWN:")
    report.append("-" * 80)
    report.append(f"View A CPU time:  {view_a_cpu:.3f} ms  (sync={stats['syncKA']['mean']:.3f} d2h={stats['startDA']['mean']+stats['waitDA']['mean']:.3f} merge={stats['cpuA']['mean']:.3f})")
    report.append(f"View B CPU time:  {view_b_cpu:.3f} ms  (sync={stats['syncKB']['mean']:.3f} d2h={stats['startDB']['mean']+stats['waitDB']['mean']:.3f} merge={stats['cpuB']['mean']:.3f})")
    report.append("")
    
    # Ray statistics
    report.append("RAY STATISTICS:")
    report.append("-" * 80)
    report.append(f"Rays A:  mean={stats['raysA']['mean']:.0f}  min={stats['raysA']['min']}  max={stats['raysA']['max']}")
    report.append(f"Rays B:  mean={stats['raysB']['mean']:.0f}  min={stats['raysB']['min']}  max={stats['raysB']['max']}")
    report.append(f"Total:   {stats['raysA']['mean'] + stats['raysB']['mean']:.0f} rays/cycle")
    report.append("")
    
    # Throughput
    total_rays = stats['raysA']['mean'] + stats['raysB']['mean']
    cycle_sec = stats['cycle']['mean'] * 1e-3
    throughput = total_rays / cycle_sec / 1e6  # Mrays/s
    
    report.append(f"Throughput:  {throughput:.1f} Mrays/s")
    report.append("")
    
    return '\n'.join(report)

def main():
    if len(sys.argv) < 2:
        print("Usage: python parse_timeline.py <timeline_file>")
        sys.exit(1)
    
    filepath = sys.argv[1]
    
    # Parse entries
    print(f"Parsing {filepath}...")
    entries = parse_timeline_file(filepath)
    
    if not entries:
        print("ERROR: No [TIMELINE] entries found in file")
        sys.exit(1)
    
    print(f"Found {len(entries)} timeline entries")
    
    # Check if timestamps are available
    has_timestamps = any(e.get('has_timestamps', False) for e in entries)
    entries_with_ts = [e for e in entries if e.get('has_timestamps', False)]
    
    if has_timestamps:
        print(f"  {len(entries_with_ts)} entries have timestamp data")
    
    # Compute statistics
    stats = compute_statistics(entries)
    
    # Generate reports
    summary = generate_summary_report(stats, len(entries))
    gantt = generate_mermaid_gantt(stats)
    
    # Generate timestamp-based gantt for first entry (if available)
    gantt_ts = None
    if entries_with_ts:
        gantt_ts = generate_timestamp_gantt(entries_with_ts[0])
    
    # Print to console
    print("")
    print(summary)
    print("")
    print(gantt)
    
    if gantt_ts:
        print("")
        print("=" * 80)
        print("FIRST CYCLE ABSOLUTE TIMING (from timestamps)")
        print("=" * 80)
        print(gantt_ts)
    
    # Save to markdown file
    output_path = filepath.rsplit('.', 1)[0] + '_analysis.md'
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write("# PWF Timeline Analysis\n\n")
        f.write(f"**Source**: `{filepath}`\n\n")
        f.write("---\n\n")
        f.write("## Summary Statistics\n\n")
        f.write("```\n")
        f.write(summary)
        f.write("\n```\n\n")
        f.write("---\n\n")
        f.write("## Average Cycle Timing Diagram\n\n")
        f.write(gantt)
        f.write("\n\n")
        
        if gantt_ts:
            f.write("---\n\n")
            f.write("## First Cycle Absolute Timing\n\n")
            f.write(gantt_ts)
            f.write("\n\n")
        
        f.write("---\n\n")
        f.write("**Notes**:\n")
        f.write("- GPU Kernel times are estimated as the overlap period (GPU B during CPU A, GPU A during CPU B)\n")
        f.write("- Actual GPU execution is asynchronous and overlaps with CPU processing\n")
        f.write("- One complete cycle processes both View A and View B\n")
        
        if has_timestamps:
            f.write(f"- Timestamp data available for {len(entries_with_ts)}/{len(entries)} cycles\n")
    
    print(f"\nAnalysis saved to: {output_path}")

if __name__ == '__main__':
    main()
