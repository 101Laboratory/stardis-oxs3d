# Wavefront Solver Dead Code Cleanup

**Status**: TODO  
**Priority**: Medium (code quality, ~5430 lines of dead code)  
**Created**: 2026-03-13  
**Branch**: `cleanup/wavefront-dead-code` (to be created on new worktree)  
**Base**: `cus3d-merge-phase` (stardis-oxs3d-merge-phase)

## Problem

`SDIS_P0_OPT` is unconditionally defined in CMakeLists.txt L160, permanently disabling the old per-tile wavefront solver. Additionally, the obsolete custar-3d CUDA backend (never completed) remains in the tree. This creates ~20,015 lines of dead code across multiple files, including:

1. **Entire `sdis_solve_wavefront.c` body** (~1700 lines) — Lines 39-1725 are inside `#else !SDIS_P0_OPT` block, never compiled
2. **Critical linker issue**: Public probe API wrappers (`sdis_solve_persistent_wavefront_probe`, `sdis_solve_persistent_wavefront_probe_batch`) are trapped inside the dead block, causing potential linker errors for 20+ tests
3. Legacy structures: `wavefront_context`, `dispatch_soa`, dead tile loop in `sdis_solve_camera.c`
4. Never-compiled files: `sdis_ray_sort.{h,c}`, backup file `sdis_wf_steps.c.bak` (~3000 lines)
5. **Obsolete custar-3d backend** (~14,400 lines) — Never-completed CUDA BVH backend, superseded by oxstar-3d (OptiX)

## Impact

- **Code bloat**: 20,015 lines of unreachable/obsolete code
- **Build system complexity**: Unnecessary P0_OPT guards everywhere
- **Linker bug**: Public APIs defined in dead code block (F2 - Critical)
- **Maintenance burden**: Dead code paths and obsolete modules confuse developers
- **Binary size**: Unnecessary object code
- **Repository clutter**: Obsolete 70-file custar-3d directory misleads contributors

## Scope

Delete ALL code related to the old per-tile wavefront solver (Phase B-2), keeping only:
- Persistent wavefront solver (Phase B-3)
- Shared step functions (`sdis_wf_steps_*.c`)
- Shared infrastructure (`path_hot`, `wf_rng_adapter`, type headers)

See [FINDINGS.md](./FINDINGS.md) for detailed breakdown of 9 dead code areas.

## Approach

**Critical constraint**: Work must be done in a new worktree to avoid disrupting ongoing oxs3d-merge-phase development.

### Phase A: Fix Critical Linker Issue (F2)
1. Extract `sdis_solve_persistent_wavefront_probe()` and `sdis_solve_persistent_wavefront_probe_batch()` from dead block in `sdis_solve_wavefront.c`
2. Move to `sdis_solve_persistent_wavefront.c` (they call internal functions already defined there)
3. Verify 20+ tests link correctly

### Phase B: Remove Dead Code
4. Delete old wavefront functions from `sdis_solve_wavefront.c`
5. Clean/delete `sdis_solve_wavefront.h`
6. Delete `dispatch_soa`: `sdis_wf_soa.{h,c}`, `test_sdis_dispatch_soa.c`
7. Delete `sdis_ray_sort.{h,c}`, `test_sdis_ray_sort.c`
8. Delete `sdis_wf_steps.c.bak`
9. Clean Phase B-2 tile loop from `sdis_solve_camera.c` (L667-L790)
10. Clean dead branch in `count_path_rays()` from `sdis_solve_persistent_wavefront.h`
10. **Delete obsolete `custar-3d/` directory** (70 files, never completed)

### Phase C: Build Cleanup
11. Remove `SDIS_P0_OPT` macro definition from CMakeLists.txt
12. Remove all `#ifdef SDIS_P0_OPT` guards
13. Remove `sdis_solve_wavefront_probe()` declaration from `sdis.h`

## Verification

```bash
# Build (zero errors)
cd stardis-oxs3d-merge-phase/build
cmake --build . --config Release > build.log 2>&1

# Tests (all pass, especially after F2 fix)
ctest -C Release --output-on-failure

# Camera rendering still works
cd ../../Stardis-Starter-Pack/porous
<stardis-executable> -M porous.txt -t 4 -V 3 -R spp=32:img=320x320...
```

## Worktree Setup

See [WORKTREE_SETUP.md](./WORKTREE_SETUP.md) for detailed instructions on creating and using the cleanup worktree.

## References

- **Analysis**: `/memories/session/plan.md` (created 2026-03-13)
- **Detailed findings**: [FINDINGS.md](./FINDINGS.md)
- **Project AGENTS.md**: `d:\Stardis-GPU\AGENTS.md`
- **Solver design docs**: `d:\Stardis-GPU\guide\pwf_design\`

## Notes

- The persistent wavefront solver (Phase B-3) is the ONLY active path, hardcoded in `sdis_solve_camera.c:647`
- Step functions (`sdis_wf_steps_*.c`) are NOT dead — they're shared by persistent wavefront
- `path_hot`, `wf_rng_adapter`, type headers are NOT dead — actively used
- `oxstar-3d/` is the ONLY active ray tracing backend — custar-3d is completely obsolete
- Obsolete design docs in `guide/cus3d/` can be cleaned up separately (out of scope for this issue)
