# Migration Order Decision

**Date**: 2026-01-18  
**Decision**: Migrate **star-2d first**, then **star-3d**

## Rationale

### Why star-2d First?
1. **Smaller codebase**: 8 source files vs 11 (star-3d)
2. **Fewer tests**: 11 test files vs 17 (star-3d)
3. **Simpler API**: 2D geometry is conceptually simpler than 3D
4. **Template creation**: Less complexity = better template for star-3d
5. **Risk reduction**: Faster feedback loop for first migration

### Dependency Satisfaction
✅ **rsys (0.15) already migrated** - Both star-2d and star-3d dependency satisfied

### Migration Sequence
```
Phase 1: ✅ rsys (0.15) - COMPLETED
Phase 2: ⏳ star-2d (0.7) - IN PROGRESS
Phase 3: ⏳ star-3d (0.10) - PENDING (after star-2d validation)
```

### Success Criteria for star-2d
Before proceeding to star-3d, star-2d must:
- [ ] CMake configuration successful on Windows
- [ ] All 11 tests pass on Windows (Debug + Release)
- [ ] Modified files work on Linux (original Makefile)
- [ ] Key tests pass on Linux (validation)
- [ ] embree4 dependency resolved (or stubbed)

### Known Risks
| Risk | Mitigation |
|------|------------|
| embree4 unavailable on Windows | Create stub library or find Windows builds |
| Symbol visibility issues | Use rsys-proven __declspec pattern |
| Test failures | Follow rsys test fix loop SOP |

## File Locations

### star-2d Migration Workspace
- **Source**: `D:\Works\Projects\Stardis-GPU\stardis-cpu\star-2d\0.7\`
- **CMakeLists.txt**: To be created
- **Audit Files**: 
  - `project_manifest.json`
  - `public_api.json`
  - `platform_assumptions.json`

### star-3d Migration Workspace (Future)
- **Source**: `D:\Works\Projects\Stardis-GPU\stardis-cpu\star-3d\0.10\`
- **Template Source**: Use star-2d CMakeLists.txt as template

## Decision Log
- **2026-01-18**: Audit phase completed, migration order determined
- **Next**: Begin star-2d CMakeLists.txt creation following make_to_cmake SOP
