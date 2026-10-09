/*
 * ox_s3d_scene_view.cpp — The heart of the wrapper
 *
 * Wraps UnifiedTracer with s3d_scene_view semantics:
 *   - Build / rebuild (scene geometry → UnifiedTracer GAS/IAS)
 *   - Single-ray trace (trace_ray → traceSingle + UV fixup)
 *   - Multi-ray trace (trace_rays → traceBatch + UV fixup)
 *   - Batch trace (trace_rays_batch → traceBatchMultiHit + filter + retrace)
 *   - Closest point (closest_point → closestPointBatch + prim range resolve)
 *   - Batch closest point (closest_point_batch → closestPointBatch + filter)
 *   - Find enclosure batch (find_enclosure_batch → findEnclosureBatch)
 *   - Sample (sample → CDF + random prim pick + UV map)
 *   - Geometry queries (area, volume, AABB, primitives_count, get_primitive)
 *
 * The UV/normal convention conversion is performed in the utility functions
 * defined in ox_s3d_internal.h (ox_s3d_util namespace).
 */

#include "ox_s3d_internal.h"
#include <chrono>

using namespace ox_s3d_util;

/* ---------------------------------------------------------------------------
 * Back-compute s3d-convention barycentrics from a closest-point position
 * using double precision.  Solves:
 *   proj = w*v0 + u*v1 + (1-w-u)*v2
 * via the normal equations of the 3×2 over-determined system.
 *
 * out_w = weight of v0,  out_u = weight of v1  (s3d convention).
 * Falls back to the raw GPU barycentrics (raw_w, raw_u) if the system is
 * degenerate (collapsed triangle).
 * ------------------------------------------------------------------------- */
static void backcompute_bary_double(
    const float* positions, const unsigned* indices, unsigned tri_id,
    const float proj[3],
    float raw_w, float raw_u,
    float& out_w, float& out_u)
{
    unsigned i0 = indices[tri_id * 3 + 0];
    unsigned i1 = indices[tri_id * 3 + 1];
    unsigned i2 = indices[tri_id * 3 + 2];

    /* Promote to double for precision */
    double v0[3], v1[3], v2[3];
    for (int k = 0; k < 3; k++) {
        v0[k] = (double)positions[i0 * 3 + k];
        v1[k] = (double)positions[i1 * 3 + k];
        v2[k] = (double)positions[i2 * 3 + k];
    }

    /* A = v0-v2,  B = v1-v2,  D = proj-v2 */
    double A[3], B[3], D[3];
    for (int k = 0; k < 3; k++) {
        A[k] = v0[k] - v2[k];
        B[k] = v1[k] - v2[k];
        D[k] = (double)proj[k] - v2[k];
    }

    double aa = A[0]*A[0] + A[1]*A[1] + A[2]*A[2];
    double ab = A[0]*B[0] + A[1]*B[1] + A[2]*B[2];
    double bb = B[0]*B[0] + B[1]*B[1] + B[2]*B[2];
    double ad = A[0]*D[0] + A[1]*D[1] + A[2]*D[2];
    double bd = B[0]*D[0] + B[1]*D[1] + B[2]*D[2];

    double det = aa * bb - ab * ab;
    if (det > 1e-30 || det < -1e-30) {
        double inv = 1.0 / det;
        double w = (bb * ad - ab * bd) * inv;
        double u = (aa * bd - ab * ad) * inv;
        /* Clamp to valid barycentric range */
        if (w < 0.0) w = 0.0;
        if (u < 0.0) u = 0.0;
        if (w + u > 1.0) {
            double s = 1.0 / (w + u);
            w *= s; u *= s;
        }
        out_w = (float)w;
        out_u = (float)u;
    } else {
        /* Degenerate triangle — use raw GPU barycentrics */
        out_w = raw_w;
        out_u = raw_u;
    }
}

/* Helper: high-res timer in ms */
static double now_ms() {
    using clk = std::chrono::high_resolution_clock;
    static auto t0 = clk::now();
    return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
}

/* ---------------------------------------------------------------------------
 * Transform helpers for instanced shapes.
 *
 * Instance transforms are 3x4 column-major:  [c0 c1 c2 t]
 *   t[0..2]=col0  t[3..5]=col1  t[6..8]=col2  t[9..11]=translation
 *
 * forward:  p_world = R * p_local + t
 * inverse:  p_local = R^-1 * (p_world - t)
 * ------------------------------------------------------------------------- */
static void transform_point_forward_d(const float tf[12],
                                      const double in[3], double out[3])
{
    out[0] = (double)tf[0]*in[0] + (double)tf[3]*in[1] + (double)tf[6]*in[2] + (double)tf[9];
    out[1] = (double)tf[1]*in[0] + (double)tf[4]*in[1] + (double)tf[7]*in[2] + (double)tf[10];
    out[2] = (double)tf[2]*in[0] + (double)tf[5]*in[1] + (double)tf[8]*in[2] + (double)tf[11];
}

static void inverse_transform_point_d(const float tf[12],
                                       const double in[3], double out[3])
{
    /* R rows from column-major layout */
    double R[3][3] = {
        {(double)tf[0], (double)tf[3], (double)tf[6]},
        {(double)tf[1], (double)tf[4], (double)tf[7]},
        {(double)tf[2], (double)tf[5], (double)tf[8]}
    };
    double pmt[3] = {
        in[0] - (double)tf[9],
        in[1] - (double)tf[10],
        in[2] - (double)tf[11]
    };
    double det = R[0][0]*(R[1][1]*R[2][2] - R[1][2]*R[2][1])
               - R[0][1]*(R[1][0]*R[2][2] - R[1][2]*R[2][0])
               + R[0][2]*(R[1][0]*R[2][1] - R[1][1]*R[2][0]);
    double inv_det = 1.0 / det;

    out[0] = ((R[1][1]*R[2][2]-R[1][2]*R[2][1])*pmt[0]
            + (R[0][2]*R[2][1]-R[0][1]*R[2][2])*pmt[1]
            + (R[0][1]*R[1][2]-R[0][2]*R[1][1])*pmt[2]) * inv_det;
    out[1] = ((R[1][2]*R[2][0]-R[1][0]*R[2][2])*pmt[0]
            + (R[0][0]*R[2][2]-R[0][2]*R[2][0])*pmt[1]
            + (R[0][2]*R[1][0]-R[0][0]*R[1][2])*pmt[2]) * inv_det;
    out[2] = ((R[1][0]*R[2][1]-R[1][1]*R[2][0])*pmt[0]
            + (R[0][1]*R[2][0]-R[0][0]*R[2][1])*pmt[1]
            + (R[0][0]*R[1][1]-R[0][1]*R[1][0])*pmt[2]) * inv_det;
}

/* ---------------------------------------------------------------------------
 * Host-side closest-point-on-triangle (double precision, Voronoi region method)
 *
 * Computes the closest point on triangle (v0,v1,v2) to query point p.
 * Outputs: out_proj (closest point), out_w (weight of v0), out_u (weight of v1)
 * Returns: squared distance from p to closest point.
 *
 * s3d convention:  P = out_w*v0 + out_u*v1 + (1-out_w-out_u)*v2
 * ------------------------------------------------------------------------- */
static double closestPointOnTriHost(
    const double p[3], const double v0[3], const double v1[3], const double v2[3],
    double out_proj[3], double& out_w, double& out_u)
{
    double ab[3], ac[3], ap[3];
    for (int k = 0; k < 3; k++) { ab[k] = v1[k]-v0[k]; ac[k] = v2[k]-v0[k]; ap[k] = p[k]-v0[k]; }
    double d1 = ab[0]*ap[0]+ab[1]*ap[1]+ab[2]*ap[2];
    double d2 = ac[0]*ap[0]+ac[1]*ap[1]+ac[2]*ap[2];
    if (d1 <= 0.0 && d2 <= 0.0) {
        out_w = 1.0; out_u = 0.0;
        for (int k = 0; k < 3; k++) out_proj[k] = v0[k];
        double d[3]; for (int k=0;k<3;k++) d[k]=p[k]-v0[k];
        return d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    }
    double bp[3]; for (int k=0;k<3;k++) bp[k]=p[k]-v1[k];
    double d3 = ab[0]*bp[0]+ab[1]*bp[1]+ab[2]*bp[2];
    double d4 = ac[0]*bp[0]+ac[1]*bp[1]+ac[2]*bp[2];
    if (d3 >= 0.0 && d4 <= d3) {
        out_w = 0.0; out_u = 1.0;
        for (int k = 0; k < 3; k++) out_proj[k] = v1[k];
        double d[3]; for (int k=0;k<3;k++) d[k]=p[k]-v1[k];
        return d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    }
    double vc = d1*d4 - d3*d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        double inv = 1.0/(d1 - d3);
        out_u = d1*inv; out_w = (-d3)*inv;
        for (int k=0;k<3;k++) out_proj[k] = v0[k] + ab[k]*out_u;
        double d[3]; for (int k=0;k<3;k++) d[k]=p[k]-out_proj[k];
        return d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    }
    double cp[3]; for (int k=0;k<3;k++) cp[k]=p[k]-v2[k];
    double d5 = ab[0]*cp[0]+ab[1]*cp[1]+ab[2]*cp[2];
    double d6 = ac[0]*cp[0]+ac[1]*cp[1]+ac[2]*cp[2];
    if (d6 >= 0.0 && d5 <= d6) {
        out_w = 0.0; out_u = 0.0;
        for (int k=0;k<3;k++) out_proj[k] = v2[k];
        double d[3]; for (int k=0;k<3;k++) d[k]=p[k]-v2[k];
        return d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    }
    double vb = d5*d2 - d1*d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        double inv = 1.0/(d2 - d6);
        double t_ac = d2*inv;
        out_w = (-d6)*inv; out_u = 0.0;
        for (int k=0;k<3;k++) out_proj[k] = v0[k] + ac[k]*t_ac;
        double d[3]; for (int k=0;k<3;k++) d[k]=p[k]-out_proj[k];
        return d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    }
    double va = d3*d6 - d5*d4;
    if (va <= 0.0 && (d4-d3) >= 0.0 && (d5-d6) >= 0.0) {
        double e1 = d4-d3, e2 = d5-d6;
        double inv = 1.0/(e1+e2);
        out_w = 0.0; out_u = e2*inv;
        double w_bc = e1*inv;
        double bc[3]; for (int k=0;k<3;k++) bc[k]=v2[k]-v1[k];
        for (int k=0;k<3;k++) out_proj[k] = v1[k] + bc[k]*w_bc;
        double d[3]; for (int k=0;k<3;k++) d[k]=p[k]-out_proj[k];
        return d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    }
    /* Interior */
    double denom = 1.0/(va+vb+vc);
    out_w = va*denom; out_u = vb*denom;
    double wt_v2 = vc*denom;
    for (int k=0;k<3;k++) out_proj[k] = v0[k] + ab[k]*out_u + ac[k]*wt_v2;
    double d[3]; for (int k=0;k<3;k++) d[k]=p[k]-out_proj[k];
    return d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
}

/* ---------------------------------------------------------------------------
 * Sphere UV helper — maps a unit normal to (st[0], st[1]) matching the
 * convention in ox_s3d_primitive.cpp:
 *   theta = st[0] * PI        →  st[0] = acos(nz) / PI
 *   phi   = st[1] * 2*PI      →  st[1] = atan2(ny, nx) / (2*PI)
 *
 * This differs from cus3d's sphere_normal_to_uv (which uses v=(1-nz)/2).
 * We must match our own s3d_primitive_get_attrib so that the position
 * recovery round-trip is exact.
 * ------------------------------------------------------------------------- */
static void sphere_normal_to_uv_ox(const float normal[3], float uv[2])
{
    const double PI_D = 3.14159265358979323846;
    double nz = (double)normal[2];
    if (nz >  1.0) nz =  1.0;
    if (nz < -1.0) nz = -1.0;
    double theta = acos(nz);
    uv[0] = (float)(theta / PI_D);

    double phi = atan2((double)normal[1], (double)normal[0]);
    if (phi < 0.0) phi += 2.0 * PI_D;
    uv[1] = (float)(phi / (2.0 * PI_D));
}

/* ---------------------------------------------------------------------------
 * Analytical closest-point on a single sphere.
 *
 * Computes the closest point on sphere (center+radius) to query position
 * `pos`.  If the result is closer than `*cur_radius` and passes the filter,
 * updates `*best_hit` and shrinks `*cur_radius`.
 *
 * `se` is the build-time snapshot of the sphere entry (world-space center,
 * filter snapshot, etc.).
 * `user_radius` is the original user-submitted search radius (for filter
 * range parameter).
 * ------------------------------------------------------------------------- */
static void cp_try_sphere(
    const s3d_scene_view::query_sphere_entry& se,
    const float pos[3],
    float* cur_radius,
    float user_radius,
    void* query_data,
    s3d_hit* best_hit)
{
    /* Direction from sphere center to query */
    float Ng[3] = {
        pos[0] - se.sphere_pos[0],
        pos[1] - se.sphere_pos[1],
        pos[2] - se.sphere_pos[2]
    };
    float len = sqrtf(Ng[0]*Ng[0] + Ng[1]*Ng[1] + Ng[2]*Ng[2]);

    /* Distance from query to sphere surface */
    float dist = fabsf(len - se.sphere_radius);
    if (dist >= *cur_radius) return;

    /* Normalize the outward normal */
    if (len > 0.0f) {
        Ng[0] /= len; Ng[1] /= len; Ng[2] /= len;
    } else {
        /* Query at sphere center: arbitrarily choose +X */
        Ng[0] = 1.0f; Ng[1] = 0.0f; Ng[2] = 0.0f;
    }

    /* Compute UV in ox convention */
    float uv[2];
    sphere_normal_to_uv_ox(Ng, uv);

    /* Flip normal if needed (XOR of shape and instance flip) */
    if (se.flip_surface) {
        Ng[0] = -Ng[0]; Ng[1] = -Ng[1]; Ng[2] = -Ng[2];
    }

    /* Build candidate hit */
    s3d_hit tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.prim.prim_id       = 0; /* sphere has a single primitive */
    tmp.prim.geom_id       = se.shape->id;
    tmp.prim.inst_id       = se.inst_shape
        ? (se.inst_shape->id != S3D_INVALID_ID ? se.inst_shape->id : S3D_INVALID_ID)
        : S3D_INVALID_ID;
    tmp.prim.scene_prim_id = 0;
    tmp.prim.shape__       = se.shape;
    tmp.prim.inst__        = se.inst_shape;
    tmp.normal[0] = Ng[0];
    tmp.normal[1] = Ng[1];
    tmp.normal[2] = Ng[2];
    tmp.uv[0] = uv[0];
    tmp.uv[1] = uv[1];
    tmp.distance = dist;

    /* Apply build-time filter snapshot */
    if (se.filter_func) {
        /* dir = reversed geometric normal (direction from surface to query),
         * matching cus3d convention. Uses pre-flip Ng sign. */
        float dir[3] = { -Ng[0], -Ng[1], -Ng[2] };
        float rng[2] = { 0.0f, user_radius };
        int rej = se.filter_func(&tmp, pos, dir, rng,
                                  query_data, se.filter_data);
        if (rej != 0) return;
    }

    /* Accepted */
    *cur_radius = dist;
    *best_hit = tmp;
}

/* ---------------------------------------------------------------------------
 * Try all sphere entries for a closest-point query.
 *
 * Iterates every sphere in the build-time `query_sphere_entries` list and
 * picks the closest accepted hit.  `*cur_radius` is used as the initial
 * search radius and is shrunk as closer hits are found.
 * ------------------------------------------------------------------------- */
static void cp_try_all_spheres(
    s3d_scene_view* sv,
    const float pos[3],
    float* cur_radius,
    float user_radius,
    void* query_data,
    s3d_hit* best_hit)
{
    for (auto& se : sv->query_sphere_entries) {
        cp_try_sphere(se, pos, cur_radius, user_radius, query_data, best_hit);
    }
}

/* ---------------------------------------------------------------------------
 * Host-side CP filter retry
 *
 * When the GPU-found closest primitive is rejected by the filter, we
 * brute-force all shapes' triangles on the host to find the closest
 * accepted hit.  This mirrors how the CPU reference (Embree) continues
 * BVH traversal after filter rejection.
 * ------------------------------------------------------------------------- */
static void cp_filter_retry(
    s3d_scene_view* sv,
    const float pos[3], float radius, void* query_data,
    s3d_hit* hit)
{
    double qp[3] = { (double)pos[0], (double)pos[1], (double)pos[2] };
    float best_dist = FLT_MAX;
    *hit = S3D_HIT_NULL;

    for (auto& pr : sv->query_prim_ranges) {
        if (!pr.shape) continue;
        if (pr.shape->type != OX_SHAPE_MESH) continue;
        if (pr.shape->positions.empty() || pr.shape->indices.empty()) continue;

        const float* verts = pr.shape->positions.data();
        const unsigned* idx = pr.shape->indices.data();
        const bool instanced = (pr.inst_shape != nullptr);

        for (unsigned t = 0; t < pr.prim_count; t++) {
            unsigned i0 = idx[t*3+0], i1 = idx[t*3+1], i2 = idx[t*3+2];
            double v0[3], v1[3], v2[3];
            for (int k = 0; k < 3; k++) {
                v0[k] = (double)verts[i0*3+k];
                v1[k] = (double)verts[i1*3+k];
                v2[k] = (double)verts[i2*3+k];
            }
            /* For instanced shapes, transform local-space vertices to world space
             * so that distances and directions are computed in world space. */
            if (instanced) {
                double tmp[3];
                transform_point_forward_d(pr.inst_shape->transform, v0, tmp);
                v0[0]=tmp[0]; v0[1]=tmp[1]; v0[2]=tmp[2];
                transform_point_forward_d(pr.inst_shape->transform, v1, tmp);
                v1[0]=tmp[0]; v1[1]=tmp[1]; v1[2]=tmp[2];
                transform_point_forward_d(pr.inst_shape->transform, v2, tmp);
                v2[0]=tmp[0]; v2[1]=tmp[1]; v2[2]=tmp[2];
            }

            double proj[3], bw, bu;
            double dsq = closestPointOnTriHost(qp, v0, v1, v2, proj, bw, bu);
            float dist = (float)sqrt(dsq);

            if (dist >= best_dist) continue;
            if (dist >= radius)    continue;

            /* Build temporary hit */
            s3d_hit tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.prim.prim_id       = t;
            tmp.prim.geom_id       = pr.shape->id;
            tmp.prim.inst_id       = pr.inst_shape
                ? (pr.inst_shape->id != S3D_INVALID_ID ? pr.inst_shape->id : S3D_INVALID_ID)
                : S3D_INVALID_ID;
            tmp.prim.scene_prim_id = pr.prim_offset + t;
            tmp.prim.shape__       = pr.shape;
            tmp.prim.inst__        = pr.inst_shape;

            /* Normal: cross(AC, AB) — left-hand convention (s3d) */
            double ac[3], ab_d[3];
            for (int k = 0; k < 3; k++) { ac[k] = v2[k]-v0[k]; ab_d[k] = v1[k]-v0[k]; }
            tmp.normal[0] = (float)(ac[1]*ab_d[2] - ac[2]*ab_d[1]);
            tmp.normal[1] = (float)(ac[2]*ab_d[0] - ac[0]*ab_d[2]);
            tmp.normal[2] = (float)(ac[0]*ab_d[1] - ac[1]*ab_d[0]);

            tmp.uv[0] = (float)bw;
            tmp.uv[1] = (float)bu;
            tmp.distance = dist;

            /* Apply build-time filter snapshot */
            if (pr.filter_func) {
                float dir_h[3] = {
                    (float)proj[0] - pos[0],
                    (float)proj[1] - pos[1],
                    (float)proj[2] - pos[2]
                };
                float rng[2] = { 0.0f, radius };
                int rej = pr.filter_func(&tmp, pos, dir_h, rng,
                                          query_data, pr.filter_data);
                if (rej != 0) continue;
            }

            /* Accepted — update best */
            best_dist = dist;
            *hit = tmp;
        }
    }

    /* Also try spheres (analytical closest-point) */
    cp_try_all_spheres(sv, pos, &best_dist, radius, query_data, hit);
}

/* ================================================================
 * Internal: rebuild the tracer from the scene's shapes
 * ================================================================ */
static res_T rebuild_tracer(s3d_scene_view* sv) {
    if (!sv || !sv->scn || !sv->scn->dev) return RES_UNKNOWN_ERR;

    s3d_device* dev = sv->scn->dev;

    /* Initialize tracer if needed */
    if (!sv->tracer_initialized) {
        if (dev->rt_ptx.empty() || dev->nn_ptx.empty())
            return RES_UNKNOWN_ERR;
        sv->tracer.init(dev->device_manager.getContext(),
                        dev->rt_ptx, dev->nn_ptx);
        sv->tracer_initialized = true;
    }

    /* Clear previous geometry */
    sv->tracer.clearAllGeometry();
    sv->shape_to_geom.clear();
    sv->geom_to_shape.clear();
    sv->geom_to_inst.clear();
    sv->shape_snapshots.clear();
    sv->inst_child_snapshots.clear();

    /* Add each enabled shape from the scene */
    for (auto& kv : sv->scn->shapes) {
        s3d_shape* shape = kv.second;
        if (!shape->enabled) continue;

        unsigned int tracer_gid = S3D_INVALID_ID;

        switch (shape->type) {
        case OX_SHAPE_MESH: {
            if (shape->ntris == 0) continue;
            TriangleMesh tm = shape->toTriangleMesh();
            bool compact = true;
            tracer_gid = sv->tracer.addGeometryMesh(
                tm, nullptr, compact, shape->flip_surface);
            break;
        }
        case OX_SHAPE_SPHERE: {
            /* Guard: sphere must have been initialized via s3d_sphere_setup */
            if (shape->sphere_radius <= 0.0f) continue;
            SphereMesh sm = shape->toSphereMesh();
            tracer_gid = sv->tracer.addGeometrySphere(
                sm, nullptr, shape->flip_surface);
            break;
        }
        case OX_SHAPE_INSTANCE:
            /* Instance shapes:
             * For now we flatten instances by recursively adding child scene
             * shapes with the instance transform. Full IAS-level instancing
             * is possible but not yet wired. */
            if (shape->child_scene) {
                /* Multi-level instancing not supported: reject if child scene
                 * contains instance shapes */
                for (auto& ck : shape->child_scene->shapes) {
                    if (ck.second->type == OX_SHAPE_INSTANCE)
                        return RES_BAD_ARG;
                }
                float row_xform[12];
                shape->getRowMajorTransform(row_xform);

                for (auto& ckv : shape->child_scene->shapes) {
                    s3d_shape* cs = ckv.second;
                    if (!cs->enabled) continue;

                    unsigned gid = S3D_INVALID_ID;
                    if (cs->type == OX_SHAPE_MESH) {
                        if (cs->ntris == 0) continue;
                        TriangleMesh tm = cs->toTriangleMesh();
                        gid = sv->tracer.addGeometryMesh(
                            tm, row_xform, true, cs->flip_surface);
                    } else if (cs->type == OX_SHAPE_SPHERE) {
                        if (cs->sphere_radius <= 0.0f) continue;
                        SphereMesh sm = cs->toSphereMesh();
                        gid = sv->tracer.addGeometrySphere(
                            sm, row_xform, cs->flip_surface);
                    } else {
                        continue;
                    }

                    if (gid != S3D_INVALID_ID) {
                        sv->shape_to_geom[cs->id]  = gid;
                        sv->geom_to_shape[gid]      = cs->id;
                        sv->geom_to_inst[gid]        = shape; /* track the instance */
                        cs->tracer_geom_id           = gid;

                        /* Snapshot child shape attributes */
                        sv->inst_child_snapshots[shape->id][cs->id] = { cs, cs->flip_surface, cs->enabled, cs->filter_func, cs->filter_data };
                    }
                }
            }
            /* Snapshot the instance shape itself */
            sv->shape_snapshots[shape->id] = { shape, shape->flip_surface, shape->enabled, shape->filter_func, shape->filter_data };
            continue; /* skip the mapping below — done inside the loop */
        }

        if (tracer_gid != S3D_INVALID_ID) {
            sv->shape_to_geom[shape->id] = tracer_gid;
            sv->geom_to_shape[tracer_gid] = shape->id;
            shape->tracer_geom_id = tracer_gid;

            /* Snapshot attributes at build time */
            sv->shape_snapshots[shape->id] = { shape, shape->flip_surface, shape->enabled, shape->filter_func, shape->filter_data };
        }
    }

    /* Build IAS (and set query mesh = same scene for CP) */
    bool geometry_added = !sv->geom_to_shape.empty();
    if (geometry_added) {
        sv->tracer.rebuildScene();
    }
    sv->has_geometry = geometry_added;

    /* Also build query mesh (closest-point uses same geometry) */
    /* Merge all mesh shapes (including instance children) into a single query mesh.
     * Also collect sphere entries for host-side analytical CP. */
    sv->query_prim_ranges.clear();
    sv->query_sphere_entries.clear();
    {
        TriangleMesh merged;
        unsigned vert_offset = 0;
        unsigned tri_offset  = 0;
        for (auto& kv : sv->scn->shapes) {
            s3d_shape* shape = kv.second;
            if (!shape->enabled) continue;

            if (shape->type == OX_SHAPE_MESH && shape->ntris > 0) {
                for (unsigned i = 0; i < shape->nverts; i++) {
                    merged.vertices.push_back(make_float3(
                        shape->positions[i*3+0],
                        shape->positions[i*3+1],
                        shape->positions[i*3+2]));
                }
                for (unsigned i = 0; i < shape->ntris; i++) {
                    merged.indices.push_back(make_uint3(
                        shape->indices[i*3+0] + vert_offset,
                        shape->indices[i*3+1] + vert_offset,
                        shape->indices[i*3+2] + vert_offset));
                }
                sv->query_prim_ranges.push_back({
                    tri_offset, shape->ntris, shape, nullptr,
                    shape->filter_func, shape->filter_data
                });
                vert_offset += shape->nverts;
                tri_offset  += shape->ntris;
            } else if (shape->type == OX_SHAPE_SPHERE) {
                if (shape->sphere_radius <= 0.0f) continue;
                s3d_scene_view::query_sphere_entry se;
                se.sphere_pos[0] = shape->sphere_pos[0];
                se.sphere_pos[1] = shape->sphere_pos[1];
                se.sphere_pos[2] = shape->sphere_pos[2];
                se.sphere_radius = shape->sphere_radius;
                se.flip_surface  = shape->flip_surface;
                se.shape         = shape;
                se.inst_shape    = nullptr;
                se.filter_func   = shape->filter_func;
                se.filter_data   = shape->filter_data;
                sv->query_sphere_entries.push_back(se);
            } else if (shape->type == OX_SHAPE_INSTANCE && shape->child_scene) {
                /* Flatten instance child meshes with transform applied */
                const float* t = shape->transform;
                for (auto& ckv : shape->child_scene->shapes) {
                    s3d_shape* cs = ckv.second;
                    if (!cs->enabled) continue;

                    if (cs->type == OX_SHAPE_MESH && cs->ntris > 0) {
                        for (unsigned i = 0; i < cs->nverts; i++) {
                            float px = cs->positions[i*3+0];
                            float py = cs->positions[i*3+1];
                            float pz = cs->positions[i*3+2];
                            /* Apply column-major 3x4 transform */
                            float tx = t[0]*px + t[3]*py + t[6]*pz + t[9];
                            float ty = t[1]*px + t[4]*py + t[7]*pz + t[10];
                            float tz = t[2]*px + t[5]*py + t[8]*pz + t[11];
                            merged.vertices.push_back(make_float3(tx, ty, tz));
                        }
                        for (unsigned i = 0; i < cs->ntris; i++) {
                            merged.indices.push_back(make_uint3(
                                cs->indices[i*3+0] + vert_offset,
                                cs->indices[i*3+1] + vert_offset,
                                cs->indices[i*3+2] + vert_offset));
                        }
                        sv->query_prim_ranges.push_back({
                            tri_offset, cs->ntris, cs, shape,
                            cs->filter_func, cs->filter_data
                        });
                        vert_offset += cs->nverts;
                        tri_offset  += cs->ntris;
                    } else if (cs->type == OX_SHAPE_SPHERE) {
                        if (cs->sphere_radius <= 0.0f) continue;
                        /* Transform sphere center to world space */
                        float cx = cs->sphere_pos[0], cy = cs->sphere_pos[1], cz = cs->sphere_pos[2];
                        s3d_scene_view::query_sphere_entry se;
                        se.sphere_pos[0] = t[0]*cx + t[3]*cy + t[6]*cz + t[9];
                        se.sphere_pos[1] = t[1]*cx + t[4]*cy + t[7]*cz + t[10];
                        se.sphere_pos[2] = t[2]*cx + t[5]*cy + t[8]*cz + t[11];
                        se.sphere_radius = cs->sphere_radius;
                        se.flip_surface  = cs->flip_surface != shape->flip_surface;
                        se.shape         = cs;
                        se.inst_shape    = shape;
                        se.filter_func   = cs->filter_func;
                        se.filter_data   = cs->filter_data;
                        sv->query_sphere_entries.push_back(se);
                    }
                }
            }
        }

        if (!merged.vertices.empty() && geometry_added) {
            /* Estimate search radius from AABB diagonal */
            float3 smin = sv->tracer.getSceneBBoxMin();
            float3 smax = sv->tracer.getSceneBBoxMax();
            float dx = smax.x - smin.x;
            float dy = smax.y - smin.y;
            float dz = smax.z - smin.z;
            float diag = sqrtf(dx*dx + dy*dy + dz*dz);
            sv->search_radius = diag * 2.0f;

            sv->tracer.setQueryMesh(merged, sv->search_radius);
            sv->query_mesh_set = true;
        }
    }

    /* Update scene AABB — compute from shapes, accounting for instance transforms */
    {
        float lo[3] = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
        float hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        bool has_any = false;

        for (auto& kv : sv->scn->shapes) {
            s3d_shape* shape = kv.second;
            if (!shape->enabled) continue;

            if (shape->type == OX_SHAPE_MESH) {
                for (unsigned i = 0; i < shape->nverts; i++) {
                    float px = shape->positions[i*3+0];
                    float py = shape->positions[i*3+1];
                    float pz = shape->positions[i*3+2];
                    if (px < lo[0]) lo[0] = px;
                    if (py < lo[1]) lo[1] = py;
                    if (pz < lo[2]) lo[2] = pz;
                    if (px > hi[0]) hi[0] = px;
                    if (py > hi[1]) hi[1] = py;
                    if (pz > hi[2]) hi[2] = pz;
                    has_any = true;
                }
            } else if (shape->type == OX_SHAPE_SPHERE) {
                if (shape->sphere_radius <= 0.0f) continue;
                float r = shape->sphere_radius;
                for (int d = 0; d < 3; d++) {
                    float v_lo = shape->sphere_pos[d] - r;
                    float v_hi = shape->sphere_pos[d] + r;
                    if (v_lo < lo[d]) lo[d] = v_lo;
                    if (v_hi > hi[d]) hi[d] = v_hi;
                }
                has_any = true;
            } else if (shape->type == OX_SHAPE_INSTANCE && shape->child_scene) {
                /* Compute child scene AABB, then transform by instance transform */
                float clo[3] = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
                float chi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
                bool child_any = false;

                for (auto& ckv : shape->child_scene->shapes) {
                    s3d_shape* cs = ckv.second;
                    if (!cs->enabled) continue;
                    if (cs->type == OX_SHAPE_MESH) {
                        for (unsigned i = 0; i < cs->nverts; i++) {
                            float px = cs->positions[i*3+0];
                            float py = cs->positions[i*3+1];
                            float pz = cs->positions[i*3+2];
                            if (px < clo[0]) clo[0] = px;
                            if (py < clo[1]) clo[1] = py;
                            if (pz < clo[2]) clo[2] = pz;
                            if (px > chi[0]) chi[0] = px;
                            if (py > chi[1]) chi[1] = py;
                            if (pz > chi[2]) chi[2] = pz;
                            child_any = true;
                        }
                    } else if (cs->type == OX_SHAPE_SPHERE) {
                        if (cs->sphere_radius <= 0.0f) continue;
                        float r = cs->sphere_radius;
                        for (int d = 0; d < 3; d++) {
                            float v_lo = cs->sphere_pos[d] - r;
                            float v_hi = cs->sphere_pos[d] + r;
                            if (v_lo < clo[d]) clo[d] = v_lo;
                            if (v_hi > chi[d]) chi[d] = v_hi;
                        }
                        child_any = true;
                    }
                }
                if (!child_any) continue;

                /* Transform all 8 AABB corners by the instance 3x4 column-major transform */
                const float* t = shape->transform;
                float corners[8][3];
                for (int c = 0; c < 8; c++) {
                    float cx = (c & 1) ? chi[0] : clo[0];
                    float cy = (c & 2) ? chi[1] : clo[1];
                    float cz = (c & 4) ? chi[2] : clo[2];
                    corners[c][0] = t[0]*cx + t[3]*cy + t[6]*cz + t[9];
                    corners[c][1] = t[1]*cx + t[4]*cy + t[7]*cz + t[10];
                    corners[c][2] = t[2]*cx + t[5]*cy + t[8]*cz + t[11];
                }
                for (int c = 0; c < 8; c++) {
                    for (int d = 0; d < 3; d++) {
                        if (corners[c][d] < lo[d]) lo[d] = corners[c][d];
                        if (corners[c][d] > hi[d]) hi[d] = corners[c][d];
                    }
                }
                has_any = true;
            }
        }

        if (has_any) {
            sv->lower[0] = lo[0]; sv->lower[1] = lo[1]; sv->lower[2] = lo[2];
            sv->upper[0] = hi[0]; sv->upper[1] = hi[1]; sv->upper[2] = hi[2];
        } else {
            /* Empty scene → degenerated AABB (lower > upper) */
            sv->lower[0] = FLT_MAX;  sv->lower[1] = FLT_MAX;  sv->lower[2] = FLT_MAX;
            sv->upper[0] = -FLT_MAX; sv->upper[1] = -FLT_MAX; sv->upper[2] = -FLT_MAX;
        }
    }

    sv->dirty    = false;
    sv->cdf_valid = false;
    return RES_OK;
}

/* ================================================================
 * Internal: build CDF for uniform surface sampling
 * ================================================================ */
static void build_cdf(s3d_scene_view* sv) {
    sv->prim_cdf.clear();
    sv->total_area = 0.0f;

    /* Use build-time snapshots so that runtime enable/disable changes
     * do not affect an already-built scene view. */
    for (auto& kv : sv->shape_snapshots) {
        const auto& snap = kv.second;
        if (!snap.enabled) continue;
        s3d_shape* shape = snap.shape;

        if (shape->type == OX_SHAPE_MESH) {
            for (unsigned i = 0; i < shape->ntris; i++) {
                unsigned i0 = shape->indices[i*3+0];
                unsigned i1 = shape->indices[i*3+1];
                unsigned i2 = shape->indices[i*3+2];
                float ax = shape->positions[i1*3+0] - shape->positions[i0*3+0];
                float ay = shape->positions[i1*3+1] - shape->positions[i0*3+1];
                float az = shape->positions[i1*3+2] - shape->positions[i0*3+2];
                float bx = shape->positions[i2*3+0] - shape->positions[i0*3+0];
                float by = shape->positions[i2*3+1] - shape->positions[i0*3+1];
                float bz = shape->positions[i2*3+2] - shape->positions[i0*3+2];
                float cx = ay * bz - az * by;
                float cy = az * bx - ax * bz;
                float cz = ax * by - ay * bx;
                float area = 0.5f * sqrtf(cx*cx + cy*cy + cz*cz);
                sv->total_area += area;
                sv->prim_cdf.push_back(sv->total_area);
            }
        } else if (shape->type == OX_SHAPE_SPHERE) {
            if (shape->sphere_radius <= 0.0f) continue;
            float area = 4.0f * 3.14159265358979323846f
                       * shape->sphere_radius * shape->sphere_radius;
            sv->total_area += area;
            sv->prim_cdf.push_back(sv->total_area);
        } else if (shape->type == OX_SHAPE_INSTANCE && shape->child_scene) {
            /* Flatten instance children into CDF using child snapshots */
            auto iit = sv->inst_child_snapshots.find(shape->id);
            if (iit != sv->inst_child_snapshots.end()) {
                for (auto& ckv : iit->second) {
                    const auto& csnap = ckv.second;
                    if (!csnap.enabled) continue;
                    s3d_shape* cs = csnap.shape;
                    if (cs->type == OX_SHAPE_MESH) {
                        for (unsigned i = 0; i < cs->ntris; i++) {
                            unsigned i0 = cs->indices[i*3+0];
                            unsigned i1 = cs->indices[i*3+1];
                            unsigned i2 = cs->indices[i*3+2];
                            float ax = cs->positions[i1*3+0] - cs->positions[i0*3+0];
                            float ay = cs->positions[i1*3+1] - cs->positions[i0*3+1];
                            float az = cs->positions[i1*3+2] - cs->positions[i0*3+2];
                            float bx = cs->positions[i2*3+0] - cs->positions[i0*3+0];
                            float by = cs->positions[i2*3+1] - cs->positions[i0*3+1];
                            float bz = cs->positions[i2*3+2] - cs->positions[i0*3+2];
                            float ccx = ay * bz - az * by;
                            float ccy = az * bx - ax * bz;
                            float ccz = ax * by - ay * bx;
                            float area = 0.5f * sqrtf(ccx*ccx + ccy*ccy + ccz*ccz);
                            sv->total_area += area;
                            sv->prim_cdf.push_back(sv->total_area);
                        }
                    } else if (cs->type == OX_SHAPE_SPHERE && cs->sphere_radius > 0.0f) {
                        float area = 4.0f * 3.14159265358979323846f
                                   * cs->sphere_radius * cs->sphere_radius;
                        sv->total_area += area;
                        sv->prim_cdf.push_back(sv->total_area);
                    }
                }
            }
        }
    }

    /* Normalize */
    if (sv->total_area > 0.0f) {
        for (auto& v : sv->prim_cdf) v /= sv->total_area;
    }
    sv->cdf_valid = true;
}

/* ================================================================
 * Internal: ensure tracer is up to date
 * ================================================================ */
static res_T ensure_built(s3d_scene_view* sv) {
    if (sv->dirty) return rebuild_tracer(sv);
    return RES_OK;
}

/* ================================================================
 * Create / Ref
 * ================================================================ */
res_T s3d_scene_view_create(s3d_scene* scn, int mask,
                             s3d_scene_view** out_view) {
    return s3d_scene_view_create2(scn, mask, nullptr, out_view);
}

res_T s3d_scene_view_create2(s3d_scene* scn, int mask,
                              const s3d_accel_struct_conf* /*cfg*/,
                              s3d_scene_view** out_view) {
    if (!scn || !out_view) return RES_BAD_ARG;
    *out_view = nullptr;

    s3d_scene_view* sv = new (std::nothrow) s3d_scene_view();
    if (!sv) return RES_MEM_ERR;

    sv->scn  = scn;
    sv->mask = mask;
    s3d_scene_ref_get(scn);

    /* Register this view with the scene */
    scn->views.push_back(sv);

    /* Build immediately so subsequent queries work */
    res_T rc = rebuild_tracer(sv);
    if (rc != RES_OK) {
        s3d_scene_ref_put(scn);
        delete sv;
        return rc;
    }

    *out_view = sv;
    return RES_OK;
}

res_T s3d_scene_view_ref_get(s3d_scene_view* sv) {
    if (!sv) return RES_BAD_ARG;
    sv->ref++;
    return RES_OK;
}

res_T s3d_scene_view_ref_put(s3d_scene_view* sv) {
    if (!sv) return RES_BAD_ARG;
    if (sv->ref == 0) return RES_BAD_ARG;
    if (--sv->ref == 0) {
        /* Unregister from scene */
        if (sv->scn) {
            auto& v = sv->scn->views;
            v.erase(std::remove(v.begin(), v.end(), sv), v.end());
            s3d_scene_ref_put(sv->scn);
        }
        sv->tracer.cleanup();
        sv->shape_snapshots.clear();
        sv->inst_child_snapshots.clear();
        delete sv;
    }
    return RES_OK;
}

res_T s3d_scene_view_get_mask(s3d_scene_view* sv, int* mask) {
    if (!sv || !mask) return RES_BAD_ARG;
    *mask = sv->mask;
    return RES_OK;
}

/* ================================================================
 * Single-ray trace
 * ================================================================ */
res_T s3d_scene_view_trace_ray(s3d_scene_view* sv,
                                const float origin[3],
                                const float direction[3],
                                const float range[2],
                                void* ray_data,
                                s3d_hit* hit)
{
    if (!sv || !origin || !direction || !range || !hit) return RES_BAD_ARG;
    if (!(sv->mask & S3D_TRACE)) return RES_BAD_OP;

    /* Validate direction is a unit vector */
    {
        float len2 = direction[0]*direction[0] + direction[1]*direction[1] + direction[2]*direction[2];
        if (fabsf(len2 - 1.0f) > 1.0e-3f) return RES_BAD_ARG;
    }

    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    *hit = S3D_HIT_NULL;

    /* Empty scene → guaranteed miss */
    if (!sv->has_geometry) return RES_OK;

    if (range[0] >= range[1]) return RES_OK; /* Degenerate range */

    Ray ray;
    ray.origin    = make_float3(origin[0], origin[1], origin[2]);
    ray.direction = make_float3(direction[0], direction[1], direction[2]);
    ray.tmin      = range[0];
    ray.tmax      = range[1];

    HitResult hr = sv->tracer.traceSingle(ray);
    if (hr.t < 0.0f) return RES_OK; /* miss */

    /* Resolve shape */
    unsigned shape_id;
    s3d_shape* shape = resolve_shape(sv, hr.geom_id, shape_id);
    s3d_shape* inst  = resolve_instance(sv, hr.geom_id);

    /* Apply filter if present — use BUILD-TIME snapshot, not live value */
    s3d_hit_filter_function_T filt = nullptr;
    void* filt_data = nullptr;
    {
        auto snap = sv->find_snapshot(shape_id);
        if (snap) {
            filt = snap->filter_func;
            filt_data = snap->filter_data;
        }
    }
    if (shape && filt) {
        hitresult_to_s3d_hit(sv, hr, shape, shape_id, hr.prim_idx, hit, inst);
        int rej = filt(hit, origin, direction, range, ray_data, filt_data);
        if (rej != 0) {
            /* Filter rejected — retry by advancing tmin past this hit */
            float retry_tmin = hr.t + 1e-6f;
            bool accepted = false;
            while (retry_tmin < ray.tmax) {
                ray.tmin = retry_tmin;
                hr = sv->tracer.traceSingle(ray);
                if (hr.t < 0.0f) break; /* no more hits */

                shape = resolve_shape(sv, hr.geom_id, shape_id);
                inst  = resolve_instance(sv, hr.geom_id);

                filt = nullptr; filt_data = nullptr;
                {
                    auto snap2 = sv->find_snapshot(shape_id);
                    if (snap2) {
                        filt = snap2->filter_func;
                        filt_data = snap2->filter_data;
                    }
                }

                hitresult_to_s3d_hit(sv, hr, shape, shape_id, hr.prim_idx, hit, inst);
                if (shape && filt) {
                    rej = filt(hit, origin, direction, range, ray_data, filt_data);
                    if (rej != 0) {
                        retry_tmin = hr.t + 1e-6f;
                        continue;
                    }
                }
                accepted = true;
                break;
            }
            if (!accepted) {
                *hit = S3D_HIT_NULL;
            }
            return RES_OK;
        }
    } else {
        hitresult_to_s3d_hit(sv, hr, shape, shape_id, hr.prim_idx, hit, inst);
    }

    return RES_OK;
}

/* ================================================================
 * Multi-ray trace (non-batch, sequential host dispatch)
 * ================================================================ */
res_T s3d_scene_view_trace_rays(s3d_scene_view* sv,
                                 size_t nrays, int /*mask*/,
                                 const float* origins,
                                 const float* directions,
                                 const float* ranges,
                                 void* rays_data,
                                 size_t sizeof_ray_data,
                                 s3d_hit* hits)
{
    if (!sv || !origins || !directions || !ranges || !hits) return RES_BAD_ARG;
    if (!(sv->mask & S3D_TRACE)) return RES_BAD_OP;
    if (nrays == 0) return RES_OK;

    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    /* Empty scene → all misses */
    if (!sv->has_geometry) {
        for (size_t i = 0; i < nrays; i++) hits[i] = S3D_HIT_NULL;
        return RES_OK;
    }

    /* Build ray buffer */
    std::vector<Ray> rays(nrays);
    for (size_t i = 0; i < nrays; i++) {
        rays[i].origin    = make_float3(origins[i*3+0], origins[i*3+1], origins[i*3+2]);
        rays[i].direction = make_float3(directions[i*3+0], directions[i*3+1], directions[i*3+2]);
        rays[i].tmin      = ranges[i*2+0];
        rays[i].tmax      = ranges[i*2+1];
    }

    /* Launch batch trace on GPU */
    std::vector<HitResult> results = sv->tracer.traceBatch(rays);

    /* Post-process: UV fixup + filter */
    for (size_t i = 0; i < nrays; i++) {
        const HitResult& hr = results[i];
        if (hr.t < 0.0f) {
            hits[i] = S3D_HIT_NULL;
            continue;
        }

        unsigned shape_id;
        s3d_shape* shape = resolve_shape(sv, hr.geom_id, shape_id);
        s3d_shape* inst  = resolve_instance(sv, hr.geom_id);
        hitresult_to_s3d_hit(sv, hr, shape, shape_id, hr.prim_idx, &hits[i], inst);

        /* Apply per-shape filter — use build-time snapshot */
        {
            s3d_hit_filter_function_T filt = nullptr;
            void* filt_data = nullptr;
            {
                auto snap = sv->find_snapshot(shape_id);
                if (snap) {
                    filt = snap->filter_func;
                    filt_data = snap->filter_data;
                }
            }
            if (shape && filt) {
                const float orig[3] = { origins[i*3+0], origins[i*3+1], origins[i*3+2] };
                const float dir[3]  = { directions[i*3+0], directions[i*3+1], directions[i*3+2] };
                const float rng[2]  = { ranges[i*2+0], ranges[i*2+1] };
                void* rd = nullptr;
                if (rays_data && sizeof_ray_data > 0)
                    rd = reinterpret_cast<char*>(rays_data) + i * sizeof_ray_data;
                int rej = filt(&hits[i], orig, dir, rng, rd, filt_data);
                if (rej != 0) {
                    /* Filter rejected — retry with single-ray advancing tmin */
                    Ray retry_ray = rays[i];
                    float retry_tmin = results[i].t + 1e-6f;
                    bool accepted = false;
                    while (retry_tmin < retry_ray.tmax) {
                        retry_ray.tmin = retry_tmin;
                        HitResult rhr = sv->tracer.traceSingle(retry_ray);
                        if (rhr.t < 0.0f) break;

                        unsigned rsid;
                        s3d_shape* rshape = resolve_shape(sv, rhr.geom_id, rsid);
                        s3d_shape* rinst  = resolve_instance(sv, rhr.geom_id);
                        hitresult_to_s3d_hit(sv, rhr, rshape, rsid, rhr.prim_idx, &hits[i], rinst);

                        s3d_hit_filter_function_T rf = nullptr;
                        void* rfd = nullptr;
                        {
                            auto rsnap = sv->find_snapshot(rsid);
                            if (rsnap) {
                                rf = rsnap->filter_func;
                                rfd = rsnap->filter_data;
                            }
                        }
                        if (rshape && rf) {
                            rej = rf(&hits[i], orig, dir, rng, rd, rfd);
                            if (rej != 0) {
                                retry_tmin = rhr.t + 1e-6f;
                                continue;
                            }
                        }
                        accepted = true;
                        break;
                    }
                    if (!accepted) hits[i] = S3D_HIT_NULL;
                }
            }
        }
    }

    return RES_OK;
}

/* ================================================================
 * Batch Trace (GPU multi-hit + CPU filter + retrace fallback)
 * ================================================================ */
static res_T batch_trace_impl(s3d_scene_view* sv,
                               const s3d_ray_request* requests,
                               size_t nrays,
                               s3d_hit* hits,
                               s3d_batch_trace_stats* stats)
{
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    if (stats) memset(stats, 0, sizeof(*stats));
    if (stats) stats->total_rays = nrays;

    /* Empty scene → all misses */
    if (!sv->has_geometry) {
        for (size_t i = 0; i < nrays; i++) hits[i] = S3D_HIT_NULL;
        if (stats) stats->batch_accepted = nrays;
        return RES_OK;
    }

    double t0 = now_ms();

    /* ---- Phase 1: GPU multi-hit batch ---- */
    std::vector<Ray> rays(nrays);
    for (size_t i = 0; i < nrays; i++) {
        rays[i].origin    = make_float3(requests[i].origin[0],
                                         requests[i].origin[1],
                                         requests[i].origin[2]);
        rays[i].direction = make_float3(requests[i].direction[0],
                                         requests[i].direction[1],
                                         requests[i].direction[2]);
        rays[i].tmin      = requests[i].range[0];
        rays[i].tmax      = requests[i].range[1];
    }

    std::vector<MultiHitResult> mhits = sv->tracer.traceBatchMultiHit(rays);

    double t1 = now_ms();
    if (stats) stats->batch_time_ms = t1 - t0;

    /* ---- Phase 2: CPU post-process (filter + UV fixup) ---- */
    std::vector<size_t> retrace_list;

    for (size_t i = 0; i < nrays; i++) {
        const MultiHitResult& mh = mhits[i];
        bool accepted = false;
        bool had_candidates = false;
        bool filter_rejected_any = false;

        for (unsigned k = 0; k < mh.count; k++) {
            const HitResult& cand = mh.hits[k];
            if (cand.t < 0.0f) continue;
            had_candidates = true;

            unsigned shape_id;
            s3d_shape* shape = resolve_shape(sv, cand.geom_id, shape_id);
            s3d_shape* inst  = resolve_instance(sv, cand.geom_id);

            s3d_hit h;
            hitresult_to_s3d_hit(sv, cand, shape, shape_id, cand.prim_idx, &h, inst);

            /* Test filter — use build-time snapshot */
            {
                s3d_hit_filter_function_T filt = nullptr;
                void* filt_data_snap = nullptr;
                {
                    auto snap = sv->find_snapshot(shape_id);
                    if (snap) {
                        filt = snap->filter_func;
                        filt_data_snap = snap->filter_data;
                    }
                }
                if (shape && filt) {
                    void* fdata = requests[i].filter_data;
                    int rej = filt(&h,
                        requests[i].origin, requests[i].direction,
                        requests[i].range, fdata, filt_data_snap);
                    if (rej != 0) {
                        filter_rejected_any = true;
                        continue; /* Try next candidate */
                    }
                }
            }

            hits[i] = h;
            accepted = true;
            if (stats) stats->batch_accepted++;
            break;
        }

        if (!accepted) {
            hits[i] = S3D_HIT_NULL;
            if (had_candidates && filter_rejected_any) {
                /* All candidates were filter-rejected — retrace needed */
                if (stats) stats->filter_rejected++;
                retrace_list.push_back(i);
            } else {
                /* Genuine miss (no candidates) — counts as accepted */
                if (stats) stats->batch_accepted++;
            }
        }
    }

    double t2 = now_ms();
    if (stats) stats->postprocess_time_ms = t2 - t1;

    /* ---- Phase 3: Retrace fallback (single-hit per rejected ray) ---- */
    if (!retrace_list.empty()) {
        for (size_t idx : retrace_list) {
            HitResult hr = sv->tracer.traceSingle(rays[idx]);
            if (hr.t < 0.0f) {
                if (stats) stats->retrace_missed++;
                continue;
            }

            unsigned shape_id;
            s3d_shape* shape = resolve_shape(sv, hr.geom_id, shape_id);
            s3d_shape* inst  = resolve_instance(sv, hr.geom_id);
            hitresult_to_s3d_hit(sv, hr, shape, shape_id, hr.prim_idx, &hits[idx], inst);
            if (stats) stats->retrace_accepted++;
        }
    }

    double t3 = now_ms();
    if (stats) stats->retrace_time_ms = t3 - t2;

    return RES_OK;
}

res_T s3d_scene_view_trace_rays_batch(s3d_scene_view* sv,
                                       const s3d_ray_request* requests,
                                       size_t nrays,
                                       s3d_hit* hits,
                                       s3d_batch_trace_stats* stats)
{
    if (!sv || !requests || !hits) return RES_BAD_ARG;
    if (nrays == 0) return RES_OK;
    return batch_trace_impl(sv, requests, nrays, hits, stats);
}

res_T s3d_scene_view_trace_rays_batch_ctx(s3d_scene_view* sv,
                                           s3d_batch_trace_context* /*ctx*/,
                                           const s3d_ray_request* requests,
                                           size_t nrays,
                                           s3d_hit* hits,
                                           s3d_batch_trace_stats* stats)
{
    /* Context-based version delegates to the same implementation.
     * The UnifiedTracer already manages its own device buffers,
     * so the context is a lightweight pass-through for now. */
    if (!sv || !requests || !hits) return RES_BAD_ARG;
    if (nrays == 0) return RES_OK;
    return batch_trace_impl(sv, requests, nrays, hits, stats);
}

/* ================================================================
 * Single closest-point query
 * ================================================================ */
res_T s3d_scene_view_closest_point(s3d_scene_view* sv,
                                    const float pos[3],
                                    float radius,
                                    void* query_data,
                                    s3d_hit* hit)
{
    if (!sv || !pos || !hit) return RES_BAD_ARG;
    if (radius <= 0.0f) return RES_BAD_ARG;
    if (!(sv->mask & S3D_TRACE)) return RES_BAD_OP;
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    *hit = S3D_HIT_NULL;

    bool have_mesh    = sv->query_mesh_set;
    bool have_spheres = !sv->query_sphere_entries.empty();
    if (!have_mesh && !have_spheres) return RES_OK;

    /* ---- GPU mesh closest point (if any triangles in query mesh) ---- */
    s3d_hit mesh_hit = S3D_HIT_NULL;
    s3d_hit_filter_function_T mesh_filter_func = nullptr;
    void* mesh_filter_data = nullptr;

    if (have_mesh) {
        /* Expand search radius if the requested radius exceeds current coverage.
         * This ensures queries with radius=INF (or very large) can reach all
         * primitives regardless of their distance from the query point. */
        {
            float eff = radius;
            if (!std::isfinite(eff) || eff > 1e30f)
                eff = 1e30f;
            if (eff > sv->search_radius) {
                sv->search_radius = eff;
                sv->tracer.setSearchRadius(sv->search_radius);
                sv->tracer.rebuildQueryGAS(/*compact=*/true);
            }
        }

        CPQuery q;
        q.position = make_float3(pos[0], pos[1], pos[2]);
        q.radius   = radius;

        std::vector<CPQuery> queries = { q };
        std::vector<CPResult> results = sv->tracer.closestPointBatch(queries);

        if (!results.empty() && results[0].distance >= 0.0f) {
            const CPResult& c = results[0];

            /* Resolve shape via query_prim_ranges table (built during rebuild_tracer) */
            unsigned shape_id = S3D_INVALID_ID;
            unsigned local_prim = c.prim_idx;
            s3d_shape* shape = nullptr;
            s3d_shape* inst_shape = nullptr;

            for (auto& pr : sv->query_prim_ranges) {
                if (c.prim_idx >= pr.prim_offset &&
                    c.prim_idx < pr.prim_offset + pr.prim_count) {
                    local_prim = c.prim_idx - pr.prim_offset;
                    shape      = pr.shape;
                    inst_shape = pr.inst_shape;
                    if (shape) shape_id = shape->id;
                    /* Capture build-time filter snapshot */
                    mesh_filter_func = pr.filter_func;
                    mesh_filter_data = pr.filter_data;
                    break;
                }
            }

            mesh_hit.prim.prim_id       = local_prim;
            mesh_hit.prim.geom_id       = shape_id;
            mesh_hit.prim.inst_id       = inst_shape
                ? (inst_shape->id != S3D_INVALID_ID ? inst_shape->id : S3D_INVALID_ID)
                : S3D_INVALID_ID;
            mesh_hit.prim.scene_prim_id = c.prim_idx;
            mesh_hit.prim.shape__       = shape;
            mesh_hit.prim.inst__        = inst_shape;
            mesh_hit.normal[0] = c.normal[0];
            mesh_hit.normal[1] = c.normal[1];
            mesh_hit.normal[2] = c.normal[2];
            /* Recover high-precision barycentrics.
             * For instanced shapes the GPU closest_pos is world-space but
             * shape->positions are local-space.  Instead of inverse-transforming
             * closest_pos (lossy float round-trip), inverse-transform the
             * *query point* and run closestPointOnTriHost in local space so
             * we get authoritative double-precision barycentrics directly. */
            if (shape && shape->type == OX_SHAPE_MESH && !shape->positions.empty() && !shape->indices.empty()) {
                if (inst_shape) {
                    /* Fix B: local-space closestPointOnTriHost for instanced meshes */
                    double qp[3] = { (double)pos[0], (double)pos[1], (double)pos[2] };
                    double ql[3];
                    inverse_transform_point_d(inst_shape->transform, qp, ql);
                    unsigned i0 = shape->indices[local_prim*3+0];
                    unsigned i1 = shape->indices[local_prim*3+1];
                    unsigned i2 = shape->indices[local_prim*3+2];
                    double v0d[3], v1d[3], v2d[3];
                    for (int k = 0; k < 3; k++) {
                        v0d[k] = (double)shape->positions[i0*3+k];
                        v1d[k] = (double)shape->positions[i1*3+k];
                        v2d[k] = (double)shape->positions[i2*3+k];
                    }
                    double proj[3], w_d, u_d;
                    closestPointOnTriHost(ql, v0d, v1d, v2d, proj, w_d, u_d);
                    mesh_hit.uv[0] = (float)w_d;
                    mesh_hit.uv[1] = (float)u_d;
                } else {
                    /* Non-instanced: backcompute from GPU closest_pos */
                    float cp_local[3] = { c.closest_pos[0], c.closest_pos[1], c.closest_pos[2] };
                    backcompute_bary_double(shape->positions.data(), shape->indices.data(), local_prim,
                                            cp_local, c.uv[0], c.uv[1],
                                            mesh_hit.uv[0], mesh_hit.uv[1]);
                }
            } else {
                mesh_hit.uv[0] = c.uv[0];
                mesh_hit.uv[1] = c.uv[1];
            }
            mesh_hit.distance = c.distance;

            /* Apply build-time filter snapshot */
            if (mesh_filter_func) {
                float dir_to_hit[3] = {
                    c.closest_pos[0] - pos[0],
                    c.closest_pos[1] - pos[1],
                    c.closest_pos[2] - pos[2]
                };
                float rng[2] = { 0.0f, radius };
                int rej = mesh_filter_func(&mesh_hit, pos, dir_to_hit, rng,
                                            query_data, mesh_filter_data);
                if (rej != 0) {
                    /* Filter rejected the GPU-found closest primitive.
                     * Retry per-shape on the host (mirrors Embree's continued
                     * BVH traversal after filter rejection). */
                    cp_filter_retry(sv, pos, radius, query_data, &mesh_hit);
                }
            }
        }
    }

    /* ---- Host-side analytical sphere closest point ---- */
    s3d_hit sphere_hit = S3D_HIT_NULL;
    if (have_spheres) {
        float sphere_radius_bound = radius;
        cp_try_all_spheres(sv, pos, &sphere_radius_bound, radius, query_data, &sphere_hit);
    }

    /* ---- Pick the closer of mesh and sphere hits ---- */
    if (!S3D_HIT_NONE(&mesh_hit) && !S3D_HIT_NONE(&sphere_hit)) {
        *hit = (mesh_hit.distance <= sphere_hit.distance) ? mesh_hit : sphere_hit;
    } else if (!S3D_HIT_NONE(&mesh_hit)) {
        *hit = mesh_hit;
    } else if (!S3D_HIT_NONE(&sphere_hit)) {
        *hit = sphere_hit;
    }

    return RES_OK;
}

/* ================================================================
 * Batch closest-point
 * ================================================================ */
static res_T batch_cp_impl(s3d_scene_view* sv,
                            const s3d_cp_request* requests,
                            size_t nqueries,
                            s3d_hit* hits,
                            s3d_batch_cp_stats* stats)
{
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    if (stats) memset(stats, 0, sizeof(*stats));
    if (stats) stats->total_queries = nqueries;

    bool have_mesh    = sv->query_mesh_set;
    bool have_spheres = !sv->query_sphere_entries.empty();

    if (!have_mesh && !have_spheres) {
        for (size_t i = 0; i < nqueries; i++) hits[i] = S3D_HIT_NULL;
        if (stats) stats->batch_accepted = nqueries;
        return RES_OK;
    }

    double t0 = now_ms();

    /* ---- GPU mesh closest-point batch ---- */
    std::vector<CPResult> results;
    if (have_mesh) {
        std::vector<CPQuery> queries(nqueries);
        float max_radius = 0.0f;
        for (size_t i = 0; i < nqueries; i++) {
            queries[i].position = make_float3(requests[i].pos[0],
                                               requests[i].pos[1],
                                               requests[i].pos[2]);
            queries[i].radius   = requests[i].radius;
            float r = requests[i].radius;
            if (!std::isfinite(r) || r > 1e30f) r = 1e30f;
            if (r > max_radius) max_radius = r;
        }

        if (max_radius > sv->search_radius) {
            sv->search_radius = max_radius;
            sv->tracer.setSearchRadius(sv->search_radius);
            sv->tracer.rebuildQueryGAS(/*compact=*/true);
        }

        results = sv->tracer.closestPointBatch(queries);
    }

    double t1 = now_ms();
    if (stats) stats->batch_time_ms = t1 - t0;

    /* Post-process each query */
    for (size_t i = 0; i < nqueries; i++) {
        /* ---- Mesh hit from GPU ---- */
        s3d_hit mesh_hit = S3D_HIT_NULL;
        s3d_hit_filter_function_T mf_func = nullptr;
        void* mf_data = nullptr;

        if (have_mesh && i < results.size()) {
            const CPResult& c = results[i];
            if (c.distance >= 0.0f) {
                unsigned shape_id = S3D_INVALID_ID;
                unsigned local_prim = c.prim_idx;
                s3d_shape* shape = nullptr;
                s3d_shape* inst_shape = nullptr;

                for (auto& pr : sv->query_prim_ranges) {
                    if (c.prim_idx >= pr.prim_offset &&
                        c.prim_idx < pr.prim_offset + pr.prim_count) {
                        local_prim = c.prim_idx - pr.prim_offset;
                        shape      = pr.shape;
                        inst_shape = pr.inst_shape;
                        if (shape) shape_id = shape->id;
                        mf_func = pr.filter_func;
                        mf_data = pr.filter_data;
                        break;
                    }
                }

                mesh_hit.prim.prim_id       = local_prim;
                mesh_hit.prim.geom_id       = shape_id;
                mesh_hit.prim.inst_id       = inst_shape
                    ? (inst_shape->id != S3D_INVALID_ID ? inst_shape->id : S3D_INVALID_ID)
                    : S3D_INVALID_ID;
                mesh_hit.prim.scene_prim_id = c.prim_idx;
                mesh_hit.prim.shape__       = shape;
                mesh_hit.prim.inst__        = inst_shape;
                mesh_hit.normal[0] = c.normal[0];
                mesh_hit.normal[1] = c.normal[1];
                mesh_hit.normal[2] = c.normal[2];
                if (shape && shape->type == OX_SHAPE_MESH && !shape->positions.empty() && !shape->indices.empty()) {
                    if (inst_shape) {
                        /* Fix B (batch): local-space closestPointOnTriHost */
                        double qp[3] = { (double)requests[i].pos[0], (double)requests[i].pos[1], (double)requests[i].pos[2] };
                        double ql[3];
                        inverse_transform_point_d(inst_shape->transform, qp, ql);
                        unsigned i0 = shape->indices[local_prim*3+0];
                        unsigned i1 = shape->indices[local_prim*3+1];
                        unsigned i2 = shape->indices[local_prim*3+2];
                        double v0d[3], v1d[3], v2d[3];
                        for (int k = 0; k < 3; k++) {
                            v0d[k] = (double)shape->positions[i0*3+k];
                            v1d[k] = (double)shape->positions[i1*3+k];
                            v2d[k] = (double)shape->positions[i2*3+k];
                        }
                        double proj[3], w_d, u_d;
                        closestPointOnTriHost(ql, v0d, v1d, v2d, proj, w_d, u_d);
                        mesh_hit.uv[0] = (float)w_d;
                        mesh_hit.uv[1] = (float)u_d;
                    } else {
                        float cp_local[3] = { c.closest_pos[0], c.closest_pos[1], c.closest_pos[2] };
                        backcompute_bary_double(shape->positions.data(), shape->indices.data(), local_prim,
                                                cp_local, c.uv[0], c.uv[1],
                                                mesh_hit.uv[0], mesh_hit.uv[1]);
                    }
                } else {
                    mesh_hit.uv[0] = c.uv[0];
                    mesh_hit.uv[1] = c.uv[1];
                }
                mesh_hit.distance = c.distance;

                /* Apply build-time filter snapshot */
                if (mf_func) {
                    float dir[3] = {
                        c.closest_pos[0] - requests[i].pos[0],
                        c.closest_pos[1] - requests[i].pos[1],
                        c.closest_pos[2] - requests[i].pos[2]
                    };
                    float rng[2] = { 0.0f, requests[i].radius };
                    int rej = mf_func(&mesh_hit, requests[i].pos, dir, rng,
                                       requests[i].query_data, mf_data);
                    if (rej != 0) {
                        cp_filter_retry(sv, requests[i].pos, requests[i].radius,
                                        requests[i].query_data, &mesh_hit);
                    }
                }
            }
        }

        /* ---- Sphere hit (host-side analytical) ---- */
        s3d_hit sphere_hit = S3D_HIT_NULL;
        if (have_spheres) {
            float sph_radius = requests[i].radius;
            cp_try_all_spheres(sv, requests[i].pos, &sph_radius,
                               requests[i].radius, requests[i].query_data, &sphere_hit);
        }

        /* ---- Pick closer ---- */
        if (!S3D_HIT_NONE(&mesh_hit) && !S3D_HIT_NONE(&sphere_hit)) {
            hits[i] = (mesh_hit.distance <= sphere_hit.distance) ? mesh_hit : sphere_hit;
        } else if (!S3D_HIT_NONE(&mesh_hit)) {
            hits[i] = mesh_hit;
        } else if (!S3D_HIT_NONE(&sphere_hit)) {
            hits[i] = sphere_hit;
        } else {
            hits[i] = S3D_HIT_NULL;
            if (stats) stats->batch_accepted++;
            continue;
        }

        if (stats) stats->batch_accepted++;
    }

    double t2 = now_ms();
    if (stats) stats->postprocess_time_ms = t2 - t1;

    return RES_OK;
}

res_T s3d_scene_view_closest_point_batch(s3d_scene_view* sv,
                                          const s3d_cp_request* requests,
                                          size_t nqueries,
                                          s3d_hit* hits,
                                          s3d_batch_cp_stats* stats)
{
    if (!sv || !requests || !hits) return RES_BAD_ARG;
    if (nqueries == 0) return RES_OK;
    return batch_cp_impl(sv, requests, nqueries, hits, stats);
}

res_T s3d_scene_view_closest_point_batch_ctx(s3d_scene_view* sv,
                                              s3d_batch_cp_context* /*ctx*/,
                                              const s3d_cp_request* requests,
                                              size_t nqueries,
                                              s3d_hit* hits,
                                              s3d_batch_cp_stats* stats)
{
    if (!sv || !requests || !hits) return RES_BAD_ARG;
    if (nqueries == 0) return RES_OK;
    return batch_cp_impl(sv, requests, nqueries, hits, stats);
}

/* ================================================================
 * Batch enclosure locate
 * ================================================================ */
static res_T batch_enc_impl(s3d_scene_view* sv,
                             const s3d_enc_locate_request* requests,
                             size_t nqueries,
                             s3d_enc_locate_result* results,
                             s3d_batch_enc_stats* stats)
{
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    if (stats) memset(stats, 0, sizeof(*stats));
    if (stats) stats->total_queries = nqueries;

    double t0 = now_ms();

    std::vector<EnclosureQuery> eq(nqueries);
    for (size_t i = 0; i < nqueries; i++) {
        eq[i].position = make_float3(requests[i].pos[0],
                                      requests[i].pos[1],
                                      requests[i].pos[2]);
    }

    std::vector<EnclosureResult> er = sv->tracer.findEnclosureBatch(eq);

    double t1 = now_ms();
    if (stats) stats->batch_time_ms = t1 - t0;

    /* Convert results */
    for (size_t i = 0; i < nqueries; i++) {
        results[i].prim_id  = er[i].prim_idx;
        results[i].distance = er[i].distance;
        results[i].side     = er[i].side;
        results[i].enc_id   = S3D_INVALID_ID; /* Solver resolves via prim_props */

        if (er[i].prim_idx >= 0) {
            if (stats) stats->resolved++;
        } else if (er[i].side == -1) {
            if (stats) stats->degenerate++;
        } else {
            if (stats) stats->missed++;
        }
    }

    double t2 = now_ms();
    if (stats) stats->postprocess_time_ms = t2 - t1;

    return RES_OK;
}

res_T s3d_scene_view_find_enclosure_batch(s3d_scene_view* sv,
                                           const s3d_enc_locate_request* requests,
                                           size_t nqueries,
                                           s3d_enc_locate_result* results,
                                           s3d_batch_enc_stats* stats)
{
    if (!sv || !requests || !results) return RES_BAD_ARG;
    if (nqueries == 0) return RES_OK;
    return batch_enc_impl(sv, requests, nqueries, results, stats);
}

res_T s3d_scene_view_find_enclosure_batch_ctx(s3d_scene_view* sv,
                                               s3d_batch_enc_context* /*ctx*/,
                                               const s3d_enc_locate_request* requests,
                                               size_t nqueries,
                                               s3d_enc_locate_result* results,
                                               s3d_batch_enc_stats* stats)
{
    if (!sv || !requests || !results) return RES_BAD_ARG;
    if (nqueries == 0) return RES_OK;
    return batch_enc_impl(sv, requests, nqueries, results, stats);
}

/* ================================================================
 * Batch Context Lifecycle
 * ================================================================ */
res_T s3d_batch_trace_context_create(s3d_batch_trace_context** ctx,
                                      size_t max_rays) {
    if (!ctx) return RES_BAD_ARG;
    *ctx = new (std::nothrow) s3d_batch_trace_context(max_rays);
    return *ctx ? RES_OK : RES_MEM_ERR;
}

void s3d_batch_trace_context_destroy(s3d_batch_trace_context* ctx) {
    delete ctx;
}

res_T s3d_batch_cp_context_create(s3d_batch_cp_context** ctx,
                                   size_t max_queries) {
    if (!ctx) return RES_BAD_ARG;
    *ctx = new (std::nothrow) s3d_batch_cp_context(max_queries);
    return *ctx ? RES_OK : RES_MEM_ERR;
}

void s3d_batch_cp_context_destroy(s3d_batch_cp_context* ctx) {
    delete ctx;
}

res_T s3d_batch_enc_context_create(s3d_batch_enc_context** ctx,
                                    size_t max_queries) {
    if (!ctx) return RES_BAD_ARG;
    *ctx = new (std::nothrow) s3d_batch_enc_context(max_queries);
    return *ctx ? RES_OK : RES_MEM_ERR;
}

void s3d_batch_enc_context_destroy(s3d_batch_enc_context* ctx) {
    delete ctx;
}

/* ================================================================
 * Uniform surface sampling
 * ================================================================ */
res_T s3d_scene_view_sample(s3d_scene_view* sv,
                             float u, float v, float w,
                             s3d_primitive* prim, float st[2])
{
    if (!sv || !prim || !st) return RES_BAD_ARG;
    if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f || w < 0.0f || w >= 1.0f)
        return RES_BAD_ARG;
    if (!(sv->mask & S3D_SAMPLE)) return RES_BAD_OP;
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    if (!sv->cdf_valid) build_cdf(sv);
    if (sv->prim_cdf.empty()) {
        /* Empty scene — return NULL primitive */
        *prim = S3D_PRIMITIVE_NULL;
        st[0] = 0.0f;
        st[1] = 0.0f;
        return RES_OK;
    }

    /* Binary search CDF for the primitive */
    auto it = std::lower_bound(sv->prim_cdf.begin(), sv->prim_cdf.end(), u);
    size_t flat_idx = (size_t)(it - sv->prim_cdf.begin());
    if (flat_idx >= sv->prim_cdf.size())
        flat_idx = sv->prim_cdf.size() - 1;

    /* Determine which shape and primitive this flat index belongs to.
     * Use build-time snapshots for enabled state, matching build_cdf. */
    size_t running = 0;
    for (auto& kv : sv->shape_snapshots) {
        const auto& snap = kv.second;
        if (!snap.enabled) continue;
        s3d_shape* shape = snap.shape;

        if (shape->type == OX_SHAPE_MESH) {
            size_t count = shape->ntris;
            if (count == 0) continue;
            if (flat_idx < running + count) {
                unsigned local = (unsigned)(flat_idx - running);
                prim->prim_id       = local;
                prim->geom_id       = shape->id;
                prim->inst_id       = S3D_INVALID_ID;
                prim->scene_prim_id = (unsigned)flat_idx;
                prim->shape__       = shape;
                prim->inst__        = nullptr;

                /* Uniform sample on triangle: (v, w) → barycentric (s, t) */
                float sq = sqrtf(v);
                st[0] = 1.0f - sq;
                st[1] = w * sq;
                return RES_OK;
            }
            running += count;
        } else if (shape->type == OX_SHAPE_SPHERE && shape->sphere_radius > 0.0f) {
            if (flat_idx < running + 1) {
                prim->prim_id       = 0;
                prim->geom_id       = shape->id;
                prim->inst_id       = S3D_INVALID_ID;
                prim->scene_prim_id = (unsigned)flat_idx;
                prim->shape__       = shape;
                prim->inst__        = nullptr;

                /* Sphere: (v, w) → parametric (theta, phi) */
                float theta = acosf(1.0f - 2.0f * v);
                float phi   = 2.0f * 3.14159265358979323846f * w;
                st[0] = theta / 3.14159265358979323846f;
                st[1] = phi   / (2.0f * 3.14159265358979323846f);
                return RES_OK;
            }
            running += 1;
        } else if (shape->type == OX_SHAPE_INSTANCE && shape->child_scene) {
            /* Flatten instance children using child snapshots */
            auto iit = sv->inst_child_snapshots.find(shape->id);
            if (iit != sv->inst_child_snapshots.end()) {
                for (auto& ckv : iit->second) {
                    const auto& csnap = ckv.second;
                    if (!csnap.enabled) continue;
                    s3d_shape* cs = csnap.shape;

                    size_t count = 0;
                    if (cs->type == OX_SHAPE_MESH) count = cs->ntris;
                    else if (cs->type == OX_SHAPE_SPHERE && cs->sphere_radius > 0.0f) count = 1;
                    else continue;

                    if (flat_idx < running + count) {
                        unsigned local = (unsigned)(flat_idx - running);
                        prim->prim_id       = local;
                        prim->geom_id       = cs->id;
                        prim->inst_id       = shape->id;
                        prim->scene_prim_id = (unsigned)flat_idx;
                        prim->shape__       = cs;
                        prim->inst__        = shape;

                        if (cs->type == OX_SHAPE_MESH) {
                            float sq = sqrtf(v);
                            st[0] = 1.0f - sq;
                            st[1] = w * sq;
                        } else {
                            float theta = acosf(1.0f - 2.0f * v);
                            float phi   = 2.0f * 3.14159265358979323846f * w;
                            st[0] = theta / 3.14159265358979323846f;
                            st[1] = phi   / (2.0f * 3.14159265358979323846f);
                        }
                        return RES_OK;
                    }
                    running += count;
                }
            }
        }
    }

    return RES_UNKNOWN_ERR;
}

/* ================================================================
 * Geometry queries: primitives_count, get_primitive, area, volume, AABB
 * ================================================================ */
res_T s3d_scene_view_primitives_count(s3d_scene_view* sv,
                                       size_t* count)
{
    if (!sv || !count) return RES_BAD_ARG;
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    size_t total = 0;
    for (auto& kv : sv->shape_snapshots) {
        const auto& snap = kv.second;
        s3d_shape* shape = snap.shape;
        if (!snap.enabled) continue;
        if (shape->type == OX_SHAPE_MESH) total += shape->ntris;
        else if (shape->type == OX_SHAPE_SPHERE) total += 1;
        else if (shape->type == OX_SHAPE_INSTANCE) {
            auto iit = sv->inst_child_snapshots.find(shape->id);
            if (iit != sv->inst_child_snapshots.end()) {
                for (auto& ckv : iit->second) {
                    const auto& csnap = ckv.second;
                    if (!csnap.enabled) continue;
                    if (csnap.shape->type == OX_SHAPE_MESH) total += csnap.shape->ntris;
                    else if (csnap.shape->type == OX_SHAPE_SPHERE) total += 1;
                }
            }
        }
    }
    *count = total;
    return RES_OK;
}

res_T s3d_scene_view_get_primitive(s3d_scene_view* sv,
                                    unsigned iprim,
                                    s3d_primitive* prim)
{
    if (!sv || !prim) return RES_BAD_ARG;
    if (!(sv->mask & S3D_GET_PRIMITIVE)) return RES_BAD_OP;
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    unsigned running = 0;
    for (auto& kv : sv->shape_snapshots) {
        const auto& snap = kv.second;
        s3d_shape* shape = snap.shape;
        if (!snap.enabled) continue;

        if (shape->type == OX_SHAPE_MESH) {
            unsigned count = shape->ntris;
            if (iprim < running + count) {
                prim->prim_id       = iprim - running;
                prim->geom_id       = shape->id;
                prim->inst_id       = S3D_INVALID_ID;
                prim->scene_prim_id = iprim;
                prim->shape__       = shape;
                prim->inst__        = nullptr;
                return RES_OK;
            }
            running += count;
        } else if (shape->type == OX_SHAPE_SPHERE) {
            if (iprim < running + 1) {
                prim->prim_id       = 0;
                prim->geom_id       = shape->id;
                prim->inst_id       = S3D_INVALID_ID;
                prim->scene_prim_id = iprim;
                prim->shape__       = shape;
                prim->inst__        = nullptr;
                return RES_OK;
            }
            running += 1;
        } else if (shape->type == OX_SHAPE_INSTANCE) {
            auto iit = sv->inst_child_snapshots.find(shape->id);
            if (iit != sv->inst_child_snapshots.end()) {
                for (auto& ckv : iit->second) {
                    const auto& csnap = ckv.second;
                    s3d_shape* cs = csnap.shape;
                    if (!csnap.enabled) continue;
                    unsigned count = (cs->type == OX_SHAPE_MESH) ? cs->ntris :
                                     (cs->type == OX_SHAPE_SPHERE) ? 1u : 0u;
                    if (count == 0) continue;
                    if (iprim < running + count) {
                        prim->prim_id       = iprim - running;
                        prim->geom_id       = cs->id;
                        prim->inst_id       = shape->id;
                        prim->scene_prim_id = iprim;
                        prim->shape__       = cs;
                        prim->inst__        = shape;
                        return RES_OK;
                    }
                    running += count;
                }
            }
        }
    }

    *prim = S3D_PRIMITIVE_NULL;
    return RES_BAD_ARG;
}

res_T s3d_scene_view_compute_area(s3d_scene_view* sv, float* area) {
    if (!sv || !area) return RES_BAD_ARG;
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    if (!sv->cdf_valid) build_cdf(sv);

    /* build_cdf now includes sphere areas in total_area */
    *area = sv->total_area;
    return RES_OK;
}

res_T s3d_scene_view_compute_volume(s3d_scene_view* sv, float* volume) {
    if (!sv || !volume) return RES_BAD_ARG;
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    float mesh_vol = 0.0f;
    float sphere_vol = 0.0f;

    for (auto& kv : sv->shape_snapshots) {
        const auto& snap = kv.second;
        s3d_shape* shape = snap.shape;
        if (!snap.enabled) continue;

        if (shape->type == OX_SHAPE_MESH) {
            float shape_vol_local = 0.0f;
            for (unsigned t = 0; t < shape->ntris; t++) {
                unsigned i0 = shape->indices[t*3+0];
                unsigned i1 = shape->indices[t*3+1];
                unsigned i2 = shape->indices[t*3+2];
                float v0x = shape->positions[i0*3+0], v0y = shape->positions[i0*3+1], v0z = shape->positions[i0*3+2];
                float v1x = shape->positions[i1*3+0], v1y = shape->positions[i1*3+1], v1z = shape->positions[i1*3+2];
                float v2x = shape->positions[i2*3+0], v2y = shape->positions[i2*3+1], v2z = shape->positions[i2*3+2];
                float cx = v1y * v2z - v1z * v2y;
                float cy = v1z * v2x - v1x * v2z;
                float cz = v1x * v2y - v1y * v2x;
                shape_vol_local += v0x * cx + v0y * cy + v0z * cz;
            }
            if (snap.flip_surface)
                shape_vol_local = -shape_vol_local;
            mesh_vol += shape_vol_local;
        }
        else if (shape->type == OX_SHAPE_SPHERE) {
            if (shape->sphere_radius <= 0.0f) continue;
            float r = shape->sphere_radius;
            float sv_val = (4.0f / 3.0f) * 3.14159265358979323846f * r * r * r;
            if (snap.flip_surface)
                sv_val = -sv_val;
            sphere_vol += sv_val;
        }
    }

    *volume = mesh_vol / 6.0f + sphere_vol;
    return RES_OK;
}

res_T s3d_scene_view_get_aabb(s3d_scene_view* sv,
                               float lower[3], float upper[3])
{
    if (!sv || !lower || !upper) return RES_BAD_ARG;
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    lower[0] = sv->lower[0]; lower[1] = sv->lower[1]; lower[2] = sv->lower[2];
    upper[0] = sv->upper[0]; upper[1] = sv->upper[1]; upper[2] = sv->upper[2];
    return RES_OK;
}
