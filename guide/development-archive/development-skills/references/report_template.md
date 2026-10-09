# Audit Report Template Reference

This file provides the EXACT template structure for `interface_incompetibility.md`. Follow this structure precisely.

---

## Template Structure

```markdown
# Star-3D Embree to CuBQL Interface Incompatibility Audit

**Generated**: [ISO 8601 timestamp]
**Audit Scope**: Header files (*.h) in stardis-cpu/star-3d/0.10/
**Tool Chain**: LSP diagnostics (lsp_diagnostics v[version]) + deepwiki-mcp MCP tools
**Documentation Sources**: EricSolshkov/Embree, EricSolshkov/cuBQL (via deepwiki-mcp)
**Auditor**: Claude (cus3d-interface-audit skill)

---

## Executive Summary

### Statistics
- **Total header files analyzed**: [N]
- **Files with incompatibilities**: [M]
- **Unique Embree types requiring migration**: [X]
- **Direct CuBQL equivalents found**: [Y]
- **Custom implementations needed**: [Z]
- **Files skipped** (access errors): [W]

### Critical Findings
1. [Most impactful incompatibility - e.g., "RTCScene used across 15 files"]
2. [Second critical issue]
3. [Third critical issue]

### Migration Complexity Assessment
- **Low complexity** (direct replacements): [X items] - Est. [N] hours
- **Medium complexity** (wrapper needed): [Y items] - Est. [M] hours  
- **High complexity** (redesign required): [Z items] - Est. [K] hours

---

## Project File Structure

### Directory Layout
```
stardis-cpu/star-3d/0.10/
├── include/
│   ├── s3d.h               (Public API entry point)
│   ├── s3d_scene.h         (Scene management API)
│   └── s3d_types.h         (Core type definitions)
├── src/
│   ├── s3d_backend.h       (Backend abstraction layer)
│   ├── s3d_raytracer.h     (Ray tracing implementation)
│   └── ...
```

### File Roles Matrix

| File Path | Role | Public API | Embree Usage | Priority |
|-----------|------|-----------|--------------|----------|
| include/s3d.h | Main entry header | Yes | Heavy (RTCScene, RTCDevice) | Critical |
| include/s3d_scene.h | Scene API | Yes | Heavy (RTCScene, RTCGeometry) | Critical |
| src/s3d_backend.h | Backend abstraction | Internal | Direct (All RTC* types) | High |
| src/s3d_raytracer.h | Ray intersection | Internal | Medium (RTCRay, RTCHit) | High |
| ... | ... | ... | ... | ... |

### Dependency Graph
```
[Public API Layer]
  s3d.h
    ├─> s3d_scene.h (RTCScene wrapper)
    └─> s3d_types.h (RTCRay/RTCHit wrappers)

[Internal Layer]
  s3d_backend.h (Embree integration point)
    ├─> RTCDevice management
    ├─> RTCScene lifecycle
    └─> RTCGeometry creation

[Implementation Layer]
  s3d_raytracer.h
    └─> RTCRayHit, RTCRayQueryContext
```

---

## Incompatibility Details

### File: include/s3d_scene.h

**Role**: Public API for scene management
**Dependencies**: s3d_types.h, s3d_backend.h
**Public Exposure**: Yes
**Embree API Surface**: RTCScene, RTCDevice, RTCGeometry

---

#### Incompatibility 1: RTCScene Type

**Missing Definition**: `RTCScene`
**Error Type**: Incomplete type / Undefined identifier
**LSP Diagnostic**:
```
Error [line 42, col 5]: Unknown type name 'RTCScene'
Error [line 89, col 1]: Incomplete definition of type 'RTCScene'
```

**Error Context**:
- **Struct member**: `struct s3d_scene_t { RTCScene scene; }` at line 42
- **Function return**: `RTCScene s3d_get_internal_scene(...)` at line 89
- **Function parameter**: `void s3d_commit_scene(RTCScene scene)` at line 103

**Usage in s3d**:
- **Primary role**: Core scene container for all geometric data
- **Used in files**: 
  - `s3d_scene.c` (scene creation, commit, destroy)
  - `s3d_raytracer.c` (ray intersection queries)
  - `s3d_builder.c` (geometry insertion)
- **Public API exposure**: Yes (via `s3d_scene.h`)
- **Call frequency**: High (every render frame)

**Embree Role** (from deepwiki-mcp):
> RTCScene is an opaque handle representing a scene object in Embree.
> 
> **Purpose**: Container for all geometric primitives (triangles, curves, instances).
> 
> **Key Characteristics**:
> - Opaque handle (pointer to internal structure)
> - Thread-safe after `rtcCommitScene()` call
> - Supports incremental updates (add/remove geometry)
> - Embeds acceleration structure (BVH) after commit
> - Device-scoped lifetime (destroyed with RTCDevice or explicit release)
>
> **Typical Usage**:
> ```c
> RTCDevice device = rtcNewDevice(NULL);
> RTCScene scene = rtcNewScene(device);
> // ... add geometries ...
> rtcCommitScene(scene);  // Build BVH
> // ... trace rays ...
> rtcReleaseScene(scene);
> ```
>
> **Performance Notes**:
> - Commit operation expensive (BVH build)
> - Ray traversal O(log N) after commit
> - Supports dynamic scenes with rtcSetSceneFlags()

**CuBQL Equivalent**:

**Direct Mapping**: `cubqlScene_t` (confidence: **High**)

**CuBQL Documentation** (from deepwiki-mcp):
> cubqlScene_t is the primary scene handle in CuBQL, analogous to RTCScene.
>
> **API Differences**:
> - Creation: `cubqlCreateScene(cubqlContext_t ctx)` vs `rtcNewScene(RTCDevice device)`
> - Commit: `cubqlBuildScene(cubqlScene_t scene, cudaStream_t stream)` (async!) vs `rtcCommitScene(scene)` (sync)
> - Thread model: GPU-side asynchronous vs CPU thread-safe
> - Memory: Explicit GPU memory management required
>
> **Key Differences**:
> 1. CuBQL requires explicit CUDA context management
> 2. Scene build is asynchronous (stream-based)
> 3. Geometry data must be in GPU-accessible memory
> 4. No implicit reference counting (manual destroy required)

**Migration Strategy**:

1. **Type Replacement**:
   ```c
   // Before (Embree)
   struct s3d_scene_t {
       RTCScene scene;
   };
   
   // After (CuBQL)
   struct s3d_scene_t {
       cubqlScene_t scene;
       cubqlContext_t ctx;    // Need context handle
       cudaStream_t stream;   // For async operations
   };
   ```

2. **API Adaptation**:
   - Wrap `cubqlCreateScene()` to match s3d creation pattern
   - Add synchronization after `cubqlBuildScene()` if caller expects immediate readiness
   - Implement reference counting wrapper (CuBQL lacks automatic refcount)

3. **Memory Management**:
   - Ensure all geometry data allocated in unified memory or explicitly transferred
   - Add GPU memory pool for efficient scene updates

**Migration Impact**: **Critical** - Used in 15+ files, public API exposed
**Migration Complexity**: **Medium** - Direct equivalent exists, but async model requires careful handling
**Estimated Effort**: 8-12 hours (API wrapper + memory management + testing)

---

#### Incompatibility 2: RTCDevice Type

[Follow same structure as Incompatibility 1]

---

[Repeat for ALL incompatibilities in this file]

---

### File: include/s3d_types.h

[Follow same structure as s3d_scene.h section]

---

[Repeat for ALL files with incompatibilities]

---

## Migration Roadmap

### Phase 1: Direct Replacements (Low Risk)
**Estimated Effort**: [N] hours total

| Embree Type | CuBQL Equivalent | Files Affected | Priority |
|-------------|------------------|----------------|----------|
| RTCScene | cubqlScene_t | 15 files | P0 |
| RTCDevice | cubqlContext_t | 8 files | P0 |
| RTCGeometry | cubqlGeometry_t | 12 files | P1 |

**Implementation Order**:
1. RTCDevice → cubqlContext_t (foundation)
2. RTCScene → cubqlScene_t (depends on device)
3. RTCGeometry → cubqlGeometry_t (depends on scene)

### Phase 2: Wrapper Implementations (Medium Risk)
**Estimated Effort**: [M] hours total

| Embree Concept | CuBQL Approach | Wrapper Design | Priority |
|----------------|----------------|----------------|----------|
| RTCRayHit (combined) | Separate cubqlRay_t + cubqlHit_t | Unified struct wrapper | P1 |
| rtcCommitScene (sync) | cubqlBuildScene (async) | Sync wrapper with stream wait | P0 |
| Geometry filters | Manual filtering in shader | Filter callback wrapper | P2 |

**Wrapper Library Design**:
- Create `s3d_cubql_compat.h` for all wrappers
- Maintain Embree-like API surface for minimal s3d changes
- Document wrapper overhead/performance implications

### Phase 3: Architectural Changes (High Risk)
**Estimated Effort**: [K] hours total

| Feature | Embree Approach | CuBQL Limitation | Redesign Strategy |
|---------|-----------------|------------------|-------------------|
| Incremental scene updates | rtcAttachGeometry + rtcCommitScene | Full rebuild required | Delta tracking + batch updates |
| Instancing hierarchy | RTCScene as geometry | Flatten or custom transform stack | Custom instance manager |
| Geometry filters (callbacks) | CPU-side filter functions | No callback support | Precompute or shader-based |

**Risk Mitigation**:
- Implement feature flags for fallback CPU path
- Extensive validation against CPU reference
- Performance benchmarking at each stage

---

## Appendix A: Tool Configuration

### LSP Server Details
- **Server**: clangd (v[version]) / Microsoft C/C++ (v[version])
- **Configuration**: [path to config file]
- **Include paths**: [list of include directories]
- **Compile flags**: [relevant flags]

### Diagnostics Coverage
- **Total header files**: [N]
- **Successfully analyzed**: [M]
- **Failed (access error)**: [P]
- **Failed (parse error)**: [Q]

**Files Not Analyzed** (if any):
| File | Reason | Impact |
|------|--------|--------|
| xyz.h | File not found | Low (private header) |

### deepwiki-mcp Query Summary
- **Repository Sources**: EricSolshkov/Embree, EricSolshkov/cuBQL
- **Embree queries** (deepwiki-mcp_read_wiki_contents / deepwiki-mcp_ask_question): [N] (success: [M], failed: [P])
- **CuBQL queries** (deepwiki-mcp_read_wiki_contents / deepwiki-mcp_ask_question): [N] (success: [M], failed: [P])
- **Average query time**: [X]ms
- **Failed queries**: [List types that failed, marked for manual review]
- **Tool Compliance**: ✅ No forbidden tools used (grep/webfetch/websearch)

---

## Appendix B: Symbols Requiring Manual Review

**Symbols where LSP resolution failed** (no grep fallback used):

| Symbol | File | LSP Issue | Next Action |
|--------|------|-----------|-------------|
| RTCInternalType | s3d_types.h | Symbol not found by LSP | Manual source inspection needed |

*(This section replaces "Grep Justifications" - grep is FORBIDDEN)*

---

## Appendix C: Manual Review Required

**Items needing expert human review**:

1. **RTCInstancedScene pattern** (s3d_instancing.h)
   - Reason: Complex instancing hierarchy, deepwiki-mcp documentation incomplete
   - Recommendation: Consult CuBQL samples, prototype alternative design

2. **Custom geometry callbacks** (s3d_custom_geo.h)  
   - Reason: No CuBQL equivalent documented
   - Recommendation: Explore shader-based approach or precomputation

---

## Sign-Off

**Audit Status**: Complete
**Report Generated**: [timestamp]
**Next Steps**:
1. Review migration roadmap with team
2. Prioritize Phase 1 implementations
3. Prototype high-risk Phase 3 items for feasibility
4. Set up GPU CI pipeline for validation

**Contact**: For audit methodology questions, reference `guide/cus3d/interface_incompetibility_audit.md`
```

---

## Field Descriptions

### Executive Summary Fields

- **Total header files analyzed**: Count from glob results
- **Files with incompatibilities**: Files with any LSP error after Embree removal
- **Unique Embree types requiring migration**: Distinct Embree type names (RTCScene, RTCDevice, etc.)
- **Direct CuBQL equivalents found**: Types with documented 1:1 CuBQL mapping
- **Custom implementations needed**: Types requiring wrappers or redesign
- **Files skipped**: Files that couldn't be analyzed (access/parse errors)

### Migration Complexity Criteria

- **Low**: Direct type/function replacement, no semantic changes
- **Medium**: Wrapper layer needed, minor semantic differences (sync/async)
- **High**: No direct equivalent, architectural redesign required

### Priority Levels

- **P0 (Critical)**: Blocks all other work, used in public API
- **P1 (High)**: Used across multiple modules, no workaround
- **P2 (Medium)**: Isolated to few files, or workaround exists
- **P3 (Low)**: Optional feature, low usage frequency

### Confidence Levels (for CuBQL mappings)

- **High**: Official CuBQL documentation confirms equivalent, tested by community
- **Medium**: Similar API found, some differences documented, untested
- **Low**: Speculative mapping, no documentation, needs prototyping
- **None**: No equivalent found, requires custom solution
