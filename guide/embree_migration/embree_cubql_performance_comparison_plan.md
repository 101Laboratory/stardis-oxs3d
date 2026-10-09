# Embree to cuBQL Performance Comparison Test Plan

**Date**: 2026-01-23  
**Project**: STARDIS-GPU Embree Migration Feasibility Study  
**Purpose**: Validate 50% performance bottleneck migration from Embree (CPU) to cuBQL (GPU)  
**Status**: 📋 PLAN - Not Yet Implemented  

---

## Executive Summary

Based on profiling results showing **50% CPU time consumed in embree module** (almost entirely from a single intersection query entry point), this test plan validates whether migrating to cuBQL GPU acceleration provides significant performance improvements.

**Test Approach**: Standalone comparison program using **identical call patterns** to @embree_couple.md, but with simplified geometry (no stardis dependencies required).

**Expected Outcome**: Quantify actual GPU acceleration gains before committing to full migration.

---

## 1. Test Objectives

### Primary Objectives
| Objective | Success Criteria | Priority |
|-----------|------------------|----------|
| **Measure BVH build time** | Compare Embree vs cuBQL build latency | P0 |
| **Measure intersection throughput** | Rays/sec for identical geometry | P0 |
| **Validate precision** | Results match within 1e-5 tolerance | P0 |
| **Profile memory usage** | GPU vs CPU memory consumption | P1 |
| **Identify bottlenecks** | CPU-GPU transfer overhead quantification | P1 |

### Secondary Objectives
- Validate batch size impact on performance
- Test with varying scene complexity (100, 1K, 10K, 100K triangles)
- Measure warmup vs sustained performance

---

## 2. Test Architecture

### 2.1 Standalone Program Structure

```
embree_cubql_comparison/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp                    # Driver program
│   ├── embree_wrapper.cpp          # Embree BVH + intersection
│   ├── cubql_wrapper.cu            # cuBQL BVH + intersection
│   ├── geometry_generator.cpp      # Test geometry creation
│   ├── ray_generator.cpp           # Random ray generation
│   └── validator.cpp               # Precision comparison
├── include/
│   ├── common_types.h              # Shared data structures
│   └── timer.h                     # High-resolution timing
└── results/
    └── comparison_report.csv       # Benchmark results
```

### 2.2 Flow Mirroring embree_couple.md

**Based on embree_couple.md analysis**, the call flow is:

```
1. Device/Context Creation
   Embree: RTCDevice device = rtcNewDevice(NULL);
   cuBQL:  (No device object - uses CUDA context)

2. Scene/BVH Creation
   Embree: RTCScene scene = rtcNewScene(device);
           rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_MEDIUM);
   cuBQL:  BinaryBVH<float,3> bvh;
           BuildConfig cfg; cfg.makeLeaves = SAH_BASED;

3. Geometry Registration
   Embree: RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);
           rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, ...);
           rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, ...);
           rtcCommitGeometry(geom);
           rtcAttachGeometry(scene, geom);
   cuBQL:  box3f *d_boxes = allocDeviceBoxes(numTriangles);
           computeBoundingBoxes<<<...>>>(d_boxes, vertices, indices, numTriangles);

4. Scene Commit (BVH Build)
   Embree: rtcCommitScene(scene);  // ← 50% time in profiling likely here
   cuBQL:  gpuBuilder(bvh, d_boxes, numTriangles, cfg);

5. Ray Intersection Query (Hot Path)
   Embree: rtcIntersect1(scene, &rayhit, NULL);  // ← This is the bottleneck!
   cuBQL:  shrinkingRadiusQuery::forEachPrim(lambda, bvh, queryPoint, maxRadius);
           // OR use batch intersection kernel
```

### 2.3 Simplified Test Geometry

**NO stardis dependencies** - use procedural generation:

```cpp
// Cornell Box variant (30 triangles)
struct SimplifiedScene {
    std::vector<float3> vertices;
    std::vector<uint3>  indices;
    
    static SimplifiedScene generateCornellBox();
    static SimplifiedScene generateSphere(int subdivisions);
    static SimplifiedScene generateRandomTriangles(int count);
};
```

---

## 3. Detailed Test Implementation

### 3.1 Embree Wrapper

```cpp
// embree_wrapper.cpp
#include <embree4/rtcore.h>

class EmbreeScene {
public:
    RTCDevice device;
    RTCScene  scene;
    
    void build(const float* vertices, int numVerts,
               const uint32_t* indices, int numTris) {
        // 1. Create device
        device = rtcNewDevice(NULL);
        
        // 2. Create scene
        scene = rtcNewScene(device);
        rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_MEDIUM);
        
        // 3. Create geometry
        RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);
        
        // 4. Set buffers (SHARED - zero copy like star-3d)
        RTCBuffer vertBuf = rtcNewSharedBuffer(device, (void*)vertices,
                                               sizeof(float) * 3 * numVerts);
        rtcSetGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0,
                            RTC_FORMAT_FLOAT3, vertBuf, 0, sizeof(float) * 3, numVerts);
        
        RTCBuffer idxBuf = rtcNewSharedBuffer(device, (void*)indices,
                                             sizeof(uint32_t) * 3 * numTris);
        rtcSetGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, 0,
                            RTC_FORMAT_UINT3, idxBuf, 0, sizeof(uint32_t) * 3, numTris);
        
        // 5. Commit geometry and scene
        rtcCommitGeometry(geom);
        rtcAttachGeometry(scene, geom);
        rtcReleaseGeometry(geom);
        
        rtcCommitScene(scene);  // ← Timed separately
    }
    
    void intersect(const float3* rayOrigins, const float3* rayDirs,
                   int numRays, HitResult* results) {
        for (int i = 0; i < numRays; i++) {
            RTCRayHit rayhit;
            rayhit.ray.org_x = rayOrigins[i].x;
            rayhit.ray.org_y = rayOrigins[i].y;
            rayhit.ray.org_z = rayOrigins[i].z;
            rayhit.ray.dir_x = rayDirs[i].x;
            rayhit.ray.dir_y = rayDirs[i].y;
            rayhit.ray.dir_z = rayDirs[i].z;
            rayhit.ray.tnear = 0.0f;
            rayhit.ray.tfar  = 1e20f;
            rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
            
            rtcIntersect1(scene, &rayhit, NULL);  // ← THE BOTTLENECK
            
            results[i].hit = (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID);
            if (results[i].hit) {
                results[i].distance = rayhit.ray.tfar;
                results[i].primID = rayhit.hit.primID;
                results[i].normal = {rayhit.hit.Ng_x, rayhit.hit.Ng_y, rayhit.hit.Ng_z};
            }
        }
    }
};
```

### 3.2 cuBQL Wrapper

```cuda
// cubql_wrapper.cu
#include <cuBQL/bvh.h>
#include <cuBQL/queries/triangleData/closestPointOnAnyTriangle.h>

using bvh3f = cuBQL::BinaryBVH<float, 3>;
using box3f = cuBQL::box_t<float, 3>;

class CuBQLScene {
public:
    bvh3f bvh;
    float3 *d_vertices;
    uint3  *d_indices;
    box3f  *d_boxes;
    int numTriangles;
    
    void build(const float* h_vertices, int numVerts,
               const uint32_t* h_indices, int numTris) {
        numTriangles = numTris;
        
        // 1. Allocate GPU memory and upload
        cudaMalloc(&d_vertices, numVerts * sizeof(float3));
        cudaMalloc(&d_indices, numTris * sizeof(uint3));
        cudaMalloc(&d_boxes, numTris * sizeof(box3f));
        
        cudaMemcpy(d_vertices, h_vertices, numVerts * sizeof(float3), cudaMemcpyHostToDevice);
        cudaMemcpy(d_indices, h_indices, numTris * sizeof(uint3), cudaMemcpyHostToDevice);
        
        // 2. Compute bounding boxes (GPU kernel)
        computeTriangleBounds<<<divRoundUp(numTris, 256), 256>>>(
            d_boxes, d_vertices, d_indices, numTris);
        cudaDeviceSynchronize();
        
        // 3. Build BVH
        cuBQL::BuildConfig cfg;
        cfg.makeLeaves = cuBQL::SAH_BASED;
        cuBQL::gpuBuilder(bvh, d_boxes, numTris, cfg);  // ← Timed separately
        cudaDeviceSynchronize();
    }
    
    void intersect(const float3* h_rayOrigins, const float3* h_rayDirs,
                   int numRays, HitResult* h_results) {
        // Upload rays to GPU
        float3 *d_rayOrigins, *d_rayDirs;
        HitResult *d_results;
        cudaMalloc(&d_rayOrigins, numRays * sizeof(float3));
        cudaMalloc(&d_rayDirs, numRays * sizeof(float3));
        cudaMalloc(&d_results, numRays * sizeof(HitResult));
        
        cudaMemcpy(d_rayOrigins, h_rayOrigins, numRays * sizeof(float3), cudaMemcpyHostToDevice);
        cudaMemcpy(d_rayDirs, h_rayDirs, numRays * sizeof(float3), cudaMemcpyHostToDevice);
        
        // Execute intersection kernel
        intersectKernel<<<divRoundUp(numRays, 256), 256>>>(
            bvh, d_vertices, d_indices, d_rayOrigins, d_rayDirs, numRays, d_results);
        cudaDeviceSynchronize();
        
        // Download results
        cudaMemcpy(h_results, d_results, numRays * sizeof(HitResult), cudaMemcpyDeviceToHost);
        
        cudaFree(d_rayOrigins);
        cudaFree(d_rayDirs);
        cudaFree(d_results);
    }
};

__global__ void computeTriangleBounds(box3f* boxes,
                                      const float3* vertices,
                                      const uint3* indices,
                                      int numTriangles) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= numTriangles) return;
    
    uint3 tri = indices[tid];
    float3 v0 = vertices[tri.x];
    float3 v1 = vertices[tri.y];
    float3 v2 = vertices[tri.z];
    
    boxes[tid].lower = fminf(fminf(v0, v1), v2);
    boxes[tid].upper = fmaxf(fmaxf(v0, v1), v2);
}

__global__ void intersectKernel(bvh3f bvh,
                                const float3* vertices,
                                const uint3* indices,
                                const float3* rayOrigins,
                                const float3* rayDirs,
                                int numRays,
                                HitResult* results) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= numRays) return;
    
    float3 org = rayOrigins[tid];
    float3 dir = rayDirs[tid];
    
    // Use cuBQL traversal template
    float closestDist = 1e20f;
    int closestPrim = -1;
    
    auto intersectLambda = [&](int primID) -> float {
        uint3 tri = indices[primID];
        float3 v0 = vertices[tri.x];
        float3 v1 = vertices[tri.y];
        float3 v2 = vertices[tri.z];
        
        // Möller-Trumbore intersection
        float3 e1 = v1 - v0;
        float3 e2 = v2 - v0;
        float3 pvec = cross(dir, e2);
        float det = dot(e1, pvec);
        
        if (fabs(det) < 1e-8f) return closestDist;
        
        float invDet = 1.0f / det;
        float3 tvec = org - v0;
        float u = dot(tvec, pvec) * invDet;
        
        if (u < 0.0f || u > 1.0f) return closestDist;
        
        float3 qvec = cross(tvec, e1);
        float v = dot(dir, qvec) * invDet;
        
        if (v < 0.0f || u + v > 1.0f) return closestDist;
        
        float t = dot(e2, qvec) * invDet;
        
        if (t > 0.0f && t < closestDist) {
            closestDist = t;
            closestPrim = primID;
        }
        
        return closestDist;
    };
    
    cuBQL::shrinkingRadiusQuery::forEachPrim(
        intersectLambda, bvh, org, closestDist * closestDist);
    
    results[tid].hit = (closestPrim >= 0);
    results[tid].distance = closestDist;
    results[tid].primID = closestPrim;
}
```

### 3.3 Benchmark Driver

```cpp
// main.cpp
#include "embree_wrapper.h"
#include "cubql_wrapper.h"
#include "timer.h"

struct BenchmarkResult {
    double buildTime_ms;
    double intersectTime_ms;
    double throughput_MRaysPerSec;
    size_t memoryUsage_MB;
    double averageError;
    double maxError;
};

BenchmarkResult runEmbreeTest(const SimplifiedScene& scene, int numRays) {
    Timer timer;
    BenchmarkResult result;
    
    // Generate rays
    auto rays = generateRandomRays(scene.getBounds(), numRays);
    std::vector<HitResult> results(numRays);
    
    // Build BVH
    EmbreeScene embreeScene;
    timer.start();
    embreeScene.build(scene.vertices.data(), scene.vertices.size(),
                     scene.indices.data(), scene.indices.size());
    result.buildTime_ms = timer.elapsed_ms();
    
    // Intersect rays (WARMUP)
    embreeScene.intersect(rays.origins.data(), rays.directions.data(),
                         std::min(1000, numRays), results.data());
    
    // Intersect rays (MEASURED)
    timer.start();
    embreeScene.intersect(rays.origins.data(), rays.directions.data(),
                         numRays, results.data());
    result.intersectTime_ms = timer.elapsed_ms();
    result.throughput_MRaysPerSec = numRays / (result.intersectTime_ms * 1000.0);
    
    return result;
}

BenchmarkResult runCuBQLTest(const SimplifiedScene& scene, int numRays) {
    // Similar structure but with cuBQL
    // ... (implementation)
}

int main() {
    std::vector<SimplifiedScene> testScenes = {
        SimplifiedScene::generateCornellBox(),
        SimplifiedScene::generateSphere(1024),
        SimplifiedScene::generateRandomTriangles(10000),
        SimplifiedScene::generateRandomTriangles(100000)
    };
    
    std::vector<int> rayCounts = {1000, 10000, 100000, 1000000};
    
    std::ofstream csv("results/comparison_report.csv");
    csv << "Scene,NumTriangles,NumRays,Backend,BuildTime_ms,IntersectTime_ms,Throughput_MRays/s,Memory_MB\n";
    
    for (const auto& scene : testScenes) {
        for (int numRays : rayCounts) {
            auto embreeResult = runEmbreeTest(scene, numRays);
            auto cubqlResult = runCuBQLTest(scene, numRays);
            
            // Validate precision
            double avgError = compareResults(embreeResult.hits, cubqlResult.hits);
            
            // Write results
            csv << scene.name << "," << scene.numTriangles << "," << numRays
                << ",Embree," << embreeResult.buildTime_ms << ","
                << embreeResult.intersectTime_ms << ","
                << embreeResult.throughput_MRaysPerSec << ","
                << embreeResult.memoryUsage_MB << "\n";
            
            csv << scene.name << "," << scene.numTriangles << "," << numRays
                << ",cuBQL," << cubqlResult.buildTime_ms << ","
                << cubqlResult.intersectTime_ms << ","
                << cubqlResult.throughput_MRaysPerSec << ","
                << cubqlResult.memoryUsage_MB << "\n";
            
            // Print speedup
            double speedup = embreeResult.intersectTime_ms / cubqlResult.intersectTime_ms;
            std::cout << scene.name << " (" << numRays << " rays): "
                      << speedup << "x speedup\n";
        }
    }
    
    return 0;
}
```

---

## 4. Test Scenarios

### Scenario Matrix

| Scenario | Triangles | Rays | Purpose |
|----------|-----------|------|---------|
| **S1: Cornell Box** | 30 | 1K | Minimal baseline |
| **S2: Simple Sphere** | 1K | 10K | Small scene |
| **S3: Complex Mesh** | 10K | 100K | Medium complexity |
| **S4: Large Mesh** | 100K | 1M | Stress test |
| **S5: Batch Variation** | 10K | 100, 1K, 10K, 100K, 1M | Batch size impact |

### Profiling Focus

For **Scenario S3** (matches typical stardis workload):
- Profile with `nvprof` / NVIDIA Nsight
- Identify breakdown:
  ```
  cuBQL Total Time:
    - CPU-GPU upload: X%
    - BVH build: Y%
    - Intersection: Z%
    - GPU-CPU download: W%
  ```

---

## 5. Validation

### 5.1 Precision Validation

```cpp
struct PrecisionValidator {
    static bool validate(const HitResult* embreeResults,
                        const HitResult* cubqlResults,
                        int numRays,
                        double tolerance = 1e-5) {
        int mismatches = 0;
        double maxError = 0.0;
        double avgError = 0.0;
        
        for (int i = 0; i < numRays; i++) {
            // Compare hit/miss
            if (embreeResults[i].hit != cubqlResults[i].hit) {
                mismatches++;
                continue;
            }
            
            if (embreeResults[i].hit) {
                // Compare distance
                double error = fabs(embreeResults[i].distance - cubqlResults[i].distance);
                avgError += error;
                maxError = std::max(maxError, error);
                
                if (error > tolerance) {
                    mismatches++;
                }
            }
        }
        
        avgError /= numRays;
        
        printf("Validation: %d/%d rays matched (%.2f%%)\n",
               numRays - mismatches, numRays,
               100.0 * (numRays - mismatches) / numRays);
        printf("  Avg Error: %.2e, Max Error: %.2e\n", avgError, maxError);
        
        return mismatches == 0;
    }
};
```

### 5.2 Expected Precision

- **Hit/Miss Agreement**: 100% (binary, must match exactly)
- **Distance Error**: < 1e-5 for 99.9% of rays
- **Normal Vectors**: < 1e-4 angular difference

---

## 6. Environment Requirements

### 6.1 Hardware
- **GPU**: NVIDIA RTX 3060 or higher (Ampere+)
- **CPU**: Any modern x64 CPU (for Embree baseline)
- **RAM**: 16 GB minimum
- **VRAM**: 6 GB minimum

### 6.2 Software

```bash
# CUDA Toolkit
CUDA >= 12.0
# Embree
embree4 >= 4.0
# Compiler
gcc >= 11 or MSVC 2022
# CMake
cmake >= 3.18
```

### 6.3 NOT Required

❌ **NO stardis-cpu dependencies**  
❌ **NO 28 internal libraries** (rsys, star-*, etc.)  
❌ **NO existing STARDIS build system**  
❌ **NO MPI**  

✅ **ONLY**: Embree, cuBQL (header-only), CUDA, standard C++17

---

## 7. Expected Results

### 7.1 Performance Hypothesis

Based on embree_couple.md showing **50% time in embree**, and typical GPU acceleration:

| Metric | Embree (CPU) | cuBQL (GPU) | Expected Speedup |
|--------|-------------|-------------|------------------|
| **BVH Build** | 100 ms | 20 ms | **5x** |
| **1M Ray Intersect** | 500 ms | 10 ms | **50x** |
| **Overall (with transfer)** | 600 ms | 50 ms | **12x** |

**Key Insight**: If intersection is truly 50% of runtime, and GPU provides 50x speedup on that part, **overall application speedup ≈ 1 / (0.5 + 0.5/50) ≈ 1.96x** (Amdahl's Law).

### 7.2 Decision Criteria

| Outcome | Decision |
|---------|----------|
| **Speedup > 5x** | ✅ Proceed with full migration |
| **Speedup 2-5x** | ⚠️ Consider hybrid CPU/GPU |
| **Speedup < 2x** | ❌ Migration not justified |

---

## 8. Implementation Timeline

### Phase 1: Setup (1-2 days)
- [ ] Create standalone CMake project
- [ ] Integrate Embree and cuBQL
- [ ] Implement geometry generators

### Phase 2: Wrappers (2-3 days)
- [ ] Implement Embree wrapper
- [ ] Implement cuBQL wrapper
- [ ] Validate API parity

### Phase 3: Benchmarking (1-2 days)
- [ ] Run all test scenarios
- [ ] Collect profiling data
- [ ] Generate comparison report

### Phase 4: Analysis (1 day)
- [ ] Analyze results
- [ ] Write recommendation
- [ ] Present to stakeholders

**Total Estimated Time**: 5-8 days

---

## 9. Deliverables

1. **Comparison Report** (`results/comparison_report.csv`)
   - Raw benchmark data
   - Speedup calculations
   - Memory usage analysis

2. **Profiling Report** (`results/profile_analysis.md`)
   - Breakdown of GPU time
   - Bottleneck identification
   - Transfer overhead quantification

3. **Recommendation Document** (`results/migration_decision.md`)
   - GO/NO-GO decision
   - Risk assessment
   - Implementation roadmap (if GO)

4. **Source Code** (entire `embree_cubql_comparison/` directory)
   - Fully reproducible test harness
   - Reusable for future benchmarking

---

## 10. Risks and Mitigations

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| CPU-GPU transfer overhead dominates | HIGH | MEDIUM | Batch optimization, async transfers |
| cuBQL API incompatibility | HIGH | LOW | Early API mapping validation |
| Precision mismatch | MEDIUM | LOW | Relaxed tolerance, double precision fallback |
| Environment setup issues | MEDIUM | MEDIUM | Docker containerization |

---

## 11. Success Metrics

✅ **Program compiles and runs** without stardis dependencies  
✅ **All scenarios complete** in < 5 minutes total  
✅ **Precision validation passes** (99%+ agreement)  
✅ **Clear performance data** for informed decision  
✅ **Reproducible results** across multiple runs  

---

## Conclusion

This standalone test plan provides a **low-risk, high-confidence** validation of the Embree → cuBQL migration hypothesis. By isolating the ray tracing bottleneck and comparing identical operations, we obtain **actionable performance data** before committing engineering resources to full integration.

**Next Steps**:
1. Review and approve this plan
2. Implement test harness
3. Execute benchmarks
4. Analyze results and decide on migration

---

**Document Version**: 1.0  
**Author**: Sisyphus (OhMyOpenCode)  
**Last Updated**: 2026-01-23  
**Related Documents**:
- embree_couple.md (call flow reference)
- embree_dependency_scope_analysis_CRITICAL_UPDATE.md (migration scope)
- embree_exposed_type_access.md (API isolation confirmation)
