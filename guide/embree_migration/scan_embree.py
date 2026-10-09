#!/usr/bin/env python3
import os
import re
import csv
import sys

# =========================
# 配置区
# =========================

SRC_ROOT = sys.argv[1] if len(sys.argv) > 1 else "src"

EXCLUDE_DIRS = {
    "third_party", "extern", "embree", ".git", "build"
}

OUTPUT_CSV = "embree_coupling_report.csv"

# Level, Category, Regex
PATTERNS = [
    # L1 生命周期
    ("L1", "Lifecycle", r"\brtc(New|Release)(Scene|Geometry|Device)\b"),
    ("L1", "Lifecycle", r"\brtc(Attach|Detach)Geometry\b"),
    ("L1", "Lifecycle", r"\brtcCommit(Scene|Geometry)\b"),

    # L2 构建质量
    ("L2", "Build", r"\brtcSet(Geometry|Scene)BuildQuality\b"),
    ("L2", "Build", r"\bRTC_BUILD_QUALITY_\w+\b"),

    # L3 查询
    ("L3", "Query", r"\brtc(Intersect|Occluded)1\b"),
    ("L3", "Query", r"\brtc(Intersect|Occluded)(4|8|16)\b"),
    ("L3", "Query", r"\bRTCRay(Hit)?\b"),

    # L4 语义扩展
    ("L4", "Semantic", r"\bRTC_GEOMETRY_TYPE_(USER|INSTANCE)\b"),
    ("L4", "Semantic", r"\brtcSetGeometry(UserPrimitiveCount|BoundsFunction|IntersectFunction|OccludedFunction)\b"),
    ("L4", "Semantic", r"\brtcSetGeometry(Intersect|Occluded)FilterFunction\b"),
    ("L4", "Semantic", r"\brtcSetGeometryInstancedScene\b"),
    ("L4", "Semantic", r"\brtcSetGeometryUserData\b"),

    # 隐性 user geometry
    ("L4", "Semantic", r"\bgeometry_.*_(intersect|bounds|occluded)\b"),
]

COMPILED = [(lvl, cat, re.compile(rx)) for lvl, cat, rx in PATTERNS]

# =========================
# 工具函数
# =========================

def should_skip_dir(path: str) -> bool:
    return any(part in EXCLUDE_DIRS for part in path.split(os.sep))

def is_source_file(name: str) -> bool:
    return name.endswith((".c", ".cpp", ".h", ".hpp"))

# =========================
# 主扫描逻辑
# =========================

rows = []

for root, dirs, files in os.walk(SRC_ROOT):
    if should_skip_dir(root):
        dirs[:] = []
        continue

    for fname in files:
        if not is_source_file(fname):
            continue

        fpath = os.path.join(root, fname)
        try:
            with open(fpath, "r", encoding="utf-8", errors="ignore") as f:
                for lineno, line in enumerate(f, 1):
                    for level, category, regex in COMPILED:
                        m = regex.search(line)
                        if m:
                            symbol = m.group(0)
                            rows.append([
                                level,
                                fpath.replace("\\", "/"),
                                lineno,
                                symbol,
                                symbol,
                                category,
                                ""  # Hint 留空给人工 / LLM 补全
                            ])
        except OSError:
            pass

# =========================
# 输出 CSV
# =========================

with open(OUTPUT_CSV, "w", newline="", encoding="utf-8") as csvfile:
    writer = csv.writer(csvfile)
    writer.writerow([
        "Level",
        "File",
        "Line",
        "Symbol",
        "EmbreeAPI",
        "Category",
        "Hint"
    ])
    writer.writerows(rows)

print(f"[OK] Embree coupling report written to: {OUTPUT_CSV}")
print(f"[OK] Total records: {len(rows)}")
