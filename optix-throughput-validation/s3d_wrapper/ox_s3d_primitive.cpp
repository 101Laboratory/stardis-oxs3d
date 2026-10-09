/*
 * ox_s3d_primitive.cpp — Primitive attribute interpolation and queries
 *
 * Implements:
 *   s3d_primitive_get_attrib    — barycentric interpolation of vertex attrs
 *   s3d_primitive_has_attrib    — attribute availability check
 *   s3d_primitive_sample        — uniform sampling on a triangle
 *   s3d_primitive_compute_area  — triangle area
 *   s3d_primitive_get_transform — identity or instance transform
 *   s3d_triangle_get_vertex_attrib — raw vertex attribute (no interpolation)
 */

#include "ox_s3d_internal.h"
#include <cmath>

/* ================================================================
 * Attribute interpolation
 * ================================================================ */
res_T s3d_primitive_get_attrib(const struct s3d_primitive* prim,
                               const enum s3d_attrib_usage attr,
                               const float st[2],
                               s3d_attrib* attrib)
{
    if (!prim || !st || !attrib) return RES_BAD_ARG;

    s3d_shape* shape = static_cast<s3d_shape*>(prim->shape__);
    if (!shape) return RES_BAD_ARG;

    /* ---- Sphere ---- */
    if (shape->type == OX_SHAPE_SPHERE) {
        if (attr == S3D_POSITION) {
            /* Parametric coords (s,t) on sphere → world position */
            float theta = st[0] * 3.14159265358979323846f;
            float phi   = st[1] * 2.0f * 3.14159265358979323846f;
            float r     = shape->sphere_radius;
            float px = shape->sphere_pos[0] + r * sinf(theta) * cosf(phi);
            float py = shape->sphere_pos[1] + r * sinf(theta) * sinf(phi);
            float pz = shape->sphere_pos[2] + r * cosf(theta);
            /* Apply instance transform if present */
            if (prim->inst__) {
                s3d_shape* inst = static_cast<s3d_shape*>(prim->inst__);
                if (inst && inst->type == OX_SHAPE_INSTANCE) {
                    const float* t = inst->transform;
                    float tx = t[0]*px + t[3]*py + t[6]*pz + t[9];
                    float ty = t[1]*px + t[4]*py + t[7]*pz + t[10];
                    float tz = t[2]*px + t[5]*py + t[8]*pz + t[11];
                    px = tx; py = ty; pz = tz;
                }
            }
            attrib->type  = S3D_FLOAT3;
            attrib->usage = S3D_POSITION;
            attrib->value[0] = px;
            attrib->value[1] = py;
            attrib->value[2] = pz;
            attrib->value[3] = 0.0f;
            return RES_OK;
        }
        if (attr == S3D_GEOMETRY_NORMAL) {
            float theta = st[0] * 3.14159265358979323846f;
            float phi   = st[1] * 2.0f * 3.14159265358979323846f;
            attrib->type  = S3D_FLOAT3;
            attrib->usage = S3D_GEOMETRY_NORMAL;
            float nx = sinf(theta) * cosf(phi);
            float ny = sinf(theta) * sinf(phi);
            float nz = cosf(theta);
            if (shape->flip_surface) { nx = -nx; ny = -ny; nz = -nz; }
            /* Apply instance 3x3 rotation to normal if present */
            if (prim->inst__) {
                s3d_shape* inst = static_cast<s3d_shape*>(prim->inst__);
                if (inst && inst->type == OX_SHAPE_INSTANCE) {
                    const float* t = inst->transform;
                    float tnx = t[0]*nx + t[3]*ny + t[6]*nz;
                    float tny = t[1]*nx + t[4]*ny + t[7]*nz;
                    float tnz = t[2]*nx + t[5]*ny + t[8]*nz;
                    nx = tnx; ny = tny; nz = tnz;
                }
            }
            attrib->value[0] = nx;
            attrib->value[1] = ny;
            attrib->value[2] = nz;
            attrib->value[3] = 0.0f;
            return RES_OK;
        }
        return RES_BAD_ARG;
    }

    /* ---- Mesh ---- */
    if (shape->type != OX_SHAPE_MESH) return RES_BAD_ARG;
    if (prim->prim_id >= shape->ntris) return RES_BAD_ARG;

    unsigned i0 = shape->indices[prim->prim_id * 3 + 0];
    unsigned i1 = shape->indices[prim->prim_id * 3 + 1];
    unsigned i2 = shape->indices[prim->prim_id * 3 + 2];

    /* s3d barycentric: st[0]=w (weight of v0), st[1]=u (weight of v1)
     * v = w*v0 + u*v1 + (1-w-u)*v2 */
    float w_bary = st[0];
    float u_bary = st[1];
    float v_bary = 1.0f - w_bary - u_bary;

    if (attr == S3D_GEOMETRY_NORMAL) {
        /* Cross product of edge vectors: (v1-v0) × (v2-v0) */
        float e1x = shape->positions[i1*3+0] - shape->positions[i0*3+0];
        float e1y = shape->positions[i1*3+1] - shape->positions[i0*3+1];
        float e1z = shape->positions[i1*3+2] - shape->positions[i0*3+2];
        float e2x = shape->positions[i2*3+0] - shape->positions[i0*3+0];
        float e2y = shape->positions[i2*3+1] - shape->positions[i0*3+1];
        float e2z = shape->positions[i2*3+2] - shape->positions[i0*3+2];
        /* CW convention: e2 × e1 (s3d uses CW winding) */
        attrib->type  = S3D_FLOAT3;
        attrib->usage = S3D_GEOMETRY_NORMAL;
        float nx = e2y * e1z - e2z * e1y;
        float ny = e2z * e1x - e2x * e1z;
        float nz = e2x * e1y - e2y * e1x;
        if (shape->flip_surface) { nx = -nx; ny = -ny; nz = -nz; }
        /* Apply instance 3x3 rotation to normal if present */
        if (prim->inst__) {
            s3d_shape* inst = static_cast<s3d_shape*>(prim->inst__);
            if (inst && inst->type == OX_SHAPE_INSTANCE) {
                const float* t = inst->transform;
                float tnx = t[0]*nx + t[3]*ny + t[6]*nz;
                float tny = t[1]*nx + t[4]*ny + t[7]*nz;
                float tnz = t[2]*nx + t[5]*ny + t[8]*nz;
                nx = tnx; ny = tny; nz = tnz;
            }
        }
        attrib->value[0] = nx;
        attrib->value[1] = ny;
        attrib->value[2] = nz;
        attrib->value[3] = 0.0f;
        return RES_OK;
    }

    if (attr == S3D_POSITION) {
        attrib->type  = S3D_FLOAT3;
        attrib->usage = S3D_POSITION;
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        for (int c = 0; c < 3; c++) {
            float val = w_bary * shape->positions[i0*3+c] +
                        u_bary * shape->positions[i1*3+c] +
                        v_bary * shape->positions[i2*3+c];
            if (c == 0) px = val;
            else if (c == 1) py = val;
            else pz = val;
        }
        /* Apply instance transform if present */
        if (prim->inst__) {
            s3d_shape* inst = static_cast<s3d_shape*>(prim->inst__);
            if (inst && inst->type == OX_SHAPE_INSTANCE) {
                const float* t = inst->transform;
                float tx = t[0]*px + t[3]*py + t[6]*pz + t[9];
                float ty = t[1]*px + t[4]*py + t[7]*pz + t[10];
                float tz = t[2]*px + t[5]*py + t[8]*pz + t[11];
                px = tx; py = ty; pz = tz;
            }
        }
        attrib->value[0] = px;
        attrib->value[1] = py;
        attrib->value[2] = pz;
        attrib->value[3] = 0.0f;
        return RES_OK;
    }

    /* Generic vertex attributes (S3D_ATTRIB_0 .. S3D_ATTRIB_3) */
    if (attr >= S3D_ATTRIB_0 && attr <= S3D_ATTRIB_3) {
        int idx = attr - S3D_ATTRIB_0;
        if (!shape->vertex_attribs[idx].valid) return RES_BAD_ARG;
        const auto& store = shape->vertex_attribs[idx];
        int fpv = 1;
        switch (store.type) {
            case S3D_FLOAT:  fpv = 1; break;
            case S3D_FLOAT2: fpv = 2; break;
            case S3D_FLOAT3: fpv = 3; break;
            case S3D_FLOAT4: fpv = 4; break;
        }
        attrib->type  = store.type;
        attrib->usage = attr;
        for (int c = 0; c < 4; c++) attrib->value[c] = 0.0f;
        for (int c = 0; c < fpv; c++) {
            attrib->value[c] =
                w_bary * store.data[i0 * fpv + c] +
                u_bary * store.data[i1 * fpv + c] +
                v_bary * store.data[i2 * fpv + c];
        }
        return RES_OK;
    }

    return RES_BAD_ARG;
}

/* ================================================================
 * has_attrib
 * ================================================================ */
res_T s3d_primitive_has_attrib(const struct s3d_primitive* prim,
                               const enum s3d_attrib_usage attr,
                               char* has_attrib)
{
    if (!prim || !has_attrib) return RES_BAD_ARG;

    /* Validate attr is a recognized usage */
    if (attr != S3D_POSITION && attr != S3D_GEOMETRY_NORMAL
        && !(attr >= S3D_ATTRIB_0 && attr <= S3D_ATTRIB_3))
        return RES_BAD_ARG;

    s3d_shape* shape = static_cast<s3d_shape*>(prim->shape__);
    if (!shape) { *has_attrib = 0; return RES_OK; }

    if (attr == S3D_POSITION || attr == S3D_GEOMETRY_NORMAL) {
        *has_attrib = 1;
        return RES_OK;
    }

    if (attr >= S3D_ATTRIB_0 && attr <= S3D_ATTRIB_3) {
        int idx = attr - S3D_ATTRIB_0;
        *has_attrib = (shape->type == OX_SHAPE_MESH && shape->vertex_attribs[idx].valid) ? 1 : 0;
        return RES_OK;
    }

    *has_attrib = 0;
    return RES_OK;
}

/* ================================================================
 * s3d_primitive_sample — uniform sampling on a triangle
 * ================================================================ */
res_T s3d_primitive_sample(const s3d_primitive* prim,
                            float u, float v,
                            float st[2])
{
    if (!prim || !st) return RES_BAD_ARG;
    if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) return RES_BAD_ARG;

    /* Standard barycentric triangle sampling */
    float sq = sqrtf(u);
    st[0] = 1.0f - sq;        /* w = weight of v0 */
    st[1] = v * sq;           /* u = weight of v1 */
    return RES_OK;
}

/* ================================================================
 * s3d_primitive_compute_area
 * ================================================================ */
res_T s3d_primitive_compute_area(const s3d_primitive* prim, float* area) {
    if (!prim || !area) return RES_BAD_ARG;
    s3d_shape* shape = static_cast<s3d_shape*>(prim->shape__);
    if (!shape) return RES_BAD_ARG;

    if (shape->type == OX_SHAPE_SPHERE) {
        *area = 4.0f * 3.14159265358979323846f * shape->sphere_radius * shape->sphere_radius;
        return RES_OK;
    }

    if (shape->type != OX_SHAPE_MESH) return RES_BAD_ARG;
    if (prim->prim_id >= shape->ntris) return RES_BAD_ARG;

    unsigned i0 = shape->indices[prim->prim_id * 3 + 0];
    unsigned i1 = shape->indices[prim->prim_id * 3 + 1];
    unsigned i2 = shape->indices[prim->prim_id * 3 + 2];

    float e1x = shape->positions[i1*3+0] - shape->positions[i0*3+0];
    float e1y = shape->positions[i1*3+1] - shape->positions[i0*3+1];
    float e1z = shape->positions[i1*3+2] - shape->positions[i0*3+2];
    float e2x = shape->positions[i2*3+0] - shape->positions[i0*3+0];
    float e2y = shape->positions[i2*3+1] - shape->positions[i0*3+1];
    float e2z = shape->positions[i2*3+2] - shape->positions[i0*3+2];

    float cx = e1y * e2z - e1z * e2y;
    float cy = e1z * e2x - e1x * e2z;
    float cz = e1x * e2y - e1y * e2x;

    *area = 0.5f * sqrtf(cx*cx + cy*cy + cz*cz);
    return RES_OK;
}

/* ================================================================
 * s3d_primitive_get_transform
 * ================================================================ */
res_T s3d_primitive_get_transform(const s3d_primitive* prim,
                                   float transform[12])
{
    if (!prim || !transform) return RES_BAD_ARG;

    /* Return identity for non-instanced primitives */
    memset(transform, 0, 12 * sizeof(float));
    transform[0] = 1.0f; /* col0.x */
    transform[4] = 1.0f; /* col1.y */
    transform[8] = 1.0f; /* col2.z */
    /* col3 = (0,0,0) translation */

    /* If the primitive comes from an instance, use the instance transform */
    if (prim->inst__ != nullptr) {
        s3d_shape* inst = static_cast<s3d_shape*>(prim->inst__);
        if (inst && inst->type == OX_SHAPE_INSTANCE) {
            memcpy(transform, inst->transform, 12 * sizeof(float));
        }
    }

    return RES_OK;
}

/* ================================================================
 * s3d_triangle_get_vertex_attrib — raw vertex attribute (no interpolation)
 * ================================================================ */
res_T s3d_triangle_get_vertex_attrib(
    const struct s3d_primitive* prim,
    const size_t ivertex,
    const enum s3d_attrib_usage usage,
    s3d_attrib* attrib)
{
    if (!prim || !attrib || ivertex >= 3) return RES_BAD_ARG;

    s3d_shape* shape = static_cast<s3d_shape*>(prim->shape__);
    if (!shape || shape->type != OX_SHAPE_MESH) return RES_BAD_ARG;
    if (prim->prim_id >= shape->ntris) return RES_BAD_ARG;

    unsigned vid = shape->indices[prim->prim_id * 3 + ivertex];

    res_T r = s3d_mesh_get_vertex_attrib(shape, vid, usage, attrib);
    if (r != RES_OK) return r;

    /* Fix A: apply instance transform for S3D_POSITION so that vertex
     * positions are in world space, consistent with s3d_primitive_get_attrib. */
    if (usage == S3D_POSITION && prim->inst__ != nullptr) {
        s3d_shape* inst = static_cast<s3d_shape*>(prim->inst__);
        if (inst && inst->type == OX_SHAPE_INSTANCE) {
            const float* t = inst->transform;
            float px = attrib->value[0], py = attrib->value[1], pz = attrib->value[2];
            attrib->value[0] = t[0]*px + t[3]*py + t[6]*pz + t[9];
            attrib->value[1] = t[1]*px + t[4]*py + t[7]*pz + t[10];
            attrib->value[2] = t[2]*px + t[5]*py + t[8]*pz + t[11];
        }
    }

    return r;
}
