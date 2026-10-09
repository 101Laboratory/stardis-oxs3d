/*
 * geometry_manager.cpp - Geometry generation implementation
 */

#include "geometry_manager.h"
#include <cmath>
#include <algorithm>

/* ---- Helper: update bounding box ---- */
static void updateBBox(TriangleMesh& mesh)
{
    mesh.bbox_min = make_float3(1e30f, 1e30f, 1e30f);
    mesh.bbox_max = make_float3(-1e30f, -1e30f, -1e30f);

    for (const auto& v : mesh.vertices) {
        mesh.bbox_min.x = std::min(mesh.bbox_min.x, v.x);
        mesh.bbox_min.y = std::min(mesh.bbox_min.y, v.y);
        mesh.bbox_min.z = std::min(mesh.bbox_min.z, v.z);
        mesh.bbox_max.x = std::max(mesh.bbox_max.x, v.x);
        mesh.bbox_max.y = std::max(mesh.bbox_max.y, v.y);
        mesh.bbox_max.z = std::max(mesh.bbox_max.z, v.z);
    }
}

/* ---- Helper: add a quad as two triangles ---- */
static void addQuad(
    TriangleMesh& mesh,
    float3 v0, float3 v1, float3 v2, float3 v3)
{
    unsigned int base = (unsigned int)mesh.vertices.size();
    mesh.vertices.push_back(v0);
    mesh.vertices.push_back(v1);
    mesh.vertices.push_back(v2);
    mesh.vertices.push_back(v3);
    mesh.indices.push_back(make_uint3(base, base + 1, base + 2));
    mesh.indices.push_back(make_uint3(base, base + 2, base + 3));
}

/* ---- Simple hash RNG (CPU version) ---- */
static unsigned int pcg_hash_cpu(unsigned int input)
{
    unsigned int state = input * 747796405u + 2891336453u;
    unsigned int word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

static float rng_float_cpu(unsigned int& seed)
{
    seed = pcg_hash_cpu(seed);
    return (float)(seed) / (float)(0xFFFFFFFFu);
}

/* ===========================================================================
 * GeometryManager::createSingleTriangle
 * =========================================================================*/
TriangleMesh GeometryManager::createSingleTriangle()
{
    TriangleMesh mesh;
    mesh.vertices = {
        make_float3(-0.5f, -0.5f, 0.0f),
        make_float3( 0.5f, -0.5f, 0.0f),
        make_float3( 0.0f,  0.5f, 0.0f)
    };
    mesh.indices = { make_uint3(0, 1, 2) };
    updateBBox(mesh);
    return mesh;
}

/* ===========================================================================
 * GeometryManager::createCornellBox
 * A standard Cornell Box with walls and two boxes inside.
 * Coordinates: X = left/right, Y = up/down, Z = front/back
 * Range: [-1, 1] in each axis
 * =========================================================================*/
TriangleMesh GeometryManager::createCornellBox()
{
    TriangleMesh mesh;

    /* Floor (white) */
    addQuad(mesh,
        make_float3(-1, -1, -1), make_float3(1, -1, -1),
        make_float3(1, -1, 1),   make_float3(-1, -1, 1));

    /* Ceiling (white) */
    addQuad(mesh,
        make_float3(-1, 1, -1), make_float3(-1, 1, 1),
        make_float3(1, 1, 1),   make_float3(1, 1, -1));

    /* Back wall (white) */
    addQuad(mesh,
        make_float3(-1, -1, 1), make_float3(1, -1, 1),
        make_float3(1, 1, 1),   make_float3(-1, 1, 1));

    /* Left wall (red) */
    addQuad(mesh,
        make_float3(-1, -1, -1), make_float3(-1, -1, 1),
        make_float3(-1, 1, 1),   make_float3(-1, 1, -1));

    /* Right wall (green) */
    addQuad(mesh,
        make_float3(1, -1, -1), make_float3(1, 1, -1),
        make_float3(1, 1, 1),   make_float3(1, -1, 1));

    /* Short box (5 visible faces = 10 triangles) */
    float bx = -0.35f, bz = -0.15f;  /* position */
    float bs = 0.3f;                  /* half-size */
    float bh = 0.3f;                  /* height from floor to top */

    /* Top */
    addQuad(mesh,
        make_float3(bx - bs, -1 + bh, bz - bs), make_float3(bx + bs, -1 + bh, bz - bs),
        make_float3(bx + bs, -1 + bh, bz + bs), make_float3(bx - bs, -1 + bh, bz + bs));
    /* Front */
    addQuad(mesh,
        make_float3(bx - bs, -1, bz - bs), make_float3(bx + bs, -1, bz - bs),
        make_float3(bx + bs, -1 + bh, bz - bs), make_float3(bx - bs, -1 + bh, bz - bs));
    /* Back */
    addQuad(mesh,
        make_float3(bx - bs, -1, bz + bs), make_float3(bx - bs, -1 + bh, bz + bs),
        make_float3(bx + bs, -1 + bh, bz + bs), make_float3(bx + bs, -1, bz + bs));
    /* Left */
    addQuad(mesh,
        make_float3(bx - bs, -1, bz - bs), make_float3(bx - bs, -1 + bh, bz - bs),
        make_float3(bx - bs, -1 + bh, bz + bs), make_float3(bx - bs, -1, bz + bs));
    /* Right */
    addQuad(mesh,
        make_float3(bx + bs, -1, bz - bs), make_float3(bx + bs, -1, bz + bs),
        make_float3(bx + bs, -1 + bh, bz + bs), make_float3(bx + bs, -1 + bh, bz - bs));

    /* Tall box */
    float tx = 0.35f, tz = 0.2f;
    float ts = 0.25f;
    float th = 0.6f;

    /* Top */
    addQuad(mesh,
        make_float3(tx - ts, -1 + th, tz - ts), make_float3(tx + ts, -1 + th, tz - ts),
        make_float3(tx + ts, -1 + th, tz + ts), make_float3(tx - ts, -1 + th, tz + ts));
    /* Front */
    addQuad(mesh,
        make_float3(tx - ts, -1, tz - ts), make_float3(tx + ts, -1, tz - ts),
        make_float3(tx + ts, -1 + th, tz - ts), make_float3(tx - ts, -1 + th, tz - ts));
    /* Back */
    addQuad(mesh,
        make_float3(tx - ts, -1, tz + ts), make_float3(tx - ts, -1 + th, tz + ts),
        make_float3(tx + ts, -1 + th, tz + ts), make_float3(tx + ts, -1, tz + ts));
    /* Left */
    addQuad(mesh,
        make_float3(tx - ts, -1, tz - ts), make_float3(tx - ts, -1 + th, tz - ts),
        make_float3(tx - ts, -1 + th, tz + ts), make_float3(tx - ts, -1, tz + ts));
    /* Right */
    addQuad(mesh,
        make_float3(tx + ts, -1, tz - ts), make_float3(tx + ts, -1, tz + ts),
        make_float3(tx + ts, -1 + th, tz + ts), make_float3(tx + ts, -1 + th, tz - ts));

    updateBBox(mesh);
    return mesh;
}

/* ===========================================================================
 * GeometryManager::createRandomTriangles
 * =========================================================================*/
TriangleMesh GeometryManager::createRandomTriangles(
    unsigned int num_triangles,
    float        bbox_extent,
    unsigned int seed)
{
    TriangleMesh mesh;
    mesh.vertices.reserve(num_triangles * 3);
    mesh.indices.reserve(num_triangles);

    unsigned int rng = seed;
    float half       = bbox_extent * 0.5f;
    float tri_size   = bbox_extent * 0.01f;  /* small triangles */

    for (unsigned int i = 0; i < num_triangles; ++i) {
        /* Random center */
        float cx = rng_float_cpu(rng) * bbox_extent - half;
        float cy = rng_float_cpu(rng) * bbox_extent - half;
        float cz = rng_float_cpu(rng) * bbox_extent - half;

        /* Three vertices around center */
        unsigned int base = (unsigned int)mesh.vertices.size();
        for (int v = 0; v < 3; ++v) {
            float dx = (rng_float_cpu(rng) - 0.5f) * tri_size;
            float dy = (rng_float_cpu(rng) - 0.5f) * tri_size;
            float dz = (rng_float_cpu(rng) - 0.5f) * tri_size;
            mesh.vertices.push_back(make_float3(cx + dx, cy + dy, cz + dz));
        }
        mesh.indices.push_back(make_uint3(base, base + 1, base + 2));
    }

    updateBBox(mesh);
    return mesh;
}

/* ===========================================================================
 * GeometryManager::createTriangleGrid
 * Uniform grid of quads, each subdivided into 2 triangles.
 * Lies in the XZ plane at Y=0.
 * =========================================================================*/
TriangleMesh GeometryManager::createTriangleGrid(int grid_size, float extent)
{
    TriangleMesh mesh;
    float half = extent * 0.5f;
    float step = extent / grid_size;

    /* Vertices: (grid_size+1) x (grid_size+1) */
    int verts_per_row = grid_size + 1;
    mesh.vertices.reserve(verts_per_row * verts_per_row);
    for (int z = 0; z <= grid_size; ++z) {
        for (int x = 0; x <= grid_size; ++x) {
            mesh.vertices.push_back(make_float3(
                -half + x * step,
                0.0f,
                -half + z * step));
        }
    }

    /* Indices: 2 triangles per quad */
    mesh.indices.reserve(grid_size * grid_size * 2);
    for (int z = 0; z < grid_size; ++z) {
        for (int x = 0; x < grid_size; ++x) {
            unsigned int i00 = z * verts_per_row + x;
            unsigned int i10 = i00 + 1;
            unsigned int i01 = i00 + verts_per_row;
            unsigned int i11 = i01 + 1;
            mesh.indices.push_back(make_uint3(i00, i10, i01));
            mesh.indices.push_back(make_uint3(i10, i11, i01));
        }
    }

    updateBBox(mesh);
    return mesh;
}

/* ===========================================================================
 * Sphere Geometry (E4)
 * =========================================================================*/

SphereMesh GeometryManager::createSingleSphere(float3 center, float radius)
{
    SphereMesh mesh;
    mesh.centers.push_back(center);
    mesh.radii.push_back(radius);
    mesh.bbox_min = make_float3(center.x - radius, center.y - radius, center.z - radius);
    mesh.bbox_max = make_float3(center.x + radius, center.y + radius, center.z + radius);
    return mesh;
}

SphereMesh GeometryManager::createRandomSpheres(
    unsigned int count,
    float        bbox_extent,
    float        max_radius,
    unsigned int seed)
{
    SphereMesh mesh;
    mesh.centers.reserve(count);
    mesh.radii.reserve(count);

    unsigned int rng = seed;
    float half = bbox_extent * 0.5f;

    mesh.bbox_min = make_float3( 1e30f,  1e30f,  1e30f);
    mesh.bbox_max = make_float3(-1e30f, -1e30f, -1e30f);

    for (unsigned int i = 0; i < count; ++i) {
        float3 c;
        c.x = rng_float_cpu(rng) * bbox_extent - half;
        c.y = rng_float_cpu(rng) * bbox_extent - half;
        c.z = rng_float_cpu(rng) * bbox_extent - half;
        float r = rng_float_cpu(rng) * max_radius + 0.01f;

        mesh.centers.push_back(c);
        mesh.radii.push_back(r);

        mesh.bbox_min.x = std::min(mesh.bbox_min.x, c.x - r);
        mesh.bbox_min.y = std::min(mesh.bbox_min.y, c.y - r);
        mesh.bbox_min.z = std::min(mesh.bbox_min.z, c.z - r);
        mesh.bbox_max.x = std::max(mesh.bbox_max.x, c.x + r);
        mesh.bbox_max.y = std::max(mesh.bbox_max.y, c.y + r);
        mesh.bbox_max.z = std::max(mesh.bbox_max.z, c.z + r);
    }

    return mesh;
}
