# Action Plan: PCIe Full-Duplex Investigation

**Priority**: High  
**Estimated effort**: 2-4 hours (diagnostic) + variable (fix complexity TBD)  
**Owner**: TBD

## Immediate Next Steps

### Step 1: GPU Capability Diagnostic (30 minutes)

**Goal**: Determine hardware capabilities for concurrent PCIe transfers.

**Implementation**:
```cpp
// Add to ox_s3d_scene_view.cpp or standalone diagnostic
cudaDeviceProp prop;
int device_id = 0;  // Or use actual device from context
cudaGetDeviceProperties(&prop, device_id);

printf("=== GPU PCIe Capabilities ===\n");
printf("Device: %s\n", prop.name);
printf("Compute Capability: %d.%d\n", prop.major, prop.minor);
printf("asyncEngineCount: %d\n", prop.asyncEngineCount);
printf("deviceOverlap: %d\n", prop.deviceOverlap);
printf("canMapHostMemory: %d\n", prop.canMapHostMemory);
printf("concurrentKernels: %d\n", prop.concurrentKernels);
printf("streamPrioritiesSupported: %d\n", prop.streamPrioritiesSupported);
printf("PCIe Generation: %d\n", prop.pciDomainID);
printf("============================\n");
```

**Integration point**: 
- Add to `s3d_batch_trace_context` constructor (runs once per view)
- Or create standalone test program: `tests/test_cuda_pcie_capabilities.cu`

**Decision tree**:
```
asyncEngineCount == 0 or 1:
  → Hardware limitation confirmed
  → Skip to Step 4 (document and close)

asyncEngineCount >= 2:
  → Hardware supports concurrent copy
  → Proceed to Step 2 (investigate driver/configuration)
```

**Validation**:
```bash
cd stardis-oxs3d-merge-phase/build
cmake --build . --config Release > build.log 2>&1
./bin/Release/stardis.exe ... 2>&1 | grep "GPU PCIe Capabilities" -A 10
```

### Step 2: Stream Priority Test (1 hour)

**Theory**: High-priority streams may reduce driver scheduling overhead.

**Implementation**:
```cpp
// Modify ox_s3d_internal.h constructor
int leastPriority, greatestPriority;
cudaDeviceGetStreamPriorityRange(&leastPriority, &greatestPriority);

// Create high-priority transfer streams
cudaStreamCreateWithPriority(&transfer_stream, 
                             cudaStreamNonBlocking, 
                             greatestPriority);
```

**Expected outcome**:
- If gap reduces (e.g., 1.6µs → 0.5µs) but overlap still zero: Driver overhead reduced but serialization remains
- If overlap > 0: **SUCCESS** — priority was the issue
- If no change: Priority is not relevant, root cause deeper

**Rollback**: Easy — just remove priority flags, back to default `cudaStreamCreate()`

### Step 3: Separate CUDA Context Test (2-3 hours)

**Theory**: Same CUDA context forces serialization. Separate contexts per view may enable concurrency.

**Implementation** (complex, needs design):
```cpp
// ox_s3d_internal.h
struct s3d_batch_trace_context {
    int dedicated_device_id;        // NEW: Each context gets own device handle
    cudaStream_t compute_stream;
    cudaStream_t transfer_stream;
    // ...
};

// Constructor: Create context, set device
s3d_batch_trace_context::s3d_batch_trace_context(...) {
    // Option A: Different logical devices (multi-GPU system only)
    dedicated_device_id = context_index % num_devices;
    cudaSetDevice(dedicated_device_id);
    
    // Option B: Same physical device, different contexts (requires CUDA MPS)
    // ... more complex, needs MPS daemon setup ...
}
```

**Challenges**:
- Multi-GPU: Requires peer access setup, complicates memory management
- CUDA MPS: Requires external daemon, may not work on consumer GPUs
- Memory pinning: May need device-specific pinned allocations

**Risk**: High complexity, may require major architectural changes

**Decision**: Only attempt if Step 1 confirms multi-engine GPU and Step 2 fails

### Step 4: Document Hardware Limitation (if confirmed)

**Scenario**: If `asyncEngineCount < 2` or all software attempts fail.

**Actions**:
1. Add findings to `RESOLUTION.md` with conclusion: "Consumer GPU hardware limitation"
2. Document workaround strategies:
   - **Minimize transfer sizes**: Compress ray data, use delta encoding
   - **Increase batch size**: Amortize transfer overhead over more work
   - **Overlap compute with transfer**: Focus on kernel efficiency instead
3. Update optimization docs (`optimization/AGENTS.md`) with PCIe limitation note
4. Rename directory to `[RESOLVED]pcie_full_duplex_serialization/`
5. Close issue with architectural note in project AGENTS.md

**Acceptance**: 29% theoretical speedup is significant but not always achievable on consumer hardware

## Alternative Approaches (if standard methods fail)

### A5: CUDA Graphs (experimental)

**Theory**: Batching operations in a graph may allow driver-level reordering.

```cpp
cudaGraph_t graph;
cudaGraphExec_t graphExec;

cudaStreamBeginCapture(stream_a, cudaStreamCaptureModeGlobal);
// Record D2H on stream_a
cudaStreamEndCapture(stream_a, &graph);

cudaStreamBeginCapture(stream_b, cudaStreamCaptureModeGlobal);
// Record H2D on stream_b
cudaStreamEndCapture(stream_b, &graph);

cudaGraphInstantiate(&graphExec, graph, NULL, NULL, 0);
cudaGraphLaunch(graphExec, master_stream);
```

**Effort**: High  
**Likelihood**: Low (graphs optimize kernel launches, less impact on memory transfers)

### A6: Unified Memory (not applicable)

Unified Memory could eliminate explicit transfers but:
- Performance unpredictable (automatic migration)
- CPU access during GPU compute causes stalls
- Not suitable for real-time solver

**Verdict**: Not viable for this workload

### A7: Hardware Upgrade Path

Document GPUs known to support concurrent copy:
- **Tesla/A100**: Multiple copy engines, verified full-duplex
- **RTX 40xx Pro**: Check specs (some have more copy engines than consumer variants)
- **H100**: Highest PCIe Gen5 bandwidth, multiple copy engines

Include hardware recommendation in final report.

## Success Metrics

**Minimal success** (hardware limitation confirmed):
- Clear diagnostic output showing `asyncEngineCount`
- Documented limitation in issue tracker
- Workaround strategies documented

**Full success** (overlap achieved):
- Re-run test with fixed configuration
- Measure `overlap > 0` in at least 80% of samples
- Quantify actual speedup (e.g., "15% faster per cycle")
- Document fix in RESOLUTION.md

## Timeline

| Step | Duration | Dependencies |
|------|----------|--------------|
| Step 1: GPU diagnostic | 30 min | None |
| Step 2: Stream priority | 1 hour | Step 1 (if multi-engine) |
| Step 3: Separate contexts | 2-3 hours | Step 2 failure |
| Step 4: Document | 1 hour | Any outcome |

**Total**: 1-5 hours depending on path

## Rollback Plan

All changes are additive (diagnostics, priority flags). Easy to revert:
```bash
git checkout ox_s3d_internal.h ox_s3d_scene_view.cpp
```

Instrumentation (timing events) stays in place — useful for future optimization.

## Open Questions

1. **Which GPU is actually being used?** (Need `nvidia-smi` or diagnostic output)
2. **Do we have access to a multi-GPU system?** (For testing separate device contexts)
3. **Is CUDA MPS available on target platform?** (Windows may not support MPS)
4. **What's the acceptable outcome?** (hardware limitation vs. must fix)

## References

- **CUDA Programming Guide**: Section 12.2 (Concurrent Data Transfers)
- **CUDA Best Practices**: Section 9.1.3 (Asynchronous Concurrent Execution)
- **NVIDIA Developer Blog**: "How to Overlap Data Transfers in CUDA C/C++"
- **cuBQL migration notes**: `guide/embree_migration/` (may have similar findings)

---

*Created: 2026-03-14*  
*Last updated: 2026-03-14*
