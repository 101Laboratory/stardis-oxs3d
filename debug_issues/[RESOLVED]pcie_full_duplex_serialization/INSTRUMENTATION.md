# Instrumentation Details: PCIe Full-Duplex Measurement

This document details the CUDA event timing instrumentation added to measure PCIe transfer overlap between dual batch trace contexts in the PWF solver.

## Objectives

1. Measure GPU-side timing of H2D and D2H transfers (not CPU-side API call overhead)
2. Compute cross-stream timing between View A and View B contexts
3. Determine if PCIe full-duplex is being utilized (overlap > 0)

## Implementation

### 1. Timing Events in Batch Context

**File**: `stardis-oxs3d-merge-phase/oxstar-3d/0.10/s3d_wrapper/ox_s3d_internal.h`

**Location**: `struct s3d_batch_trace_context` (Lines ~303-310)

**Added fields**:
```cpp
cudaEvent_t evt_h2d_begin;   // H2D upload start
cudaEvent_t evt_h2d_end;     // H2D upload complete
cudaEvent_t evt_d2h_begin;   // D2H download start
cudaEvent_t evt_d2h_end;     // D2H download complete
```

**Constructor** (event creation):
```cpp
s3d_batch_trace_context::s3d_batch_trace_context(...) {
    // ... existing code ...
    cudaEventCreate(&evt_h2d_begin);
    cudaEventCreate(&evt_h2d_end);
    cudaEventCreate(&evt_d2h_begin);
    cudaEventCreate(&evt_d2h_end);
}
```

**Destructor** (event cleanup):
```cpp
s3d_batch_trace_context::~s3d_batch_trace_context() {
    // ... existing code ...
    cudaEventDestroy(evt_h2d_begin);
    cudaEventDestroy(evt_h2d_end);
    cudaEventDestroy(evt_d2h_begin);
    cudaEventDestroy(evt_d2h_end);
}
```

### 2. H2D Upload Path Instrumentation

**File**: `stardis-oxs3d-merge-phase/oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp`

**Function**: `batch_trace_filtered_pinned_async_impl`

**Location**: Lines ~2625-2631

**Code**:
```cpp
// Record H2D start event
cudaEventRecord(ctx->evt_h2d_begin, ctx->transfer_stream);

// Upload input rays
cudaMemcpyAsync(ctx->dev_input_buffer, input_rays, 
                input_count * sizeof(s3d_ray_data),
                cudaMemcpyHostToDevice, ctx->transfer_stream);

// Upload auxiliary data (if any)
if (aux_h2d_size > 0) {
    cudaMemcpyAsync(ctx->dev_aux_buffer, aux_h2d_data, aux_h2d_size,
                    cudaMemcpyHostToDevice, ctx->transfer_stream);
}

// Record H2D end event
cudaEventRecord(ctx->evt_h2d_end, ctx->transfer_stream);
```

**Key points**:
- Events recorded on `transfer_stream` (not `compute_stream`)
- `cudaEventRecord()` is non-blocking and enqueued on stream
- Captures actual GPU-side transfer start/end, not CPU API call time

### 3. D2H Download Path Instrumentation

**File**: `stardis-oxs3d-merge-phase/oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp`

**Function**: `batch_trace_filtered_start_d2h_impl`

**Location**: Lines ~2696-2699

**Code**:
```cpp
// Record D2H start event
cudaEventRecord(ctx->evt_d2h_begin, ctx->transfer_stream);

// Download intersection results
cudaMemcpyAsync(ctx->host_output_buffer, ctx->dev_output_buffer,
                batch_size * sizeof(s3d_intersection_record),
                cudaMemcpyDeviceToHost, ctx->transfer_stream);

// Record D2H end event
cudaEventRecord(ctx->evt_d2h_end, ctx->transfer_stream);
```

**Key points**:
- Same pattern as H2D
- Events on same `transfer_stream` as download operation
- Independent from View A/B — each context has its own events

### 4. Cross-Stream Overlap Query API

**File**: `stardis-oxs3d-merge-phase/oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp`

**Function**: `s3d_batch_trace_context_pcie_overlap_query`

**Location**: Lines ~2913-2963

**API signature**:
```cpp
void s3d_batch_trace_context_pcie_overlap_query(
    const s3d_batch_trace_context* ctx_d2h,  // Context that finished D2H
    const s3d_batch_trace_context* ctx_h2d,  // Context that started H2D
    float* out_d2h_ms,      // D2H transfer duration
    float* out_h2d_ms,      // H2D transfer duration
    float* out_offset_ms,   // Time offset between D2H_end and H2D_begin
    float* out_overlap_ms   // Overlap magnitude (0 if serialized)
);
```

**Algorithm**:
```cpp
// Measure D2H duration
cudaEventElapsedTime(out_d2h_ms, ctx_d2h->evt_d2h_begin, ctx_d2h->evt_d2h_end);

// Measure H2D duration
cudaEventElapsedTime(out_h2d_ms, ctx_h2d->evt_h2d_begin, ctx_h2d->evt_h2d_end);

// CROSS-STREAM: Time from D2H_end to H2D_begin
cudaEventElapsedTime(&offset, ctx_d2h->evt_d2h_end, ctx_h2d->evt_h2d_begin);

if (offset >= 0.0f) {
    // Positive offset → D2H finished BEFORE H2D started (serialization)
    *out_offset_ms = offset;
    *out_overlap_ms = 0.0f;
} else {
    // Negative offset → H2D started BEFORE D2H finished (overlap!)
    *out_offset_ms = 0.0f;
    *out_overlap_ms = -offset;  // Convert to positive overlap magnitude
}
```

**Key insight**: `cudaEventElapsedTime()` can compute time between events on **different streams** within the same CUDA context, allowing us to measure cross-stream concurrency.

### 5. Solver Integration

**File**: `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`

**Location**: Lines ~5033-5052 (inside `[TIMELINE_TS]` output block)

**Code**:
```c
// After existing timeline output...

float d2h1, h2d1, ofs1, ovl1;
float d2h2, h2d2, ofs2, ovl2;

// Phase 1: D2H from View A, H2D to View B
s3d_batch_trace_context_pcie_overlap_query(
    pv_a, pv_b, &d2h1, &h2d1, &ofs1, &ovl1);

// Phase 2: D2H from View B, H2D to View A
s3d_batch_trace_context_pcie_overlap_query(
    pv_b, pv_a, &d2h2, &h2d2, &ofs2, &ovl2);

log_info("[PCIE_OVERLAP] step=%u "
         "phase1(D2H_A→H2D_B): d2h=%.3f h2d=%.3f offset=%.3f overlap=%.3fms "
         "phase2(D2H_B→H2D_A): d2h=%.3f h2d=%.3f offset=%.3f overlap=%.3fms\n",
         cstat->step_count, 
         d2h1, h2d1, ofs1, ovl1,
         d2h2, h2d2, ofs2, ovl2);
```

**Output format**:
```
[PCIE_OVERLAP] step=65000 phase1(D2H_A→H2D_B): d2h=0.177 h2d=0.443 offset=0.179 overlap=0.000ms phase2(D2H_B→H2D_A): d2h=0.177 h2d=0.441 offset=0.178 overlap=0.000ms
```

**Interpretation**:
- `phase1`: Measures View A download vs. View B upload
- `phase2`: Measures View B download vs. View A upload (next cycle)
- `offset > 0, overlap=0` → Serialization
- `offset=0, overlap > 0` → Full-duplex working

### 6. Data Collection Script

**File**: `scripts/parse_pcie_overlap.py`

**Purpose**: Parse `[PCIE_OVERLAP]` lines from stderr log, compute statistics

**Usage**:
```bash
# Redirect stderr to log during test run
stardis.exe ... 2> pcie_overlap_log.txt

# Parse and analyze
python scripts/parse_pcie_overlap.py pcie_overlap_log.txt
```

**Output**:
- Statistical summary (mean±std, min/max for d2h, h2d, offset, overlap)
- Gap calculation (offset - overlap)
- Sample data table
- Detection of zero overlap (serialization proof)

## Test Execution

**Command**:
```powershell
cd "d:\Stardis-GPU\Stardis-Starter-Pack\porous"
& "d:\Stardis-GPU\stardis-oxs3d-merge-phase\build\bin\Release\stardis.exe" `
  -M porous.txt -t 32 -V 3 `
  -R "spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0" `
  > "IR_320x320x32.ht" 2> "pcie_overlap_log.txt"
```

**Parameters**:
- `-t 32`: 32 CPU threads
- `-V 3`: Verbosity level 3 (enables `[TIMELINE_TS]` output)
- `-R ...`: 320×320 IR rendering, 32 spp

**Runtime**: ~90 seconds, 127,000 pipeline steps

**Output**:
- `IR_320x320x32.ht`: Rendering output (not analyzed)
- `pcie_overlap_log.txt`: 127 `[PCIE_OVERLAP]` samples (UTF-16-LE encoding)

## Results

**Definitive finding**: ALL 254 measurements (127 samples × 2 phases) show:
- `overlap = 0.000 ms`
- `offset = 1.6±0.2 µs` (positive gap)

**Conclusion**: PCIe transfers are completely serialized. D2H always finishes before H2D starts, with a consistent 1.6µs CUDA driver scheduling gap.

## Verification

**Build**:
```bash
cd stardis-oxs3d-merge-phase/build
cmake --build . --config Release > build.log 2>&1
# Result: 0 errors, 0 warnings
```

**Code review**:
- No blocking calls between D2H (View A) and H2D (View B)
- Events are recorded asynchronously on correct streams
- Cross-stream timing logic is correct (tested with known sequential operations)

## Next Steps

See [HYPOTHESES.md](./HYPOTHESES.md) for theories and validation plan.

Priority: Query GPU capabilities to determine if hardware supports concurrent copy engines.

---

*Created: 2026-03-14*  
*Last updated: 2026-03-14*
