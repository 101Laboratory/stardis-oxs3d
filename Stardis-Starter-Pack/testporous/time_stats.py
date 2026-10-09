import sys
import re
import csv
import statistics
from collections import defaultdict

if len(sys.argv) != 2:
    print("Usage: python script.py inputfile.txt")
    sys.exit(1)

input_file = sys.argv[1]

base = input_file.rsplit(".", 1)[0]
csv_file = base + ".csv"
stats_file = base + "_stats.txt"

timeline_pattern = re.compile(r'\[TIMELINE\]\s+(.*)')
kv_pattern = re.compile(r'(\w+)=([0-9.]+)')

rows = []
columns = set()

with open(input_file, "r") as f:
    for line in f:
        if "persistent_wavefront: entering drain phase" in line:
            break

        m = timeline_pattern.search(line)
        if not m:
            continue

        content = m.group(1)

        row = {}
        for k, v in kv_pattern.findall(content):
            val = float(v)
            row[k] = val
            columns.add(k)

        rows.append(row)

columns = sorted(columns)

# ------------------------
# 写CSV
# ------------------------

with open(csv_file, "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(columns)

    for r in rows:
        writer.writerow([r.get(c, "") for c in columns])

# ------------------------
# 统计
# ------------------------

data = defaultdict(list)

for r in rows:
    for k, v in r.items():
        data[k].append(v)

def percentile(values, p):
    values = sorted(values)
    k = (len(values)-1) * p
    f = int(k)
    c = min(f+1, len(values)-1)
    if f == c:
        return values[int(k)]
    d = k - f
    return values[f] + (values[c] - values[f]) * d

stats = {}

for col in columns:
    vals = data[col]

    if not vals:
        continue

    count = len(vals)
    mean = statistics.mean(vals)
    min_v = min(vals)
    max_v = max(vals)
    std = statistics.stdev(vals) if count > 1 else 0

    stats[col] = {
        "count": count,
        "mean": mean,
        "min": min_v,
        "max": max_v,
        "std": std,
        "p50": percentile(vals, 0.50),
        "p90": percentile(vals, 0.90),
        "p99": percentile(vals, 0.99)
    }

stat_names = ["count", "mean", "min", "max", "std", "p50", "p90", "p99"]

# ------------------------
# 计算列宽（用于对齐）
# ------------------------

col_width = {}

for col in columns:
    max_val_len = max(len(f"{stats[col][s]:.6f}") if s!="count" else len(str(stats[col][s]))
                      for s in stat_names)
    col_width[col] = max(len(col), max_val_len) + 2

stat_col_width = max(len(s) for s in stat_names) + 2

# ------------------------
# 写stats文件
# ------------------------

with open(stats_file, "w") as f:

    # header
    f.write("".ljust(stat_col_width))
    for col in columns:
        f.write(col.ljust(col_width[col]))
    f.write("\n")

    f.write("-" * (stat_col_width + sum(col_width.values())))
    f.write("\n")

    # rows
    for s in stat_names:
        f.write(s.ljust(stat_col_width))

        for col in columns:
            val = stats[col][s]

            if s == "count":
                txt = str(val)
            else:
                txt = f"{val:.6f}"

            f.write(txt.ljust(col_width[col]))

        f.write("\n")

print("CSV written to:", csv_file)
print("Stats written to:", stats_file)