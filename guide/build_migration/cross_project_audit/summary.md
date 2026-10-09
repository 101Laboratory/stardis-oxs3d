# Cross-Project Audit Summary

**Date**: 2026-01-18  
**Scope**: star-2d (0.7), star-3d (0.10)  
**Auditor**: Automated analysis based on multi-project SOP

## Overview

This audit analyzes the compatibility and conflicts between star-2d and star-3d for Windows/CMake migration. Both projects are independent geometry libraries that share a common dependency on rsys but have no direct interdependencies.

## Key Findings

### ✅ Positive Indicators
1. **No Direct Dependencies**: star-2d and star-3d do not depend on each other
2. **Parallel Migratable**: Can be migrated independently after rsys
3. **Consistent Build Patterns**: Both use identical Makefile/config.mk structure
4. **Same rsys Version**: Both require rsys >=0.14 (satisfied by 0.15)
5. **API Namespace Separation**: `s2d_*` vs `s3d_*` - no symbol conflicts

### ⚠️  Risks and Challenges
1. **Embree4 Dependency**: Both require embree4 >=4.0 (external, Windows availability unknown)
2. **Linux-Only Tools**: objcopy, strip usage in static library builds
3. **Hardened Flags**: Security flags unavailable on Windows/MSVC
4. **Symbol Visibility**: GCC visibility attributes need __declspec equivalent

## Dependency Analysis

### Dependency Graph
```
rsys (0.15) ✅ Migrated
├── star-2d (0.7) ⏳ To migrate
└── star-3d (0.10) ⏳ To migrate
```

### Shared Dependencies
| Dependency | star-2d | star-3d | Status | Notes |
|-----------|---------|---------|--------|-------|
| rsys | >=0.14 | >=0.14 | ✅ Compatible | rsys 0.15 satisfies both |
| embree4 | >=4.0 | >=4.0 | ⚠️  External | Windows availability TBD |
| math lib | -lm | -lm | ✅ Compatible | MSVC provides automatically |

## API Compatibility

### Consumed rsys APIs (Common)
Both projects consume identical rsys APIs:
- Core types: `res_T`, `RES_OK`
- Symbol visibility: `EXPORT_SYM`, `IMPORT_SYM`, `LOCAL_SYM`
- Platform macros: `BEGIN_DECLS`, `END_DECLS`, `BIT`, `ASSERT`
- External types: `struct logger`, `struct mem_allocator`

**Conclusion**: No API conflicts. rsys provides consistent interface to both.

### Exported APIs (No Conflicts)
- star-2d: All symbols prefixed with `s2d_`
- star-3d: All symbols prefixed with `s3d_`
- **No namespace collisions detected**

## Build System Compatibility

### Common Build Flags
| Category | Flags | Windows Equivalent |
|----------|-------|-------------------|
| C Standard | `-std=c99` | `/std:c11` (closest MSVC) |
| Warnings | `-Wall -Wextra -Wshadow` | `/W4` |
| PIC/PIE | `-fPIC -fPIE` | N/A (default on Windows) |
| Visibility | `-fvisibility=hidden` | __declspec control |
| Hardened | Multiple flags | ❌ Most unavailable |

### Platform-Specific Differences
| Feature | Linux (GCC) | Windows (MSVC) | Migration Strategy |
|---------|-------------|----------------|-------------------|
| objcopy | Required for static lib | ❌ Unavailable | Skip or use lib.exe |
| strip | Used in release builds | ❌ Unavailable | Use /DEBUG:NONE |
| pkg-config | Dependency resolution | ❌ Not standard | Use CMake find_package |
| Hardened flags | Full support | ❌ Partial/none | Document differences |

## Migration Order

### Recommended Sequence
1. ✅ **rsys** (already completed)
2. **star-2d** OR **star-3d** (can be done in parallel or any order)
3. Cross-validate both projects together

### Rationale
- Both projects have identical dependency on rsys
- No interdependency means no forced ordering
- Parallel migration is feasible if resources allow
- For testing/validation: migrate one first, use as template for second

## Risk Assessment

| Risk | Severity | Impact | Mitigation |
|------|----------|--------|------------|
| Embree4 unavailable on Windows | **HIGH** | Blocking | Verify embree4 Windows builds exist OR create stub for testing |
| Symbol visibility mismatch | Medium | Build errors | Use platform-adapted macros (already in rsys) |
| Hardened flags missing | Low | Security reduction | Document as expected difference |
| objcopy unavailable | Low | Static lib build only | Skip static build or use alternatives |

## Action Items

### Before Migration
- [ ] Verify embree4 availability on Windows (version >=4.0)
- [ ] Review rsys migration artifacts for reusable patterns
- [ ] Confirm CMakeLists.txt template from rsys migration

### During Migration
- [ ] Create CMakeLists.txt for star-2d using rsys template
- [ ] Create CMakeLists.txt for star-3d using rsys template
- [ ] Adapt compile flags per platform (if/WIN32 branches)
- [ ] Handle embree4 dependency (find_package or manual)
- [ ] Test symbol visibility on Windows (DLL export/import)

### After Migration
- [ ] Verify all tests pass on Windows (Debug + Release)
- [ ] Verify modified files work on Linux (original Makefile)
- [ ] Cross-validate: ensure star-2d and star-3d work together with rsys
- [ ] Update dependency_graph.md with migration status

## Conclusion

star-2d and star-3d are **low-risk candidates for parallel migration**. Their independence and identical dependency patterns make them ideal for testing the multi-project migration SOP. The primary external risk is embree4 availability on Windows, which should be verified before proceeding.
