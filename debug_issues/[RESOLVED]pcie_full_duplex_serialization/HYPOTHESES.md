# Hypotheses: PCIe Full-Duplex Serialization

This document tracks theories about why PCIe full-duplex is not working despite having separate CUDA streams for View A and View B batch trace contexts.

## Hypothesis Priority

| ID | Theory | Likelihood | Test Effort | Status |
|----|--------|------------|-------------|--------|
| H1 | Same CUDA context limitation | ~~High~~ | Low | **ELIMINATED** (minimal test disproves) |
| H2 | No concurrent copy engines | ~~High~~ | Low | **ELIMINATED** (minimal test disproves) |
| H3 | CUDA driver serialization policy | ~~Medium~~ | Medium | **ELIMINATED** (implicit by H1/H2) |
| H4 | Stream synchronization barrier | **HIGH** | Low | **PRIMARY SUSPECT** |
| H5 | Memory allocation incompatibility | Low | Low | Pending |
| H6 | D2H completes before H2D issued | **HIGH** | Low | **PRIMARY SUSPECT** |

---

## H1: Same CUDA Context Limitation — ❌ ELIMINATED

**Theory**: Consumer GPUs running both transfers through the same CUDA context cannot achieve true bidirectional PCIe concurrency.

**ELIMINATED BY**: Minimal validation test (`cuda-duplex-validation/test.cu`)
- 256MB H2D + 256MB D2H on separate streams within **same** CUDA context
- Total span (21.66ms) = max(H2D 11.47ms, D2H 21.66ms), NOT sum (33.13ms)
- **Full duplex confirmed on same context, same device**
- See `cuda-duplex-validation/out.txt` for raw results

---

## H2: No Concurrent Copy Engines — ❌ ELIMINATED

**Theory**: GPU hardware lacks dedicated copy engines for simultaneous H2D and D2H.

**ELIMINATED BY**: Same minimal test as H1. The 256MB bidirectional test achieved full overlap (total span = max, not sum), proving the GPU has at least 2 copy engines (asyncEngineCount >= 2).

---

## H3: CUDA Driver Serialization Policy — ❌ ELIMINATED

**Theory**: CUDA runtime serializes `cudaMemcpyAsync()` calls to same device across streams.

**ELIMINATED BY**: Minimal test uses identical API (`cudaMemcpyAsync` on separate `cudaStreamCreate` streams) and achieves full overlap. Driver does NOT serialize — the issue must be in how stardis issues the calls.

---

## H4: Stream Synchronization Barrier — 🔴 PRIMARY SUSPECT

**Theory**: There's an implicit synchronization point (e.g., event query, stream wait, or blocking API call) between D2H and H2D that forces serialization.

**Evidence FOR (strengthened by minimal test)**:
- Minimal test proves hardware + driver support full duplex with same APIs
- Gap is suspiciously consistent (1.6µs σ=0.2µs), suggesting a deterministic sync point
- **The only difference between minimal test (works) and stardis (fails) is the code path between D2H and H2D issuance**
- Minimal test issues both transfers back-to-back before any sync; stardis may have hidden sync between them

**Evidence AGAINST**:
- Initial code review shows no obvious `cudaStreamSynchronize()` / `cudaDeviceSynchronize()` between D2H and H2D
- But: code path between `gpu_start_d2h_all()` and `gpu_launch_all()` traverses solver logic that may contain blocking calls

**Key question**: Is there a `cudaEventSynchronize()`, `cudaStreamSynchronize()`, or `cudaStreamWaitEvent()` between the D2H issuance and the H2D issuance?

**Test**:
1. Trace the EXACT CPU call path from D2H issuance (View A) to H2D issuance (View B)
2. Search for ANY blocking CUDA call on that path
3. Key functions to audit: `gpu_start_d2h_all()` → solver logic → `gpu_launch_all()`
4. Also check: does `gpu_launch_all()` wait for previous compute to finish before issuing H2D?

**Validation**: 
- Code audit of critical path between D2H and H2D calls
- Add CPU-side timing to narrow down where serialization occurs
- If blocking call found: remove or reorder to allow concurrent issue

---

## H5: Memory Allocation Incompatibility

**Theory**: The pinned memory allocations (`cudaHostAlloc()`) for View A and View B may not support concurrent DMA, requiring the driver to serialize access.

**Evidence FOR**:
- Pinned memory is page-locked, but allocation type/flags may matter
- If memory was allocated with `cudaHostAllocDefault` instead of `cudaHostAllocPortable`, concurrency might be affected

**Evidence AGAINST**:
- All memory is allocated with `cudaHostAlloc()` which should support async transfers
- Code review (`ox_s3d_internal.h`) shows proper pinned allocation

**Test**:
Check allocation flags:
```cpp
// In ox_s3d_internal.h constructor
cudaHostAlloc(&ptr, size, cudaHostAllocDefault);  // Current?
vs.
cudaHostAlloc(&ptr, size, cudaHostAllocPortable | cudaHostAllocWriteCombined);
```

**Validation**: Try different allocation flags and re-measure.

---

## H6: D2H Already Complete When H2D Issued — 🔴 PRIMARY SUSPECT

**Theory**: The stardis PWF pipeline's CPU-side sequencing ensures D2H has already physically completed on the GPU before the H2D `cudaMemcpyAsync()` call is even issued by the CPU. The transfers are not "in-flight simultaneously" — the overlap window never exists.

**Evidence FOR (strong)**:
- **Minimal test contrast**: `test.cu` issues H2D and D2H back-to-back in ~microseconds (CPU-side). Both transfers are **in the GPU command queue before either completes**. This is why overlap works.
- **Stardis architecture**: The PWF pipeline does significant CPU work between D2H and H2D:
  1. `gpu_start_d2h_all()` → issues D2H async on View A's transfer_stream
  2. CPU does `merged_pass()` postprocessing (Phase A/B/C/D) → **hundreds of µs of CPU work**
  3. `gpu_launch_all()` → issues H2D async on View B's transfer_stream
- **D2H is only 177µs**: By the time CPU finishes merged_pass and calls H2D, the 177µs D2H has long completed on the GPU
- **1.6µs gap = time between `cudaEventRecord(h2d_begin)` and actual DMA start**: Not a "serialization gap" — it's just the GPU processing the newly-arrived H2D command

**Relationship to H4**:
H4 looks for explicit sync barriers. H6 is subtler — there may be **no explicit barrier**, but the CPU-side latency between D2H and H2D issuance is simply longer than the D2H transfer duration (~177µs). The D2H completes "naturally" before H2D is even submitted.

**Test**:
1. Add CPU-side timestamps around the critical path:
   ```c
   t0 = omp_get_wtime();
   gpu_start_d2h_all(view_a);  // Issues D2H async
   t1 = omp_get_wtime();
   merged_pass(...);           // CPU postprocessing
   t2 = omp_get_wtime();
   gpu_launch_all(view_b);     // Issues H2D async
   t3 = omp_get_wtime();
   ```
2. If `(t2 - t0) > 177µs` → **H6 confirmed**: CPU work takes longer than D2H, so D2H finishes before H2D is issued

**Fix direction (if confirmed)**:
- **Reorder pipeline**: Issue H2D **immediately** after D2H, before merged_pass
  ```c
  gpu_start_d2h_all(view_a);   // D2H async
  gpu_launch_all(view_b);       // H2D async — NOW both in-flight!
  merged_pass(...);              // CPU work while GPU transfers overlap
  ```
- **Requirement**: H2D input data must be ready before merged_pass — may need pipeline restructuring
- **Alternative**: Issue D2H and H2D from separate CPU threads to decouple timing

---

## Testing Order (REVISED after minimal test 2026-03-14)

1. ~~**H1/H2 (GPU capabilities)**~~ — **ELIMINATED** by minimal test
2. ~~**H3 (driver serialization)**~~ — **ELIMINATED** by minimal test
3. **H6 (D2H completes before H2D issued)** — **HIGHEST PRIORITY**. Add CPU timestamps to measure time between D2H and H2D issuance. If CPU path > 177µs, this is the root cause.
4. **H4 (sync barriers)** — Audit code path for hidden blocking calls. May be secondary to H6.
5. **H5 (memory flags)** — Low priority, unlikely given minimal test uses same APIs.

## Expected Outcomes (REVISED)

**Scenario A: ~~Hardware limitation~~** — **ELIMINATED** by minimal test

**Scenario B: CPU-side pipeline ordering issue** (H6 confirmed)
- Restructure pipeline to issue D2H + H2D back-to-back before CPU postprocessing
- Re-measure to confirm overlap > 0
- Quantify actual speedup
- Close issue with `[RESOLVED]` + commit hash

**Scenario C: Hidden synchronization barrier** (H4 confirmed)
- Identify and remove/reorder the blocking call
- Re-measure to confirm overlap > 0
- Close issue with `[RESOLVED]` + commit hash

---

## Minimal Validation Test Results (2026-03-14)

**File**: `cuda-duplex-validation/test.cu`  
**Results**: `cuda-duplex-validation/out.txt`

```
H2D time: 11.470880 ms
D2H time: 21.656704 ms
Total span: 21.658752 ms
OVERLAP DETECTED (full duplex)
```

**Analysis**:
- 256MB bidirectional transfer on same CUDA context, separate streams
- Total span (21.66ms) ≈ max(H2D, D2H) = 21.66ms
- Sum would be 33.13ms → actual is 65% of sum → **full overlap confirmed**
- H2D (11.47ms) ran entirely within D2H's time window
- **Proves**: Same device, same context, same API → full duplex works when both transfers are in-flight simultaneously

**Implication**: Root cause is NOT hardware/driver. It's how/when stardis issues the transfer commands.

---

*Created: 2026-03-14*  
*Last updated: 2026-03-14 — H1/H2/H3 eliminated, H6 added as primary suspect*
