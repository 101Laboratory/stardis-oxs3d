# cuBQL Migration Guide: Closest Point Queries
## From Embree to GPU-Accelerated cuBQL

**Date:** 2026-02-02  
**Project:** Stardis-GPU  
**Target:** Migrate closest point query implementations from CPU-based Embree to GPU-accelerated cuBQL  
**Status:** Draft

---

## 1. Overview

This guide provides a comprehensive migration path for closest point queries in the Stardis project. The existing implementation uses Intel Embree's `rtcPointQuery()` API with callback-based geometry processing. The target implementation uses NVIDIA's cuBQL library with lambda-based shrinking radius queries for GPU acceleration.

**Key Objectives:**
- Maintain external API compatibility (no changes to public function signatures)
- Replace all Embree types and functions with cuBQL equivalents
- Preserve geometry processing logic (triangles, spheres, line segments)
- Support instancing and transform hierarchies
- Implement efficient GPU acceleration with progressive radius pruning

---

## 2. Implementation Principles

### 2.1 Embree Pattern (Current)
```c
// Callback-based approach
void closest_point_callback(const RTCPointQueryFunctionArguments* args) {
    struct point_query_context* ctx = (struct point_query_context*)args->userPtr;
    // Process primitive, update closest hit
    args->query->radius = new_radius; // Shrink radius for pruning
}

// Public API
void s3d_scene_view_closest_point(s3d_scene_view* view, s3d_point_query* query) {
    RTCPointQuery embree_query = { ... };
    RTCPointQueryContext embree_context = { ... };
    struct point_query_context ctx = { ... };
    rtcPointQuery(view->embree_scene, &embree_query, &embree_context, 
                  closest_point_callback, &ctx);
}
```

**Characteristics:**
- Single callback function per query type
- `userPtr` passes arbitrary context data
- Radius updated in-place for progressive pruning
- Geometry processing inside callback with primitive ID lookup

### 2.2 cuBQL Pattern (Target)
```cpp
// Lambda-based approach
shrinkingRadiusQuery::forEachPrim(bvh, query_center, initial_radius_squared,
    [&](uint32_t prim_id, float& current_radius_squared) -> bool {
        // Process primitive, update closest hit
        current_radius_squared = new_radius_squared; // Shrink radius
        return true; // Continue traversal
    });
```

**Characteristics:**
- Lambda captures context by reference
- Radius passed by reference for in-place updates
- Returns `bool` for early termination control
- Direct primitive ID access without additional lookup
- Template-based for dimension and type flexibility

### 2.3 Key Migration Concepts

1. **Radius Management**: Both use progressive shrinking for optimal pruning
2. **State Passing**: Embree uses `userPtr`, cuBQL uses lambda capture
3. **Geometry Processing**: Math helpers remain unchanged, coordinate transforms may need adaptation
4. **Instancing**: Both support transform hierarchies with stack management
5. **Early Termination**: Both allow stopping when radius reaches zero

---

## 3. Standard Process Flow

### 3.1 Embree Flow
```
1. Initialize RTCPointQuery with center and radius
2. Initialize RTCPointQueryContext for traversal state
3. Create point_query_context with geometry data, output hit
4. Call rtcPointQuery() with callback
5. Callback invoked per potential primitive:
   a. Look up geometry data using geometry ID
   b. Transform query to local space if instanced
   c. Compute closest point using primitive ID
   d. Update hit if closer than current best
   e. Shrink query radius to distance to new hit
6. Return closest hit in output structure
```

### 3.2 cuBQL Flow
```
1. Build BinaryBVH<T,D> from scene geometry (pre-process)
2. Initialize query center and squared radius
3. Capture context (geometry data, output hit, transforms) in lambda
4. Call shrinkingRadiusQuery::forEachPrim() with lambda
5. Lambda invoked per primitive during BVH traversal:
   a. Access geometry data directly via prim_id (no geometry ID lookup)
   b. Apply instance transforms if in instanced BVH
   c. Compute closest point using existing math helpers
   d. Update hit if closer, shrink current_radius_squared
   e. Return true to continue, false to terminate early
6. Output hit contains closest point information
```

### 3.3 Dimensionality Handling

- **3D Queries**: Use `BinaryBVH<3, double>` for double precision 3D
- **2D Queries**: Use `BinaryBVH<2, double>` for double precision 2D
- **Mixed Precision**: Consider `float` for GPU performance if accuracy permits

---

## 4. API Mapping Table

| Embree API | cuBQL Equivalent | Notes |
|------------|------------------|-------|
| `RTCPointQuery` | `center` + `radius_squared` | Store as separate variables |
| `RTCPointQueryContext` | Traversal state in lambda | Implicit in cuBQL traversal |
| `RTCPointQueryFunctionArguments` | Lambda parameters `(prim_id, radius_squared)` | |
| `args->geometryUserID` | Geometry ID in separate mapping | May need geometry ID array |
| `args->primID` | `prim_id` parameter | Direct primitive index |
| `args->query->radius` | `current_radius_squared` reference | Update in-place |
| `rtcPointQuery()` | `shrinkingRadiusQuery::forEachPrim()` | Main query function |
| `rtcInitPointQueryContext()` | Not needed | cuBQL manages traversal |
| Instance transform stack | Nested BVH or transform application | Different instancing model |

---

## 5. Code Examples

### 5.1 3D Mesh Closest Point (Triangles)

**Embree Original:**
```c
static void closest_point(const RTCPointQueryFunctionArguments* args) {
    struct point_query_context* ctx = (struct point_query_context*)args->userPtr;
    const uint32_t geom_id = args->geometryUserID;
    const uint32_t prim_id = args->primID;
    
    // Get mesh data
    const s3d_mesh* mesh = ctx->meshes[geom_id];
    const double* vertices = mesh->vertices;
    const uint32_t* triangles = mesh->triangles;
    
    // Compute closest point on triangle
    double closest[3];
    double distance_squared = closest_point_triangle(
        &vertices[triangles[prim_id * 3] * 3],
        &vertices[triangles[prim_id * 3 + 1] * 3],
        &vertices[triangles[prim_id * 3 + 2] * 3],
        args->query->x, args->query->y, args->query->z,
        closest);
    
    // Update hit if closer
    if (distance_squared < ctx->hit->distance_squared) {
        ctx->hit->distance_squared = distance_squared;
        ctx->hit->point[0] = closest[0];
        ctx->hit->point[1] = closest[1];
        ctx->hit->point[2] = closest[2];
        ctx->hit->geom_id = geom_id;
        ctx->hit->prim_id = prim_id;
        
        // Shrink radius for pruning
        args->query->radius = sqrt(distance_squared);
    }
}
```

**cuBQL Migration:**
```cpp
void closest_point_mesh_cubql(const BinaryBVH<3, double>& bvh,
                              const double query_center[3],
                              double initial_radius,
                              const std::vector<s3d_mesh*>& meshes,
                              s3d_hit* hit) {
    
    double radius_squared = initial_radius * initial_radius;
    hit->distance_squared = radius_squared;
    
    shrinkingRadiusQuery::forEachPrim(bvh, query_center, radius_squared,
        [&](uint32_t prim_id, double& current_radius_squared) -> bool {
            // Map prim_id to mesh and triangle (requires custom mapping)
            uint32_t mesh_idx, tri_idx;
            map_primitive_to_mesh(prim_id, mesh_idx, tri_idx);
            
            const s3d_mesh* mesh = meshes[mesh_idx];
            const double* vertices = mesh->vertices;
            const uint32_t* triangles = mesh->triangles;
            
            // Compute closest point
            double closest[3];
            const double* v0 = &vertices[triangles[tri_idx * 3] * 3];
            const double* v1 = &vertices[triangles[tri_idx * 3 + 1] * 3];
            const double* v2 = &vertices[triangles[tri_idx * 3 + 2] * 3];
            
            double distance_squared = closest_point_triangle(
                v0, v1, v2,
                query_center[0], query_center[1], query_center[2],
                closest);
            
            // Update hit if closer
            if (distance_squared < hit->distance_squared) {
                hit->distance_squared = distance_squared;
                hit->point[0] = closest[0];
                hit->point[1] = closest[1];
                hit->point[2] = closest[2];
                hit->geom_id = mesh_idx;
                hit->prim_id = tri_idx;
                
                // Shrink radius for pruning
                current_radius_squared = distance_squared;
            }
            
            return current_radius_squared > 0.0; // Continue if radius > 0
        });
}
```

### 5.2 Sphere Geometry

**Embree Original:**
```c
static void closest_point_sphere(const RTCPointQueryFunctionArguments* args) {
    struct point_query_context* ctx = (struct point_query_context*)args->userPtr;
    const s3d_sphere* sphere = &ctx->spheres[args->geometryUserID];
    
    // Transform query to sphere local space
    double local_query[3];
    transform_point(local_query, args->query->x, args->query->y, args->query->z,
                    sphere->inv_transform);
    
    // Compute closest point on sphere
    double closest[3];
    double distance_squared = closest_point_sphere_geometry(
        sphere->center, sphere->radius,
        local_query[0], local_query[1], local_query[2],
        closest);
    
    // Transform closest point back to world space
    transform_point_back(closest, closest, sphere->transform);
    
    // Update hit (similar to mesh example)
    // ...
}
```

**cuBQL Migration:**
```cpp
// Spheres can be represented as primitives with custom intersection logic
// or as separate BVH containing sphere primitives
void closest_point_spheres_cubql(const BinaryBVH<3, double>& sphere_bvh,
                                 const double query_center[3],
                                 double initial_radius,
                                 const std::vector<s3d_sphere>& spheres,
                                 s3d_hit* hit) {
    
    double radius_squared = initial_radius * initial_radius;
    hit->distance_squared = radius_squared;
    
    shrinkingRadiusQuery::forEachPrim(sphere_bvh, query_center, radius_squared,
        [&](uint32_t prim_id, double& current_radius_squared) -> bool {
            const s3d_sphere& sphere = spheres[prim_id];
            
            // Transform query to sphere local space
            double local_query[3];
            transform_point(local_query, query_center[0], query_center[1], query_center[2],
                            sphere.inv_transform);
            
            // Compute closest point
            double closest_local[3];
            double distance_squared = closest_point_sphere_geometry(
                sphere.center, sphere.radius,
                local_query[0], local_query[1], local_query[2],
                closest_local);
            
            // Transform back to world space
            double closest[3];
            transform_point_back(closest, closest_local, sphere.transform);
            
            // Update hit if closer
            if (distance_squared < hit->distance_squared) {
                hit->distance_squared = distance_squared;
                hit->point[0] = closest[0];
                hit->point[1] = closest[1];
                hit->point[2] = closest[2];
                hit->geom_id = SPHERE_GEOM_TYPE;
                hit->prim_id = prim_id;
                
                current_radius_squared = distance_squared;
            }
            
            return current_radius_squared > 0.0;
        });
}
```

### 5.3 2D Line Segments

**Embree Original:**
```c
static void closest_point_segments(const RTCPointQueryFunctionArguments* args) {
    struct point_query_context* ctx = (struct point_query_context*)args->userPtr;
    const s2d_line_segments* segments = &ctx->segments[args->geometryUserID];
    const uint32_t seg_id = args->primID;
    
    const double* p0 = &segments->points[seg_id * 4]; // x1, y1, x2, y2
    const double* p1 = &segments->points[seg_id * 4 + 2];
    
    double closest[2];
    double distance_squared = closest_point_segment(
        p0[0], p0[1], p1[0], p1[1],
        args->query->x, args->query->y,
        closest);
    
    // Update hit (2D version)
    // ...
}
```

**cuBQL Migration:**
```cpp
void closest_point_segments_cubql(const BinaryBVH<2, double>& segment_bvh,
                                  const double query_center[2],
                                  double initial_radius,
                                  const std::vector<s2d_line_segments>& segments_list,
                                  s2d_hit* hit) {
    
    double radius_squared = initial_radius * initial_radius;
    hit->distance_squared = radius_squared;
    
    shrinkingRadiusQuery::forEachPrim(segment_bvh, query_center, radius_squared,
        [&](uint32_t prim_id, double& current_radius_squared) -> bool {
            // Map prim_id to segment group and segment index
            uint32_t group_idx, seg_idx;
            map_primitive_to_segment(prim_id, group_idx, seg_idx);
            
            const s2d_line_segments& segments = segments_list[group_idx];
            const double* p0 = &segments.points[seg_idx * 4];
            const double* p1 = &segments.points[seg_idx * 4 + 2];
            
            double closest[2];
            double distance_squared = closest_point_segment(
                p0[0], p0[1], p1[0], p1[1],
                query_center[0], query_center[1],
                closest);
            
            if (distance_squared < hit->distance_squared) {
                hit->distance_squared = distance_squared;
                hit->point[0] = closest[0];
                hit->point[1] = closest[1];
                hit->geom_id = group_idx;
                hit->prim_id = seg_idx;
                
                current_radius_squared = distance_squared;
            }
            
            return current_radius_squared > 0.0;
        });
}
```

### 5.4 Instancing Support

**Embree Pattern:** Uses `rtcPointQueryContext` with transform stack

**cuBQL Approach:** Two options:
1. **Nested BVHs**: Build separate BVH for each instance, query recursively
2. **Transform Application**: Apply instance transforms to query point before traversal

**Recommended:** Nested BVHs for better GPU performance:
```cpp
void closest_point_instanced_cubql(const BinaryBVH<3, double>& top_bvh,
                                   const double query_center[3],
                                   double initial_radius,
                                   const InstanceData* instances,
                                   s3d_hit* hit) {
    
    shrinkingRadiusQuery::forEachPrim(top_bvh, query_center, initial_radius * initial_radius,
        [&](uint32_t instance_id, double& radius_squared) -> bool {
            const InstanceData& inst = instances[instance_id];
            
            // Transform query to instance local space
            double local_query[3];
            transform_point(local_query, query_center, inst.world_to_local);
            
            // Query instance BVH with transformed point
            double local_radius = sqrt(radius_squared);
            s3d_hit local_hit;
            
            closest_point_mesh_cubql(inst.mesh_bvh, local_query, local_radius,
                                     inst.meshes, &local_hit);
            
            if (local_hit.distance_squared < radius_squared) {
                // Transform hit back to world space
                double world_point[3];
                transform_point_back(world_point, local_hit.point, inst.local_to_world);
                
                // Update global hit
                if (local_hit.distance_squared < hit->distance_squared) {
                    hit->distance_squared = local_hit.distance_squared;
                    hit->point[0] = world_point[0];
                    hit->point[1] = world_point[1];
                    hit->point[2] = world_point[2];
                    hit->geom_id = instance_id;
                    hit->prim_id = local_hit.prim_id;
                    
                    radius_squared = local_hit.distance_squared;
                }
            }
            
            return radius_squared > 0.0;
        });
}
```

---

## 6. Performance Considerations

### 6.1 GPU vs CPU Trade-offs
- **Advantage**: Massive parallelism for multiple queries (10-100x throughput)
- **Challenge**: Latency for single queries (CPU may be faster due to kernel launch overhead)
- **Recommendation**: Batch queries for GPU efficiency (minimum 1000 queries per batch)
- **Break-even Point**: Typically 10-100 queries before GPU outperforms CPU

### 6.2 Memory Layout Optimization
- **Structure of Arrays (SoA)**: Better for GPU SIMD, enables coalesced memory access
- **Geometry Data**: Store in GPU-accessible memory (CUDA unified memory or device memory)
- **BVH Construction**: Pre-build on GPU, reuse across frames for static geometry
- **Data Locality**: Group similar geometry types together to improve cache efficiency
- **Padding**: Align structures to 128-byte boundaries for optimal memory transactions

### 6.3 Precision Considerations
- **Double vs Float**: RTX 4090 supports double precision but with 1/32 throughput vs float
- **Accuracy Needs**: Thermal simulation may require double precision for energy conservation
- **Hybrid Approach**: Use float for BVH traversal, double for geometry math (requires conversion)
- **Mixed Precision Strategy**:
  ```cpp
  // BVH built with float precision for traversal speed
  BinaryBVH<3, float> bvh_float = buildBVH<float>(vertices, triangles);
  // Geometry math in double precision for accuracy
  double closest_point = closest_point_triangle_double(v0, v1, v2, query);
  ```

### 6.4 BVH Quality
- **Construction Time**: cuBQL provides fast BVH builders (SAH, LBVH, PLOC)
- **Quality vs Speed**: Trade-off for dynamic scenes (SAH highest quality, LBVH fastest)
- **Update Strategies**: Incremental updates for moving geometry (refit vs rebuild)
- **Compression**: Use quantized bounding boxes for memory reduction (8-bit or 16-bit)
- **Treelet Reordering**: Optimize for GPU warp efficiency (group spatially close nodes)

### 6.5 Query Batching Strategies
- **Uniform Grid Batching**: Group queries by spatial proximity for coherent traversal
- **Priority Batching**: Process high-priority queries first (e.g., near-field thermal)
- **Streaming**: Overlap computation with data transfer using CUDA streams
- **Dynamic Batching**: Adjust batch size based on query workload and GPU utilization
- **Example Batch Processing**:
  ```cpp
  std::vector<double3> query_points = ...; // Batch of query points
  std::vector<s3d_hit> results(query_points.size());
  
  // Process batch with single kernel launch
  parallel_for(query_points.size(), [&](uint32_t i) {
    shrinkingRadiusQuery::forEachPrim(bvh, query_points[i], radius_squared,
      [&](uint32_t prim_id, double& current_radius) {
        // Process primitive
        return current_radius > 0.0;
      });
  });
  ```

### 6.6 GPU Memory Hierarchy
- **L1/L2 Cache**: Optimize for spatial locality (coalesced memory accesses)
- **Shared Memory**: Use for query data shared within thread block
- **Constant Memory**: Store scene-wide parameters (global transforms, material properties)
- **Texture Memory**: Cache frequently accessed geometry data (vertex positions)
- **Unified Memory**: Simplifies CPU-GPU data management but may have performance overhead

### 6.7 Asynchronous Operations
- **CUDA Streams**: Concurrent kernel execution and data transfers
- **Graph Capture**: Record sequence of operations for repeated execution
- **Event-based Synchronization**: Measure timing without blocking CPU
- **Pipeline Overlap**: Overlap BVH construction with query processing
- **Example Async Pattern**:
  ```cpp
  cudaStream_t stream1, stream2;
  cudaStreamCreate(&stream1);
  cudaStreamCreate(&stream2);
  
  // Concurrent BVH build and query processing
  buildBVHAsync(bvh_data, triangles, stream1);
  processQueriesAsync(queries, bvh_data, results, stream2);
  
  cudaStreamSynchronize(stream1);
  cudaStreamSynchronize(stream2);
  ```

### 6.8 Profiling and Optimization Tools
- **Nsight Compute**: Analyze kernel performance, memory bandwidth, warp efficiency
- **Nsight Systems**: System-wide timeline of CPU-GPU interactions
- **CUDA Profiler**: Identify bottlenecks in memory access and instruction throughput
- **Key Metrics to Monitor**:
  - **Occupancy**: Percentage of theoretical warp occupancy
  - **Memory Throughput**: GB/s compared to theoretical maximum
  - **Divergence**: Branch divergence within warps
  - **Cache Hit Rate**: L1/L2 cache efficiency
- **Optimization Checklist**:
  1. Maximize occupancy (adjust block size, register usage)
  2. Minimize global memory accesses (use shared memory, cache)
  3. Reduce divergence (avoid conditionals in hot paths)
  4. Use vectorized loads (float4, double2)
  5. Enable compiler optimizations (`-use_fast_math`, `-ftz=true`)

---

## 7. Testing Strategy

### 7.1 Validation Approach
**Goal**: Ensure cuBQL implementation produces identical results to Embree within acceptable tolerance.

1. **Unit Tests**: Compare cuBQL results with Embree for identical scenes
   - **Scene Types**: Single triangle, multiple meshes, spheres, mixed geometry
   - **Query Patterns**: Grid queries, random queries, edge case positions
   - **Validation Metrics**: Position error, distance squared error, primitive ID matching

2. **Tolerance Levels**:
   - **Strict Tolerance**: 1e-6 for double precision (identical hardware)
   - **Relaxed Tolerance**: 1e-4 for mixed precision implementations
   - **Relative vs Absolute**: Use relative error for large distances, absolute for near-zero
   ```cpp
   bool validate_hit(const s3d_hit& embree_hit, const s3d_hit& cubql_hit) {
       double pos_error = distance(embree_hit.point, cubql_hit.point);
       double dist_error = fabs(embree_hit.distance_squared - cubql_hit.distance_squared);
       return pos_error < 1e-6 && dist_error < 1e-6;
   }
   ```

3. **Edge Case Coverage**:
   - **Zero-radius queries**: Should return no hit or exact point match
   - **Degenerate geometry**: Triangles with zero area, coincident vertices
   - **Instancing boundaries**: Queries at instance transform boundaries
   - **Out-of-bounds queries**: Far from geometry, inside geometry
   - **Numerical extremes**: Very large coordinates, very small distances

### 7.2 Performance Benchmarking
**Goal**: Quantify performance improvements and identify optimization opportunities.

1. **Benchmark Scenarios**:
   - **Micro-benchmarks**: Single query latency, BVH build time
   - **Throughput tests**: Batch sizes 1, 10, 100, 1000, 10000
   - **Scalability tests**: Varying scene complexity (100 to 1M triangles)
   - **Real-world workloads**: Production scene queries from thermal simulation

2. **Measurement Methodology**:
   - **Warm-up runs**: Discard first 3 runs to account for caching
   - **Statistical significance**: 100 iterations per test case
   - **Timing precision**: Use CUDA events for GPU timing, high-resolution CPU clocks
   - **Memory tracking**: Monitor GPU memory allocation and fragmentation

3. **Key Performance Indicators (KPIs)**:
   - **Speedup Ratio**: `embree_time / cubql_time`
   - **Throughput**: Queries per second (QPS)
   - **GPU Utilization**: SM occupancy, memory bandwidth utilization
   - **Power Efficiency**: Queries per watt (for thermal simulation context)

4. **Benchmark Suite Structure**:
   ```cpp
   class ClosestPointBenchmark {
   public:
       void run_single_query_latency();
       void run_batch_throughput(size_t batch_size);
       void run_scene_scaling(size_t triangle_count);
       void run_memory_usage_tracking();
       
   private:
       EmbreeBackend embree_backend;
       cuBQLBackend cubql_backend;
       std::vector<TestScene> test_scenes;
   };
   ```

### 7.3 Integration Testing
**Goal**: Ensure cuBQL integration works seamlessly with existing Stardis codebase.

1. **Existing Test Suite**:
   - **Compile-time integration**: Verify headers and linking work
   - **Runtime integration**: Replace Embree backend with cuBQL in test executables
   - **Backward compatibility**: All existing tests must pass (allow tolerance adjustments)

2. **Randomized Testing**:
   - **Fuzzing**: Generate random scenes with random queries
   - **Property-based testing**: Validate invariants (closest point is within radius, etc.)
   - **Monte Carlo validation**: Statistical comparison of result distributions
   ```cpp
   void random_scene_test(size_t iterations) {
       for (size_t i = 0; i < iterations; ++i) {
           TestScene scene = generate_random_scene();
           std::vector<Query> queries = generate_random_queries();
           auto embree_results = run_embree(scene, queries);
           auto cubql_results = run_cubql(scene, queries);
           validate_results(embree_results, cubql_results);
       }
   }
   ```

3. **Real-world Scene Validation**:
   - **Production data**: Use actual thermal simulation scenes
   - **Parameter sweep**: Test across simulation parameter ranges
   - **Long-running stability**: 24-hour continuous operation test
   - **Memory leak detection**: Monitor memory growth over time

4. **Cross-platform Validation**:
   - **Windows/Linux**: Ensure consistent behavior across OS
   - **GPU architectures**: Test on different NVIDIA GPUs (RTX 3060, 4090, A100)
   - **Driver versions**: Validate across CUDA toolkit versions (11.8, 12.0, 12.4)

### 7.4 Continuous Integration Pipeline
**Goal**: Automate testing to catch regressions early.

1. **CI/CD Integration**:
   - **Pre-commit hooks**: Run unit tests before commit
   - **Nightly builds**: Comprehensive performance benchmarking
   - **GPU-enabled CI**: Dedicated GPU runners for continuous testing

2. **Automated Test Matrix**:
   | Dimension | Values | Purpose |
   |-----------|--------|---------|
   | Precision | float, double | Validate precision handling |
   | Geometry | triangles, spheres, mixed | Coverage across geometry types |
   | Instancing | none, single, nested | Test transform hierarchies |
   | Batch Size | 1, 10, 100, 1000 | Performance scaling validation |

3. **Test Result Reporting**:
   - **Automated dashboards**: Visualize performance trends over time
   - **Regression alerts**: Notify when performance degrades beyond threshold
   - **Artifact storage**: Store benchmark results for historical comparison

### 7.5 Debugging and Diagnostics
**Goal**: Provide tools to diagnose failures during migration.

1. **Debug Output**:
   - **Verbose logging**: Per-primitive processing logs (compile-time enabled)
   - **BVH visualization**: Export BVH structure for visual inspection
   - **Query trajectory**: Log traversal path for specific queries

2. **Validation Tools**:
   ```cpp
   class QueryDebugger {
   public:
       void enable_tracing(uint32_t query_id);
       void dump_traversal_path(const BinaryBVH<3, double>& bvh);
       void compare_with_embree(const RTCPointQuery& embree_query);
   };
   ```

3. **Failure Reproduction**:
   - **Minimal reproducers**: Automatically reduce failing scenes to minimal case
   - **Deterministic execution**: Seed RNG for reproducible failures
   - **Bug reporting templates**: Structured format for issue tracking

---

## 8. Remaining Questions & Next Steps

### 8.1 Open Questions & Technical Analysis

#### 8.1.1 BVH Construction API
**Question**: How does cuBQL build BVHs from our geometry format?

**Current Understanding**:
- cuBQL provides `BinaryBVHBuilder` template classes for different construction algorithms
- Geometry input typically requires arrays of bounding boxes and primitive IDs
- The library supports both static (pre-built) and dynamic (refitable) BVHs

**Research Needed**:
1. **Input Format Compatibility**:
   - How to convert our triangle mesh format (vertices + indices) to cuBQL's expected format
   - Support for custom geometry types (spheres, line segments)
   - Instancing representation (separate BVH per instance vs. transform application)

2. **Construction Algorithms**:
   - **SAH (Surface Area Heuristic)**: Highest quality, slower construction
   - **LBVH (Linear BVH)**: Fast construction, suitable for dynamic scenes
   - **PLOC (Parallel Locally Ordered Clustering)**: Balance between quality and speed

3. **Integration Strategy**:
   ```cpp
   // Potential integration pattern
   cuBQL::BinaryBVH<3, double> build_mesh_bvh(
       const std::vector<double3>& vertices,
       const std::vector<uint3>& triangles) {
       
       // Convert to AABBs
       std::vector<cuBQL::AABB<double, 3>> boxes;
       for (const auto& tri : triangles) {
           boxes.push_back(compute_triangle_aabb(
               vertices[tri.x], vertices[tri.y], vertices[tri.z]));
       }
       
       // Build BVH
       cuBQL::BinaryBVHBuilder<3, double> builder;
       return builder.build(boxes, cuBQL::BuildConfig::SAH);
   }
   ```

**Action Items**:
- Examine cuBQL header files for builder API signatures
- Create prototype BVH construction with sample geometry
- Benchmark construction time vs. Embree's `rtcCommitScene()`

#### 8.1.2 Memory Management
**Question**: Who owns BVH memory (CPU/GPU), lifecycle management?

**Current Understanding**:
- cuBQL likely uses CUDA device memory for BVH storage
- Memory ownership may be explicit (user allocates) or implicit (library manages)
- CPU-GPU transfers may be required for query results

**Key Considerations**:
1. **Memory Ownership Models**:
   - **Library-managed**: cuBQL allocates/deallocates internally (simpler)
   - **User-managed**: User provides memory buffers (more control)
   - **Unified Memory**: Single address space (simplifies but may have performance impact)

2. **Lifecycle Management**:
   - BVH lifetime relative to scene geometry updates
   - Memory pooling for frequent BVH rebuilds
   - GPU memory fragmentation over long-running sessions

3. **Multi-GPU Considerations**:
   - BVH replication across multiple GPUs
   - Load balancing for query distribution
   - Peer-to-peer memory access for large scenes

**Recommended Approach**:
```cpp
class cuBQLSceneManager {
public:
    // Build BVH on GPU, keep device memory
    void build_scene(const GeometryData& geometry);
    
    // Query from multiple CPU threads
    void query_closest_point(const QueryBatch& queries, ResultBatch& results);
    
    // Cleanup GPU resources
    ~cuBQLSceneManager();
    
private:
    cuBQL::BinaryBVH<3, double> bvh_;
    cudaStream_t stream_;
    // Geometry data in GPU memory
};
```

#### 8.1.3 Thread Safety
**Question**: Can multiple threads query the same BVH concurrently?

**Current Understanding**:
- GPU kernels are inherently thread-safe for read-only data
- Concurrent reads to BVH should be safe
- Concurrent BVH modification (refit/rebuild) requires synchronization

**Thread Safety Levels**:
1. **Read-only Concurrent Queries**: Likely safe (BVH is immutable during queries)
2. **Concurrent BVH Build + Query**: Requires synchronization (potentially unsafe)
3. **Multiple GPU Contexts**: May require explicit multi-context support

**Implementation Guidelines**:
- **Immutable BVH**: Once built, never modified (safe for concurrent reads)
- **Versioning**: Use atomic counters for BVH version, reject queries during rebuild
- **Queueing**: Serialize queries during BVH updates
- **Multiple BVH Copies**: Maintain read-only copies for query threads

**Thread Safety Pattern**:
```cpp
class ThreadSafeCuBQLBackend {
    std::shared_ptr<const cuBQL::BinaryBVH<3, double>> current_bvh_;
    std::mutex bvh_update_mutex_;
    
public:
    // Thread-safe query
    void query(const Query& q, Result& r) {
        auto bvh = std::atomic_load(&current_bvh_);
        shrinkingRadiusQuery::forEachPrim(*bvh, ...);
    }
    
    // Exclusive BVH update
    void update_bvh(const GeometryData& geometry) {
        std::lock_guard<std::mutex> lock(bvh_update_mutex_);
        auto new_bvh = build_bvh(geometry);
        std::atomic_store(&current_bvh_, new_bvh);
    }
};
```

#### 8.1.4 Error Handling
**Question**: How does cuBQL report errors (invalid BVH, out-of-memory)?

**Current Understanding**:
- CUDA-based libraries typically use return codes or exceptions
- Error reporting may be through CUDA error codes or custom error types
- GPU kernel failures may be asynchronous and require explicit checking

**Error Categories**:
1. **Construction Errors**: Invalid input data, unsupported geometry types
2. **Memory Errors**: Out of GPU memory, allocation failures
3. **Runtime Errors**: Kernel launch failures, illegal memory access
4. **Numerical Errors**: Degenerate geometry, precision issues

**Recommended Error Handling Strategy**:
```cpp
try {
    cuBQL::BinaryBVHBuilder<3, double> builder;
    auto bvh = builder.build(geometry, config);
    
    // Check for CUDA errors
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        throw std::runtime_error(cudaGetErrorString(err));
    }
    
    // Query with error checking
    shrinkingRadiusQuery::forEachPrim(bvh, query, radius,
        [&](uint32_t prim_id, double& radius_sq) -> bool {
            // Return false to abort on error
            if (prim_id >= max_primitives) return false;
            // ...
            return true;
        });
        
} catch (const cuBQL::exception& e) {
    LOG_ERROR("cuBQL error: {}", e.what());
    // Fall back to Embree or report error
}
```

**Integration with Existing Error System**:
- Map cuBQL errors to existing `res_T` error codes
- Maintain compatibility with `OK`, `BA`, `CHK` macros
- Add GPU-specific error categories (memory, kernel, driver)

### 8.2 Implementation Roadmap
1. **Phase 1**: Create cuBQL wrapper with Embree-compatible API (stub implementation)
2. **Phase 2**: Implement mesh closest point queries (triangles)
3. **Phase 3**: Add sphere and line segment support
4. **Phase 4**: Implement instancing with transform hierarchies
5. **Phase 5**: Optimize memory layout and GPU performance
6. **Phase 6**: Integrate with Stardis-GPU build system

### 8.3 Dependencies
- **cuBQL Library**: Need to obtain and link NVIDIA's cuBQL
- **CUDA Toolkit**: Required for GPU compilation
- **Build System**: CMake integration for mixed C/C++/CUDA

---

## 9. References

1. Embree API Documentation: https://www.embree.org/api.html
2. cuBQL GitHub/NVIDIA Documentation: [To be located]
3. Stardis CPU Source: `stardis-cpu/star-3d/0.10/src/s3d_scene_view_closest_point.c`
4. Stardis CPU Source: `stardis-cpu/star-2d/0.7/src/s2d_scene_view_closest_point.c`

---

*Document maintained by: Sisyphus AI Agent*  
*Last Updated: 2026-02-02*