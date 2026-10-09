/*
 * ox_s3d_shape.cpp — s3d_shape lifecycle + mesh/sphere/instance setup
 *
 * Each s3d_shape is a host-side data container.  The actual GPU acceleration
 * structure is built when the shape is committed to a scene_view (via
 * UnifiedTracer::addGeometryMesh / addGeometrySphere).
 *
 * Shape IDs are assigned lazily — the scene assigns them when a shape is
 * attached.  The compact range guarantee can be maintained by reusing IDs.
 */

#include "ox_s3d_internal.h"

/* ================================================================
 * Mesh shape
 * ================================================================ */
res_T s3d_shape_create_mesh(s3d_device* dev, s3d_shape** out_shape) {
    if (!dev || !out_shape) return RES_BAD_ARG;
    *out_shape = nullptr;

    s3d_shape* s = new (std::nothrow) s3d_shape();
    if (!s) return RES_MEM_ERR;

    s->type = OX_SHAPE_MESH;
    s->id = dev->next_shape_id++;
    *out_shape = s;
    return RES_OK;
}

res_T s3d_mesh_setup_indexed_vertices(
    s3d_shape* shape,
    unsigned ntris,
    void (*get_indices)(unsigned itri, unsigned ids[3], void* ctx),
    unsigned nverts,
    s3d_vertex_data attribs[],
    unsigned nattribs,
    void* data)
{
    if (!shape || shape->type != OX_SHAPE_MESH) return RES_BAD_ARG;
    if (!attribs || nattribs == 0) return RES_BAD_ARG;
    if (ntris == 0 || nverts == 0) return RES_BAD_ARG;

    /* ---- Validate attrib list ---- */
    bool has_position = false;
    bool any_keep = false;
    for (unsigned a = 0; a < nattribs; a++) {
        const s3d_vertex_data& vd = attribs[a];
        /* S3D_KEEP means get==NULL with valid usage */
        if (vd.get == nullptr) { /* S3D_KEEP */
            /* If usage is invalid (S3D_VERTEX_DATA_NULL), reject */
            if (vd.usage == S3D_ATTRIBS_COUNT__) return RES_BAD_ARG;
            any_keep = true;
            if (vd.usage == S3D_POSITION) has_position = true;
        } else {
            if (vd.usage == S3D_POSITION) has_position = true;
        }
    }
    if (!has_position) return RES_BAD_ARG;

    /* ---- KEEP validation: ntris/nverts must match existing ---- */
    if (get_indices == nullptr) { /* S3D_KEEP for indices */
        if (shape->ntris != ntris) return RES_BAD_ARG;
    }
    if (any_keep) {
        if (shape->nverts != 0 && shape->nverts != nverts) return RES_BAD_ARG;
    }

    /* ---- KEEP type consistency for vertex attribs ---- */
    for (unsigned a = 0; a < nattribs; a++) {
        const s3d_vertex_data& vd = attribs[a];
        if (vd.get != nullptr) continue; /* Not KEEP */
        if (vd.usage >= S3D_ATTRIB_0 && vd.usage <= S3D_ATTRIB_3) {
            int idx = vd.usage - S3D_ATTRIB_0;
            if (shape->vertex_attribs[idx].valid &&
                shape->vertex_attribs[idx].type != vd.type)
                return RES_BAD_ARG;
        }
    }

    /* ---- Fetch indices ---- */
    if (get_indices) {
        shape->indices.resize(ntris * 3);
        for (unsigned i = 0; i < ntris; i++) {
            unsigned ids[3];
            get_indices(i, ids, data);
            shape->indices[i * 3 + 0] = ids[0];
            shape->indices[i * 3 + 1] = ids[1];
            shape->indices[i * 3 + 2] = ids[2];
        }
    }

    /* ---- Fetch vertex attributes ---- */
    for (unsigned a = 0; a < nattribs; a++) {
        const s3d_vertex_data& vd = attribs[a];
        if (vd.get == nullptr) continue; /* S3D_KEEP */

        if (vd.usage == S3D_POSITION) {
            /* Store positions in shape->positions (float3 per vertex) */
            shape->positions.resize(nverts * 3);
            for (unsigned v = 0; v < nverts; v++) {
                float val[4] = { 0, 0, 0, 0 };
                vd.get(v, val, data);
                shape->positions[v * 3 + 0] = val[0];
                shape->positions[v * 3 + 1] = val[1];
                shape->positions[v * 3 + 2] = val[2];
            }
        } else if (vd.usage >= S3D_ATTRIB_0 && vd.usage <= S3D_ATTRIB_3) {
            int idx = vd.usage - S3D_ATTRIB_0;
            int floats_per_vert = 1;
            switch (vd.type) {
                case S3D_FLOAT:  floats_per_vert = 1; break;
                case S3D_FLOAT2: floats_per_vert = 2; break;
                case S3D_FLOAT3: floats_per_vert = 3; break;
                case S3D_FLOAT4: floats_per_vert = 4; break;
            }
            auto& store = shape->vertex_attribs[idx];
            store.type  = vd.type;
            store.valid = true;
            store.data.resize(nverts * floats_per_vert);
            for (unsigned v = 0; v < nverts; v++) {
                float val[4] = { 0, 0, 0, 0 };
                vd.get(v, val, data);
                for (int c = 0; c < floats_per_vert; c++)
                    store.data[v * floats_per_vert + c] = val[c];
            }
        }
    }

    shape->ntris  = ntris;
    shape->nverts = nverts;
    return RES_OK;
}

res_T s3d_mesh_copy(const s3d_shape* src, s3d_shape* dst) {
    if (!src || !dst) return RES_BAD_ARG;
    if (src->type != OX_SHAPE_MESH || dst->type != OX_SHAPE_MESH)
        return RES_BAD_ARG;

    dst->ntris     = src->ntris;
    dst->nverts    = src->nverts;
    dst->positions = src->positions;
    dst->indices   = src->indices;
    for (int i = 0; i < 4; i++)
        dst->vertex_attribs[i] = src->vertex_attribs[i];
    return RES_OK;
}

res_T s3d_mesh_get_vertices_count(const s3d_shape* shape, unsigned* nverts) {
    if (!shape || shape->type != OX_SHAPE_MESH || !nverts) return RES_BAD_ARG;
    *nverts = shape->nverts;
    return RES_OK;
}

res_T s3d_mesh_get_triangles_count(const s3d_shape* shape, unsigned* ntris) {
    if (!shape || shape->type != OX_SHAPE_MESH || !ntris) return RES_BAD_ARG;
    *ntris = shape->ntris;
    return RES_OK;
}

res_T s3d_mesh_get_triangle_indices(const s3d_shape* shape, unsigned itri,
                                     unsigned ids[3]) {
    if (!shape || shape->type != OX_SHAPE_MESH || !ids) return RES_BAD_ARG;
    if (itri >= shape->ntris) return RES_BAD_ARG;
    ids[0] = shape->indices[itri * 3 + 0];
    ids[1] = shape->indices[itri * 3 + 1];
    ids[2] = shape->indices[itri * 3 + 2];
    return RES_OK;
}

res_T s3d_mesh_get_vertex_attrib(
    const struct s3d_shape* shape,
    const unsigned ivert,
    const enum s3d_attrib_usage usage,
    struct s3d_attrib* attrib)
{
    if (!shape || shape->type != OX_SHAPE_MESH || !attrib) return RES_BAD_ARG;
    if (ivert >= shape->nverts) return RES_BAD_ARG;

    if (usage == S3D_POSITION) {
        attrib->type  = S3D_FLOAT3;
        attrib->usage = S3D_POSITION;
        attrib->value[0] = shape->positions[ivert * 3 + 0];
        attrib->value[1] = shape->positions[ivert * 3 + 1];
        attrib->value[2] = shape->positions[ivert * 3 + 2];
        attrib->value[3] = 0.0f;
        return RES_OK;
    }

    if (usage >= S3D_ATTRIB_0 && usage <= S3D_ATTRIB_3) {
        int idx = usage - S3D_ATTRIB_0;
        if (!shape->vertex_attribs[idx].valid) return RES_BAD_ARG;
        const auto& store = shape->vertex_attribs[idx];
        attrib->type  = store.type;
        attrib->usage = usage;
        int fpv = 1;
        switch (store.type) {
            case S3D_FLOAT:  fpv = 1; break;
            case S3D_FLOAT2: fpv = 2; break;
            case S3D_FLOAT3: fpv = 3; break;
            case S3D_FLOAT4: fpv = 4; break;
        }
        for (int c = 0; c < 4; c++) attrib->value[c] = 0.0f;
        for (int c = 0; c < fpv; c++)
            attrib->value[c] = store.data[ivert * fpv + c];
        return RES_OK;
    }

    return RES_BAD_ARG;
}

/* ---- Filter function ---- */
res_T s3d_mesh_set_hit_filter_function(s3d_shape* shape,
                                        s3d_hit_filter_function_T func,
                                        void* filter_data) {
    if (!shape || shape->type != OX_SHAPE_MESH) return RES_BAD_ARG;
    shape->filter_func = func;
    shape->filter_data = filter_data;
    return RES_OK;
}

res_T s3d_mesh_get_hit_filter_data(s3d_shape* shape, void** data) {
    if (!shape || shape->type != OX_SHAPE_MESH || !data) return RES_BAD_ARG;
    *data = shape->filter_data;
    return RES_OK;
}

/* ================================================================
 * Sphere shape
 * ================================================================ */
res_T s3d_shape_create_sphere(s3d_device* dev, s3d_shape** out_shape) {
    if (!dev || !out_shape) return RES_BAD_ARG;
    *out_shape = nullptr;

    s3d_shape* s = new (std::nothrow) s3d_shape();
    if (!s) return RES_MEM_ERR;

    s->type = OX_SHAPE_SPHERE;
    s->id = dev->next_shape_id++;
    *out_shape = s;
    return RES_OK;
}

res_T s3d_sphere_setup(s3d_shape* shape, const float pos[3], float radius) {
    if (!shape || !pos || shape->type != OX_SHAPE_SPHERE) return RES_BAD_ARG;
    if (radius <= 0.0f) return RES_BAD_ARG;
    shape->sphere_pos[0] = pos[0];
    shape->sphere_pos[1] = pos[1];
    shape->sphere_pos[2] = pos[2];
    shape->sphere_radius = radius;
    return RES_OK;
}

res_T s3d_sphere_set_hit_filter_function(s3d_shape* shape,
                                          s3d_hit_filter_function_T func,
                                          void* filter_data) {
    if (!shape || shape->type != OX_SHAPE_SPHERE) return RES_BAD_ARG;
    shape->filter_func = func;
    shape->filter_data = filter_data;
    return RES_OK;
}

res_T s3d_sphere_get_hit_filter_data(s3d_shape* shape, void** data) {
    if (!shape || shape->type != OX_SHAPE_SPHERE || !data) return RES_BAD_ARG;
    *data = shape->filter_data;
    return RES_OK;
}

/* ================================================================
 * Shape common API
 * ================================================================ */
res_T s3d_shape_ref_get(s3d_shape* shape) {
    if (!shape) return RES_BAD_ARG;
    shape->ref++;
    return RES_OK;
}

res_T s3d_shape_ref_put(s3d_shape* shape) {
    if (!shape) return RES_BAD_ARG;
    if (shape->ref == 0) return RES_BAD_ARG;
    fprintf(stderr, "[DBG] shape_ref_put shape=%p id=%u type=%d ref=%u->%u\n",
            (void*)shape, shape->id, (int)shape->type, (unsigned)shape->ref, (unsigned)(shape->ref - 1));
    fflush(stderr);
    if (--shape->ref == 0) {
        /* Release child_scene reference for instance shapes */
        if (shape->type == OX_SHAPE_INSTANCE && shape->child_scene) {
            fprintf(stderr, "[DBG]   releasing child_scene=%p\n", (void*)shape->child_scene); fflush(stderr);
            s3d_scene_ref_put(shape->child_scene);
            shape->child_scene = nullptr;
        }
        fprintf(stderr, "[DBG]   deleting shape=%p\n", (void*)shape); fflush(stderr);
        delete shape;
        fprintf(stderr, "[DBG]   shape deleted OK\n"); fflush(stderr);
    }
    return RES_OK;
}

res_T s3d_shape_get_id(const s3d_shape* shape, unsigned* id) {
    if (!shape || !id) return RES_BAD_ARG;
    *id = shape->id;
    return RES_OK;
}

res_T s3d_shape_enable(s3d_shape* shape, char enable) {
    if (!shape) return RES_BAD_ARG;
    shape->enabled = (enable != 0);
    return RES_OK;
}

res_T s3d_shape_is_enabled(const s3d_shape* shape, char* is_enabled) {
    if (!shape || !is_enabled) return RES_BAD_ARG;
    *is_enabled = shape->enabled ? 1 : 0;
    return RES_OK;
}

res_T s3d_shape_flip_surface(s3d_shape* shape) {
    if (!shape) return RES_BAD_ARG;
    shape->flip_surface = !shape->flip_surface;
    return RES_OK;
}

/* ================================================================
 * Instance API
 * ================================================================ */
res_T s3d_instance_set_position(s3d_shape* shape, const float pos[3]) {
    if (!shape || !pos || shape->type != OX_SHAPE_INSTANCE) return RES_BAD_ARG;
    /* Set translation column (column 3 of 3x4 column-major) */
    shape->transform[9]  = pos[0];
    shape->transform[10] = pos[1];
    shape->transform[11] = pos[2];
    return RES_OK;
}

res_T s3d_instance_translate(
    struct s3d_shape* shape,
    const enum s3d_transform_space space,
    const float translation[3])
{
    if (!shape || !translation || shape->type != OX_SHAPE_INSTANCE) return RES_BAD_ARG;
    if (space != S3D_LOCAL_TRANSFORM && space != S3D_WORLD_TRANSFORM) return RES_BAD_ARG;
    shape->transform[9]  += translation[0];
    shape->transform[10] += translation[1];
    shape->transform[11] += translation[2];
    return RES_OK;
}

res_T s3d_instance_set_transform(s3d_shape* shape, const float t[12]) {
    if (!shape || !t || shape->type != OX_SHAPE_INSTANCE) return RES_BAD_ARG;
    memcpy(shape->transform, t, 12 * sizeof(float));
    return RES_OK;
}

res_T s3d_instance_transform(
    struct s3d_shape* shape, 
    const enum s3d_transform_space space,
    const float transform[12]) {
    /* Multiply existing transform by t (column-major 3×4 × 3×4) */
    if (!shape || !transform || shape->type != OX_SHAPE_INSTANCE) return RES_BAD_ARG;
    if (space != S3D_LOCAL_TRANSFORM && space != S3D_WORLD_TRANSFORM) return RES_BAD_ARG;

    float old[12];
    memcpy(old, shape->transform, sizeof(old));

    /* C = A * B where A = old, B = t, both 3x4 column-major
     * c_col_j = A * b_col_j (for the 3x3 part)
     * translation_col = A * b_translation_col + a_translation_col */
    for (int col = 0; col < 3; col++) {
        for (int row = 0; row < 3; row++) {
            shape->transform[col * 3 + row] =
                old[0 * 3 + row] * transform[col * 3 + 0] +
                old[1 * 3 + row] * transform[col * 3 + 1] +
                old[2 * 3 + row] * transform[col * 3 + 2];
        }
    }
    /* Translation column */
    for (int row = 0; row < 3; row++) {
        shape->transform[9 + row] =
            old[0 * 3 + row] * transform[9] +
            old[1 * 3 + row] * transform[10] +
            old[2 * 3 + row] * transform[11] +
            old[9 + row];
    }
    return RES_OK;
}
