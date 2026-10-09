# PCIe Full-Duplex Serialization — Issue Directory Index

**Quick Status**: ACTIVE, HIGH PRIORITY  
**Created**: 2026-03-14  
**Last Updated**: 2026-03-14

## 30-Second Summary

GPU-side CUDA event timing proves that PCIe transfers are **completely serialized** despite dual-stream architecture. All 254 measurements show zero overlap with consistent 1.6µs gap between D2H end and H2D start. This wastes 29% of potential PCIe bandwidth (~179µs per pipeline cycle). Root cause unknown — may be hardware limitation (single copy engine on consumer GPU) or driver configuration issue.

## File Guide

### 📋 Start Here
- **[README.md](./README.md)** — Full problem description, measurements, current architecture, impact analysis

### 🔬 Investigation
- **[HYPOTHESES.md](./HYPOTHESES.md)** — 6 theories ranked by likelihood, with validation tests for each
- **[ACTION_PLAN.md](./ACTION_PLAN.md)** — Step-by-step diagnostic plan (GPU capabilities → stream priority → separate contexts)

### 📊 Evidence
- **[DATA_SUMMARY.md](./DATA_SUMMARY.md)** — Statistical analysis of 127 samples (254 measurements), proof of zero overlap
- **[INSTRUMENTATION.md](./INSTRUMENTATION.md)** — Technical details of CUDA event timing implementation

### 🚀 Next Steps
1. Run GPU capability diagnostic (Step 1 in ACTION_PLAN.md)
2. Check `asyncEngineCount`:
   - If `< 2` → Hardware limit, document and close
   - If `>= 2` → Try stream priority (Step 2)
3. If priority fails → Consider separate contexts (Step 3 — complex)

## Key Numbers

- **Zero overlap**: 254/254 measurements = 0.000 ms
- **Serialization gap**: 1.6±0.2 µs
- **Current transfer time**: 177 (D2H) + 1.6 (gap) + 440 (H2D) = 619 µs
- **Ideal overlap**: max(177, 440) = 440 µs
- **Lost opportunity**: 179 µs per cycle (**29% slowdown**)

## Code Locations

**Instrumentation** (CUDA events):
- `ox_s3d_internal.h` L303-310 (event fields in struct)
- `ox_s3d_scene_view.cpp` L2625-2631 (H2D timing)
- `ox_s3d_scene_view.cpp` L2696-2699 (D2H timing)
- `ox_s3d_scene_view.cpp` L2913-2963 (overlap query API)
- `sdis_solve_persistent_wavefront.c` L5033-5052 (solver output)

**Data**:
- `Stardis-Starter-Pack/porous/pcie_overlap_log.txt` (raw UTF-16 log)
- `Stardis-Starter-Pack/porous/pcie_overlap_pure.txt` (clean UTF-8, 127 samples)
- `scripts/parse_pcie_overlap.py` (parser and statistics)

## Related Issues

- `optimization/wf_pipeline/` — PWF dual-buffer pipeline design
- `guide/pwf_design/` — Persistent wavefront architecture docs
- CUDA Programming Guide Section 12.2 — Concurrent Data Transfers

---

**Status Legend**:  
🔴 ACTIVE — Under active investigation  
🟡 TODO — Tracked but not yet started  
🟢 RESOLVED — Root cause found, fix implemented

*This is issue tracking entry — see parent `debug_issues/AGENTS.md` for full issue index.*
