---
name: star3d-embree-to-cubql-migration
description: Specialized skill for migrating the star-3d/stardis-cus3d project from Embree CPU ray tracing to cuBQL GPU acceleration without intermediate abstraction layers. Use this skill when: (1) User requests migration of star-3d or stardis-cus3d codebase, (2) Working on Embree to cuBQL function rewrites, (3) Analyzing function call hierarchies for GPU migration, (4) Validating migration progress and compilation. This skill enforces strict tool usage rules (LSP-only for symbols, Context7/deepwiki-mcp-only for external docs) and implements top-down module migration with reference counting preservation.
---

# Star-3D Embree to cuBQL Migration

Hard backend rewrite: Embree (CPU) → cuBQL (GPU), no abstraction layer, function-level tracking.

## Core Principles

1. **Direct replacement** - No intermediate abstraction
2. **Top-down migration** - Follow call chain from public API to Embree calls
3. **Function-level tracking** - Not per-file
4. **Exact reference counting** - Preserve all ownership semantics
5. **Strict tools** - LSP for symbols, Context7/deepwiki for docs, NO fallbacks

## ⚠️ CRITICAL: Tool Violations = Migration Failure

**Using forbidden tools causes**: Wrong functions rewritten, API misuse bugs, hidden corruption, cost explosion.

**Forbidden**: grep/ast-grep (symbols), webfetch/websearch (docs), any fallbacks when tools fail.

**On tool failure**: Mark function unavailable, skip (don't work around). Second failure → ABORT session immediately.

## Migration Workflow

### Phase 0: Verify Tools

Test before starting: `lsp_symbols` (LSP), `context7_resolve-library-id` (Context7), `deepwiki-mcp_ask_question` (deepwiki). Any fails → STOP, report to user.

### Phase 1: Select Module

Read `references/function-hierarchy.md` and `interface_incompetibility.md`. Select unmigrated module, list functions in call-chain order.

### Phase 2: Per-Function Analysis

**For each function**:

#### Step 2.1: Understand Function

**Use LSP to trace Star-3D call chain**:
- Follow `s3d_*`, `geometry_*`, `mesh_*`, etc. (Star-3D functions) → recursively analyze
- Stop at `rtc*` (Embree API) → query interface semantics only
- Stop at pure computation (memcpy, math) → note

**Query Embree interface semantics** (not internals):
```
context7_query-docs("/embree", "What does rtcIntersect1 do? Purpose, parameters, return value")
```
DO: Query purpose, parameters, return values. DON'T: Query internal algorithms, BVH implementation.

**Understand high-level purpose**: What geometric operation? (ray intersection, bounds, etc.) What data? (meshes, scenes, etc.)

**Document semantics**:
```c
/* FUNCTION PURPOSE: Ray-scene intersection
 * STAR-3D CHAIN: hit_setup → rtcIntersect1 → scene_view_geometry_from_embree_id
 * EMBREE USAGE: rtcIntersect1 - traverse BVH, find closest hit
 * NON-EMBREE: Error handling, result assembly
 */
```

**Query cuBQL functionality** (not direct API mapping):
```
deepwiki-mcp_ask_question("EricSolshkov/cuBQL", "How to implement ray-scene intersection? Need: BVH traversal, closest hit, primitive ID")
```
DON'T: "cuBQL equivalent of rtcIntersect1?" DO: "How to achieve ray intersection in cuBQL?"

**If LSP fails**: Mark `LSP_UNAVAILABLE`, skip. **If docs fail**: Mark `DOCS_UNAVAILABLE`, skip. **NO fallbacks**.

#### Step 2.2: Check Migration Need

Has Embree API? → Rewrite. No Embree? → Mark `SKIPPED_NO_EMBREE`, continue.

#### Step 2.3: Rewrite

1. Add semantic doc block (Embree API usage, Star-3D chain, non-Embree ops)
2. Comment out original (preserve completely)
3. Write cuBQL equivalent (preserve ref counting EXACTLY)
4. Add migration marker (date, APIs replaced, ref counting status)

**Reference counting**: Every `ref_get` needs matching `ref_put`. Preserve error path cleanup.

### Phase 3: Compile & Test

```bash
cd stardis-cus3d/star-3d/0.10/build
cmake -DSTAR3D_USE_CUBQL=ON .. && cmake --build . && ctest
```
Failure → fix → retry. Repeated failure → mark `COMPILE_FAILED`, continue to next module.

### Phase 4: Iterate

Repeat until all modules done. Complete = all migrated + compile success + tests pass.

## Tool Rules (ABSOLUTE)

**Required**: LSP (symbols), Context7/deepwiki (docs), read/edit/write (files).
**Forbidden**: grep/ast-grep (symbols), webfetch/websearch (docs), bash text tools.

**Error protocol**: Tool fails → mark unavailable → skip (NO workarounds). Same tool fails twice → ABORT session (report config issue).

**Never**: Use grep when LSP fails, use webfetch when Context7 fails, continue after repeated failures, guess API behavior.

## External Repositories

**cuBQL**: `EricSolshkov/cuBQL` (deepwiki), `/cuBQL` (Context7). Query: BVH construction, ray types, traversal APIs.
**Embree**: `RenderKit/embree` (Context7 only). Query: Interface semantics, parameters, return values.

## Migration Tracking

```bash
python scripts/track_migration.py --list-unmigrated
python scripts/track_migration.py --mark-migrated <func> --status MIGRATED
python scripts/track_migration.py --report
```

## Reference Documents

- `function-hierarchy.md` - Complete call chain
- `interface_incompetibility.md` - Embree/cuBQL mappings
- `migration-protocol.md` - Detailed per-function steps
- `tool-restrictions.md` - Full tool compliance rules

## Common Patterns

**Device**: Embree `rtcNewDevice` → cuBQL implicit CUDA context + optional `GpuMemoryResource`
**Scene**: Embree `RTCScene` → cuBQL `BinaryBVH<float,3>`
**Ray tracing**: Embree `rtcIntersect1` → cuBQL `fixedRayQuery::forEachPrim` with callback

## Verification Checklist

Before marking module complete: LSP used for symbols, cuBQL docs queried, ref counting preserved, original code commented, compilation succeeds, tests pass, no forbidden tools.

## Troubleshooting

**LSP fails**: Mark `LSP_UNAVAILABLE`, skip. Don't use grep.
**Docs unavailable**: Mark `DOCS_UNAVAILABLE`, skip. Don't use webfetch.
**Compile errors**: Fix using LSP diagnostics + cuBQL docs. Don't proceed without fixing.
**Repeated failures**: ABORT session, report config issue.
