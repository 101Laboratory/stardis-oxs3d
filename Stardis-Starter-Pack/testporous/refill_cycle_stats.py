"""
Parse IR_stardis-ox_320x320x32_*.txt log files and extract
the refill-phase pipeline cycle composition (timing breakdown).
"""
import re
import glob
import os

LOG_DIR = os.path.dirname(os.path.abspath(__file__))
pattern = os.path.join(LOG_DIR, "IR_stardis-ox_320x320x32_*.txt")

# Regex patterns for pipeline phase timing lines
re_submit   = re.compile(r'submit\s+=\s+([\d.]+)s\s+\[\s*([\d.]+)%\s*\]')
re_wait_d2h = re.compile(r'wait_d2h\s+=\s+([\d.]+)s\s+\[\s*([\d.]+)%\s*\].*?trace=([\d.]+)s\s+enc=([\d.]+)s\s+cp=([\d.]+)s')
re_merged   = re.compile(r'merged_pass\s+=\s+([\d.]+)s\s+\[\s*([\d.]+)%\s*\]')
re_compact  = re.compile(r'compact\+rfill=\s+([\d.]+)s\s+\[\s*([\d.]+)%\s*\]')
re_house    = re.compile(r'housekeeping\s+=\s+([\d.]+)s\s+\[\s*([\d.]+)%\s*\]')
re_total    = re.compile(r'total=\s*([\d.]+)s\s+wall=([\d.]+)s\s+coverage=([\d.]+)%')
re_refill   = re.compile(r'refill_phase:\s+rays=(\d+)\s+\(([\d.]+)%\)\s+wall=([\d.]+)s')
re_drain    = re.compile(r'drain_phase:\s+rays=(\d+)\s+\(([\d.]+)%\)\s+.*?wall=([\d.]+)s')
re_pool     = re.compile(r'pool=(\d+)')
re_steps    = re.compile(r'steps=(\d+)')
re_trace_detail = re.compile(r'trace_stall=\s+([\d.]+)s.*?kern=([\d.]+)s\s+d2h=([\d.]+)s,\s+([\d.]+)%\s+hidden')
re_trace_post   = re.compile(r'trace_post\s+=\s+([\d.]+)s.*?post=([\d.]+)s\s+retrace=([\d.]+)s')
re_submit_bk    = re.compile(r'submit breakdown.*?sync_kernel=([\d.]+)s\s+start_d2h\+launch=([\d.]+)s')

files = sorted(f for f in glob.glob(pattern) if '_stats.txt' not in f)

records = []

for fpath in files:
    fname = os.path.basename(fpath)
    rec = {"file": fname}
    
    with open(fpath, "r") as f:
        text = f.read()
    
    # Extract pool size from filename
    m = re.search(r'_(\d+)_dsphere', fname)
    if m:
        rec["pool"] = int(m.group(1))
    
    # Single pool marker
    rec["single_pool"] = "single" in fname
    
    # Refill/drain phases
    m = re_refill.search(text)
    if m:
        rec["refill_rays"] = int(m.group(1))
        rec["refill_pct"]  = float(m.group(2))
        rec["refill_wall"] = float(m.group(3))
    
    m = re_drain.search(text)
    if m:
        rec["drain_rays"] = int(m.group(1))
        rec["drain_pct"]  = float(m.group(2))
        rec["drain_wall"] = float(m.group(3))
    
    # Pipeline phases
    m = re_submit.search(text)
    if m:
        rec["submit_s"]   = float(m.group(1))
        rec["submit_pct"] = float(m.group(2))
    
    m = re_wait_d2h.search(text)
    if m:
        rec["wait_d2h_s"]     = float(m.group(1))
        rec["wait_d2h_pct"]   = float(m.group(2))
        rec["d2h_trace_s"]    = float(m.group(3))
        rec["d2h_enc_s"]      = float(m.group(4))
        rec["d2h_cp_s"]       = float(m.group(5))
    
    m = re_merged.search(text)
    if m:
        rec["merged_s"]   = float(m.group(1))
        rec["merged_pct"] = float(m.group(2))
    
    m = re_compact.search(text)
    if m:
        rec["compact_s"]   = float(m.group(1))
        rec["compact_pct"] = float(m.group(2))
    
    m = re_house.search(text)
    if m:
        rec["house_s"]   = float(m.group(1))
        rec["house_pct"] = float(m.group(2))
    
    m = re_total.search(text)
    if m:
        rec["phase_total_s"] = float(m.group(1))
        rec["phase_wall_s"]  = float(m.group(2))
        rec["coverage_pct"]  = float(m.group(3))
    
    # Trace detail
    m = re_trace_detail.search(text)
    if m:
        rec["trace_stall_s"]  = float(m.group(1))
        rec["gpu_kern_s"]     = float(m.group(2))
        rec["gpu_d2h_s"]      = float(m.group(3))
        rec["hidden_pct"]     = float(m.group(4))
    
    m = re_trace_post.search(text)
    if m:
        rec["trace_post_s"]   = float(m.group(1))
    
    m = re_submit_bk.search(text)
    if m:
        rec["submit_sync_kern_s"] = float(m.group(1))
        rec["submit_launch_s"]    = float(m.group(2))
    
    records.append(rec)

# ============================================================
# Print per-file summary
# ============================================================
print("=" * 100)
print("Refill-Phase Pipeline Cycle Composition (per log file)")
print("=" * 100)

header = f"{'Pool':>8s} {'Mode':>6s} | {'submit':>9s} {'wait_d2h':>9s} {'merged':>9s} {'compact':>9s} {'house':>9s} | {'total':>9s} {'wall':>9s} {'cov%':>6s}"
print(header)
print("-" * len(header))

# Sort by pool size, dual first
dual_records = [r for r in records if not r["single_pool"]]
single_records = [r for r in records if r["single_pool"]]

for r in sorted(dual_records, key=lambda x: x.get("pool", 0)):
    pool = r.get("pool", "?")
    mode = "dual"
    print(f"{pool:>8} {mode:>6s} | "
          f"{r.get('submit_s',0):>8.1f}s {r.get('wait_d2h_s',0):>8.1f}s {r.get('merged_s',0):>8.1f}s "
          f"{r.get('compact_s',0):>8.1f}s {r.get('house_s',0):>8.1f}s | "
          f"{r.get('phase_total_s',0):>8.1f}s {r.get('phase_wall_s',0):>8.1f}s {r.get('coverage_pct',0):>5.1f}%")

for r in sorted(single_records, key=lambda x: x.get("pool", 0)):
    pool = r.get("pool", "?")
    mode = "single"
    print(f"{pool:>8} {mode:>6s} | "
          f"{r.get('submit_s',0):>8.1f}s {r.get('wait_d2h_s',0):>8.1f}s {r.get('merged_s',0):>8.1f}s "
          f"{r.get('compact_s',0):>8.1f}s {r.get('house_s',0):>8.1f}s | "
          f"{r.get('phase_total_s',0):>8.1f}s {r.get('phase_wall_s',0):>8.1f}s {r.get('coverage_pct',0):>5.1f}%")

# ============================================================
# Print percentage view
# ============================================================
print()
print("=" * 100)
print("Percentage Breakdown (of accumulated phase time)")
print("=" * 100)

header2 = f"{'Pool':>8s} {'Mode':>6s} | {'submit%':>8s} {'wait%':>8s} {'merged%':>8s} {'compact%':>8s} {'house%':>8s} | {'refill_wall':>11s} {'drain_wall':>10s}"
print(header2)
print("-" * len(header2))

for r in sorted(dual_records, key=lambda x: x.get("pool", 0)):
    pool = r.get("pool", "?")
    print(f"{pool:>8} {'dual':>6s} | "
          f"{r.get('submit_pct',0):>7.1f}% {r.get('wait_d2h_pct',0):>7.1f}% {r.get('merged_pct',0):>7.1f}% "
          f"{r.get('compact_pct',0):>7.1f}% {r.get('house_pct',0):>7.1f}% | "
          f"{r.get('refill_wall',0):>10.1f}s {r.get('drain_wall',0):>9.1f}s")

for r in sorted(single_records, key=lambda x: x.get("pool", 0)):
    pool = r.get("pool", "?")
    print(f"{pool:>8} {'single':>6s} | "
          f"{r.get('submit_pct',0):>7.1f}% {r.get('wait_d2h_pct',0):>7.1f}% {r.get('merged_pct',0):>7.1f}% "
          f"{r.get('compact_pct',0):>7.1f}% {r.get('house_pct',0):>7.1f}% | "
          f"{r.get('refill_wall',0):>10.1f}s {r.get('drain_wall',0):>9.1f}s")

# ============================================================
# Compute averages across dual-pool runs
# ============================================================
print()
print("=" * 100)
print("AVERAGE Cycle Composition (dual-pool runs only, N=%d)" % len(dual_records))
print("=" * 100)

import statistics

phase_keys = [
    ("submit_pct",   "submit"),
    ("wait_d2h_pct", "wait_d2h"),
    ("merged_pct",   "merged_pass"),
    ("compact_pct",  "compact+rfill"),
    ("house_pct",    "housekeeping"),
]

for key, label in phase_keys:
    vals = [r[key] for r in dual_records if key in r]
    if vals:
        avg = statistics.mean(vals)
        std = statistics.stdev(vals) if len(vals) > 1 else 0
        mn  = min(vals)
        mx  = max(vals)
        print(f"  {label:>16s}: mean={avg:6.1f}%  std={std:5.1f}%  min={mn:5.1f}%  max={mx:5.1f}%")

# Coverage
cov_vals = [r["coverage_pct"] for r in dual_records if "coverage_pct" in r]
if cov_vals:
    print(f"  {'coverage':>16s}: mean={statistics.mean(cov_vals):6.1f}%  std={statistics.stdev(cov_vals) if len(cov_vals)>1 else 0:5.1f}%  min={min(cov_vals):5.1f}%  max={max(cov_vals):5.1f}%")

# ============================================================
# Trace sub-breakdown (wait_d2h components)
# ============================================================
print()
print("=" * 100)
print("wait_d2h Sub-breakdown (trace/enc/cp)")
print("=" * 100)

for r in sorted(dual_records + single_records, key=lambda x: (x["single_pool"], x.get("pool", 0))):
    pool = r.get("pool", "?")
    mode = "single" if r["single_pool"] else "dual"
    d2h_total = r.get("wait_d2h_s", 0)
    trace = r.get("d2h_trace_s", 0)
    enc   = r.get("d2h_enc_s", 0)
    cp    = r.get("d2h_cp_s", 0)
    stall = r.get("trace_stall_s", 0)
    post  = r.get("trace_post_s", 0)
    kern  = r.get("gpu_kern_s", 0)
    d2h   = r.get("gpu_d2h_s", 0)
    hidden = r.get("hidden_pct", 0)
    
    print(f"  pool={pool:>5} ({mode:>6s}): wait_d2h={d2h_total:>7.1f}s  "
          f"[trace={trace:.1f}s  enc={enc:.3f}s  cp={cp:.3f}s]  "
          f"stall={stall:.1f}s (kern={kern:.1f}s d2h={d2h:.1f}s {hidden:.0f}%hidden)  "
          f"post={post:.1f}s")
