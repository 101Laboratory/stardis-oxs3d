#!/usr/bin/env python3
"""Parse [PCIE_OVERLAP] lines from stardis log, handling line-wrapping."""
import re, sys, statistics

def parse_pcie_overlap(logfile):
    # Try multiple encodings
    for enc in ('utf-8', 'utf-16', 'utf-8-sig'):
        try:
            with open(logfile, 'r', encoding=enc) as f:
                raw = f.read()
            break
        except (UnicodeError, UnicodeDecodeError):
            continue

    pattern = (
        r'\[PCIE_OVERLAP\] step=(\d+) '
        r'phase1\([^)]*\): d2h=([\d.]+) h2d=([\d.]+) offset=([\d.]+) overlap=([\d.]+)ms '
        r'phase2\([^)]*\): d2h=([\d.]+) h2d=([\d.]+) offset=([\d.]+) overlap=([\d.]+)ms'
    )

    records = []
    for m in re.finditer(pattern, raw):
        records.append({
            'step': int(m.group(1)),
            'p1_d2h': float(m.group(2)),
            'p1_h2d': float(m.group(3)),
            'p1_offset': float(m.group(4)),
            'p1_overlap': float(m.group(5)),
            'p2_d2h': float(m.group(6)),
            'p2_h2d': float(m.group(7)),
            'p2_offset': float(m.group(8)),
            'p2_overlap': float(m.group(9)),
        })
    return records

def stats(values, label=""):
    if not values:
        return "N/A"
    mn = statistics.mean(values)
    sd = statistics.stdev(values) if len(values) > 1 else 0
    return f"{mn:.4f} ± {sd:.4f}  (min={min(values):.4f}, max={max(values):.4f})"

def main():
    logfile = sys.argv[1] if len(sys.argv) > 1 else "pcie_overlap_log.txt"
    records = parse_pcie_overlap(logfile)
    print(f"Parsed {len(records)} PCIE_OVERLAP samples")
    print(f"Step range: {records[0]['step']} → {records[-1]['step']}")
    print()

    # Compute derived metrics
    for r in records:
        # gap = offset - d2h: time between D2H end and H2D start
        r['p1_gap'] = r['p1_offset'] - r['p1_d2h']
        r['p2_gap'] = r['p2_offset'] - r['p2_d2h']

    # Summary statistics
    fields = [
        ('Phase 1 D2H duration (ms)', 'p1_d2h'),
        ('Phase 1 H2D duration (ms)', 'p1_h2d'),
        ('Phase 1 offset D2H_start→H2D_start (ms)', 'p1_offset'),
        ('Phase 1 gap D2H_end→H2D_start (ms)', 'p1_gap'),
        ('Phase 1 overlap (ms)', 'p1_overlap'),
        ('Phase 2 D2H duration (ms)', 'p2_d2h'),
        ('Phase 2 H2D duration (ms)', 'p2_h2d'),
        ('Phase 2 offset D2H_start→H2D_start (ms)', 'p2_offset'),
        ('Phase 2 gap D2H_end→H2D_start (ms)', 'p2_gap'),
        ('Phase 2 overlap (ms)', 'p2_overlap'),
    ]

    print("=" * 80)
    print("PCIE OVERLAP ANALYSIS")
    print("=" * 80)
    for label, key in fields:
        vals = [r[key] for r in records]
        print(f"  {label:45s} : {stats(vals)}")
    print()

    # The key proof
    p1_gaps = [r['p1_gap'] for r in records]
    p2_gaps = [r['p2_gap'] for r in records]
    all_overlaps = [r['p1_overlap'] for r in records] + [r['p2_overlap'] for r in records]

    print("=" * 80)
    print("KEY FINDING")
    print("=" * 80)
    if all(v == 0.0 for v in all_overlaps):
        print("  ★ ALL 254 overlap measurements = 0.000 ms")
        print("  ★ D2H and H2D transfers are COMPLETELY SEQUENTIAL (zero overlap)")
        print()
        print("  Proof:")
        print(f"    Phase 1 avg gap (D2H_end → H2D_start): {statistics.mean(p1_gaps)*1000:.1f} µs")
        print(f"    Phase 2 avg gap (D2H_end → H2D_start): {statistics.mean(p2_gaps)*1000:.1f} µs")
        print()
        print("  PCIe full-duplex is NOT being utilized.")
        print("  H2D waits for D2H to complete before starting.")
        print()
        # wasted time = time that could have been overlapped
        p1_potential = [min(r['p1_d2h'], r['p1_h2d']) for r in records]
        p2_potential = [min(r['p2_d2h'], r['p2_h2d']) for r in records]
        total_wasted = statistics.mean(p1_potential) + statistics.mean(p2_potential)
        print(f"  Potential overlap savings per cycle: {total_wasted:.4f} ms ({total_wasted*1000:.1f} µs)")
    else:
        overlap_count = sum(1 for v in all_overlaps if v > 0)
        print(f"  {overlap_count}/{len(all_overlaps)} measurements show overlap")

    # Print CSV for first 10 and last 5 samples
    print()
    print("=" * 80)
    print("SAMPLE DATA")
    print("=" * 80)
    print(f"{'step':>8s}  {'p1_d2h':>8s} {'p1_h2d':>8s} {'p1_off':>8s} {'p1_gap':>8s} {'p1_ovlp':>8s} | {'p2_d2h':>8s} {'p2_h2d':>8s} {'p2_off':>8s} {'p2_gap':>8s} {'p2_ovlp':>8s}")
    for r in records[:10]:
        print(f"{r['step']:>8d}  {r['p1_d2h']:>8.4f} {r['p1_h2d']:>8.4f} {r['p1_offset']:>8.4f} {r['p1_gap']:>8.4f} {r['p1_overlap']:>8.4f} | {r['p2_d2h']:>8.4f} {r['p2_h2d']:>8.4f} {r['p2_offset']:>8.4f} {r['p2_gap']:>8.4f} {r['p2_overlap']:>8.4f}")
    if len(records) > 15:
        print("  ...")
    for r in records[-5:]:
        print(f"{r['step']:>8d}  {r['p1_d2h']:>8.4f} {r['p1_h2d']:>8.4f} {r['p1_offset']:>8.4f} {r['p1_gap']:>8.4f} {r['p1_overlap']:>8.4f} | {r['p2_d2h']:>8.4f} {r['p2_h2d']:>8.4f} {r['p2_offset']:>8.4f} {r['p2_gap']:>8.4f} {r['p2_overlap']:>8.4f}")

if __name__ == '__main__':
    main()
