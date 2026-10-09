/*
 * geometry_manager.h - Geometry generation for test scenes
 *
 * Extensions:
 *   E4 - SphereMesh + createSingleSphere / createRandomSpheres
 */
#pragma once

#include <cuda_runtime.h>
#include <vector>
#include "ray_types.h"    /* SphereData */

/* ---- Triangle Mesh Data (host-side) ---- */
struct TriangleMesh {
    std::vector<float3> vertices;
    std::vector<uint3>  indices;
    float3              bbox_min;
    float3              bbox_max;
};

/* ---- Sphere Mesh Data (host-side, E4) ---- */
struct SphereMesh {
    std::vector<float3> centers;
    std::vector<float>  radii;
    float3              bbox_min;
    float3              bbox_max;
};

class GeometryManager {
public:
    /* Generate a single triangle (minimal test case) */
    static TriangleMesh createSingleTriangle();

    /* Generate a Cornell Box (~30 triangles)
     * Standard test scene: 5 walls + 2 boxes */
    static TriangleMesh createCornellBox();

    /* Generate a random triangle mesh for throughput testing
     * @param num_triangles  Number of random triangles
     * @param bbox_extent    Scene extent (centered at origin)
     * @param seed           Random seed */
    static TriangleMesh createRandomTriangles(
        unsigned int num_triangles,
        float        bbox_extent = 10.0f,
        unsigned int seed        = 42);

    /* Generate a uniform triangle grid (predictable intersection pattern)
     * @param grid_size  Grid dimension (grid_size x grid_size quads -> 2*grid_size^2 tris)
     * @param extent     Physical size of the grid */
    static TriangleMesh createTriangleGrid(
        int   grid_size,
        float extent = 10.0f);

    /* ---- Sphere Geometry (E4) ---- */

    /* Generate a single sphere (minimal test case) */
    static SphereMesh createSingleSphere(
        float3 center = make_float3(0.0f, 0.0f, 0.0f),
        float  radius = 1.0f);

    /* Generate random spheres for testing
     * @param count       Number of spheres
     * @param bbox_extent Scene extent (centered at origin)
     * @param max_radius  Maximum sphere radius
     * @param seed        Random seed */
    static SphereMesh createRandomSpheres(
        unsigned int count,
        float        bbox_extent = 10.0f,
        float        max_radius  = 1.0f,
        unsigned int seed        = 42);
};
