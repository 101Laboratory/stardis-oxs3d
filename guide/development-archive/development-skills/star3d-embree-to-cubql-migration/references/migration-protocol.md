# Per-Function Migration Protocol

Detailed procedure for migrating a single Star-3D function from Embree to cuBQL.

## Input/Output

**Input**: Function name, location, call chain context from function-hierarchy.md
**Output**: Migration status (MIGRATED/SKIPPED_NO_EMBREE/LSP_UNAVAILABLE/DOCS_UNAVAILABLE/REWRITE_FAILED), modified file(s), compilation result

## Step-by-Step Procedure

### Step 1: Locate Definition (LSP)

```typescript
lsp_goto_definition(filePath: "<file>", line: <line>, character: <col>)
```
**Fail**: Mark `LSP_UNAVAILABLE`, skip. **DON'T**: Use grep.

### Step 2: Analyze Implementation

Read function via `read(filePath)`. Identify: Embree APIs (`rtc*`), ref counting (`ref_get`/`ref_put`), error paths, data structures.

**No Embree APIs**: Mark `SKIPPED_NO_EMBREE`, continue to next.

### Step 3: Trace Star-3D Call Chain

**Use LSP recursively**: Follow `s3d_*`, `geometry_*`, `mesh_*`, etc. Stop at `rtc*` (Embree) or pure computation (memcpy, math).

**Build call tree**:
```
current_function
├─ helper_function (Star-3D) → analyze
├─ rtcAPI (Embree) → stop, note
└─ memcpy → stop
```

### Step 4: Query Embree Interface Semantics

**For each Embree API**:
```typescript
context7_query-docs("/embree", "What does rtcFunc do? Purpose, parameters, return value")
```

**DO**: Query purpose, usage, parameters. **DON'T**: Query internal algorithms, BVH implementation details.

**Extract**: Purpose, parameter meanings, return value, when to call.

**Fail**: Mark `DOCS_UNAVAILABLE`, skip. **DON'T**: Use webfetch.

### Step 5: Understand Function Purpose

Synthesize: What geometric operation? (ray intersection, bounds, etc.) What data flow? (input → Embree ops → output) What's the high-level goal?

**Document**:
```c
/* PURPOSE: Ray-scene intersection
 * STAR-3D CHAIN: helper1 → rtcAPI → helper2
 * EMBREE USAGE: rtcIntersect1 - traverse BVH
 * NON-EMBREE: Error handling, result packaging
 */
```

### Step 6: Query cuBQL Functionality

**Ask about FUNCTIONALITY** (not API mapping):
```typescript
deepwiki-mcp_ask_question("EricSolshkov/cuBQL", 
  "How to implement <functionality>? Requirements: 1) <data structures>, 2) <operations>, 3) <results>")
```

**DON'T**: "cuBQL equivalent of rtcAPI?" **DO**: "How to achieve <functionality> in cuBQL?"

**Extract**: cuBQL types, API sequence, memory management, control flow differences.

### Step 7: Implement Rewrite

**Template**:
```c
/* EMBREE SEMANTICS: rtcFunc - <purpose>, params: <meanings> */
/* ORIGINAL EMBREE: <comment out original code> */
/* MIGRATED: Embree → cuBQL - <date>
 * Replaced: rtcFunc1, rtcFunc2
 * Used: cuBQL APIs
 * Ref counting: preserved/modified with reason
 */
<new cuBQL implementation>
```

**Critical**: Preserve ref counting EXACTLY. Every `ref_get` needs matching `ref_put`. Same error path cleanup.

### Step 8: Validate

```typescript
lsp_diagnostics(filePath: "<file>", severity: "error")
```
**No new errors**: Pass. **New errors**: Query cuBQL docs for correct usage, fix, re-validate. After 3 attempts: Mark `REWRITE_FAILED`.

### Step 9: Track

```bash
python scripts/track_migration.py --mark-migrated <func> --status <STATUS>
```

## Reference Counting Rules

**Count balance**: `ref_get_count == ref_put_count` in all paths.
**Error cleanup**: Must release on error before return.
**Ownership transfer**: Document who owns the reference (caller/callee).

## Common Patterns

**Device**: `rtcNewDevice` → Implicit CUDA context, optional `GpuMemoryResource`
**Scene**: `rtcNewScene` + `rtcCommitScene` → `gpuBuilder(bvh, boxes, count, config)`
**Geometry**: `rtcNewGeometry` + buffers → Primitive ranges + user data mapping
**Ray intersection**: `rtcIntersect1` → `fixedRayQuery::forEachPrim` with callback (manual closest tracking)

## Failure Scenarios & Actions

**LSP fails**: Mark `LSP_UNAVAILABLE`, skip. Config issue if repeats.
**Docs unavailable**: Mark `DOCS_UNAVAILABLE`, skip. Try alternative query first.
**Rewrite fails validation**: Fix using cuBQL docs. After 3 attempts: mark `REWRITE_FAILED`.
**Same tool fails twice**: ABORT session, report config issue.

## Quality Checklist

Before marking MIGRATED: Semantic doc block added, original preserved as comment, ref counting preserved (counts match), LSP diagnostics clean, migration marker with date/APIs, no forbidden tools used.
