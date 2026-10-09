# Detailed Dead Code Findings

## F1. `sdis_solve_wavefront.c` — Entire Body Dead (~1700 lines)

**File**: `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_wavefront.c`

**Structure**:
```c
Line 34:   #ifdef SDIS_P0_OPT
Line 35-38:  /* Comment: old per-tile wavefront disabled */
Line 39:   #else  /* !SDIS_P0_OPT */
Line 40-1725: [ALL CODE] — NEVER COMPILED
Line 1725: #endif  /* !SDIS_P0_OPT */
```

**Dead Functions** (15 total):
1. `wf_context_create()` (L76) — static, allocates wavefront_context
2. `wf_context_destroy()` (L142) — static, cleanup
3. `init_all_paths()` (L172) — static, Morton order + SPP init
4. `collect_ray_requests()` (L339) — LOCAL_SYM, collects rays into batch
5. `distribute_and_advance()` (L501) — static, distributes results + cascades
6. `wf_batch_closest_point()` (L685) — static, Step C3 batch CP
7. `update_active_count()` (L762) — static, counts active paths
8. `collect_results()` (L775) — static, gathers results into tile
9. `solve_tile_wavefront()` (L810) — PUBLIC, main entry point (tile mode)
10. `init_paths_from_probe()` (L1265) — static, probe mode init
11. `collect_results_probe()` (L1330) — static, probe results aggregation
12. `solve_wavefront_probe()` (L1355) — LOCAL_SYM, internal probe loop
13. `sdis_solve_wavefront_probe()` (L1475) — PUBLIC API, probe wrapper
14. `sdis_solve_persistent_wavefront_probe()` (L1542) — PUBLIC API **[CRITICAL: see F2]**
15. `sdis_solve_persistent_wavefront_probe_batch()` (L1606) — PUBLIC API **[CRITICAL: see F2]**

**Status**: Functions 1-13 are pure dead code. Functions 14-15 are ACTIVE but trapped in dead block (see F2).

---

## F2. CRITICAL: Public Probe APIs Trapped in Dead Block

**Issue**: `sdis_solve_persistent_wavefront_probe()` (L1542) and `sdis_solve_persistent_wavefront_probe_batch()` (L1606) are:
- Defined **inside** the `#else !SDIS_P0_OPT` dead block
- Declared in `sdis.h` (L1698, L1712) as `SDIS_API` public symbols
- Called by 20+ test files: `test_sdis_wf_a*.c`, `test_sdis_wf_b*.c`, etc.

**Impact**: When `SDIS_P0_OPT` is defined (current state), these functions are not compiled, causing linker errors for all tests that call them.

**Call Sites** (19+ tests):
- `test_sdis_wf_a2_volumic.c:251`
- `test_sdis_wf_a3_contact_resistance.c:490`
- `test_sdis_wf_a6_volumic_power4.c:359`
- `test_sdis_wf_a7_solve_probe3.c:279`
- `test_sdis_wf_b3_boundary_flux.c:325`
- `test_sdis_wf_b5_probe_list.c:282`
- `test_sdis_wf_c1_condrad.c:495`
- `test_sdis_wf_c3_picard_multi.c:579`
- `test_sdis_wf_c4_flux2.c:526`
- `test_sdis_wf_d1_convection.c:287, 314`
- `test_sdis_wf_d2_convection_nonuniform.c:269`
- `test_sdis_wf_e1_unsteady.c:221`
- `test_sdis_wf_e2_unsteady_1d.c:268`
- `test_sdis_wf_e3_unsteady_analytic.c:240, 294, 333`
- `test_sdis_wf_e5_unsteady_atm.c:556, 597`
- `test_sdis_wf_f1_external_flux.c:496, 540`
- `test_sdis_wf_f2_diffuse_radiance.c:412`
- `test_sdis_wf_g1_robustness.c:299, 339`
- `test_sdis_wf_i1_volumic_power2.c:441`
- `test_sdis_wf_i3_enclosure_limit.c:359`
- `test_sdis_b4_m9_wos.c:655, 830`

**Solution**: Extract these two functions from the dead block and move to `sdis_solve_persistent_wavefront.c` (they delegate to `solve_persistent_wavefront_probe[_batch]()` internal functions already declared in that file's header).

---

## F3. `sdis_solve_wavefront.h` — `wavefront_context` Struct Dead

**File**: `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_wavefront.h`

**Dead Declarations**:
- `struct wavefront_context` (L52-120) — per-tile scheduler, only used by dead wavefront.c
- `solve_tile_wavefront()` (L93-107)
- `collect_ray_requests()` (L109)
- `solve_wavefront_probe()` (L118-127)

**Status**: If dead code is removed from sdis_solve_wavefront.c, this header becomes empty/trivial.

---

## F4. `dispatch_soa` — Legacy, Production-Dead

**Files**:
- `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_wf_soa.h`
- `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_wf_soa.c`

**Purpose**: Structure-of-Arrays layout for dispatch fields (phase, active, needs_ray, ray_bucket, ray_count_ext).

**Status**:
- **Compiled** (CMakeLists.txt L81)
- **NOT used** by persistent wavefront (replaced by `path_hot` compact AoS)
- Only consumers:
  - `test_sdis_dispatch_soa.c` (CMakeLists.txt L345-350)
  - `test_sdis_b4_m2_ray_bucketing.c`

**Functions**:
- `dispatch_soa_alloc()`
- `dispatch_soa_free()`
- `dispatch_soa_sync_from_path()`
- `dispatch_soa_sync_to_path()`
- `dispatch_soa_assert_consistent()`

**Comment**: `sdis_solve_persistent_wavefront.c:568` explicitly notes: "P0_OPT: path_hot array (replaces P1 dispatch_soa)"

---

## F5. `sdis_ray_sort` — Never Compiled

**Files**:
- `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_ray_sort.h`
- `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_ray_sort.c`
- `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/test_sdis_ray_sort.c`

**Status**: **NOT in CMakeLists.txt**, never built. Pure dead files.

---

## F6. `sdis_wf_steps.c.bak` — Backup (~3000 lines)

**File**: `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_wf_steps.c.bak`

**Purpose**: Complete pre-split unified implementation of all step functions before modularization into:
- `sdis_wf_steps_core.c`
- `sdis_wf_steps_enc.c`
- `sdis_wf_steps_cnd.c`
- `sdis_wf_steps_bnd_ss.c`
- `sdis_wf_steps_bnd_sf.c`
- `sdis_wf_steps_bnd_sfn.c`
- `sdis_wf_steps_bnd_ext.c`
- `sdis_wf_steps_cnv.c`

**Status**: NOT in CMakeLists.txt, never compiled. Archival backup only.

---

## F7. `sdis_solve_camera.c` — Phase B-2 Tile Loop Dead

**File**: `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_camera.c`

**Dead Code**:
- Line 646: `//const int use_persistent_wf = (pwf_env && pwf_env[0] == '1');` (commented out)
- Line 647: `const int use_persistent_wf = 1;` (hardcoded)
- Line 649-663: Persistent wavefront call + early `goto persistent_wf_done;`
- **Lines 667-790**: Entire Phase B-2 tile loop (unreachable) (~125 lines)
  - Line 672: `STARDIS_WAVEFRONT` env var check
  - Line 677: `const int use_wavefront = ...` toggle
  - Line 682-757: OMP parallel for loop (Morton scan, tile allocation)
  - Line 728-738: `#ifdef SDIS_P0_OPT` guard → `solve_tile_wavefront` disabled
  - Line 759-790: Progress + tile gather

**Impact**: The entire tile-based loop is unreachable after the early `goto`.

---

## F8. `count_path_rays()` Dead Branch

**File**: `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.h`

**Location**: Around L466

**Dead Code**:
```c
#ifdef SDIS_P0_OPT
  (void)p;
  return (size_t)p->ray_req.ray_count;  /* hot fields unavailable */
#else
  if(p->phase == PATH_ENC_QUERY_EMIT && p->ray_count_ext == 6)  // DEAD
    return 6;
  return (size_t)p->ray_req.ray_count;
#endif
```

**Issue**: The `#else` branch reads `p->phase` and `p->ray_count_ext`, but these fields **no longer exist** in `struct path_state` (moved to `path_hot`). This branch would fail to compile if activated.

**Status**: The active `#ifdef SDIS_P0_OPT` branch only reads `p->ray_req.ray_count`.

---

## F9. `sdis.h` — Dead Public API Declaration

**File**: `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis.h`

**Dead Declaration**:
- Lines 1687-1691: `sdis_solve_wavefront_probe()` — old wavefront probe API

**Status**: This API is declared but defined only in the dead block of `sdis_solve_wavefront.c`. No callers exist.

---

## F10. `custar-3d/` — Obsolete CUDA BVH Backend (~14,400 lines)

**Directory**: `stardis-oxs3d-merge-phase/custar-3d/0.10/`

**Purpose**: GPU-accelerated 3D ray tracing library using NVIDIA cuBQL for STARDIS thermal solver.

**Status**: **Obsolete** — completely superseded by oxstar-3d (OptiX backend)
- Not referenced in root CMakeLists.txt
- No build system integration
- No production code references
- Status in README: "Skeleton implementation - awaiting implementation" (never completed)

**Content**:
- 70 source files (~722 KB)
- 8 core modules (device, mem, types, geom_store, bvh, trace, prim, math)
- Test files for geometry, tracing, instancing, etc.
- CMakeLists.txt, README.md

**History**:
- Original CUDA BVH backend based on cuBQL
- Development stopped when OptiX was chosen as the production backend
- OptiX provides better performance and easier maintenance
- All solver code now uses oxstar-3d exclusively

**References**:
- Design docs: `guide/cus3d/` (also obsolete)
- Replacement: `oxstar-3d/` (active OptiX backend)

**Cleanup Action**: Delete entire `custar-3d/` directory

---

## Summary Table

| Finding | Files | Lines (approx) | Status |
|---------|-------|----------------|--------|
| F1 | sdis_solve_wavefront.c body | ~1700 | Dead (P0_OPT guard) |
| F2 | probe wrappers in F1 | ~180 | CRITICAL: Active but trapped |
| F3 | sdis_solve_wavefront.h | ~100 | Dead (struct + declarations) |
| F4 | dispatch_soa (.h + .c + test) | ~300 | Compiled but unused |
| F5 | ray_sort (.h + .c + test) | ~200 | Never compiled |
| F6 | sdis_wf_steps.c.bak | ~3000 | Never compiled |
| F7 | camera.c Phase B-2 tile loop | ~125 | Unreachable (goto skip) |
| F8 | count_path_rays #else branch | ~5 | Won't compile if activated |
| F9 | sdis.h dead declaration | ~5 | No definition/callers |
| F10 | custar-3d/ entire directory | ~14400 | Obsolete CUDA backend (70 files) |
| **Total** | | **~20015** | |
