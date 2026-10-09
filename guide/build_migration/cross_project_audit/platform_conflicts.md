# Platform Assumptions Conflicts

**Date**: 2026-01-18  
**Projects**: star-2d (0.7), star-3d (0.10)

## Summary

star-2d and star-3d share **identical platform assumptions** with no conflicts. Both can use the same Windows/MSVC migration strategy.

## Shared Platform Assumptions

### Compiler Requirements
| Assumption | star-2d | star-3d | Conflict | Notes |
|------------|---------|---------|----------|-------|
| C Standard | c99 | c99 | ✅ None | Both use `-std=c99` |
| Variadic macros | Required | Required | ✅ None | C99 feature |
| Inline functions | Required | Required | ✅ None | C99 feature |

### Build Tools
| Tool | star-2d | star-3d | Windows Equivalent | Conflict |
|------|---------|---------|-------------------|----------|
| objcopy | Used | Used | ❌ None (skip/alternative) | ✅ Same strategy |
| strip | Used | Used | ❌ None (use linker flags) | ✅ Same strategy |
| pkg-config | Used | Used | CMake find_package | ✅ Same strategy |
| ar | Used | Used | lib.exe | ✅ Same strategy |
| ranlib | Used | Used | N/A (lib.exe auto-indexes) | ✅ Same strategy |

### Hardened Compilation Flags
Both projects use **identical hardening flags**:

```makefile
# From config.mk (both projects)
CFLAGS_HARDENED =
  -D_FORTIFY_SOURCES=2
  -fcf-protection=full
  -fstack-clash-protection
  -fstack-protector-strong

LDFLAGS_HARDENED = -Wl,-z,relro,-z,now
```

**Windows Status**: ❌ Most flags unavailable on MSVC
**Conflict**: ✅ None - both will omit the same flags on Windows

### Symbol Visibility
Both projects use **identical visibility mechanism**:

```c
// From s2d.h and s3d.h (identical pattern)
#if defined(S*D_SHARED_BUILD)
  #define S*D_API extern EXPORT_SYM
#elif defined(S*D_STATIC)
  #define S*D_API extern LOCAL_SYM
#else
  #define S*D_API extern IMPORT_SYM
#endif
```

These macros come from rsys, which provides platform adaptation.

**Conflict**: ✅ None - both delegate to rsys macros

## External Dependencies

### embree4
| Aspect | star-2d | star-3d | Conflict |
|--------|---------|---------|----------|
| Required Version | >=4.0 | >=4.0 | ✅ None |
| Link Method | pkg-config | pkg-config | ✅ None |
| Windows Availability | Unknown | Unknown | ⚠️  Shared risk |

**Action Required**: Verify embree4 Windows builds exist before migration (affects both projects equally).

### Math Library
| Aspect | star-2d | star-3d | Windows Behavior |
|--------|---------|---------|------------------|
| Link Flag | `-lm` | `-lm` | Automatic (MSVC) |
| Conflict | ✅ None | ✅ None | Same strategy |

## File Format Assumptions

### Line Endings
- **Both projects**: Assume Unix LF
- **Windows Migration**: Git autocrlf or .gitattributes will handle
- **Conflict**: ✅ None

### Path Separators
- **Both projects**: Use forward slashes in Makefiles
- **Windows Migration**: CMake handles path normalization
- **Conflict**: ✅ None

## CMake Migration Implications

### Common CMake Template
Because platform assumptions are identical, both projects can use the **same CMake template** with minimal changes:

```cmake
# Common to both projects
cmake_minimum_required(VERSION 3.20)
project(${PROJECT_NAME} VERSION ${VERSION})

# Platform detection
if(WIN32)
  # MSVC-specific flags
  target_compile_options(... /W4 /O2)
  target_compile_definitions(... _CRT_SECURE_NO_WARNINGS)
else()
  # GCC/Linux flags
  target_compile_options(... -Wall -Wextra -pedantic)
  target_compile_options(... -fvisibility=hidden)
  # Hardening flags
  target_compile_options(... -fstack-protector-strong)
endif()

# find_package(embree4 4.0 REQUIRED)
target_link_libraries(... rsys embree4 m)
```

**Benefit**: Lessons learned from migrating one project directly apply to the other.

## Threading and Concurrency

### Analysis
- **No threading detected** in public APIs of either project
- **Embree4 may use threading internally** (irrelevant to our migration)
- **rsys provides threading primitives** (mutex, condition, etc.) but neither project exposes them

**Conflict**: ✅ None

## Conclusion

star-2d and star-3d have **zero platform assumption conflicts**. They can use:
- **Identical Windows/MSVC flag mappings**
- **Same build tool substitution strategy**
- **Same CMakeLists.txt template**
- **Same external dependency resolution approach**

This makes them ideal candidates for:
1. **Template-driven migration**: Migrate first project, reuse CMakeLists.txt structure for second
2. **Parallel migration**: If done by different developers, ensure they use the same template
3. **Validation cross-check**: Use one as reference for the other

### Recommended Approach
1. Migrate star-2d first (fewer source files: 8 vs 11)
2. Document deviations from template
3. Apply same template to star-3d with adjustments
4. Validate both projects use consistent Windows/Linux branching
