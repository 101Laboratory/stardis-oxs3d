# PWF Dual-Buffer CPU-GPU Timing Analysis

## Timing Evolution

### O13 (基准线): Async Submit + Stream Auto-Ordering
以下数据来自 O13 性能分析。O16 层叠在 O13 之上，主要改善缓存行为（NT Store + 64B对齐），测务中的 merged_pass 时间应扙键受益。
```
timing (pipeline phases):
    submit       =  29.789s [24.9%] (async thread, overlapped)
    wait_d2h     =  27.160s [22.7%]  (trace=22.804s  enc=0.014s  cp=0.012s)
      trace_stall=   3.185s           (device: kern=3.396s d2h=3.029s, 50% hidden)
      trace_post =  19.619s           (host: post=19.619s retrace=0.000s)
    merged_pass  =  78.508s [65.7%]
    compact+rfill=   8.898s [ 7.4%]
    housekeeping =   0.578s [ 0.5%]
    total= 144.933s  wall=119.494s  coverage=129.8%

batch trace profiling: calls=411461  avg_batch=31381  min=2  max=51712
  gpu_kernel:        total=3396.2ms (13.0%)  avg=0.01ms/call
  d2h_wait:          total=3028.9ms (11.6%)  avg=0.01ms/call
  cpu_postprocess:   total=19618.8ms (75.3%)  avg=0.05ms/call
  gpu_throughput: 3802.0 Mrays/s  (kernel only)
```

### O12 Baseline (Stream Auto-Ordering, sync submit)
```
timing (pipeline phases):
    submit       =  27.312s [19.8%]
    wait_d2h     =  29.503s [21.4%]
    merged_pass  =  70.337s [51.0%]
    compact+rfill=   9.175s [ 6.7%]
    housekeeping =   1.314s [ 1.0%]
    total= 137.641s  wall=137.730s  coverage=99.9%
```

### Pre-O12 Baseline (Old pipeline)
```
timing: total_timed=143.570s  wall=142.128s  coverage=101.0%
  cascade=71.925s  gpu_launch=22.312s  gpu_sync=1.739s
  gpu_kernel=21.196s  cpu_post=21.186s
```

---

## Timeline Visualization (O13 Async Submit Pipeline)

```mermaid
gantt
    title PWF O13 Async Submit Pipeline (119.5s wall, 129.8% coverage)
    dateFormat X
    axisFormat %s
    
    section Submit Thread
    submit(A)        :active, sub_a1, 0, 15s
    submit(B)        :active, sub_b1, 15, 30s
    submit(A)        :active, sub_a2, 60, 75s
    submit(B)        :active, sub_b2, 75, 90s
    
    section GPU (Async)
    H2D+Kern+D2H A1 :crit, gpu_a1, 0, 20s
    H2D+Kern+D2H B1 :crit, gpu_b1, 15, 35s
    H2D+Kern+D2H A2 :crit, gpu_a2, 60, 80s
    H2D+Kern+D2H B2 :crit, gpu_b2, 75, 95s
    
    section Main Thread (CPU)
    wait_d2h(A)      :done, wait_a1, 20, 27s
    merged+compact A :merge_a1, 27, 60s
    wait_d2h(B)      :done, wait_b1, 35, 42s
    merged+compact B :merge_b1, 42, 75s
    wait_d2h(A)      :done, wait_a2, 80, 87s
    merged+compact A :merge_a2, 87, 115s
    Drain            :milestone, drain, 119s, 119s
```

**Key insight**: submit thread (orange) runs concurrently with main thread (blue).
Coverage > 100% confirms submit time is fully hidden behind CPU work.

---

## Time Breakdown (O13, 119.5s wall)

### Pipeline Phases (5 Mutually Exclusive)
```
submit       =  29.8s  [24.9%]  ← HIDDEN (async thread)
wait_d2h     =  27.2s  [22.7%]
  trace_stall=   3.2s            (kern=3.4s d2h=3.0s, 50% hidden)
  trace_post =  19.6s            (host postprocess)
merged_pass  =  78.5s  [65.7%]  ← PRIMARY CPU BOTTLENECK
compact+rfill=   8.9s  [ 7.4%]
housekeeping =   0.6s  [ 0.5%]
─────────────────────────────────
total_timed  = 144.9s
wall_time    = 119.5s
coverage     = 129.8% (>100% = overlap working)
hidden_time  ≈  25.4s (≈ submit time)
```

### GPU Execution (Overlapped)
```
GPU Kernel Time:    3.396s   (kern only, 3802 Mrays/s)
D2H Wait:          3.029s
CPU Post:         19.619s
411,461 batches × avg 31,381 rays
```

### Bottleneck Identification (O13)
```
1. Merged Pass: 78.5s (65.7%) ← DOMINANT, OMP cascade
2. Submit:      29.8s (24.9%) ← HIDDEN by async thread
3. Wait D2H:   27.2s (22.7%) ← mostly cpu_postprocess (19.6s)
4. Compact:      8.9s ( 7.4%) ← compact + refill
5. Housekeeping: 0.6s ( 0.5%) ← negligible
```

---

## O13 Dual-Buffer Mechanism

### Pattern (Async Submit)
```
┌─────────────────────────────────────────────────────────┐
│ Submit Thread (background):                             │
│   WaitForMultipleObjects(go[0], go[1])                  │
│   → gpu_submit_all(H2D→Kernel→D2H) for triggered view  │
│   → SetEvent(done[vi])                                  │
│                                                         │
│ Main Thread:                                            │
│ Half-A:                                                 │
│   ensure_done(view=0) → wait_d2h(A) → merged(A)        │
│   → compact(A) → refill(A) → signal_submit(A)          │
│ Half-B:                                                 │
│   ensure_done(view=1) → wait_d2h(B) → merged(B)        │
│   → compact(B) → refill(B) → signal_submit(B)          │
│                                                         │
│ Overlap: submit(A) runs during Half-B's ~40s CPU work   │
└─────────────────────────────────────────────────────────┘
```

### View Sizes
```
Pool size:      ~1024 paths
View A size:    ~512 paths
View B size:    ~512 paths
Capacity:       view_size × 1.1 (10% headroom)
```

### Performance History
```
Pipeline Version      | Wall Time | Coverage | Key Change
─────────────────────┼──────────┼─────────┼─────────────────────
Pre-O12 (sync all)    | 142.1s    | 101.0%   | Baseline dual-buffer
O12 (stream ordering) | 137.7s    | 99.9%    | H2D→K→D2H auto-order
O13 v1 (single chan)  | 146.8s    |  94.7%   | Broken (idle gate)
O13 v2 (per-view)     | 119.5s    | 129.8%   | Async submit, -16%O16 (NT store+64B TL) | N/A       |  N/A     | DRAM IO opt, 减少merged_pass cache污染```

---

## Optimization Opportunities (Remaining)

### 1. Cascade Acceleration (78.5s target)
```
- Profile cascade_phase_time[] breakdown
- Identify hot path_phase functions
- O16 (NT Store + 64B TL) 已实施：write路径绕过cache，减少cascade L1/L2驱逐
- Consider SIMD vectorization of cascade loop (O17)
- Reduce branch mispredictions
```

### 2. CPU Postprocess Reduction (19.6s target)
```
- GPU inline filtering (L4 extension)
- Reduce per-hit host-side work
```

### 3. Compact+Refill Optimization (8.9s target)
```
- Remove unused ray bucketing (see optimization/[TODO]remove_ray_bucket)
- Simplify collect to single-pass sequential write
```
```

### 3. GPU Filtering (L4 Mode A)
```
Currently: cpu_postprocess = 21.2s
With GPU filter: ~0s (eliminate CPU retrace)
Requires: per-shape enclosure upload
```

### 4. Better Load Balancing
```
- Adaptive view size based on active count
- Earlier merge trigger (current: 12.5% threshold)
```

---

## References
- Code: `stardis-oxs3d-o16/.../sdis_solve_persistent_wavefront.c`
- Design: `guide/upper-parallelization/phase_b3_persistent_wavefront.md`
- Optimization: [OPTIMIZATION.md](OPTIMIZATION.md)
- Data Flow: [DATA_FLOW.md](DATA_FLOW.md)

---

*Updated: 2026-03-13*
