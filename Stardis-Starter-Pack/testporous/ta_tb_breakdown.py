"""
Compute T_A / T_B decomposition from pipeline timing logs.
T_A = merged_pass + compact+rfill + housekeeping  (CPU state advancement)
T_B^dev = gpu_kernel + d2h_wait + submit_launch   (GPU query pipeline, device-side)
T_B^host = trace_post                              (CPU post-processing of trace results)
"""
import re, glob, os

LOG_DIR = os.path.dirname(os.path.abspath(__file__))
pattern = os.path.join(LOG_DIR, "IR_stardis-ox_320x320x32_*.txt")
files = sorted(f for f in glob.glob(pattern) if "_stats.txt" not in f)

header = (
    f"{'Pool':>8s} {'Mode':>6s} | "
    f"{'T_A':>8s} {'merged':>8s} {'comp':>8s} {'house':>8s} | "
    f"{'T_B^dev':>8s} {'kern':>8s} {'d2h':>8s} {'launch':>8s} | "
    f"{'T_B^host':>8s} | "
    f"{'T_A/T_Bd':>9s} {'Wall':>8s}"
)
print(header)
print("-" * len(header))

for fpath in files:
    fname = os.path.basename(fpath)
    with open(fpath) as f:
        text = f.read()

    m_pool = re.search(r"_(\d+)_dsphere", fname)
    pool = int(m_pool.group(1)) if m_pool else 0
    single = "single" in fname
    mode = "single" if single else "dual"

    merged = float(re.search(r"merged_pass\s+=\s+([\d.]+)s", text).group(1))
    compact = float(re.search(r"compact\+rfill=\s+([\d.]+)s", text).group(1))
    house_m = re.search(r"housekeeping\s+=\s+([\d.]+)s", text)
    house = float(house_m.group(1)) if house_m else 0.0
    T_A = merged + compact + house

    kern_m = re.search(r"kern=([\d.]+)s\s+d2h=([\d.]+)s", text)
    kern = float(kern_m.group(1)) if kern_m else 0.0
    d2h = float(kern_m.group(2)) if kern_m else 0.0
    launch_m = re.search(r"start_d2h\+launch=([\d.]+)s", text)
    launch = float(launch_m.group(1)) if launch_m else 0.0
    T_B_dev = kern + d2h + launch

    post_m = re.search(r"trace_post\s+=\s+([\d.]+)s", text)
    T_B_host = float(post_m.group(1)) if post_m else 0.0

    wall_m = re.search(r"wall=([\d.]+)s\s+coverage", text)
    wall = float(wall_m.group(1)) if wall_m else 0.0

    ratio = T_A / T_B_dev if T_B_dev > 0 else float("inf")

    print(
        f"{pool:>8} {mode:>6s} | "
        f"{T_A:>7.1f}s {merged:>7.1f}s {compact:>7.1f}s {house:>7.1f}s | "
        f"{T_B_dev:>7.1f}s {kern:>7.1f}s {d2h:>7.1f}s {launch:>7.1f}s | "
        f"{T_B_host:>7.1f}s | "
        f"{ratio:>8.1f}x {wall:>7.1f}s"
    )
