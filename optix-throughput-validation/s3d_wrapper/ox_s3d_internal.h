/*
 * ox_s3d_internal.h - Internal implementation structures
 *
 * Defines the concrete types behind the opaque s3d handles:
 *   s3d_device     → wraps DeviceManager + OptiX context
 *   s3d_shape      → mesh/sphere/instance data + per-shape filter
 *   s3d_scene      → collection of attached shapes
 *   s3d_scene_view → wraps UnifiedTracer, manages IAS + query GAS
 *
 * Not part of the public API.
 */
#pragma once

#include "ox_s3d_types.h"  /* includes s3d.h (public API + S3D_API dllexport/dllimport) */
#include "../src/device_manager.h"
#include "../src/unified_tracer.h"
#include "../src/geometry_manager.h"
#include "../src/buffer_manager.h"
#include "../include/ray_types.h"
#include "../include/nn_types.h"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <cmath>

/* ================================================================
 * s3d_shape — concrete shape data
 * ================================================================ */
struct s3d_shape {
    ref_T               ref;
    unsigned int        id;           /* compact shape name (auto-assigned) */
    enum ox_shape_type  type;
    bool                enabled;
    bool                flip_surface;

    /* Filter */
    s3d_hit_filter_function_T filter_func;
    void*                     filter_data;

    /* Mesh data (type == OX_SHAPE_MESH) */
    unsigned int              ntris;
    unsigned int              nverts;
    std::vector<float>        positions;         /* nverts * 3 */
    std::vector<unsigned int> indices;           /* ntris  * 3 */
    /* Per-vertex attribute arrays: 0=ATTRIB_0 .. 3=ATTRIB_3 */
    struct vertex_attrib_store {
        enum s3d_type          type;
        std::vector<float>     data;    /* nverts * type_count */
        bool                   valid;
    } vertex_attribs[4];   /* S3D_ATTRIB_0 .. S3D_ATTRIB_3 */

    /* Sphere data (type == OX_SHAPE_SPHERE) */
    float sphere_pos[3];
    float sphere_radius;

    /* Instance data (type == OX_SHAPE_INSTANCE) */
    float transform[12];     /* 3x4 column-major affine */
    s3d_scene* child_scene;  /* scene being instanced */

    /* Tracer-assigned geom_id (set when added to scene_view) */
    unsigned int tracer_geom_id;

    s3d_shape() : ref(1), id(S3D_INVALID_ID), type(OX_SHAPE_MESH),
                  enabled(true), flip_surface(false),
                  filter_func(nullptr), filter_data(nullptr),
                  ntris(0), nverts(0),
                  sphere_radius(0.0f), child_scene(nullptr),
                  tracer_geom_id(S3D_INVALID_ID)
    {
        memset(sphere_pos, 0, sizeof(sphere_pos));
        /* Identity transform */
        memset(transform, 0, sizeof(transform));
        transform[0] = 1.0f; transform[4] = 1.0f; transform[8] = 1.0f;
        for (int i = 0; i < 4; i++) {
            vertex_attribs[i].type = S3D_FLOAT;
            vertex_attribs[i].valid = false;
        }
    }

    /* Convert column-major 3x4 to row-major 3x4 for OptiX */
    void getRowMajorTransform(float out[12]) const {
        /* column-major [c0 c1 c2 c3] each 3 floats →
         * row-major: row0 = {c0[0], c1[0], c2[0], c3[0]} etc */
        out[0]  = transform[0]; out[1]  = transform[3]; out[2]  = transform[6]; out[3]  = transform[9];
        out[4]  = transform[1]; out[5]  = transform[4]; out[6]  = transform[7]; out[7]  = transform[10];
        out[8]  = transform[2]; out[9]  = transform[5]; out[10] = transform[8]; out[11] = transform[11];
    }

    /* Build TriangleMesh for the Tracer */
    TriangleMesh toTriangleMesh() const {
        TriangleMesh m;
        m.vertices.resize(nverts);
        for (unsigned i = 0; i < nverts; i++) {
            m.vertices[i] = make_float3(positions[i*3+0], positions[i*3+1], positions[i*3+2]);
        }
        m.indices.resize(ntris);
        for (unsigned i = 0; i < ntris; i++) {
            m.indices[i] = make_uint3(indices[i*3+0], indices[i*3+1], indices[i*3+2]);
        }
        /* bbox computed by tracer */
        m.bbox_min = make_float3(0,0,0);
        m.bbox_max = make_float3(0,0,0);
        return m;
    }

    /* Build SphereMesh for the Tracer */
    SphereMesh toSphereMesh() const {
        SphereMesh sm;
        sm.centers.push_back(make_float3(sphere_pos[0], sphere_pos[1], sphere_pos[2]));
        sm.radii.push_back(sphere_radius);
        sm.bbox_min = make_float3(sphere_pos[0] - sphere_radius,
                                   sphere_pos[1] - sphere_radius,
                                   sphere_pos[2] - sphere_radius);
        sm.bbox_max = make_float3(sphere_pos[0] + sphere_radius,
                                   sphere_pos[1] + sphere_radius,
                                   sphere_pos[2] + sphere_radius);
        return sm;
    }
};

/* ================================================================
 * s3d_scene — collection of shapes
 * ================================================================ */
struct s3d_scene {
    ref_T                            ref;
    s3d_device*                      dev;
    std::map<unsigned int, s3d_shape*> shapes;  /* keyed by shape->id */
    unsigned int                     next_shape_id;
    std::vector<s3d_scene_view*>     views;     /* active views */

    s3d_scene() : ref(1), dev(nullptr), next_shape_id(0) {}
};

/* ================================================================
 * s3d_device — entry point wrapping OptiX
 * ================================================================ */
struct s3d_device {
    ref_T            ref;
    DeviceManager    device_manager;
    int              verbose;
    bool             initialized;

    /* PTX sources (loaded once) */
    std::string      rt_ptx;
    std::string      nn_ptx;

    /* Global shape ID counter — shapes get unique IDs at creation time */
    unsigned int     next_shape_id;

    s3d_device() : ref(1), verbose(0), initialized(false), next_shape_id(0) {}
};

/* ================================================================
 * s3d_scene_view — the active query state wrapping UnifiedTracer
 * ================================================================ */
struct s3d_scene_view {
    ref_T             ref;
    s3d_scene*        scn;
    int               mask;
    int               build_quality;

    /* The core tracer */
    UnifiedTracer     tracer;
    bool              tracer_initialized;

    /* Shape → tracer geom_id mapping */
    std::map<unsigned int, unsigned int> shape_to_geom; /* shape_id → tracer geom_id */
    std::map<unsigned int, unsigned int> geom_to_shape; /* tracer geom_id → shape_id */
    /* Instance tracking: tracer geom_id → instance shape (for flattened instances) */
    std::map<unsigned int, s3d_shape*>   geom_to_inst;  /* tracer geom_id → instance shape */

    /* ---- Build-time attribute snapshots ---- */
    struct shape_snapshot {
        s3d_shape* shape;       /* shape pointer (geometry data is immutable after creation) */
        bool       flip_surface;
        bool       enabled;
        s3d_hit_filter_function_T filter_func; /* build-time filter snapshot */
        void*                     filter_data;
    };
    std::map<unsigned int, shape_snapshot> shape_snapshots;  /* shape_id → build-time state */

    /* Instance children snapshots: inst_shape_id → { child_shape_id → snapshot } */
    std::map<unsigned int, std::map<unsigned int, shape_snapshot>> inst_child_snapshots;

    /* Find snapshot for a shape_id: first check top-level shape_snapshots,
     * then fall back to inst_child_snapshots (for instance child shapes). */
    const shape_snapshot* find_snapshot(unsigned int sid) const {
        auto it = shape_snapshots.find(sid);
        if (it != shape_snapshots.end()) return &it->second;
        for (auto& ikv : inst_child_snapshots) {
            auto cit = ikv.second.find(sid);
            if (cit != ikv.second.end()) return &cit->second;
        }
        return nullptr;
    }

    /* Scene AABB */
    float lower[3];
    float upper[3];

    /* Dirty flag — rebuild needed */
    bool dirty;

    /* Did the last rebuild produce any geometry? */
    bool has_geometry;

    /* Sampling CDF (for S3D_SAMPLE) */
    std::vector<float>    prim_cdf;    /* cumulative area distribution */
    float                 total_area;
    bool                  cdf_valid;

    /* Query mesh state for CP — uses same scene geometry */
    bool   query_mesh_set;
    float  search_radius;

    /* Prim range table for CP: maps merged-mesh prim_idx → shape */
    struct query_prim_range {
        unsigned int prim_offset;  /* first triangle index in merged mesh */
        unsigned int prim_count;   /* number of triangles */
        s3d_shape*   shape;        /* owning shape */
        s3d_shape*   inst_shape;   /* instance shape, or nullptr */
        /* Build-time filter snapshot (filter set after build is not applied) */
        s3d_hit_filter_function_T filter_func;
        void*        filter_data;
    };
    std::vector<query_prim_range> query_prim_ranges;

    /* Sphere entries for CP: analytical closest-point on sphere (host-side) */
    struct query_sphere_entry {
        float        sphere_pos[3]; /* world-space center (pre-transformed) */
        float        sphere_radius;
        bool         flip_surface;
        s3d_shape*   shape;         /* sphere shape */
        s3d_shape*   inst_shape;    /* parent instance, or nullptr */
        /* Build-time filter snapshot */
        s3d_hit_filter_function_T filter_func;
        void*        filter_data;
    };
    std::vector<query_sphere_entry> query_sphere_entries;

    s3d_scene_view()
        : ref(1), scn(nullptr), mask(0), build_quality(0),
          tracer_initialized(false), dirty(true), has_geometry(false),
          total_area(0.0f), cdf_valid(false),
          query_mesh_set(false), search_radius(1.0f)
    {
        memset(lower, 0, sizeof(lower));
        memset(upper, 0, sizeof(upper));
    }
};

/* ================================================================
 * Batch Context Objects
 * ================================================================ */
struct s3d_batch_trace_context {
    size_t              max_rays;
    CudaBuffer<Ray>             d_rays;
    CudaBuffer<MultiHitResult>  d_multi_hits;

    s3d_batch_trace_context(size_t max)
        : max_rays(max) {
        d_rays.alloc(static_cast<unsigned int>(max));
        d_multi_hits.alloc(static_cast<unsigned int>(max));
    }
};

struct s3d_batch_cp_context {
    size_t              max_queries;
    CudaBuffer<CPQuery>  d_queries;
    CudaBuffer<CPResult> d_results;

    s3d_batch_cp_context(size_t max)
        : max_queries(max) {
        d_queries.alloc(static_cast<unsigned int>(max));
        d_results.alloc(static_cast<unsigned int>(max));
    }
};

struct s3d_batch_enc_context {
    size_t                       max_queries;
    CudaBuffer<EnclosureQuery>   d_queries;
    CudaBuffer<EnclosureResult>  d_results;

    s3d_batch_enc_context(size_t max)
        : max_queries(max) {
        d_queries.alloc(static_cast<unsigned int>(max));
        d_results.alloc(static_cast<unsigned int>(max));
    }
};

/* ================================================================
 * Hit Fixup Utilities
 * ================================================================ */
namespace ox_s3d_util {

/* Clamp value to [0,1] */
static inline float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/*
 * trace_hit_fixup — convert OptiX Möller-Trumbore UV to s3d CW convention
 *
 * OptiX: bary = (u, v) where hit = (1-u-v)*v0 + u*v1 + v*v2
 * s3d:   uv = (w, u)   where w = 1-u-v (s3d CW convention)
 *
 * Also negates the normal to match s3d's CW winding order.
 *
 * Applied to ray tracing results.  CP results need the same UV
 * conversion (done inline in the CP code paths).
 */
static inline void trace_hit_fixup(struct s3d_hit* hit, bool shape_flip) {
    float u = hit->uv[0];
    float v = hit->uv[1];
    float w = 1.0f - u - v;

    /* Clamp for floating point precision */
    w = clamp01(w);
    u = clamp01(u);

    hit->uv[0] = w;
    hit->uv[1] = u;

    /* Negate normal: OptiX CCW → s3d CW */
    hit->normal[0] = -hit->normal[0];
    hit->normal[1] = -hit->normal[1];
    hit->normal[2] = -hit->normal[2];

    /* Note: shape_flip is already handled in the device program (E7),
     * so we do NOT double-flip here */
    (void)shape_flip;
}

/* Convert HitResult → s3d_hit (with fixup) */
static inline void hitresult_to_s3d_hit(
    const s3d_scene_view* sv,
    const HitResult& hr,
    s3d_shape* shape,
    unsigned int shape_id,
    unsigned int local_prim_id,
    struct s3d_hit* out,
    s3d_shape* inst = nullptr)
{
    if (hr.t < 0.0f) {
        *out = S3D_HIT_NULL;
        return;
    }

    out->prim.prim_id       = local_prim_id;
    out->prim.geom_id       = shape_id;
    out->prim.inst_id       = inst ? inst->id : S3D_INVALID_ID;
    out->prim.scene_prim_id = hr.prim_idx;
    out->prim.shape__       = shape;
    out->prim.inst__        = inst;

    out->normal[0] = hr.normal[0];
    out->normal[1] = hr.normal[1];
    out->normal[2] = hr.normal[2];
    out->uv[0]     = hr.bary_u;
    out->uv[1]     = hr.bary_v;
    out->distance   = hr.t;

    /* Apply fixup based on shape type */
    if (shape && shape->type == OX_SHAPE_SPHERE) {
        /* Sphere: OptiX closest-hit returns normal = hit_point - center (unnormalized),
         * already flipped by device program if flip_surface is set.
         * bary_u/bary_v are zero — compute parametric UV from the normal.
         * Do NOT negate normal (sphere normal from device is already correct). */
        float nx = out->normal[0];
        float ny = out->normal[1];
        float nz = out->normal[2];
        /* For UV computation, we need the OUTWARD normal (un-flipped) */
        float ox = nx, oy = ny, oz = nz;
        bool flip = shape->flip_surface;
        if (sv) {
            auto snap = sv->find_snapshot(shape->id);
            if (snap) flip = snap->flip_surface;
        }
        if (flip) { ox = -ox; oy = -oy; oz = -oz; }
        float len = sqrtf(ox*ox + oy*oy + oz*oz);
        if (len > 0.0f) { ox /= len; oy /= len; oz /= len; }
        if (oz > 1.0f) oz = 1.0f;
        if (oz < -1.0f) oz = -1.0f;
        float theta = acosf(oz);
        float phi = atan2f(oy, ox);
        if (phi < 0.0f) phi += 2.0f * 3.14159265358979323846f;
        out->uv[0] = theta / 3.14159265358979323846f;
        out->uv[1] = phi / (2.0f * 3.14159265358979323846f);
    } else {
        /* Mesh: Apply UV/normal fixup for RT path (CCW → CW) */
        trace_hit_fixup(out, shape ? shape->flip_surface : false);
    }
}

/* Convert MultiHitResult candidate → s3d_hit */
static inline void multihit_to_s3d_hit(
    const s3d_scene_view* sv,
    const HitResult& candidate,
    s3d_shape* shape,
    unsigned int shape_id,
    unsigned int local_prim_id,
    struct s3d_hit* out)
{
    hitresult_to_s3d_hit(sv, candidate, shape, shape_id, local_prim_id, out);
}

/* Resolve prim_idx from tracer geom_id to shape_id using scene_view mapping */
static inline s3d_shape* resolve_shape(
    const s3d_scene_view* sv,
    unsigned int tracer_geom_id,
    unsigned int& out_shape_id)
{
    auto it = sv->geom_to_shape.find(tracer_geom_id);
    if (it == sv->geom_to_shape.end()) {
        out_shape_id = S3D_INVALID_ID;
        return nullptr;
    }
    out_shape_id = it->second;
    auto sit = sv->scn->shapes.find(out_shape_id);
    if (sit == sv->scn->shapes.end()) {
        /* Shape might be in child scene (flattened instances) */
        if (sv->geom_to_inst.count(tracer_geom_id)) {
            s3d_shape* inst = sv->geom_to_inst.at(tracer_geom_id);
            if (inst && inst->child_scene) {
                auto csit = inst->child_scene->shapes.find(out_shape_id);
                if (csit != inst->child_scene->shapes.end())
                    return csit->second;
            }
        }
        return nullptr;
    }
    return sit->second;
}

/* Resolve the instance shape pointer from tracer geom_id */
static inline s3d_shape* resolve_instance(
    const s3d_scene_view* sv,
    unsigned int tracer_geom_id)
{
    auto it = sv->geom_to_inst.find(tracer_geom_id);
    if (it == sv->geom_to_inst.end()) return nullptr;
    return it->second;
}

} /* namespace ox_s3d_util */

/* Forward declarations are NOT needed here:
 * s3d.h (included via ox_s3d_types.h) already declares all s3d_*
 * functions with extern "C" linkage and S3D_API export attributes.
 * Redeclaring them here with different parameter types (e.g., void*
 * instead of struct logger*) would create separate C++ overloads
 * that shadow the extern "C" versions and break DLL export.
 */
