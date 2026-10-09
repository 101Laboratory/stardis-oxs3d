# Embree to cuBQL API Migration Mapping Table

**Date**: 2026-01-23  
**Project**: STARDIS-GPU Embree → cuBQL Migration  
**Purpose**: Complete mapping of all Embree API calls to cuBQL equivalents  
**Source**: Based on embree_couple.md call flow analysis + cuBQL documentation  

---

## Executive Summary

This document provides a **complete, actionable mapping** of every Embree API call used in star-3d to its cuBQL equivalent. Based on embree_couple.md analysis, **all Embree usage patterns have been identified** and mapped.

**Migration Complexity**: MEDIUM  
- ✅ **1-to-1 mappings**: Device init, BVH build, basic intersection  
- ⚠️ **Requires adaptation**: Scene flags, buffer sharing, multi-geometry handling  
- 🔴 **No direct equivalent**: Some Embree features (custom intersection functions need CUDA kernels)  

---

## Table of Contents

1. [Device Management](#1-device-management)
2. [Scene/BVH Creation](#2-scenebvh-creation)
3. [Geometry Setup](#3-geometry-setup)
4. [Buffer Management](#4-buffer-management)
5. [BVH Build Configuration](#5-bvh-build-configuration)
6. [Ray Intersection Queries](#6-ray-intersection-queries)
7. [Memory Management](#7-memory-management)
8. [Error Handling](#8-error-handling)
9. [Advanced Features](#9-advanced-features)
10. [Complete Migration Checklist](#10-complete-migration-checklist)

---

## 1. Device Management

### 1.1 Device Creation

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `RTCDevice rtcNewDevice(const char* config)` | **N/A** - Uses active CUDA context | cuBQL operates on current CUDA device; no explicit device object |
| `rtcSetDeviceErrorFunction(device, callback, userPtr)` | **Manual error checking**: `cudaGetLastError()` after operations | Use CUDA error handling instead |
| `rtcReleaseDevice(device)` | **N/A** - CUDA context management | No cleanup needed for cuBQL "device" |
| `rtcRetainDevice(device)` | **N/A** | cuBQL doesn't use reference counting for devices |

**Migration Pattern**:
```cpp
// EMBREE
RTCDevice device = rtcNewDevice(NULL);
rtcSetDeviceErrorFunction(device, errorCallback, userData);

// cuBQL
// No device creation needed - ensure CUDA context is initialized
cudaSetDevice(0); // Select GPU
// Use CUDA_CHECK macro for error handling
```

### 1.2 Device Properties

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `rtcGetDeviceProperty(device, prop)` | `cudaDeviceGetAttribute()` | Query CUDA device properties |
| Device error state | `cudaGetLastError()` | CUDA error model |

---

## 2. Scene/BVH Creation

### 2.1 Scene Object

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `RTCScene rtcNewScene(RTCDevice device)` | `cuBQL::BinaryBVH<T, D> bvh;` | BVH is the scene structure; T=float, D=3 for float3 |
| `rtcReleaseScene(scene)` | `cuBQL::free(bvh, stream)` | Explicit cleanup of BVH resources |
| `rtcRetainScene(scene)` | Manual copy/reference | No reference counting |

**Data Type Mapping**:
```cpp
// EMBREE
RTCScene scene;

// cuBQL (for float3 data)
using bvh3f = cuBQL::BinaryBVH<float, 3>;
bvh3f bvh;

// For double3 (if needed for precision)
using bvh3d = cuBQL::BinaryBVH<double, 3>;
bvh3d bvh;
```

### 2.2 Scene Flags

| Embree Flag | cuBQL Equivalent | Migration Notes |
|-------------|------------------|-----------------|
| `RTC_SCENE_FLAG_ROBUST` | **Implicit** in BVH build | cuBQL builders are numerically robust by default |
| `RTC_SCENE_FLAG_DYNAMIC` | `BuildConfig::allowUpdate = true` | Enables BVH refitting |
| `RTC_SCENE_FLAG_COMPACT` | `BuildConfig::compact = true` | Minimize memory usage |
| `rtcSetSceneFlags(scene, flags)` | Set via `BuildConfig` struct | Pass config to `gpuBuilder()` |

**Migration Pattern**:
```cpp
// EMBREE
rtcSetSceneFlags(scene, RTC_SCENE_FLAG_ROBUST | RTC_SCENE_FLAG_DYNAMIC);

// cuBQL
cuBQL::BuildConfig cfg;
cfg.allowUpdate = true; // For dynamic scenes
cfg.makeLeaves = cuBQL::SAH_BASED; // For robust builds
```

---

## 3. Geometry Setup

### 3.1 Geometry Creation

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `RTCGeometry rtcNewGeometry(device, type)` | **N/A** - Represented as bounding boxes | cuBQL works with pre-computed bounds |
| `RTC_GEOMETRY_TYPE_TRIANGLE` | `box_t<T,D>` array | Compute bounds from triangles |
| `RTC_GEOMETRY_TYPE_USER` | Custom bounds + traversal lambda | User-defined primitives |
| `rtcCommitGeometry(geom)` | **N/A** | No explicit commit; bounds are ready to use |
| `rtcAttachGeometry(scene, geom)` | Include bounds in `boxes[]` array | All geometry bounds go to `gpuBuilder()` |
| `rtcReleaseGeometry(geom)` | Free `d_boxes` array | Manual GPU memory management |

**Migration Pattern**:
```cpp
// EMBREE
RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);
// ... set buffers ...
rtcCommitGeometry(geom);
unsigned int geomID = rtcAttachGeometry(scene, geom);
rtcReleaseGeometry(geom);

// cuBQL
// 1. Compute bounding boxes (GPU kernel)
box3f *d_boxes;
cudaMalloc(&d_boxes, numTriangles * sizeof(box3f));
computeTriangleBounds<<<...>>>(d_boxes, d_vertices, d_indices, numTriangles);

// 2. Build BVH (implicitly "attaches" all geometry)
cuBQL::gpuBuilder(bvh, d_boxes, numTriangles, cfg);

// 3. Cleanup
cudaFree(d_boxes); // After BVH build
```

### 3.2 Bounding Box Computation

**Required GPU Kernel** (not provided by cuBQL):
```cuda
__global__ void computeTriangleBounds(
    cuBQL::box_t<float, 3>* boxes,
    const float3* vertices,
    const uint3* indices,
    int numTriangles)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= numTriangles) return;
    
    uint3 tri = indices[tid];
    float3 v0 = vertices[tri.x];
    float3 v1 = vertices[tri.y];
    float3 v2 = vertices[tri.z];
    
    boxes[tid].lower.x = fminf(fminf(v0.x, v1.x), v2.x);
    boxes[tid].lower.y = fminf(fminf(v0.y, v1.y), v2.y);
    boxes[tid].lower.z = fminf(fminf(v0.z, v1.z), v2.z);
    
    boxes[tid].upper.x = fmaxf(fmaxf(v0.x, v1.x), v2.x);
    boxes[tid].upper.y = fmaxf(fmaxf(v0.y, v1.y), v2.y);
    boxes[tid].upper.z = fmaxf(fmaxf(v0.z, v1.z), v2.z);
}
```

---

## 4. Buffer Management

### 4.1 Buffer Creation

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `rtcNewBuffer(device, size)` | `cudaMalloc()` | Standard CUDA allocation |
| `rtcNewSharedBuffer(device, ptr, size)` | **N/A** - cuBQL requires GPU memory | Must copy host data to device |
| `rtcReleaseBuffer(buffer)` | `cudaFree()` | Manual cleanup |

**Key Difference**: Embree's zero-copy shared buffers **not available** in cuBQL. Must explicitly upload data.

### 4.2 Vertex Buffers

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `rtcSetGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, ...)` | Store in separate GPU array | Keep `d_vertices` alive for intersection tests |
| `rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, ...)` | `cudaMemcpy(d_vertices, h_vertices, size, H2D)` | Explicit upload |
| `RTC_FORMAT_FLOAT3` | `float3*` | CUDA vector type |

**Migration Pattern**:
```cpp
// EMBREE
float* vertices = ...; // Host memory
rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0,
    RTC_FORMAT_FLOAT3, vertices, 0, sizeof(float)*3, numVerts);

// cuBQL
float3 *d_vertices;
cudaMalloc(&d_vertices, numVerts * sizeof(float3));
cudaMemcpy(d_vertices, h_vertices, numVerts * sizeof(float3), cudaMemcpyHostToDevice);
// Keep d_vertices alive - needed during traversal!
```

### 4.3 Index Buffers

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `rtcSetGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, ...)` | Store in separate GPU array | Keep `d_indices` alive |
| `RTC_FORMAT_UINT3` | `uint3*` | CUDA vector type |

---

## 5. BVH Build Configuration

### 5.1 Build Quality

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_LOW)` | `cfg.makeLeaves = NEVER` | Fast build, lower quality |
| `rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_MEDIUM)` | `cfg.makeLeaves = SPATIAL_MEDIAN` (default) | Balanced |
| `rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_HIGH)` | `cfg.makeLeaves = SAH_BASED` | Best quality, slower build |
| `rtcCommitScene(scene)` | `cuBQL::gpuBuilder(bvh, boxes, count, cfg)` | Actual BVH construction |

**BuildConfig Options**:
```cpp
struct BuildConfig {
    enum {
        NEVER,           // Build complete binary tree (fast)
        SPATIAL_MEDIAN,  // Median split (default, balanced)
        SAH_BASED        // Surface Area Heuristic (best quality)
    } makeLeaves;
    
    bool allowUpdate;  // Enable refitting for dynamic scenes
    bool compact;      // Minimize memory (slower build)
};
```

### 5.2 Scene Commit (BVH Build)

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `rtcCommitScene(scene)` | `cuBQL::gpuBuilder(bvh, boxes, count, cfg)` | **This is where 50% time is spent!** |

**Migration Pattern**:
```cpp
// EMBREE
rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_MEDIUM);
rtcCommitScene(scene); // ← Builds BVH (CPU)

// cuBQL
cuBQL::BuildConfig cfg;
cfg.makeLeaves = cuBQL::SPATIAL_MEDIAN; // Medium quality
cuBQL::gpuBuilder(bvh, d_boxes, numPrimitives, cfg); // ← Builds BVH (GPU)
cudaDeviceSynchronize(); // Wait for completion
```

---

## 6. Ray Intersection Queries

### 6.1 Single Ray Intersection (THE BOTTLENECK)

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `rtcIntersect1(scene, &rayhit, args)` | **Custom traversal kernel** using `shrinkingRadiusQuery::forEachPrim()` | **This is where Embree spends 50% time!** |
| `RTCRayHit` struct | Separate `RayDesc` + result handling | Split ray parameters and hit results |

**Migration Pattern** (THE MOST IMPORTANT PART):

```cpp
// ============================================
// EMBREE (CPU, single ray at a time)
// ============================================
RTCRayHit rayhit;
rayhit.ray.org_x = org[0];
rayhit.ray.org_y = org[1];
rayhit.ray.org_z = org[2];
rayhit.ray.dir_x = dir[0];
rayhit.ray.dir_y = dir[1];
rayhit.ray.dir_z = dir[2];
rayhit.ray.tnear = range[0];
rayhit.ray.tfar = range[1];
rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;

rtcIntersect1(scene, &rayhit, NULL); // ← BOTTLENECK

if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID) {
    hit->distance = rayhit.ray.tfar;
    hit->primID = rayhit.hit.primID;
    hit->normal[0] = rayhit.hit.Ng_x;
    hit->normal[1] = rayhit.hit.Ng_y;
    hit->normal[2] = rayhit.hit.Ng_z;
}

// ============================================
// cuBQL (GPU, batch of rays)
// ============================================
__global__ void intersectBatch(
    cuBQL::BinaryBVH<float, 3> bvh,
    const float3* vertices,
    const uint3* indices,
    const float3* rayOrigins,
    const float3* rayDirs,
    const float2* rayRanges, // (tnear, tfar)
    int numRays,
    HitResult* results)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= numRays) return;
    
    float3 org = rayOrigins[tid];
    float3 dir = rayDirs[tid];
    float tnear = rayRanges[tid].x;
    float tfar = rayRanges[tid].y;
    
    // Initialize result
    float closestDist = tfar;
    int closestPrim = -1;
    float3 closestNormal = {0, 0, 0};
    
    // Define intersection lambda
    auto intersectLambda = [&](int primID) -> float {
        uint3 tri = indices[primID];
        float3 v0 = vertices[tri.x];
        float3 v1 = vertices[tri.y];
        float3 v2 = vertices[tri.z];
        
        // Möller-Trumbore ray-triangle intersection
        float3 e1 = v1 - v0;
        float3 e2 = v2 - v0;
        float3 pvec = cross(dir, e2);
        float det = dot(e1, pvec);
        
        if (fabsf(det) < 1e-8f) return closestDist;
        
        float invDet = 1.0f / det;
        float3 tvec = org - v0;
        float u = dot(tvec, pvec) * invDet;
        if (u < 0.0f || u > 1.0f) return closestDist;
        
        float3 qvec = cross(tvec, e1);
        float v = dot(dir, qvec) * invDet;
        if (v < 0.0f || u + v > 1.0f) return closestDist;
        
        float t = dot(e2, qvec) * invDet;
        
        if (t >= tnear && t < closestDist) {
            closestDist = t;
            closestPrim = primID;
            closestNormal = normalize(cross(e1, e2));
        }
        
        return closestDist; // Update search radius
    };
    
    // Traverse BVH using cuBQL traversal template
    cuBQL::shrinkingRadiusQuery::forEachPrim(
        intersectLambda,
        bvh,
        org,
        closestDist * closestDist // cuBQL uses squared distances
    );
    
    // Write result
    results[tid].hit = (closestPrim >= 0);
    results[tid].distance = closestDist;
    results[tid].primID = closestPrim;
    results[tid].normal = closestNormal;
}

// CPU-side call
void traceRays(const bvh3f& bvh,
               const float3* d_vertices,
               const uint3* d_indices,
               const RayBatch& rays,
               HitResult* d_results)
{
    int blockSize = 256;
    int numBlocks = (rays.count + blockSize - 1) / blockSize;
    
    intersectBatch<<<numBlocks, blockSize>>>(
        bvh, d_vertices, d_indices,
        rays.origins, rays.directions, rays.ranges,
        rays.count, d_results
    );
    cudaDeviceSynchronize();
}
```

### 6.2 Ray Types

| Embree Feature | cuBQL Equivalent | Notes |
|----------------|------------------|-------|
| `rtcIntersect1` (closest hit) | `shrinkingRadiusQuery` traversal | Find closest intersection |
| `rtcOccluded1` (any hit) | Custom traversal with early exit | Stop at first hit |
| Ray masks | Manual filtering in lambda | Filter primitives in intersection test |

---

## 7. Memory Management

### 7.1 Resource Allocation

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `rtcNewBuffer(device, size)` | `cudaMalloc(&ptr, size)` | Standard CUDA allocation |
| Device memory | `cudaMalloc()` | Always GPU memory |
| Managed memory | `cudaMallocManaged()` | Optional, for CPU-GPU sharing |
| `rtcReleaseBuffer(buffer)` | `cudaFree(ptr)` | Manual cleanup |

### 7.2 BVH Memory Management

| Operation | cuBQL API | Notes |
|-----------|-----------|-------|
| Allocate BVH | Automatic in `gpuBuilder()` | `bvh.nodes` and `bvh.primIDs` allocated internally |
| Free BVH | `cuBQL::free(bvh, stream)` | Releases `nodes` and `primIDs` arrays |
| Custom allocator | Pass `GpuMemoryResource` to builder | Advanced usage |

**Migration Pattern**:
```cpp
// EMBREE
// No explicit BVH memory management - handled internally

// cuBQL
bvh3f bvh;
cuBQL::gpuBuilder(bvh, d_boxes, count, cfg);
// ... use bvh ...
cuBQL::free(bvh); // Explicit cleanup
```

---

## 8. Error Handling

### 8.1 Error Callbacks

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `rtcSetDeviceErrorFunction(device, callback, userPtr)` | Use CUDA error checking | `cudaGetLastError()`, `cudaDeviceSynchronize()` |
| `rtcGetDeviceError(device)` | `cudaGetLastError()` | Get last CUDA error |

**Migration Pattern**:
```cpp
// EMBREE
void errorCallback(void* userPtr, RTCError code, const char* str) {
    fprintf(stderr, "Embree Error %d: %s\n", code, str);
}
rtcSetDeviceErrorFunction(device, errorCallback, nullptr);

// cuBQL
#define CUDA_CHECK(call) do { \
    cudaError_t err = call; \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA Error: %s at %s:%d\n", \
                cudaGetErrorString(err), __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

CUDA_CHECK(cudaMalloc(&d_ptr, size));
cuBQL::gpuBuilder(bvh, d_boxes, count, cfg);
CUDA_CHECK(cudaDeviceSynchronize());
```

---

## 9. Advanced Features

### 9.1 Custom Intersection Functions (Embree User Geometry)

| Embree Feature | cuBQL Equivalent | Notes |
|----------------|------------------|-------|
| `rtcSetGeometryIntersectFunction(geom, func)` | Custom lambda in traversal | Implement in CUDA kernel |
| User-defined primitives | Compute custom bounds | Still use `box_t<T,D>` for BVH |

**Example: Sphere Intersection** (from embree_couple.md)

```cpp
// EMBREE
void sphereIntersect(const RTCIntersectFunctionNArguments* args) {
    // Custom C function for sphere intersection
}
rtcSetGeometryIntersectFunction(geom, sphereIntersect);

// cuBQL
__device__ bool intersectSphere(const Sphere& sphere,
                                const float3& org,
                                const float3& dir,
                                float& t)
{
    float3 oc = org - sphere.center;
    float a = dot(dir, dir);
    float b = 2.0f * dot(oc, dir);
    float c = dot(oc, oc) - sphere.radius * sphere.radius;
    float discriminant = b*b - 4*a*c;
    
    if (discriminant < 0) return false;
    
    t = (-b - sqrtf(discriminant)) / (2*a);
    return t > 0.0f;
}

// Use in traversal lambda
auto intersectLambda = [&](int primID) -> float {
    if (intersectSphere(spheres[primID], org, dir, t)) {
        // Update closest hit
    }
    return closestDist;
};
```

### 9.2 Dynamic BVH Updates

| Embree Feature | cuBQL Equivalent | Notes |
|----------------|------------------|-------|
| `rtcSetSceneFlags(scene, RTC_SCENE_FLAG_DYNAMIC)` | `cfg.allowUpdate = true` | Enable refitting |
| `rtcUpdateGeometryBuffer(geom, ...)` | Update `d_boxes` array | Recompute bounds |
| `rtcCommitScene(scene)` (incremental) | **Not supported** - rebuild BVH | cuBQL always does full rebuild |

**Note**: cuBQL does **not** support incremental BVH updates. For dynamic scenes, rebuild the entire BVH.

### 9.3 Multi-Geometry Scenes

| Embree Feature | cuBQL Equivalent | Notes |
|----------------|------------------|-------|
| Multiple `rtcAttachGeometry()` calls | **Single `boxes[]` array** for all primitives | Flatten all geometry into one bounds array |
| Geometry IDs | Track manually via `primID` | Map `primID` → geometry type |

**Migration Pattern**:
```cpp
// EMBREE
unsigned int geomID1 = rtcAttachGeometry(scene, geom1); // Triangles
unsigned int geomID2 = rtcAttachGeometry(scene, geom2); // Spheres
rtcCommitScene(scene);

// cuBQL
// 1. Compute bounds for ALL primitives
std::vector<box3f> allBoxes;
allBoxes.reserve(numTriangles + numSpheres);

// Add triangle bounds
for (int i = 0; i < numTriangles; i++)
    allBoxes.push_back(computeTriangleBounds(triangles[i]));

// Add sphere bounds
for (int i = 0; i < numSpheres; i++)
    allBoxes.push_back(computeSphereBounds(spheres[i]));

// 2. Upload to GPU
box3f *d_boxes;
cudaMalloc(&d_boxes, allBoxes.size() * sizeof(box3f));
cudaMemcpy(d_boxes, allBoxes.data(), allBoxes.size() * sizeof(box3f), H2D);

// 3. Build single BVH
cuBQL::gpuBuilder(bvh, d_boxes, allBoxes.size(), cfg);

// 4. In intersection lambda, check primitive type
auto intersectLambda = [&](int primID) -> float {
    if (primID < numTriangles) {
        // Triangle intersection
    } else {
        // Sphere intersection
        int sphereID = primID - numTriangles;
    }
    return closestDist;
};
```

---

## 10. Complete Migration Checklist

### 10.1 Mandatory Changes

- [ ] **Replace Embree includes**
  ```cpp
  // OLD
  #include <embree4/rtcore.h>
  
  // NEW
  #include <cuBQL/bvh.h>
  #include <cuBQL/builder/cuda.h>
  #include <cuBQL/traversal/shrinkingRadiusQuery.h>
  ```

- [ ] **Change data structures**
  ```cpp
  // OLD
  RTCDevice device;
  RTCScene scene;
  RTCGeometry geom;
  
  // NEW
  cuBQL::BinaryBVH<float, 3> bvh;
  box3f *d_boxes;
  ```

- [ ] **Allocate GPU memory**
  ```cpp
  cudaMalloc(&d_vertices, ...);
  cudaMalloc(&d_indices, ...);
  cudaMalloc(&d_boxes, ...);
  ```

- [ ] **Implement bounds computation kernel**
  ```cuda
  __global__ void computeTriangleBounds(...) { ... }
  ```

- [ ] **Replace rtcCommitScene with gpuBuilder**
  ```cpp
  // OLD
  rtcCommitScene(scene);
  
  // NEW
  cuBQL::gpuBuilder(bvh, d_boxes, numPrimitives, cfg);
  ```

- [ ] **Replace rtcIntersect1 with batch kernel**
  ```cuda
  __global__ void intersectBatch(...) {
      cuBQL::shrinkingRadiusQuery::forEachPrim(lambda, bvh, org, maxDist);
  }
  ```

### 10.2 Optional Optimizations

- [ ] **Use persistent GPU buffers** (avoid repeated alloc/free)
- [ ] **Batch ray uploads** (amortize transfer overhead)
- [ ] **Async streams** (overlap compute and transfer)
- [ ] **Custom memory allocators** (`GpuMemoryResource`)
- [ ] **Profiling** (nvprof/Nsight to identify bottlenecks)

### 10.3 Testing

- [ ] **Functional tests**: Same results as Embree (< 1e-5 error)
- [ ] **Performance tests**: Measure actual speedup
- [ ] **Memory tests**: Check for leaks, validate cleanup
- [ ] **Edge cases**: Empty scenes, single triangles, degenerate geometry

---

## 11. Key Differences Summary

| Aspect | Embree | cuBQL | Impact |
|--------|--------|-------|--------|
| **Memory** | CPU (zero-copy shared buffers) | GPU (explicit uploads) | Must copy data |
| **Execution** | CPU multi-threaded | GPU massively parallel | **Huge speedup potential** |
| **API style** | Object-oriented (RTCDevice, RTCScene) | Data-oriented (BVH struct) | Simpler, more explicit |
| **Batching** | Single ray API | **Requires batching** | Must rewrite intersection loop |
| **BVH update** | Incremental refit | Full rebuild only | Slower for dynamic scenes |
| **Custom geometry** | C callback functions | CUDA device lambdas | More flexible |
| **Error handling** | Callback-based | CUDA error codes | Standard CUDA patterns |

---

## 12. Migration Effort Estimate

| Component | Files Affected | Estimated Effort | Risk |
|-----------|----------------|------------------|------|
| **Device management** | `s3d_device.c/h` | 2-4 hours | LOW |
| **Scene/BVH creation** | `s3d_scene_view.c` | 8-16 hours | MEDIUM |
| **Geometry setup** | `s3d_geometry.c` | 4-8 hours | LOW |
| **Intersection query** | `s3d_scene_view_trace_ray.c` | **16-24 hours** | **HIGH** |
| **Custom geometry (spheres)** | `s3d_sphere.c` | 4-8 hours | MEDIUM |
| **Testing & validation** | New test files | 16-32 hours | HIGH |
| **Performance tuning** | All files | 8-16 hours | MEDIUM |

**Total Estimated Time**: 58-108 hours (1.5 to 2.5 weeks)

---

## 13. Critical Migration Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| **Precision loss** (float32 vs double) | HIGH | Use `BinaryBVH<double, 3>` if needed |
| **CPU-GPU transfer overhead** | MEDIUM | Batch operations, use persistent buffers |
| **API incompatibility** | LOW | Well-documented mapping |
| **Performance regression** | MEDIUM | Thorough benchmarking before committing |
| **Memory leaks** | MEDIUM | Careful resource management, use CUDA-MEMCHECK |

---

## Conclusion

This mapping provides a **complete, actionable guide** for migrating all Embree calls to cuBQL. The most critical change is **replacing single-ray CPU intersection with batched GPU kernels**, which is where the 50% performance bottleneck will be eliminated.

**Key Takeaways**:
1. ✅ **1-to-1 mappings exist** for most operations
2. ⚠️ **Batch processing required** for ray queries (design change)
3. ⚠️ **Explicit memory management** needed (no shared buffers)
4. ✅ **Expected performance gain**: 10-100x (depending on batch size)

**Next Steps**:
1. Implement standalone comparison (see `embree_cubql_performance_comparison_plan.md`)
2. Validate performance gains
3. If successful, proceed with full migration using this mapping

---

**Document Version**: 1.0  
**Author**: Sisyphus (OhMyOpenCode)  
**Last Updated**: 2026-01-23  
**Related Documents**:
- `embree_couple.md` (Embree usage analysis)
- `embree_cubql_performance_comparison_plan.md` (Validation test plan)
- `embree_dependency_scope_analysis_CRITICAL_UPDATE.md` (Migration scope)
