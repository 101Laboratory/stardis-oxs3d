/*
 * main.cpp - OptiX 9.1.0 Unified Pipeline Benchmark
 *
 * Single pipeline initialization, handles both ray tracing and
 * closest-point (on triangle surface) queries.  Supports scene
 * modification between query batches.
 *
 * Tests:
 *   1. RT validation (single hit, miss, batch)
 *   2. CP validation (above-center, on-surface, on-vertex, far, floor, batch)
 *   3. RT ray count sweep
 *   4. RT scene complexity sweep (via setTriangleMesh)
 *   5. CP query count sweep
 *   6. CP triangle count sweep (via setQueryMesh)
 *   7. Multi-stream concurrent RT launch
 *   8. Scene modification demo (add/remove + rebuild + re-verify)
 *
 * Usage:
 *   optix_throughput [options]
 *     --no-validate    Skip validation tests
 *     --max-rays <N>   Maximum ray/query count (default: 64M)
 *     --iters <N>      Timed iterations per test (default: 10)
 *     --device <id>    CUDA device ID (default: 0)
 *     --csv <file>     CSV output file
 */

#include "device_manager.h"
#include "buffer_manager.h"
#include "geometry_manager.h"
#include "unified_tracer.h"
#include "optix_check.h"
#include "ray_types.h"
#include "nn_types.h"
#include "kernels.h"
#include "nn_kernels.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <chrono>
#include <algorithm>

/* ======================================================================
 * PTX File Loading
 * ====================================================================*/
static std::string loadPtxFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open PTX file: " + filename);
    }
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

static std::string findAndLoadPtx()
{
    std::vector<std::string> search_paths = {
        "programs.ptx",
        "../programs.ptx",
        "../../programs.ptx",
#ifdef OPTIX_PTX_FILE_PATH
        OPTIX_PTX_FILE_PATH,
#endif
    };

    for (const auto& path : search_paths) {
        std::ifstream test(path);
        if (test.good()) {
            test.close();
            std::cout << "  Found PTX: " << path << "\n";
            return loadPtxFile(path);
        }
    }

    throw std::runtime_error(
        "Could not find programs.ptx. Searched:\n"
        "  - programs.ptx (current dir)\n"
        "  - ../programs.ptx\n"
        "  - ../../programs.ptx\n"
        "  - OPTIX_PTX_FILE_PATH compile-time path\n"
        "Make sure the project is built successfully first.");
}

static std::string findAndLoadNNPtx()
{
    std::vector<std::string> search_paths = {
        "nn_programs.ptx",
        "../nn_programs.ptx",
        "../../nn_programs.ptx",
#ifdef OPTIX_NN_PTX_FILE_PATH
        OPTIX_NN_PTX_FILE_PATH,
#endif
    };

    for (const auto& path : search_paths) {
        std::ifstream test(path);
        if (test.good()) {
            test.close();
            std::cout << "  Found NN PTX: " << path << "\n";
            return loadPtxFile(path);
        }
    }

    throw std::runtime_error(
        "Could not find nn_programs.ptx. Searched:\n"
        "  - nn_programs.ptx (current dir)\n"
        "  - ../nn_programs.ptx\n"
        "  - ../../nn_programs.ptx\n"
        "  - OPTIX_NN_PTX_FILE_PATH compile-time path\n"
        "Make sure the project is built successfully first.");
}

/* ======================================================================
 * Helper: Generate random triangle mesh on CPU
 * (used by CP triangle-sweep benchmark and scene mod demo)
 * ====================================================================*/
/* ======================================================================
 * Formatting Helpers
 * ====================================================================*/
static std::string formatNumber(unsigned int n)
{
    std::string s = std::to_string(n);
    int pos = (int)s.length() - 3;
    while (pos > 0) {
        s.insert(pos, ",");
        pos -= 3;
    }
    return s;
}

static std::string formatNumberShort(unsigned int n)
{
    if (n >= 1000000) return std::to_string(n / 1000000) + "M";
    if (n >= 1000)    return std::to_string(n / 1000) + "K";
    return std::to_string(n);
}

static void printHeader(const std::string& title)
{
    std::cout << "\n" << std::string(72, '=') << "\n"
              << "  " << title << "\n"
              << std::string(72, '=') << "\n";
}

static void printSubHeader(const std::string& title)
{
    std::cout << "\n--- " << title << " ---\n";
}

/* ======================================================================
 * Validation: RT single ray correctness
 * ====================================================================*/
static bool runRTValidation(UnifiedTracer& tracer)
{
    printSubHeader("Validation: Single Ray Tracing");

    /* Test 1: Ray that should hit */
    Ray ray_hit;
    ray_hit.origin    = make_float3(0.0f, 0.0f, -2.0f);
    ray_hit.direction = make_float3(0.0f, 0.0f, 1.0f);
    ray_hit.tmin      = 0.0f;
    ray_hit.tmax      = 1e16f;

    HitResult result = tracer.traceSingle(ray_hit);
    bool hit_ok = (result.t > 0.0f);
    std::cout << "  Hit test:  t=" << result.t
              << " bary=(" << result.bary_u << ", " << result.bary_v << ")"
              << " prim=" << result.prim_idx
              << (hit_ok ? " [PASS]" : " [FAIL]") << "\n";

    /* Test 2: Ray that should miss */
    Ray ray_miss;
    ray_miss.origin    = make_float3(100.0f, 100.0f, -2.0f);
    ray_miss.direction = make_float3(0.0f, 0.0f, 1.0f);
    ray_miss.tmin      = 0.0f;
    ray_miss.tmax      = 1e16f;

    result = tracer.traceSingle(ray_miss);
    bool miss_ok = (result.t < 0.0f);
    std::cout << "  Miss test: t=" << result.t
              << (miss_ok ? " [PASS]" : " [FAIL]") << "\n";

    /* Test 3: Batch trace */
    printSubHeader("Validation: Batch Ray Tracing");
    std::vector<Ray> test_rays(100);
    for (int i = 0; i < 100; ++i) {
        test_rays[i].origin    = make_float3(
            -0.4f + 0.8f * (i % 10) / 10.0f,
            -0.4f + 0.8f * (i / 10) / 10.0f,
            -2.0f);
        test_rays[i].direction = make_float3(0.0f, 0.0f, 1.0f);
        test_rays[i].tmin      = 0.0f;
        test_rays[i].tmax      = 1e16f;
    }

    auto results = tracer.traceBatch(test_rays);
    int num_hits = 0;
    for (const auto& r : results) {
        if (r.t >= 0.0f) num_hits++;
    }
    bool batch_ok = (num_hits > 0);
    std::cout << "  Batch (100 rays): " << num_hits << " hits / 100 rays"
              << (batch_ok ? " [PASS]" : " [FAIL]") << "\n";

    return hit_ok && miss_ok && batch_ok;
}

/* ======================================================================
 * Validation: Watertight Ray-Triangle Intersection (Seam Test)
 * Ported from star-3d test_s3d_seams.c
 *
 * A small square (2 triangles) sharing a diagonal edge is transformed
 * (180-deg pitch rotation + z-translation).  A specific ray known to
 * hit exactly on the shared edge is traced.  If the ray tracer is not
 * watertight the ray will leak through the seam and report a miss.
 *
 * Additional sub-tests:
 *   - Float vs double precision transform (vertex positions differ
 *     slightly, both must hit).
 *   - Systematic edge-targeting rays along the entire shared diagonal.
 *   - Grid mesh with many internal edges + batch seam-targeted rays.
 * ====================================================================*/
static bool runWatertightTest(UnifiedTracer& tracer)
{
    printSubHeader("Watertight Seam Test (ref: test_s3d_seams)");
    bool all_ok = true;

    /* ---- Original square at z=0 (same as CPU test) ----
     *   v0(-0.1,-0.1,0)  v1(0.1,-0.1,0)
     *   v3(-0.1, 0.1,0)  v2(0.1, 0.1,0)
     * Triangles: {0,2,1} and {2,0,3}  (diagonal v0-v2 is the seam) */
    const float sq[4][3] = {
        {-0.1f, -0.1f, 0.0f},
        { 0.1f, -0.1f, 0.0f},
        { 0.1f,  0.1f, 0.0f},
        {-0.1f,  0.1f, 0.0f}
    };

    /* ---- The exact seam ray from test_s3d_seams.c ---- */
    Ray seam_ray;
    seam_ray.origin    = make_float3( 3.3492994308471680f,
                                      -9.7470426559448242f,
                                       2.6555661803570274f);
    seam_ray.direction = make_float3(-0.26465030351986046f,
                                      0.77017831656345948f,
                                      0.58033229924097962f);
    seam_ray.tmin = 0.0f;
    seam_ray.tmax = 1e16f;

    /* ---- Sub-test A: float / double precision transform ----
     * Transform = 180-deg pitch (rotation around X) + translate (0,0,10)
     * Pitch matrix:  [1  0   0 ]
     *                [0  c  -s ]
     *                [0  s   c ]  with c=cos(pi), s=sin(pi)             */
    for (int use_double = 1; use_double >= 0; --use_double) {
        std::vector<float3> verts(4);

        if (use_double) {
            const double PI_D = 3.14159265358979323846;
            double cd = cos(PI_D), sd = sin(PI_D);
            for (int i = 0; i < 4; ++i) {
                double x = (double)sq[i][0];
                double y = (double)sq[i][1];
                double z = (double)sq[i][2];
                verts[i] = make_float3(
                    (float)x,
                    (float)(cd * y - sd * z),
                    (float)(sd * y + cd * z + 10.0));
            }
        } else {
            const float PI_F = 3.14159265358979323846f;
            float cf = cosf(PI_F), sf = sinf(PI_F);
            for (int i = 0; i < 4; ++i) {
                float x = sq[i][0], y = sq[i][1], z = sq[i][2];
                verts[i] = make_float3(
                    x,
                    cf * y - sf * z,
                    sf * y + cf * z + 10.0f);
            }
        }

        std::vector<uint3> idx = { make_uint3(0,2,1), make_uint3(2,0,3) };

        TriangleMesh mesh;
        mesh.vertices = verts;
        mesh.indices  = idx;
        mesh.bbox_min = mesh.bbox_max = verts[0];
        for (const auto& v : verts) {
            mesh.bbox_min.x = (std::min)(mesh.bbox_min.x, v.x);
            mesh.bbox_min.y = (std::min)(mesh.bbox_min.y, v.y);
            mesh.bbox_min.z = (std::min)(mesh.bbox_min.z, v.z);
            mesh.bbox_max.x = (std::max)(mesh.bbox_max.x, v.x);
            mesh.bbox_max.y = (std::max)(mesh.bbox_max.y, v.y);
            mesh.bbox_max.z = (std::max)(mesh.bbox_max.z, v.z);
        }

        tracer.setTriangleMesh(mesh);
        HitResult hr = tracer.traceSingle(seam_ray);
        bool hit = (hr.t > 0.0f);

        const char* label = use_double ? "double" : "float";
        std::cout << "  Seam ray (" << label << " xform): ";
        if (hit) {
            float3 p;
            p.x = seam_ray.origin.x + seam_ray.direction.x * hr.t;
            p.y = seam_ray.origin.y + seam_ray.direction.y * hr.t;
            p.z = seam_ray.origin.z + seam_ray.direction.z * hr.t;
            std::cout << "Hit [" << p.x << " " << p.y << " " << p.z
                      << "] t=" << hr.t << " prim=" << hr.prim_idx
                      << " [PASS]\n";
        } else {
            std::cout << "No hit [FAIL]\n";
        }
        all_ok = all_ok && hit;
    }

    /* ---- Sub-test B: systematic edge-targeted rays ----
     * Rebuild with double-precision transform.  The shared edge (v0-v2)
     * runs from (-0.1, 0.1, 10) to (0.1, -0.1, 10).  Shoot rays from
     * below (z-5) straight up at N uniformly-spaced points on the edge. */
    {
        const double PI_D = 3.14159265358979323846;
        double cd = cos(PI_D), sd = sin(PI_D);
        std::vector<float3> verts(4);
        for (int i = 0; i < 4; ++i) {
            double x = (double)sq[i][0];
            double y = (double)sq[i][1];
            double z = (double)sq[i][2];
            verts[i] = make_float3(
                (float)x,
                (float)(cd * y - sd * z),
                (float)(sd * y + cd * z + 10.0));
        }
        std::vector<uint3> idx = { make_uint3(0,2,1), make_uint3(2,0,3) };
        TriangleMesh mesh;
        mesh.vertices = verts;
        mesh.indices  = idx;
        mesh.bbox_min = mesh.bbox_max = verts[0];
        for (const auto& v : verts) {
            mesh.bbox_min.x = (std::min)(mesh.bbox_min.x, v.x);
            mesh.bbox_min.y = (std::min)(mesh.bbox_min.y, v.y);
            mesh.bbox_min.z = (std::min)(mesh.bbox_min.z, v.z);
            mesh.bbox_max.x = (std::max)(mesh.bbox_max.x, v.x);
            mesh.bbox_max.y = (std::max)(mesh.bbox_max.y, v.y);
            mesh.bbox_max.z = (std::max)(mesh.bbox_max.z, v.z);
        }
        tracer.setTriangleMesh(mesh);

        float3 e0 = verts[0]; /* (-0.1,  0.1, 10) */
        float3 e1 = verts[2]; /* ( 0.1, -0.1, 10) */
        const int N_EDGE = 21;
        int edge_hits = 0;
        for (int i = 0; i < N_EDGE; ++i) {
            /* Slightly inset from exact vertices to avoid vertex-corner
             * ambiguity (which is not a watertight issue). */
            float t_p = 0.01f + 0.98f * (float)i / (float)(N_EDGE - 1);
            float3 tgt;
            tgt.x = e0.x + t_p * (e1.x - e0.x);
            tgt.y = e0.y + t_p * (e1.y - e0.y);
            tgt.z = e0.z + t_p * (e1.z - e0.z);

            Ray ray;
            ray.origin    = make_float3(tgt.x, tgt.y, tgt.z - 5.0f);
            ray.direction = make_float3(0.0f, 0.0f, 1.0f);
            ray.tmin      = 0.0f;
            ray.tmax      = 1e16f;

            HitResult hr = tracer.traceSingle(ray);
            if (hr.t > 0.0f) ++edge_hits;
        }
        bool edge_ok = (edge_hits == N_EDGE);
        std::cout << "  Edge-targeted rays: " << edge_hits << "/" << N_EDGE
                  << " hits" << (edge_ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && edge_ok;
    }

    /* ---- Sub-test C: oblique edge-targeted rays ----
     * Same edge, but rays arrive at 45-deg angles from various
     * azimuthal directions (harder case for BVH traversal). */
    {
        float3 e0 = make_float3(-0.1f,  0.1f, 10.0f);
        float3 e1 = make_float3( 0.1f, -0.1f, 10.0f);
        const int N_AZ = 8;   /* azimuthal directions */
        const int N_T  = 5;   /* positions along edge */
        int oblique_hits = 0;
        const int oblique_total = N_AZ * N_T;

        for (int it = 0; it < N_T; ++it) {
            float t_p = (float)(it + 1) / (float)(N_T + 1); /* skip endpoints */
            float3 tgt;
            tgt.x = e0.x + t_p * (e1.x - e0.x);
            tgt.y = e0.y + t_p * (e1.y - e0.y);
            tgt.z = e0.z + t_p * (e1.z - e0.z);

            for (int ia = 0; ia < N_AZ; ++ia) {
                float angle = (float)ia * 6.2831853f / (float)N_AZ;
                /* Direction toward target at 45-deg elevation */
                float dx = cosf(angle) * 0.70710678f;
                float dy = sinf(angle) * 0.70710678f;
                float dz = 0.70710678f; /* sin(45) */

                Ray ray;
                ray.origin    = make_float3(
                    tgt.x - dx * 5.0f,
                    tgt.y - dy * 5.0f,
                    tgt.z - dz * 5.0f);
                ray.direction = make_float3(dx, dy, dz);
                ray.tmin      = 0.0f;
                ray.tmax      = 1e16f;

                HitResult hr = tracer.traceSingle(ray);
                if (hr.t > 0.0f) ++oblique_hits;
            }
        }
        bool oblique_ok = (oblique_hits == oblique_total);
        std::cout << "  Oblique edge rays: " << oblique_hits << "/" << oblique_total
                  << " hits" << (oblique_ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && oblique_ok;
    }

    /* ---- Sub-test D: grid mesh seam test (batch) ----
     * Create a 10x10 grid (200 triangles, many shared edges).  Shoot
     * a batch of rays at every interior vertex (shared by up to 6 tris).
     * All must hit. */
    {
        const int GRID = 10;
        TriangleMesh grid = GeometryManager::createTriangleGrid(GRID, 2.0f);
        tracer.setTriangleMesh(grid);

        /* Interior vertices are at (i,j) for 1<=i,j<=GRID-1.
         * Grid vertices are laid out as (GRID+1) x (GRID+1).
         * Grid lies in the XZ plane at Y=0.
         * Physical position: x = -1 + 2*i/GRID, y = 0, z = -1 + 2*j/GRID */
        std::vector<Ray> seam_rays;
        for (int j = 1; j < GRID; ++j) {
            for (int i = 1; i < GRID; ++i) {
                Ray r;
                r.origin = make_float3(
                    -1.0f + 2.0f * (float)i / (float)GRID,
                    -2.0f,
                    -1.0f + 2.0f * (float)j / (float)GRID);
                r.direction = make_float3(0.0f, 1.0f, 0.0f);
                r.tmin = 0.0f;
                r.tmax = 1e16f;
                seam_rays.push_back(r);
            }
        }

        auto results = tracer.traceBatch(seam_rays);
        int grid_hits = 0;
        for (const auto& hr : results) {
            if (hr.t > 0.0f) ++grid_hits;
        }
        bool grid_ok = (grid_hits == (int)seam_rays.size());
        std::cout << "  Grid seam batch: " << grid_hits << "/"
                  << seam_rays.size() << " hits"
                  << (grid_ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && grid_ok;
    }

    return all_ok;
}

/* ======================================================================
 * Validation: Instance Tracing (IAS)
 * Verifies that instancing (IAS -> child GAS) works correctly:
 *   - Two instances of a single triangle at different positions
 *   - Hits on each instance independently
 *   - Miss between instances
 * ====================================================================*/
static bool runInstanceValidation(UnifiedTracer& tracer)
{
    printSubHeader("Validation: Instance Tracing (IAS)");
    bool all_ok = true;

    TriangleMesh base = GeometryManager::createSingleTriangle();

    /* Two instances: identity + translated by (5, 0, 0) */
    float transforms[24];
    memset(transforms, 0, sizeof(transforms));
    /* Instance 0: identity */
    transforms[0] = 1.0f; transforms[5] = 1.0f; transforms[10] = 1.0f;
    /* Instance 1: translate (5, 0, 0) */
    transforms[12] = 1.0f; transforms[17] = 1.0f; transforms[22] = 1.0f;
    transforms[15] = 5.0f; /* tx */

    tracer.setTriangleInstances(base, transforms, 2);
    std::cout << "  Built IAS: " << tracer.getNumGeometries() << " geometries\n";

    /* Test 1: Ray hitting instance 0 */
    {
        Ray ray;
        ray.origin    = make_float3(0.0f, 0.0f, -2.0f);
        ray.direction = make_float3(0.0f, 0.0f, 1.0f);
        ray.tmin = 0.0f; ray.tmax = 1e16f;
        HitResult hr = tracer.traceSingle(ray);
        bool ok = (hr.t > 0.0f);
        std::cout << "  Instance 0 hit: t=" << hr.t
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && ok;
    }

    /* Test 2: Ray hitting instance 1 */
    {
        Ray ray;
        ray.origin    = make_float3(5.0f, 0.0f, -2.0f);
        ray.direction = make_float3(0.0f, 0.0f, 1.0f);
        ray.tmin = 0.0f; ray.tmax = 1e16f;
        HitResult hr = tracer.traceSingle(ray);
        bool ok = (hr.t > 0.0f);
        std::cout << "  Instance 1 hit: t=" << hr.t
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && ok;
    }

    /* Test 3: Ray between instances (should miss) */
    {
        Ray ray;
        ray.origin    = make_float3(2.5f, 0.0f, -2.0f);
        ray.direction = make_float3(0.0f, 0.0f, 1.0f);
        ray.tmin = 0.0f; ray.tmax = 1e16f;
        HitResult hr = tracer.traceSingle(ray);
        bool ok = (hr.t < 0.0f);
        std::cout << "  Between instances: t=" << hr.t
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && ok;
    }

    /* Test 4: Instance with rotation (seam test from test_s3d_seams.c) */
    {
        /* Square mesh: 2 triangles sharing a diagonal edge */
        TriangleMesh sq;
        sq.vertices = {
            make_float3(-0.1f, -0.1f, 0.0f),
            make_float3( 0.1f, -0.1f, 0.0f),
            make_float3( 0.1f,  0.1f, 0.0f),
            make_float3(-0.1f,  0.1f, 0.0f)
        };
        sq.indices = { make_uint3(0,2,1), make_uint3(2,0,3) };
        sq.bbox_min = make_float3(-0.1f, -0.1f, 0.0f);
        sq.bbox_max = make_float3( 0.1f,  0.1f, 0.0f);

        /* Transform: 180-deg pitch + translate (0, 0, 10)
         * [1    0       0   0  ]
         * [0  cos(pi) -sin(pi) 0  ]
         * [0  sin(pi)  cos(pi) 10 ] */
        const double PI_D = 3.14159265358979323846;
        float xf[12];
        xf[0] = 1.0f; xf[1] = 0.0f;                   xf[ 2] = 0.0f;                    xf[ 3] = 0.0f;
        xf[4] = 0.0f; xf[5] = (float)cos(PI_D);       xf[ 6] = (float)(-sin(PI_D));     xf[ 7] = 0.0f;
        xf[8] = 0.0f; xf[9] = (float)sin(PI_D);       xf[10] = (float)cos(PI_D);        xf[11] = 10.0f;

        tracer.setTriangleInstances(sq, xf, 1);

        /* Exact seam ray from test_s3d_seams.c */
        Ray seam_ray;
        seam_ray.origin    = make_float3( 3.3492994308471680f,
                                          -9.7470426559448242f,
                                           2.6555661803570274f);
        seam_ray.direction = make_float3(-0.26465030351986046f,
                                          0.77017831656345948f,
                                          0.58033229924097962f);
        seam_ray.tmin = 0.0f; seam_ray.tmax = 1e16f;

        HitResult hr = tracer.traceSingle(seam_ray);
        bool ok = (hr.t > 0.0f);
        std::cout << "  Instance seam ray: t=" << hr.t
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && ok;
    }

    /* Test 5: Batch trace across many instances */
    {
        TriangleMesh base2 = GeometryManager::createCornellBox();
        const unsigned int N_INST = 100;
        std::vector<float> xforms(N_INST * 12);
        int grid_side = 10; /* 10x10 grid */
        float spacing = 3.0f;
        for (unsigned int i = 0; i < N_INST; ++i) {
            int ix = (int)(i % grid_side);
            int iz = (int)(i / grid_side);
            float* t = xforms.data() + i * 12;
            memset(t, 0, sizeof(float) * 12);
            t[0] = 1.0f; t[5] = 1.0f; t[10] = 1.0f;
            t[3]  = ix * spacing;
            t[11] = iz * spacing;
        }
        tracer.setTriangleInstances(base2, xforms.data(), N_INST);

        /* Shoot a ray at each instance center */
        std::vector<Ray> batch_rays(N_INST);
        for (unsigned int i = 0; i < N_INST; ++i) {
            int ix = (int)(i % grid_side);
            int iz = (int)(i / grid_side);
            batch_rays[i].origin    = make_float3(
                ix * spacing, 0.0f, iz * spacing - 5.0f);
            batch_rays[i].direction = make_float3(0.0f, 0.0f, 1.0f);
            batch_rays[i].tmin = 0.0f;
            batch_rays[i].tmax = 1e16f;
        }
        auto results = tracer.traceBatch(batch_rays);
        int batch_hits = 0;
        for (const auto& hr : results)
            if (hr.t > 0.0f) ++batch_hits;
        bool ok = (batch_hits == (int)N_INST);
        std::cout << "  Batch (" << N_INST << " instances): "
                  << batch_hits << "/" << N_INST << " hits"
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && ok;
    }

    return all_ok;
}

/* ======================================================================
 * Validation: Closest-Point (on triangle surface) query correctness
 * Uses a known mesh geometry so we can verify exact distances.
 * ====================================================================*/
static bool runCPValidation(UnifiedTracer& tracer)
{
    printSubHeader("Closest-Point Validation: Single & Batch Queries");

    /* Use a single triangle at z=0:
     * v0(-0.5, -0.5, 0), v1(0.5, -0.5, 0), v2(0, 0.5, 0) */
    TriangleMesh cp_mesh = GeometryManager::createSingleTriangle();
    float cp_radius = 5.0f; /* search radius */
    tracer.setQueryMesh(cp_mesh, cp_radius);

    /* Test 1: Point directly above triangle center → distance = height */
    {
        float3 center = make_float3(0.0f, 0.0f, 0.0f); /* centroid approx */
        float3 query  = make_float3(0.0f, 0.0f, 1.0f); /* 1 unit above */
        NNResult r = tracer.querySingle(query);
        bool ok = (r.distance >= 0.0f && fabsf(r.distance - 1.0f) < 1e-4f
                   && r.prim_idx == 0);
        std::cout << "  Above-center: dist=" << r.distance
                  << " tri=" << r.prim_idx
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        if (!ok) return false;
    }

    /* Test 2: Point on the triangle surface → distance ~0 */
    {
        float3 query = make_float3(0.0f, -0.2f, 0.0f); /* inside triangle */
        NNResult r = tracer.querySingle(query);
        bool ok = (r.distance >= 0.0f && r.distance < 1e-5f
                   && r.prim_idx == 0);
        std::cout << "  On-surface:   dist=" << r.distance
                  << " tri=" << r.prim_idx
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        if (!ok) return false;
    }

    /* Test 3: Point on a vertex → distance = 0 */
    {
        NNResult r = tracer.querySingle(cp_mesh.vertices[0]);
        bool ok = (r.distance >= 0.0f && r.distance < 1e-5f);
        std::cout << "  On-vertex:    dist=" << r.distance
                  << " tri=" << r.prim_idx
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        if (!ok) return false;
    }

    /* Test 4: Point far away → miss (outside search radius) */
    {
        float3 query = make_float3(1000.0f, 1000.0f, 1000.0f);
        NNResult r = tracer.querySingle(query);
        bool ok = (r.distance < 0.0f);
        std::cout << "  Far-query:    dist=" << r.distance
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        if (!ok) return false;
    }

    /* Test 5: Multi-triangle mesh — Cornell Box, query at floor surface.
     * Floor has triangles at y=-1. A point at (0, -1, 0) should be
     * on the floor surface (distance ~0). */
    {
        TriangleMesh cornell = GeometryManager::createCornellBox();
        tracer.setQueryMesh(cornell, 2.0f);

        float3 query = make_float3(0.0f, -1.0f, 0.0f); /* on floor */
        NNResult r = tracer.querySingle(query);
        bool ok = (r.distance >= 0.0f && r.distance < 1e-4f);
        std::cout << "  Cornell-floor: dist=" << r.distance
                  << " tri=" << r.prim_idx
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        if (!ok) return false;
    }

    /* Test 6: Batch query — 100 random points near Cornell Box surfaces */
    {
        TriangleMesh cornell = GeometryManager::createCornellBox();
        tracer.setQueryMesh(cornell, 2.0f);

        std::vector<float3> batch_queries(100);
        for (int i = 0; i < 100; ++i) {
            /* Points on/near the floor (y = -1 ± 0.1) */
            float x = -0.9f + 1.8f * (float)(i % 10) / 9.0f;
            float z = -0.9f + 1.8f * (float)(i / 10) / 9.0f;
            batch_queries[i] = make_float3(x, -1.0f + 0.05f * (float)(i % 3), z);
        }

        auto results = tracer.queryBatch(batch_queries);
        int found = 0;
        for (const auto& r : results)
            if (r.distance >= 0.0f) ++found;
        bool ok = (found == 100);
        std::cout << "  Batch (100):  " << found << "/100 found"
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        if (!ok) return false;
    }

    return true;
}

/* ======================================================================
 * Benchmark: RT Ray Count Sweep
 * ====================================================================*/
static void benchmarkRayCountSweep(
    UnifiedTracer&                   tracer,
    const std::vector<unsigned int>& ray_counts,
    float3                           bbox_min,
    float3                           bbox_max,
    int                              num_iters,
    const std::string&               scene_name,
    std::ostream&                    csv)
{
    printSubHeader("Ray Count Sweep - " + scene_name);

    std::cout << std::right
              << std::setw(12) << "Rays"
              << std::setw(14) << "Time(ms)"
              << std::setw(14) << "MRays/s"
              << std::setw(12) << "GRays/s"
              << std::setw(10) << "Hit%"
              << "\n"
              << std::string(62, '-') << "\n";

    for (unsigned int count : ray_counts) {
        CudaBuffer<Ray>       d_rays;
        CudaBuffer<HitResult> d_hits;
        d_rays.alloc(count);
        d_hits.alloc(count);

        generateRandomRaysDevice(d_rays.get(), count, bbox_min, bbox_max, 12345);
        CUDA_CHECK(cudaDeviceSynchronize());

        TraceResult result = tracer.traceBatchTimed(
            d_rays.get(), d_hits.get(), count, num_iters, 3);

        std::cout << std::right
                  << std::setw(12) << formatNumber(count)
                  << std::setw(14) << std::fixed << std::setprecision(3) << result.trace_time_ms
                  << std::setw(14) << std::fixed << std::setprecision(2) << result.mrays_per_sec
                  << std::setw(12) << std::fixed << std::setprecision(4) << result.grays_per_sec
                  << std::setw(9)  << std::fixed << std::setprecision(1) << (result.hit_rate * 100.0f) << "%"
                  << "\n";

        csv << scene_name << ","
            << count << ","
            << result.trace_time_ms << ","
            << result.mrays_per_sec << ","
            << result.grays_per_sec << ","
            << result.hit_rate << "\n";
    }
}

/* ======================================================================
 * Benchmark: RT Scene Complexity Sweep
 * Reuses the unified tracer — just swaps triangle meshes.
 * ====================================================================*/
static void benchmarkSceneComplexity(
    UnifiedTracer&   tracer,
    unsigned int     fixed_ray_count,
    int              num_iters,
    std::ostream&    csv)
{
    printSubHeader("Scene Complexity Sweep (" + formatNumber(fixed_ray_count) + " rays)");

    std::cout << std::right
              << std::setw(12) << "Triangles"
              << std::setw(14) << "Build(ms)"
              << std::setw(14) << "Trace(ms)"
              << std::setw(14) << "MRays/s"
              << std::setw(12) << "GRays/s"
              << std::setw(10) << "Hit%"
              << "\n"
              << std::string(76, '-') << "\n";

    std::vector<unsigned int> tri_counts = {
        1, 100, 1000, 10000, 100000, 500000, 1000000
    };

    for (unsigned int tri_count : tri_counts) {
        TriangleMesh mesh;
        if (tri_count == 1) {
            mesh = GeometryManager::createSingleTriangle();
        } else if (tri_count <= 100) {
            mesh = GeometryManager::createCornellBox();
            tri_count = (unsigned int)mesh.indices.size();
        } else {
            mesh = GeometryManager::createRandomTriangles(tri_count);
        }

        /* Time the scene replacement (GAS rebuild) */
        auto build_start = std::chrono::high_resolution_clock::now();
        tracer.setTriangleMesh(mesh);
        CUDA_CHECK(cudaDeviceSynchronize());
        auto build_end   = std::chrono::high_resolution_clock::now();
        float build_ms   = std::chrono::duration<float, std::milli>(build_end - build_start).count();

        /* Generate random rays and trace */
        CudaBuffer<Ray>       d_rays;
        CudaBuffer<HitResult> d_hits;
        d_rays.alloc(fixed_ray_count);
        d_hits.alloc(fixed_ray_count);

        generateRandomRaysDevice(d_rays.get(), fixed_ray_count,
                                 mesh.bbox_min, mesh.bbox_max, 42);
        CUDA_CHECK(cudaDeviceSynchronize());

        TraceResult result = tracer.traceBatchTimed(
            d_rays.get(), d_hits.get(), fixed_ray_count, num_iters, 3);

        std::cout << std::right
                  << std::setw(12) << formatNumber(tri_count)
                  << std::setw(14) << std::fixed << std::setprecision(3) << build_ms
                  << std::setw(14) << std::fixed << std::setprecision(3) << result.trace_time_ms
                  << std::setw(14) << std::fixed << std::setprecision(2) << result.mrays_per_sec
                  << std::setw(12) << std::fixed << std::setprecision(4) << result.grays_per_sec
                  << std::setw(9)  << std::fixed << std::setprecision(1) << (result.hit_rate * 100.0f) << "%"
                  << "\n";

        csv << "complexity," << tri_count << ","
            << build_ms << ","
            << result.trace_time_ms << ","
            << result.mrays_per_sec << ","
            << result.grays_per_sec << ","
            << result.hit_rate << "\n";
    }
}

/* ======================================================================
 * Benchmark: Closest-Point Query Count Sweep
 * ====================================================================*/
static void benchmarkCPQuerySweep(
    UnifiedTracer&                   tracer,
    const std::vector<unsigned int>& query_counts,
    int                              num_iters,
    std::ostream&                    csv)
{
    printSubHeader("CP Query Count Sweep (" + formatNumber(tracer.getNumQueryTriangles())
                   + " tris, radius=" + std::to_string(tracer.getSearchRadius()).substr(0, 5) + ")");

    std::cout << std::right
              << std::setw(12) << "Queries"
              << std::setw(14) << "Time(ms)"
              << std::setw(14) << "MQuery/s"
              << std::setw(12) << "GQuery/s"
              << std::setw(10) << "Found%"
              << "\n"
              << std::string(62, '-') << "\n";

    for (unsigned int count : query_counts) {
        CudaBuffer<float3>   d_queries;
        CudaBuffer<NNResult> d_results;
        d_queries.alloc(count);
        d_results.alloc(count);

        generateRandomQueriesDevice(d_queries.get(), count,
                                    tracer.getQueryBBoxMin(),
                                    tracer.getQueryBBoxMax(), 12345);
        CUDA_CHECK(cudaDeviceSynchronize());

        NNQueryResult qr = tracer.queryBatchTimed(
            d_queries.get(), d_results.get(), count, num_iters, 3);

        std::cout << std::right
                  << std::setw(12) << formatNumber(count)
                  << std::setw(14) << std::fixed << std::setprecision(3) << qr.query_time_ms
                  << std::setw(14) << std::fixed << std::setprecision(2) << qr.mqueries_per_sec
                  << std::setw(12) << std::fixed << std::setprecision(4) << qr.gqueries_per_sec
                  << std::setw(9)  << std::fixed << std::setprecision(1) << (qr.found_rate * 100.0f) << "%"
                  << "\n";

        csv << "cp_sweep," << count << ","
            << 0 << ","
            << qr.query_time_ms << ","
            << qr.mqueries_per_sec << ","
            << qr.gqueries_per_sec << ","
            << qr.found_rate << "\n";
    }
}

/* ======================================================================
 * Benchmark: CP Triangle Count Sweep — reuses unified tracer, swaps query mesh
 * ====================================================================*/
static void benchmarkCPTriSweep(
    UnifiedTracer&   tracer,
    unsigned int     fixed_queries,
    int              num_iters,
    std::ostream&    csv)
{
    printSubHeader("CP Triangle Count Sweep (" + formatNumber(fixed_queries) + " queries)");

    std::cout << std::right
              << std::setw(12) << "Triangles"
              << std::setw(14) << "Build(ms)"
              << std::setw(14) << "Query(ms)"
              << std::setw(14) << "MQuery/s"
              << std::setw(12) << "GQuery/s"
              << std::setw(10) << "Found%"
              << "\n"
              << std::string(76, '-') << "\n";

    std::vector<unsigned int> tri_counts = {
        1, 100, 1000, 10000, 100000, 500000, 1000000
    };

    for (unsigned int tc : tri_counts) {
        TriangleMesh mesh;
        if (tc == 1)
            mesh = GeometryManager::createSingleTriangle();
        else if (tc <= 100) {
            mesh = GeometryManager::createCornellBox();
            tc = (unsigned int)mesh.indices.size();
        } else
            mesh = GeometryManager::createRandomTriangles(tc);

        /* Search radius proportional to scene extent / cube-root of triangle count */
        float extent = 10.0f;
        float radius = extent / std::pow((float)tc, 1.0f / 3.0f) * 3.0f;

        auto build_start = std::chrono::high_resolution_clock::now();
        tracer.setQueryMesh(mesh, radius);
        CUDA_CHECK(cudaDeviceSynchronize());
        auto build_end = std::chrono::high_resolution_clock::now();
        float build_ms = std::chrono::duration<float, std::milli>(
            build_end - build_start).count();

        CudaBuffer<float3>   d_queries;
        CudaBuffer<NNResult> d_results;
        d_queries.alloc(fixed_queries);
        d_results.alloc(fixed_queries);

        generateRandomQueriesDevice(d_queries.get(), fixed_queries,
                                    tracer.getQueryBBoxMin(),
                                    tracer.getQueryBBoxMax(), 42);
        CUDA_CHECK(cudaDeviceSynchronize());

        NNQueryResult qr = tracer.queryBatchTimed(
            d_queries.get(), d_results.get(), fixed_queries, num_iters, 3);

        std::cout << std::right
                  << std::setw(12) << formatNumber(tc)
                  << std::setw(14) << std::fixed << std::setprecision(3) << build_ms
                  << std::setw(14) << std::fixed << std::setprecision(3) << qr.query_time_ms
                  << std::setw(14) << std::fixed << std::setprecision(2) << qr.mqueries_per_sec
                  << std::setw(12) << std::fixed << std::setprecision(4) << qr.gqueries_per_sec
                  << std::setw(9)  << std::fixed << std::setprecision(1) << (qr.found_rate * 100.0f) << "%"
                  << "\n";

        csv << "cp_complexity," << tc << ","
            << build_ms << ","
            << qr.query_time_ms << ","
            << qr.mqueries_per_sec << ","
            << qr.gqueries_per_sec << ","
            << qr.found_rate << "\n";
    }
}

/* ======================================================================
 * Benchmark: Multi-Stream Concurrent RT Launch
 * ====================================================================*/
static void benchmarkMultiStream(
    UnifiedTracer& tracer,
    unsigned int   total_rays,
    float3         bbox_min,
    float3         bbox_max,
    int            num_iters,
    std::ostream&  csv)
{
    printSubHeader("Multi-Stream Concurrent Launch (" + formatNumber(total_rays) + " total rays)");

    std::vector<int> stream_counts = { 1, 2, 4, 8 };

    std::cout << std::right
              << std::setw(10) << "Streams"
              << std::setw(14) << "Rays/Stream"
              << std::setw(14) << "Total(ms)"
              << std::setw(14) << "MRays/s"
              << std::setw(12) << "GRays/s"
              << "\n"
              << std::string(64, '-') << "\n";

    for (int num_streams : stream_counts) {
        unsigned int rays_per_stream = total_rays / num_streams;

        std::vector<cudaStream_t>          streams(num_streams);
        std::vector<CudaBuffer<Ray>>       ray_bufs(num_streams);
        std::vector<CudaBuffer<HitResult>> hit_bufs(num_streams);

        for (int s = 0; s < num_streams; ++s) {
            CUDA_CHECK(cudaStreamCreate(&streams[s]));
            ray_bufs[s].alloc(rays_per_stream);
            hit_bufs[s].alloc(rays_per_stream);
            generateRandomRaysDevice(ray_bufs[s].get(), rays_per_stream,
                                     bbox_min, bbox_max, 42 + s, streams[s]);
        }
        CUDA_CHECK(cudaDeviceSynchronize());

        /* Warm-up */
        for (int w = 0; w < 3; ++w) {
            for (int s = 0; s < num_streams; ++s) {
                tracer.traceBatch(ray_bufs[s].get(), hit_bufs[s].get(),
                                  rays_per_stream, streams[s]);
            }
            CUDA_CHECK(cudaDeviceSynchronize());
        }

        /* Timed runs */
        std::vector<float> times(num_iters);
        cudaEvent_t start, stop;
        CUDA_CHECK(cudaEventCreate(&start));
        CUDA_CHECK(cudaEventCreate(&stop));

        for (int i = 0; i < num_iters; ++i) {
            CUDA_CHECK(cudaEventRecord(start, 0));
            for (int s = 0; s < num_streams; ++s) {
                tracer.traceBatch(ray_bufs[s].get(), hit_bufs[s].get(),
                                  rays_per_stream, streams[s]);
            }
            CUDA_CHECK(cudaEventRecord(stop, 0));
            CUDA_CHECK(cudaEventSynchronize(stop));
            float ms = 0;
            CUDA_CHECK(cudaEventElapsedTime(&ms, start, stop));
            times[i] = ms;
        }

        CUDA_CHECK(cudaEventDestroy(start));
        CUDA_CHECK(cudaEventDestroy(stop));

        for (int s = 0; s < num_streams; ++s) {
            CUDA_CHECK(cudaStreamDestroy(streams[s]));
        }

        std::sort(times.begin(), times.end());
        float median_ms = times[num_iters / 2];
        double mrays    = (double)total_rays / (median_ms * 1000.0);
        double grays    = mrays / 1000.0;

        std::cout << std::right
                  << std::setw(10) << num_streams
                  << std::setw(14) << formatNumber(rays_per_stream)
                  << std::setw(14) << std::fixed << std::setprecision(3) << median_ms
                  << std::setw(14) << std::fixed << std::setprecision(2) << mrays
                  << std::setw(12) << std::fixed << std::setprecision(4) << grays
                  << "\n";

        csv << "multistream," << num_streams << ","
            << rays_per_stream << ","
            << median_ms << ","
            << mrays << ","
            << grays << "\n";
    }
}

/* ======================================================================
 * Benchmark: Instance Count Sweep
 * Same base mesh (CornellBox), varying number of instances.
 * Measures IAS build + trace throughput vs instance count.
 * ====================================================================*/
static void benchmarkInstanceSweep(
    UnifiedTracer&  tracer,
    unsigned int    fixed_ray_count,
    int             num_iters,
    std::ostream&   csv)
{
    TriangleMesh base = GeometryManager::createCornellBox();
    unsigned int base_tris = (unsigned int)base.indices.size();

    printSubHeader("Instance Count Sweep ("
                   + formatNumber(fixed_ray_count) + " rays, base="
                   + std::to_string(base_tris) + " tris)");

    std::cout << std::right
              << std::setw(12) << "Instances"
              << std::setw(12) << "TotalTris"
              << std::setw(14) << "Build(ms)"
              << std::setw(14) << "Trace(ms)"
              << std::setw(14) << "MRays/s"
              << std::setw(12) << "GRays/s"
              << std::setw(10) << "Hit%"
              << "\n"
              << std::string(88, '-') << "\n";

    std::vector<unsigned int> instance_counts = {
        1, 10, 100, 1000, 10000
    };

    for (unsigned int N : instance_counts) {
        /* Generate grid transforms (identity rotation + translation) */
        std::vector<float> xforms(N * 12);
        int grid_side = (int)std::ceil(std::cbrt((float)N));
        float spacing = 3.0f;

        for (unsigned int i = 0; i < N; ++i) {
            int ix = (int)(i % grid_side);
            int iy = (int)((i / grid_side) % grid_side);
            int iz = (int)(i / (grid_side * grid_side));
            float* t = xforms.data() + i * 12;
            memset(t, 0, sizeof(float) * 12);
            t[0] = 1.0f; t[5] = 1.0f; t[10] = 1.0f;
            t[3]  = ix * spacing;
            t[7]  = iy * spacing;
            t[11] = iz * spacing;
        }

        auto build_start = std::chrono::high_resolution_clock::now();
        tracer.setTriangleInstances(base, xforms.data(), N);
        CUDA_CHECK(cudaDeviceSynchronize());
        auto build_end = std::chrono::high_resolution_clock::now();
        float build_ms = std::chrono::duration<float, std::milli>(
            build_end - build_start).count();

        CudaBuffer<Ray>       d_rays;
        CudaBuffer<HitResult> d_hits;
        d_rays.alloc(fixed_ray_count);
        d_hits.alloc(fixed_ray_count);

        generateRandomRaysDevice(d_rays.get(), fixed_ray_count,
                                 tracer.getTriBBoxMin(),
                                 tracer.getTriBBoxMax(), 42);
        CUDA_CHECK(cudaDeviceSynchronize());

        TraceResult result = tracer.traceBatchTimed(
            d_rays.get(), d_hits.get(), fixed_ray_count, num_iters, 3);

        std::cout << std::right
                  << std::setw(12) << formatNumber(N)
                  << std::setw(12) << formatNumber(base_tris * N)
                  << std::setw(14) << std::fixed << std::setprecision(3) << build_ms
                  << std::setw(14) << std::fixed << std::setprecision(3) << result.trace_time_ms
                  << std::setw(14) << std::fixed << std::setprecision(2) << result.mrays_per_sec
                  << std::setw(12) << std::fixed << std::setprecision(4) << result.grays_per_sec
                  << std::setw(9)  << std::fixed << std::setprecision(1) << (result.hit_rate * 100.0f) << "%"
                  << "\n";

        csv << "instancing," << N << ","
            << build_ms << ","
            << result.trace_time_ms << ","
            << result.mrays_per_sec << ","
            << result.grays_per_sec << ","
            << result.hit_rate << "\n";
    }
}

/* ======================================================================
 * Scene Modification Demo
 * Demonstrates add/remove + BVH rebuild between query batches.
 * ====================================================================*/
static bool runSceneModificationDemo(
    UnifiedTracer&           tracer)
{
    printSubHeader("Scene Modification Demo");
    bool all_ok = true;

    /* 1. Record baseline counts */
    unsigned int base_geoms   = tracer.getNumGeometries();
    unsigned int base_cp_tris = tracer.getNumQueryTriangles();
    std::cout << "  Baseline: " << base_geoms << " RT geometries, "
              << base_cp_tris << " CP triangles\n";

    /* 2. Add extra geometry via multi-geometry API */
    {
        auto extra_mesh = GeometryManager::createRandomTriangles(1000);
        tracer.addGeometryMesh(extra_mesh);
        tracer.rebuildScene();
        std::cout << "  Added 1000-tri geometry -> " << tracer.getNumGeometries() << " geometries\n";

        /* Verify RT still works */
        Ray ray;
        ray.origin    = make_float3(0.0f, 0.0f, -5.0f);
        ray.direction = make_float3(0.0f, 0.0f, 1.0f);
        ray.tmin      = 0.0f;
        ray.tmax      = 1e16f;
        HitResult hit = tracer.traceSingle(ray);
        bool ok = (hit.t > 0.0f);
        std::cout << "  RT after add: t=" << hit.t << (ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && ok;
    }

    /* 3. Replace query mesh */
    {
        auto new_cp_mesh = GeometryManager::createRandomTriangles(1000, 10.0f, 9999);
        float radius = 10.0f / std::pow(1000.0f, 1.0f / 3.0f) * 3.0f;
        tracer.setQueryMesh(new_cp_mesh, radius);
        std::cout << "  Replaced CP mesh -> " << tracer.getNumQueryTriangles() << " total\n";

        /* Verify CP still works — query at a vertex of new mesh */
        float3 query = new_cp_mesh.vertices[0];
        NNResult nn_r = tracer.querySingle(query);
        bool ok = (nn_r.distance >= 0.0f && nn_r.distance < 1e-4f);
        std::cout << "  CP after replace: dist=" << nn_r.distance
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && ok;
    }

    /* 4. Clear all RT geometry and set new mesh */
    {
        tracer.clearAllGeometry();
        std::cout << "  Cleared RT geometries -> " << tracer.getNumGeometries() << " total\n";

        auto new_mesh = GeometryManager::createSingleTriangle();
        tracer.setTriangleMesh(new_mesh);
        std::cout << "  Set single RT triangle -> " << tracer.getNumGeometries() << " geometries\n";

        /* Verify RT with new mesh */
        Ray ray;
        ray.origin    = make_float3(0.0f, 0.0f, -2.0f);
        ray.direction = make_float3(0.0f, 0.0f, 1.0f);
        ray.tmin      = 0.0f;
        ray.tmax      = 1e16f;
        HitResult hit = tracer.traceSingle(ray);
        bool ok = (hit.t > 0.0f);
        std::cout << "  RT after replace: t=" << hit.t << (ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && ok;
    }

    /* 5. Clear query mesh and set new one */
    {
        tracer.clearQueryMesh();
        std::cout << "  Cleared CP mesh -> " << tracer.getNumQueryTriangles() << " total\n";

        auto new_mesh = GeometryManager::createRandomTriangles(10000, 5.0f, 12345);
        float radius = 5.0f / std::pow(10000.0f, 1.0f / 3.0f) * 3.0f;
        tracer.setQueryMesh(new_mesh, radius);
        std::cout << "  Set 10K CP triangles -> " << tracer.getNumQueryTriangles() << " total\n";

        /* Verify CP with new mesh — query at a vertex */
        NNResult nn_r = tracer.querySingle(new_mesh.vertices[0]);
        bool ok = (nn_r.distance >= 0.0f && nn_r.distance < 1e-4f);
        std::cout << "  CP after replace: dist=" << nn_r.distance
                  << (ok ? " [PASS]" : " [FAIL]") << "\n";
        all_ok = all_ok && ok;
    }

    return all_ok;
}

/* ======================================================================
 * MAIN
 * ====================================================================*/
int main(int argc, char* argv[])
{
    /* ---- Parse arguments ---- */
    bool         do_validate  = true;
    unsigned int max_rays     = 64 * 1024 * 1024;
    int          num_iters    = 10;
    int          device_id    = 0;
    std::string  csv_file     = "optix_throughput_results.csv";

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--no-validate")          do_validate = false;
        else if (arg == "--max-rays" && i+1 < argc) max_rays  = std::stoul(argv[++i]);
        else if (arg == "--iters"    && i+1 < argc) num_iters = std::stoi(argv[++i]);
        else if (arg == "--device"   && i+1 < argc) device_id = std::stoi(argv[++i]);
        else if (arg == "--csv"      && i+1 < argc) csv_file  = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --no-validate      Skip validation tests\n"
                      << "  --max-rays <N>     Maximum ray/query count (default: 64M)\n"
                      << "  --iters <N>        Timed iterations (default: 10)\n"
                      << "  --device <id>      CUDA device (default: 0)\n"
                      << "  --csv <file>       CSV output file\n";
            return 0;
        }
    }

    try {
        /* ============================================================
         * 1. Device Initialization
         * ============================================================*/
        printHeader("OptiX 9.1.0 Unified Pipeline Benchmark");

        DeviceManager device;
        device.init(device_id, do_validate);
        device.printDeviceInfo();

        /* ============================================================
         * 2. Load PTX and Create Unified Pipeline
         * ============================================================*/
        printSubHeader("Unified Pipeline Setup");
        std::string rt_ptx = findAndLoadPtx();
        std::cout << "  RT PTX loaded (" << rt_ptx.size() << " bytes)\n";

        std::string nn_ptx = findAndLoadNNPtx();
        std::cout << "  NN PTX loaded (" << nn_ptx.size() << " bytes)\n";

        UnifiedTracer tracer;
        tracer.init(device.getContext(), rt_ptx, nn_ptx);

        /* ============================================================
         * 3. Build Initial Scene
         * ============================================================*/
        printSubHeader("Scene Construction");

        /* Triangle scene: Cornell Box */
        TriangleMesh cornell = GeometryManager::createCornellBox();
        tracer.setTriangleMesh(cornell);
        std::cout << "  Cornell Box: "
                  << cornell.indices.size() << " triangles, "
                  << cornell.vertices.size() << " vertices\n";

        /* Query mesh for closest-point queries: 100K random triangles */
        const unsigned int cp_num_tris = 100000;
        TriangleMesh cp_mesh = GeometryManager::createRandomTriangles(cp_num_tris, 10.0f, 42);
        float avg_spacing = 10.0f / std::pow((float)cp_num_tris, 1.0f / 3.0f);
        float cp_radius   = avg_spacing * 3.0f;
        tracer.setQueryMesh(cp_mesh, cp_radius);
        std::cout << "  Query mesh: " << formatNumber(cp_num_tris)
                  << " triangles, radius=" << cp_radius << "\n";

        /* ============================================================
         * 4. Validation
         * ============================================================*/
        if (do_validate) {
            printHeader("Validation Tests");

            bool rt_valid = runRTValidation(tracer);
            if (!rt_valid) {
                std::cerr << "\n*** RT VALIDATION FAILED ***\n";
                return 1;
            }
            std::cout << "\n  All RT validation tests PASSED.\n";

            bool wt_valid = runWatertightTest(tracer);
            if (!wt_valid) {
                std::cerr << "\n*** WATERTIGHT SEAM TEST FAILED ***\n";
                return 1;
            }
            std::cout << "\n  All watertight seam tests PASSED.\n";

            bool inst_valid = runInstanceValidation(tracer);
            if (!inst_valid) {
                std::cerr << "\n*** INSTANCE VALIDATION FAILED ***\n";
                return 1;
            }
            std::cout << "\n  All instance validation tests PASSED.\n";

            /* Restore scene for CP validation */
            tracer.setTriangleMesh(cornell);
            tracer.setQueryMesh(cp_mesh, cp_radius);

            bool cp_valid = runCPValidation(tracer);
            if (!cp_valid) {
                std::cerr << "\n*** CLOSEST-POINT VALIDATION FAILED ***\n";
                return 1;
            }
            std::cout << "\n  All closest-point validation tests PASSED.\n";
        }

        /* ============================================================
         * 5. Throughput Benchmarks
         * ============================================================*/
        printHeader("Throughput Benchmarks");

        std::ofstream csv(csv_file);
        csv << "test,param,build_ms,trace_ms,mrays_per_sec,grays_per_sec,hit_rate\n";

        /* 5a. RT ray count sweep (Cornell Box) */
        /* Restore Cornell Box (complexity sweep may have changed it) */
        tracer.setTriangleMesh(cornell);

        std::vector<unsigned int> ray_counts;
        for (unsigned int n = 1024; n <= max_rays; n *= 4) {
            ray_counts.push_back(n);
        }
        if (ray_counts.back() < max_rays) {
            ray_counts.push_back(max_rays);
        }

        benchmarkRayCountSweep(
            tracer, ray_counts,
            cornell.bbox_min, cornell.bbox_max,
            num_iters, "CornellBox", csv);

        /* 5b. RT scene complexity sweep */
        benchmarkSceneComplexity(tracer, 1024 * 1024, num_iters, csv);

        /* Restore Cornell Box for multi-stream test */
        tracer.setTriangleMesh(cornell);

        /* 5c. Multi-stream concurrent launch */
        benchmarkMultiStream(
            tracer,
            4 * 1024 * 1024,
            cornell.bbox_min, cornell.bbox_max,
            num_iters, csv);

        /* ============================================================
         * 5d. Instance Count Sweep (IAS)
         * ============================================================*/
        benchmarkInstanceSweep(tracer, 1024 * 1024, num_iters, csv);

        /* Restore Cornell Box for subsequent tests */
        tracer.setTriangleMesh(cornell);

        /* ============================================================
         * 6. Closest-Point Benchmarks
         * ============================================================*/
        printHeader("Closest-Point Benchmarks");

        /* Restore default query mesh */
        tracer.setQueryMesh(cp_mesh, cp_radius);

        /* 6a. CP query count sweep */
        std::vector<unsigned int> cp_query_counts;
        for (unsigned int n = 1024; n <= max_rays; n *= 4) {
            cp_query_counts.push_back(n);
        }
        if (cp_query_counts.back() < max_rays) {
            cp_query_counts.push_back(max_rays);
        }

        benchmarkCPQuerySweep(tracer, cp_query_counts, num_iters, csv);

        /* 6b. CP triangle count sweep */
        benchmarkCPTriSweep(tracer, 1024 * 1024, num_iters, csv);

        /* ============================================================
         * 7. Scene Modification Demo
         * ============================================================*/
        printHeader("Scene Modification Demo");

        /* Restore baseline scene for demo */
        tracer.setTriangleMesh(cornell);
        tracer.setQueryMesh(cp_mesh, cp_radius);

        bool mod_ok = runSceneModificationDemo(tracer);
        if (!mod_ok) {
            std::cerr << "\n*** SCENE MODIFICATION TESTS FAILED ***\n";
        } else {
            std::cout << "\n  All scene modification tests PASSED.\n";
        }

        csv.close();

        /* ============================================================
         * 8. Summary
         * ============================================================*/
        printHeader("Benchmark Complete");
        std::cout << "  Results saved to: " << csv_file << "\n"
                  << "  Max ray/query count tested: " << formatNumber(max_rays) << "\n"
                  << "  Iterations per test: " << num_iters << "\n"
                  << "  Architecture: Unified pipeline (1 init, dual SBT)\n";

        /* ============================================================
         * 9. Cleanup
         * ============================================================*/
        printSubHeader("Cleanup");
        tracer.cleanup();
        device.shutdown();
        std::cout << "  All resources released.\n";

    } catch (const std::exception& e) {
        std::cerr << "\n*** ERROR: " << e.what() << " ***\n";
        return 1;
    }

    return 0;
}
