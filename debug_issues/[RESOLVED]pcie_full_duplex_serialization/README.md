# PCIe Full-Duplex Serialization Issue

**Status**: RESOLVED (2026-03-14)  
**Resolution**: O12 stream auto-ordering + O13 async submit  
**Priority**: High (46% potential performance gain)  
**Created**: 2026-03-14  
**Codebase**: `stardis-oxs3d-merge-phase`  
**GPU**: NVIDIA consumer GPU (RTX series assumed)

## Problem

**Expected**: Dual-stream architecture with separate `compute_stream` and `transfer_stream` per batch trace context should enable PCIe full-duplex operation, allowing simultaneous D2H (View A) + H2D (View B) transfers during PWF pipeline.

**Observed**: GPU-side CUDA event timing shows **zero overlap** in all 254 measurements across 127 pipeline cycles. Transfers are completely serialized with a consistent 1.6µs CUDA driver gap between D2H completion and H2D start.

## Impact

- **Performance loss**: 355µs per pipeline cycle wasted (46% potential speedup)
- **Incorrect architecture assumption**: Separate CUDA streams do NOT automatically guarantee PCIe concurrency
- **Underutilized hardware**: PCIe Gen3/4 x16 bidirectional bandwidth (~30 GB/s each direction) not being used

## Measurements

**Test configuration**:
- 32 threads, 320×320 IR rendering, 127,000 pipeline steps
- Binary: `stardis-oxs3d-merge-phase/build/bin/Release/stardis.exe`
- Scene: `porous.txt` (Stardis-Starter-Pack)

**Instrumentation**: 4 CUDA timing events per batch context:
- `evt_h2d_begin`, `evt_h2d_end` (around H2D uploads)
- `evt_d2h_begin`, `evt_d2h_end` (around D2H download)

**Cross-stream timing**: `cudaEventElapsedTime()` between View A and View B contexts

### Statistical Summary (127 samples)

**Phase 1 (D2H_A → H2D_B)**:
- D2H: 177±2 µs (min=164, max=184)
- H2D: 445±19 µs (min=414, max=615)
- Gap: 1.6±0.2 µs (min=1.1, max=2.8)
- **Overlap: 0.000±0.000 ms** (ALL 254 measurements = 0)

**Phase 2 (D2H_B → H2D_A)**:
- D2H: 178±2 µs (min=164, max=188)
- H2D: 440±19 µs (min=407, max=616)
- Gap: 1.6±0.2 µs (min=1.1, max=2.9)
- **Overlap: 0.000±0.000 ms** (ALL 254 measurements = 0)

**Key observation**: The 1.6µs gap represents CUDA driver scheduling overhead. D2H always completes fully before H2D begins, proving transfers are serialized at the hardware/driver level.

## Current Architecture

### Code Locations

**Batch trace context** (`ox_s3d_internal.h` L303-310):
```cpp
struct s3d_batch_trace_context {
    cudaStream_t compute_stream;   // For kernel launches
    cudaStream_t transfer_stream;  // For memory transfers
    cudaEvent_t evt_h2d_begin;     // H2D upload start
    cudaEvent_t evt_h2d_end;       // H2D upload complete
    cudaEvent_t evt_d2h_begin;     // D2H download start
    cudaEvent_t evt_d2h_end;       // D2H download complete
    // ... other fields
};
```

**H2D instrumentation** (`ox_s3d_scene_view.cpp` L2625-2631):
```cpp
cudaEventRecord(ctx->evt_h2d_begin, ctx->transfer_stream);
cudaMemcpyAsync(...);  // Upload rays
cudaMemcpyAsync(...);  // Upload aux data
cudaEventRecord(ctx->evt_h2d_end, ctx->transfer_stream);
```

**D2H instrumentation** (`ox_s3d_scene_view.cpp` L2696-2699):
```cpp
cudaEventRecord(ctx->evt_d2h_begin, ctx->transfer_stream);
cudaMemcpyAsync(...);  // Download results
cudaEventRecord(ctx->evt_d2h_end, ctx->transfer_stream);
```

**Cross-stream overlap query** (`ox_s3d_scene_view.cpp` L2913-2963):
- Takes two contexts (e.g., View A and View B)
- Computes elapsed time between D2H_end (ctx_d2h) and H2D_start (ctx_h2d)
- Returns offset (gap if positive, overlap magnitude if negative)
- **Current result: offset always positive (~1.6µs), overlap always zero**

**Solver integration** (`sdis_solve_persistent_wavefront.c` L5033-5052):
- Calls overlap query twice per `[TIMELINE_TS]` output:
  - Phase 1: `s3d_batch_trace_context_pcie_overlap_query(pv_a, pv_b)` (D2H_A → H2D_B)
  - Phase 2: `s3d_batch_trace_context_pcie_overlap_query(pv_b, pv_a)` (D2H_B → H2D_A)
- Outputs `[PCIE_OVERLAP]` line with 8 timing values per phase

### PWF Pipeline Context

The persistent wavefront (PWF) solver uses a dual-buffer architecture with two batch trace contexts (View A and View B) alternating between GPU work and CPU postprocessing. The expected full-duplex pattern:

```
Cycle N:   VIEW_A: [Compute] → [D2H download]
                    VIEW_B:              [H2D upload] → [Compute]
           ═══════════════════════════════════════════════════════
Expected:                      ⬐─── Overlap ───⬎
Actual:                    D2H ends → 1.6µs gap → H2D starts
```

## Hypotheses

See [HYPOTHESES.md](./HYPOTHESES.md) for detailed theories and validation status.

### Eliminated (2026-03-14, by minimal validation test)

A standalone CUDA test (`cuda-duplex-validation/test.cu`) with 256MB bidirectional transfers on the **same context, same device, separate streams** achieved full overlap (total span = max, not sum). This **eliminates all hardware/driver hypotheses**:

- ~~H1: Same CUDA context limitation~~ — Minimal test uses same context, overlap works
- ~~H2: No concurrent copy engines~~ — asyncEngineCount >= 2 proven by behavior
- ~~H3: CUDA driver serialization~~ — Same API, same driver, overlap works

### Active suspects

- **H6 (PRIMARY): D2H completes before H2D is issued** — The CPU-side pipeline does `merged_pass()` postprocessing (~hundreds of µs) between `gpu_start_d2h_all()` and `gpu_launch_all()`. Since D2H is only 177µs, it completes on the GPU long before H2D is even submitted. The overlap window never exists.
- **H4: Hidden stream synchronization barrier** — Possible secondary cause if explicit blocking call exists on the critical path.

### Root cause direction

This is a **pipeline scheduling issue**, not a hardware limitation. The fix requires restructuring the CPU-side call order to ensure D2H and H2D are issued back-to-back (both in-flight on GPU simultaneously) before CPU postprocessing begins.

## Data Collection

**Raw log**: `Stardis-Starter-Pack/porous/pcie_overlap_log.txt` (UTF-16-LE, 127 samples)  
**Clean data**: `Stardis-Starter-Pack/porous/pcie_overlap_pure.txt` (UTF-8, 127 samples)  
**Parser**: `scripts/parse_pcie_overlap.py` (statistical analysis)

**Sample output** (step 65000):
```
[PCIE_OVERLAP] step=65000 
  phase1(D2H_A→H2D_B): d2h=0.177 h2d=0.443 offset=0.179 overlap=0.000ms 
  phase2(D2H_B→H2D_A): d2h=0.177 h2d=0.441 offset=0.178 overlap=0.000ms
```

## Verification Plan

1. ~~**GPU capability query**~~ — **ELIMINATED**: Minimal test proves hardware OK
2. ~~**Stream priority test**~~ — **ELIMINATED**: Not a driver/stream config issue
3. **CPU-side timing** — Measure time between `gpu_start_d2h_all()` and `gpu_launch_all()` calls. If > 177µs, confirms H6.
4. **Pipeline reorder** — Issue H2D immediately after D2H (before `merged_pass`). Re-measure overlap.
5. **Data dependency audit** — Verify H2D input data is ready before merged_pass (may need pipeline restructure).

## Success Criteria

- ~~Identify root cause of serialization (HW limitation vs SW configuration)~~ → **SW scheduling confirmed**
- Verify H6 with CPU-side timestamps (time between D2H issue and H2D issue > D2H duration)
- Restructure pipeline to issue D2H + H2D back-to-back
- Measure non-zero PCIe overlap (target: 150-170µs from smaller transfer)
- Quantify actual speedup vs. current serialized baseline

## References

- **Minimal validation test**: `cuda-duplex-validation/test.cu` + `cuda-duplex-validation/out.txt` (proves hardware full-duplex works)
- **Instrumentation code**: See modifications in `ox_s3d_internal.h`, `ox_s3d_scene_view.cpp`, `sdis_solve_persistent_wavefront.c`
- **PWF design docs**: `guide/pwf_design/`
- **Optimization history**: `optimization/wf_pipeline/`
- **CUDA best practices**: NVIDIA CUDA C Programming Guide, Section 12 (Concurrent Execution)

## Resolution (2026-03-14)

**Root cause H6 confirmed and fixed** by O12 + O13:

1. **O12 Stream Auto-Ordering**: `gpu_submit_all` queues H2D→Kernel→D2H as consecutive async calls on per-view streams. `cudaStreamWaitEvent` gates D2H on kernel completion. This reorders the pipeline so H2D and D2H can overlap across views.

2. **O13 Async Submit Thread**: `gpu_submit_all` offloaded to a dedicated thread with per-view channels (`WaitForMultipleObjects` multiplexing). Main thread proceeds to CPU work immediately after signaling, eliminating the CPU scheduling gap that caused serialization.

**Results (32 threads, porous scene)**:
- O12 alone: `wait_d2h=29.5s`, GPU 83% hidden, `wall=137.7s`, no boost is confirmed, as expected.
- O13 final (per-view channels): `submit=29.8s` fully overlapped, `coverage=129.8%`, `wall=119.5s` → **13.2% wall-clock reduction**

**Commits**: `opt/stream-auto-order` (O12), `opt/merge-phase` ec873d2 (O13)

---

*Created: 2026-03-14*  
*Resolved: 2026-03-14*
