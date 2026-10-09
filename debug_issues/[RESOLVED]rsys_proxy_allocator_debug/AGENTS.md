# rsys Proxy Allocator Debug Investigation - Status Report

**Issue**: star-3d Debug tests fail with exit code 3  
**Location**: `mem_shutdown_proxy_allocator()` in rsys  
**Status**: 🔍 **DIAGNOSTICS APPLIED - AWAITING DETAILED DEBUG**  
**Branch**: `debug/proxy-allocator-leak-investigation`  
**Date**: 2026-01-18 09:35 AM SGT

---

## Current Findings

### ✅ Confirmed Facts

1. **Failure Point**: `mem_shutdown_proxy_allocator()` (user verified via VS debugger)
2. **Exit Code**: Debug=3, Release=0
3. **ASSERT Definition**:
   ```c
   #ifndef NDEBUG
     #define ASSERT(C) assert(C)  // Standard C assert - triggers immediately
   #else
     #define ASSERT(C) (void)0     // No-op in Release
   #endif
   ```

4. **Failure Line** (line 347 in modified file):
   ```c
   ASSERT(proxy_data->node_list == NULL);
   ```

5. **Output Size**:
   ```
   Debug:   307,202 lines, 2,924,630 bytes
   Release: 307,202 lines, 2,924,630 bytes
   (Identical - large PPM is not the problem)
   ```

6. **Test Pattern**:
   - ❌ Failing: All 5 are rendering tests (output 640×480 PPM)
   - ✅ Passing: All 12 are unit tests (no/minimal output)

### ⚠️ Unexpected Behavior

**Diagnostic patch applied but NO output captured**:
- Added `fprintf(stderr, ...)` before ASSERT
- Added `fflush(stderr)` to force output
- Rebuilt rsys + star-3d
- Ran test → stderr is empty!

**Possible Explanations**:
1. **assert() aborts before fprintf**: Standard `assert()` may call `abort()` which terminates process immediately
2. **Buffering issue**: stderr buffered despite fflush (unlikely on Windows)
3. **Different code path**: Assertion triggers elsewhere before reaching our fprintf
4. **VS Debugger interference**: Debugger may catch assert before our code runs

---

## Investigation Approach (Next Steps)

### Option A: Use VS Debugger (RECOMMENDED - User Has It)

**User reported**: "断点触发在mem_shutdown_proxy_allocator，不在check_memory_allocator"

**Next debugging session in VS**:

```cpp
// Set breakpoint at line 347: ASSERT(proxy_data->node_list == NULL);
// When breakpoint hits:

1. Inspect: proxy_data->node_list
   - Is it NULL or valid pointer?
   - If valid, examine first few nodes

2. For each node in list:
   - node->size = ?
   - node->filename = ?
   - node->fileline = ?
   - node->next / prev = ?

3. Evaluate: MEM_SIZE(proxy_data->allocator, node)
   - Returns 0 or actual size?

4. Evaluate: proxy_allocated_size(proxy_data)
   - Should be 0 (check_memory_allocator passed)
   
5. Count nodes manually:
   node = proxy_data->node_list;
   count = 0;
   while(node) { count++; node = node->next; }
   // How many nodes?
```

**This will definitively show**:
- How many nodes remain
- What they point to (file:line of allocation)
- Size discrepancy (node->size vs MEM_SIZE)

---

### Option B: Disable ASSERT Temporarily (GET DATA)

**Strategy**: Replace ASSERT with conditional abort to capture data first.

```c
// In mem_proxy_allocator.c line 347, replace:
ASSERT(proxy_data->node_list == NULL);

// With:
if(proxy_data->node_list != NULL) {
  struct mem_node* node = proxy_data->node_list;
  fprintf(stderr, "FATAL: node_list not NULL! First node:\n");
  fprintf(stderr, "  size=%lu, at %s:%u\n", 
    (unsigned long)node->size,
    node->filename ? node->filename : "?",
    node->fileline);
  fprintf(stderr, "  MEM_SIZE returns: %lu\n",
    (unsigned long)MEM_SIZE(proxy_data->allocator, node));
  fflush(stderr);
  abort();  // Still fail, but after output
}
```

**Rebuild, run, capture stderr** → Will show first leaked allocation

---

### Option C: Comment Out ASSERT (TEMPORARY - SEE IF WORKS)

**Strategy**: Disable the check entirely to see if tests pass.

```c
// Line 347
/* ASSERT(proxy_data->node_list == NULL); */  // DISABLED FOR INVESTIGATION
```

**If tests pass**: Confirms node_list inconsistency exists but doesn't cause actual memory corruption.  
**If tests still fail**: Problem is elsewhere (mutex_destroy or MEM_RM).

---

## Hypotheses Ranked by Likelihood

### 🔴 Hypothesis 1: proxy_free doesn't unlink from list (VERY LIKELY)

**Theory**: `MEM_RM(proxy_data->allocator, node)` frees the memory BUT node remains in list.

**Why**:
- Line 143: `MEM_RM(proxy_data->allocator, node);` called AFTER unlinking
- But if list unlinking (lines 132-141) has bug, node stays in list
- `MEM_SIZE` on freed memory might return 0 (undefined behavior)

**How to verify** (VS Debugger):
- Set breakpoint in `proxy_free` line 143
- Run failing test
- Check if this breakpoint is hit for all allocations
- Check if node_list head is updated correctly

**Expected finding**: Some allocations not going through proxy_free.

---

### 🟡 Hypothesis 2: embree uses different allocator (LIKELY)

**Theory**: embree allocates memory via its own allocator, not through proxy.

**Why**:
- embree is external library with own memory management
- s3d might pass proxy allocator to embree, but embree keeps internal pools
- embree device shutdown might not flush all pools in Debug mode

**How to verify** (Code Review):
```c
// Find s3d_device_create in s3d_device.c
// Check if rtcNewDevice() receives custom allocator callbacks
// Example:
RTCDevice device = rtcNewDevice(NULL);  // Uses default allocator (BAD)
// vs
RTCDevice device = rtcNewDevice("set_memory_monitor_function=<callback>");  // Custom
```

**If embree uses default allocator**: It bypasses proxy_allocator → node_list mismatch.

---

### 🟢 Hypothesis 3: Image library internal buffers (POSSIBLE)

**Theory**: `image_write_ppm_stream` allocates temporary buffers not tracked by proxy.

**Why**:
- Writing 3MB PPM might allocate temp buffers for formatting
- If these use a different allocator, they bypass tracking

**How to verify**:
```c
// Find image_write_ppm_stream in rsys/src/image.c
// Check all allocations inside this function
// Verify they use the image's allocator (passed via image_init)
```

---

### 🟢 Hypothesis 4: Reference counting bug (POSSIBLE)

**Theory**: s3d objects (device, scene, view) not fully released despite ref_put calls.

**Why**:
- Line 122-124: `s3d_device_ref_put`, `s3d_scene_ref_put`, `s3d_scene_view_ref_put`
- If reference count doesn't reach 0, destructor doesn't run
- Embree resources held by these objects never freed

**How to verify** (Add logging):
```c
// In s3d_device.c (device destructor)
static void s3d_device_destructor(void* obj) {
  fprintf(stderr, "[S3D-DEBUG] Device destructor called\n");
  // ... existing code
}
```

**Expected**: If destructor not called, ref counting is broken.

---

## Recommended Investigation Sequence

### Step 1: VS Debugger Session (15 min) - **DO THIS FIRST**

**What to do**:
1. Open `test_s3d_sphere_box.exe` in VS (Debug config)
2. Set breakpoint at `mem_proxy_allocator.c:347` (ASSERT line)
3. Run until breakpoint
4. Inspect `proxy_data->node_list`:
   - Copy node addresses, sizes, filenames, line numbers
   - Count total nodes manually
   - Check `MEM_SIZE(proxy_data->allocator, first_node)`
5. Take screenshot or copy watch window data

**Expected**: You'll see 1-3 nodes remaining with:
- `node->size > 0` BUT `MEM_SIZE returns 0`
- Allocation locations pointing to embree or image code

**This single debugging session will answer**:
- How many nodes leaked?
- Where were they allocated?
- What's their size discrepancy?

---

### Step 2: Apply Option B Patch (10 min) - **IF VS DEBUGGING NOT AVAILABLE**

Apply the conditional abort patch from Option B above to force output before crash.

---

### Step 3: Minimal Reproduction (20 min) - **AFTER identifying allocation source**

Create minimal test case based on Step 1 findings:

**If embree-related**:
```c
// test_embree_allocator.c
#include <rsys/rsys.h>
#include "s3d_device.h"

int main(void) {
  struct mem_allocator alloc;
  struct s3d_device* dev;
  
  mem_init_proxy_allocator(&alloc, &mem_default_allocator);
  s3d_device_create(NULL, &alloc, 0, &dev);
  s3d_device_ref_put(dev);
  
  CHK(mem_allocated_size(&alloc) == 0);
  mem_shutdown_proxy_allocator(&alloc);  // Does this fail?
  return 0;
}
```

**If image-related**:
```c
// test_image_ppm_allocator.c
#include <rsys/rsys.h>
#include <rsys/image.h>

int main(void) {
  struct mem_allocator alloc;
  struct image img;
  
  mem_init_proxy_allocator(&alloc, &mem_default_allocator);
  image_init(&alloc, &img);
  image_setup(&img, 640, 480, 640*3, IMAGE_RGB8, NULL);
  image_write_ppm_stream(&img, 0, stdout);
  image_release(&img);
  
  CHK(mem_allocated_size(&alloc) == 0);
  mem_shutdown_proxy_allocator(&alloc);  // Does this fail?
  return 0;
}
```

---

## Potential Quick Fixes (Based on Hypotheses)

### Fix A: Force Node List Cleanup (DEFENSIVE)

```c
void
mem_shutdown_proxy_allocator(struct mem_allocator* proxy)
{
  struct proxy_data* proxy_data = NULL;

  ASSERT(proxy);
  proxy_data = proxy->data;
  if(proxy_data) {
    /* Force cleanup of residual nodes (RSYS-DEBUG-001 workaround) */
    if(proxy_data->node_list != NULL) {
      struct mem_node* node = proxy_data->node_list;
      fprintf(stderr, "WARNING: Cleaning %lu residual node(s)\n",
        (unsigned long)proxy_dump(proxy_data, NULL, 0));
      
      while(node) {
        struct mem_node* next = node->next;
        MEM_RM(proxy_data->allocator, node);  // Force free
        node = next;
      }
      proxy_data->node_list = NULL;
    }
    
    ASSERT(proxy_data->node_list == NULL);  // Should pass now
    if(proxy_data->mutex) mutex_destroy(proxy_data->mutex);
    MEM_RM(proxy_data->allocator, proxy_data);
  }
  memset(proxy, 0, sizeof(struct mem_allocator));
}
```

**Risk**: May cause double-free if nodes were already freed. Need validation first.

---

### Fix B: Tolerant Mode (WORKAROUND)

```c
void
mem_shutdown_proxy_allocator(struct mem_allocator* proxy)
{
  struct proxy_data* proxy_data = NULL;

  ASSERT(proxy);
  proxy_data = proxy->data;
  if(proxy_data) {
    #ifndef RSYS_STRICT_SHUTDOWN
      /* Tolerant mode: warn but don't abort on residual nodes */
      if(proxy_data->node_list != NULL) {
        fprintf(stderr, "WARNING: node_list not empty at shutdown (continuing anyway)\n");
      }
    #else
      /* Strict mode: abort on any leak */
      ASSERT(proxy_data->node_list == NULL);
    #endif
    
    if(proxy_data->mutex) mutex_destroy(proxy_data->mutex);
    MEM_RM(proxy_data->allocator, proxy_data);
  }
  memset(proxy, 0, sizeof(struct mem_allocator));
}
```

**Compile with**: Default (tolerant) or `-DRSYS_STRICT_SHUTDOWN` (strict)

---

## What User Should Do Next

### 🔴 IMMEDIATE (15 minutes):

**Open VS Debugger and capture node_list contents**:

```
1. Open: D:\Works\Projects\Stardis-GPU\stardis-cpu\star-3d\0.10\star-3d.sln
2. Set Debug configuration
3. Set project: test_s3d_sphere_box
4. Set breakpoint: mem_proxy_allocator.c line 347
   (Or search for "ASSERT(proxy_data->node_list == NULL)")
5. Start Debugging (F5)
6. When breakpoint hits:
   - Watch window: Add "proxy_data->node_list"
   - If not NULL, expand it:
     - node_list->size
     - node_list->filename
     - node_list->fileline
     - node_list->next
   - If next != NULL, examine next node too
7. Screenshot or copy watch window data
```

**Expected data format**:
```
proxy_data->node_list = 0x000001234567 {mem_node}
  size = 921600
  filename = 0x00007FF... "D:/Works/.../image.c"
  fileline = 234
  next = 0x000000000 (NULL)
  prev = 0x000000000 (NULL)
```

**This will show**: Which allocation wasn't freed and where it came from.

---

### 🟡 ALTERNATIVE (if VS unavailable): Apply Option B Patch

Modify line 347 to output before aborting:

```c
if(proxy_data->node_list != NULL) {
  struct mem_node* node = proxy_data->node_list;
  fprintf(stderr, "\nFATAL: Residual allocation detected:\n");
  fprintf(stderr, "  Node: %p\n", (void*)node);
  fprintf(stderr, "  Size: %lu bytes (node->size field)\n", (unsigned long)node->size);
  fprintf(stderr, "  MEM_SIZE: %lu bytes (via allocator query)\n", 
    (unsigned long)MEM_SIZE(proxy_data->allocator, node));
  fprintf(stderr, "  Location: %s:%u\n",
    node->filename ? node->filename : "unknown", node->fileline);
  fflush(stderr);
}
ASSERT(proxy_data->node_list == NULL);
```

Then run: `test_s3d_sphere_box.exe 2>leak.txt` and examine `leak.txt`.

---

## Expected Root Causes (Prediction)

Based on evidence, most likely scenario:

### Scenario A: embree Internal Allocation (70% confidence)

**What happens**:
1. `s3d_device_create` initializes embree device
2. embree allocates internal structures (BVH, thread pools)
3. embree uses its own allocator, NOT proxy_allocator
4. Test renders scene (embree allocations active)
5. `image_write_ppm_stream` succeeds
6. `s3d_device_ref_put` calls `rtcReleaseDevice`
7. **embree delays cleanup** in Debug mode (for validation)
8. proxy_allocator shutdown happens
9. Some proxy-allocated wrapper still points to embree memory
10. node_list has embree-related nodes

**Fix**: Ensure embree device is fully released before proxy shutdown.

---

### Scenario B: Image Library Buffer Not Freed (20% confidence)

**What happens**:
1. `image_setup` allocates 921,600 bytes (640×480×3) for pixels
2. `image_write_ppm_stream` allocates temp buffer for formatting
3. **Temp buffer not freed** before returning
4. `image_release(&img)` frees main buffer but not temp
5. Temp buffer node remains in list

**Fix**: Audit `image_write_ppm_stream` for cleanup.

---

### Scenario C: proxy_free Unlinking Bug (10% confidence)

**What happens**:
1. Memory is freed via `MEM_RM`
2. But node_list unlinking (lines 132-141) has edge case bug
3. Node marked as freed but stays in list
4. `MEM_SIZE` returns 0 for freed nodes
5. `allocated_size()` returns 0 (sums zero sizes)
6. But list not empty

**Fix**: Fix unlinking logic in proxy_free.

---

## Files Modified (debug branch)

```
stardis-cpu/rsys/0.15/
├── .git/ (initialized, branch: debug/proxy-allocator-leak-investigation)
└── src/mem_proxy_allocator.c (lines 309-347 modified with diagnostics)
```

**Changes**:
- Added fprintf before all ASSERTs
- Added node_list dump with detailed info
- Added fflush(stderr) calls
- **Status**: Built successfully, but diagnostics not appearing (assert() aborts first)

---

## Next Actions (Priority Order)

| Priority | Action | Time | Blocker |
|----------|--------|------|---------|
| 🔴 **CRITICAL** | VS Debug session to inspect node_list | 15 min | None - user has VS |
| 🟡 **HIGH** | Apply Option B patch (conditional abort with output) | 10 min | None |
| 🟡 **HIGH** | Review s3d_device embree integration | 20 min | Need node_list data first |
| 🟢 **MEDIUM** | Audit image_write_ppm_stream for temp buffers | 20 min | Need node_list data first |
| 🟢 **MEDIUM** | Create minimal reproduction test | 15 min | Need root cause hypothesis |
| 🟢 **LOW** | Check if issue exists on Linux | 10 min | None |

---

## Success Criteria

**Investigation complete when**:
- ✅ Root cause identified (which component's allocation remains)
- ✅ Fix implemented and tested
- ✅ All 5 Debug tests pass (exit code 0)
- ✅ No regressions in passing tests
- ✅ Solution documented in rsys CHANGELOG

**Target**: All 17 star-3d tests pass in Debug (currently 12/17).

---

**Current Status**: 🟡 **DIAGNOSTIC PATCH APPLIED - AWAITING VS DEBUGGER SESSION**

**Recommendation**: Use VS debugger to inspect `node_list` at breakpoint (15 min investment, high value).

---

**Report Generated**: 2026-01-18 09:35 AM SGT  
**Branch**: debug/proxy-allocator-leak-investigation  
**Modified Files**: 1 (mem_proxy_allocator.c)  
**Next Update**: After VS debugger session or Option B patch results
