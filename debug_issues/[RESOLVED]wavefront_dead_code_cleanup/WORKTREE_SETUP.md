# Worktree Setup for Dead Code Cleanup

## Overview

All dead code cleanup work must be done in a separate worktree to avoid disrupting ongoing development in the main `oxs3d-merge-phase` working tree. This ensures:
- Main development continues uninterrupted
- Cleanup can be tested thoroughly before merging
- Easy rollback if issues are discovered

## Step 1: Commit Any Pending Changes (Main Worktree)

```powershell
# In main worktree: d:\Stardis-GPU\stardis-oxs3d-merge-phase
cd d:\Stardis-GPU\stardis-oxs3d-merge-phase
git status

# If there are uncommitted changes, commit them first
# (Currently: no uncommitted changes, last commit was P4 pipeline cleanup)
```

## Step 2: Create Cleanup Branch Worktree

```powershell
# Create new worktree in parallel directory
cd d:\Stardis-GPU
git -C stardis-oxs3d-merge-phase worktree add -b cleanup/wavefront-dead-code ../stardis-oxs3d-dead-code-cleanup cus3d-merge-phase

# Verify creation
git -C stardis-oxs3d-merge-phase worktree list
```

**Expected Output**:
```
d:/Stardis-GPU/stardis-oxs3d-merge-phase       <commit-hash> [oxs3d-merge-phase]
d:/Stardis-GPU/stardis-oxs3d-dead-code-cleanup <commit-hash> [cleanup/wavefront-dead-code]
```

## Step 3: Switch to Cleanup Worktree

```powershell
cd d:\Stardis-GPU\stardis-oxs3d-dead-code-cleanup

# Verify branch
git branch --show-current
# Should show: cleanup/wavefront-dead-code

# Verify base
git log --oneline -1
# Should show: latest commit from cus3d-merge-phase
```

## Step 4: Perform Cleanup Work

Follow the phase plan in [README.md](./README.md):
1. **Phase A**: Fix F2 — extract probe wrappers
2. **Phase B**: Remove all dead code (F1, F3-F10)
   - F1-F9: Old wavefront solver code
   - F10: Delete obsolete `custar-3d/` directory (70 files)
3. **Phase C**: Build cleanup — remove P0_OPT macro

## Step 5: Build & Test in Cleanup Worktree

```powershell
# Configure build
cd stardis-oxs3d-dead-code-cleanup
mkdir build -ErrorAction SilentlyContinue
cd build
cmake -G "Visual Studio 17 2022" -A x64 -DS3D_BACKEND=optix ..

# Build
cmake --build . --config Release > build.log 2>&1

# Check build log
Get-Content build.log | Select-String -Pattern "error|warning"

# Run tests
ctest -C Release --output-on-failure

# Verify critical tests (probe APIs)
ctest -C Release -R "test_sdis_wf_a2|test_sdis_wf_b3|test_sdis_wf_c1" --output-on-failure
```

## Step 6: Verify Camera Solver Works

```powershell
cd d:\Stardis-GPU\Stardis-Starter-Pack\porous

# Run IR rendering test
d:\Stardis-GPU\stardis-oxs3d-dead-code-cleanup\build\stardis\Release\stardis.exe `
  -M porous.txt -t 4 -V 3 `
  -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
  > IR_rendering_320x320x32_cleanup_test.ht

# Compare with reference (if available)
```

## Step 7: Commit Cleanup Changes

```powershell
cd d:\Stardis-GPU\stardis-oxs3d-dead-code-cleanup

# Stage deletions and modifications
git add -u

# Stage new files (if any)
git add .

# Commit with detailed message
git commit -m "refactor: remove 20,015 lines of dead code (wavefront + custar-3d)

F1: Delete sdis_solve_wavefront.c old wavefront body (1700 lines)
    - Extracted probe wrappers to sdis_solve_persistent_wavefront.c (F2 fix)
    - Removed 13 dead functions: wf_context_create/destroy, init_all_paths,
      collect_ray_requests, distribute_and_advance, etc.

F3: Delete sdis_solve_wavefront.h (wavefront_context struct)

F4: Delete dispatch_soa (sdis_wf_soa.h/c, test_sdis_dispatch_soa.c)

F5: Delete sdis_ray_sort (never compiled)

F6: Delete sdis_wf_steps.c.bak (3000-line backup)

F7: Clean sdis_solve_camera.c Phase B-2 tile loop (125 lines)
    - Removed unreachable tile loop after goto persistent_wf_done
    - Removed STARDIS_WAVEFRONT env var toggle

F8: Clean count_path_rays() dead branch in sdis_solve_persistent_wavefront.h

F9: Remove sdis_solve_wavefront_probe() declaration from sdis.h

F10: Delete obsolete custar-3d/ directory (14,400 lines, 70 files)
     - Never-completed CUDA BVH backend
     - Superseded by oxstar-3d (OptiX)
     - Not referenced in build system

Build verified: zero errors, all tests pass.
20+ probe tests now link correctly after F2 fix."
```

## Step 8: Push to Remote (Optional)

```powershell
# Push cleanup branch to remote for review
git push origin cleanup/wavefront-dead-code

# Create pull request from cleanup/wavefront-dead-code → cus3d-merge-phase
```

## Step 9: Cleanup After Merge

Once the cleanup is merged into `cus3d-merge-phase`:

```powershell
# Switch back to main worktree
cd d:\Stardis-GPU\stardis-oxs3d-merge-phase

# Pull merged changes
git checkout cus3d-merge-phase
git pull

# Remove cleanup worktree
git worktree remove ../stardis-oxs3d-dead-code-cleanup

# Delete cleanup branch (if merged)
git branch -d cleanup/wavefront-dead-code
git push origin --delete cleanup/wavefront-dead-code
```

## Troubleshooting

### Issue: Worktree Creation Fails

```powershell
# If worktree directory already exists, remove it first
Remove-Item -Recurse -Force d:\Stardis-GPU\stardis-oxs3d-dead-code-cleanup

# If branch already exists, delete it
git branch -D cleanup/wavefront-dead-code
```

### Issue: Build Fails in Cleanup Worktree

```powershell
# Re-run CMake clean
cd d:\Stardis-GPU\stardis-oxs3d-dead-code-cleanup\build
Remove-Item -Recurse -Force *
cmake -G "Visual Studio 17 2022" -A x64 -DS3D_BACKEND=optix ..
cmake --build . --config Release > build.log 2>&1
```

### Issue: Tests Fail After F2 Fix

If probe tests still fail to link, verify:
1. `sdis_solve_persistent_wavefront_probe[_batch]()` are defined in `sdis_solve_persistent_wavefront.c`
2. They're declared in `sdis.h` with `SDIS_API`
3. `SDIS_P0_OPT` is still defined in CMakeLists.txt (until Phase C)

## Quick Reference

| Command | Purpose |
|---------|---------|
| `git worktree list` | Show all worktrees |
| `git worktree add -b <branch> <path> <base>` | Create new worktree |
| `git worktree remove <path>` | Delete worktree |
| `git branch -d <branch>` | Delete branch (after merge) |

## File Structure

```
d:\Stardis-GPU\
├── stardis-oxs3d-merge-phase\          # Main worktree (oxs3d-merge-phase branch)
│   ├── .git\
│   ├── stardis-solver\
│   ├── oxstar-3d\
│   └── ...
│
└── stardis-oxs3d-dead-code-cleanup\    # Cleanup worktree (cleanup/wavefront-dead-code)
    ├── .git -> ../stardis-oxs3d-merge-phase/.git/worktrees/stardis-oxs3d-dead-code-cleanup
    ├── stardis-solver\
    ├── oxstar-3d\
    └── ...
```
