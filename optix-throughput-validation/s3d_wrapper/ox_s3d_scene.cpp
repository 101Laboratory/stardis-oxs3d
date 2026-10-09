/*
 * ox_s3d_scene.cpp — s3d_scene lifecycle + shape attach/detach/clear
 *
 * A scene is a named collection of shapes.  It doesn't own an acceleration
 * structure itself — that's the scene_view's job.  The scene assigns
 * compact geom_ids to shapes on attach and recycles them on detach.
 */

#include "ox_s3d_internal.h"

/* ================================================================
 * Create / Ref
 * ================================================================ */
res_T s3d_scene_create(s3d_device* dev, s3d_scene** out_scn) {
    if (!dev || !out_scn) return RES_BAD_ARG;
    *out_scn = nullptr;

    s3d_scene* scn = new (std::nothrow) s3d_scene();
    if (!scn) return RES_MEM_ERR;

    scn->dev = dev;
    s3d_device_ref_get(dev);

    *out_scn = scn;
    return RES_OK;
}

res_T s3d_scene_ref_get(s3d_scene* scn) {
    if (!scn) return RES_BAD_ARG;
    scn->ref++;
    return RES_OK;
}

res_T s3d_scene_ref_put(s3d_scene* scn) {
    if (!scn) return RES_BAD_ARG;
    if (scn->ref == 0) return RES_BAD_ARG;
    fprintf(stderr, "[DBG] scene_ref_put scn=%p ref=%u->%u\n",
            (void*)scn, (unsigned)scn->ref, (unsigned)(scn->ref - 1));
    fflush(stderr);
    if (--scn->ref == 0) {
        /* Release all shapes */
        for (auto& kv : scn->shapes)
            s3d_shape_ref_put(kv.second);
        scn->shapes.clear();
        /* Release device */
        fprintf(stderr, "[DBG]   releasing device=%p\n", (void*)scn->dev); fflush(stderr);
        s3d_device_ref_put(scn->dev);
        fprintf(stderr, "[DBG]   deleting scene=%p\n", (void*)scn); fflush(stderr);
        delete scn;
        fprintf(stderr, "[DBG]   scene deleted OK\n"); fflush(stderr);
    }
    return RES_OK;
}

/* ================================================================
 * Instantiate — create a shape whose geometry is this scene's content
 * ================================================================ */
res_T s3d_scene_instantiate(s3d_scene* scn, s3d_shape** out_shape) {
    if (!scn || !out_shape) return RES_BAD_ARG;
    *out_shape = nullptr;

    s3d_shape* s = new (std::nothrow) s3d_shape();
    if (!s) return RES_MEM_ERR;

    s->type = OX_SHAPE_INSTANCE;
    s->child_scene = scn;
    s->id = scn->dev->next_shape_id++;
    s3d_scene_ref_get(scn);

    *out_shape = s;
    return RES_OK;
}

/* ================================================================
 * Attach / Detach / Clear
 * ================================================================ */
res_T s3d_scene_attach_shape(s3d_scene* scn, s3d_shape* shape) {
    if (!scn || !shape) return RES_BAD_ARG;

    /* Guard: an instance shape that references this scene cannot be attached
     * back to it (would create a recursive loop) */
    if (shape->type == OX_SHAPE_INSTANCE && shape->child_scene == scn)
        return RES_BAD_ARG;

    /* Already attached? */
    if (shape->id != S3D_INVALID_ID) {
        auto it = scn->shapes.find(shape->id);
        if (it != scn->shapes.end() && it->second == shape)
            return RES_OK; /* idempotent */
    }

    /* Use the shape's existing ID (assigned at creation time).
     * Only assign a new one if somehow it doesn't have one yet. */
    if (shape->id == S3D_INVALID_ID) {
        shape->id = scn->next_shape_id++;
    }

    scn->shapes[shape->id] = shape;
    s3d_shape_ref_get(shape);

    return RES_OK;
}

res_T s3d_scene_detach_shape(s3d_scene* scn, s3d_shape* shape) {
    if (!scn || !shape) return RES_BAD_ARG;
    if (shape->id == S3D_INVALID_ID) return RES_BAD_ARG;

    auto it = scn->shapes.find(shape->id);
    if (it == scn->shapes.end() || it->second != shape)
        return RES_BAD_ARG;

    scn->shapes.erase(it);

    s3d_shape_ref_put(shape);

    return RES_OK;
}

res_T s3d_scene_clear(s3d_scene* scn) {
    if (!scn) return RES_BAD_ARG;

    for (auto& kv : scn->shapes) {
        s3d_shape_ref_put(kv.second);
    }
    scn->shapes.clear();

    return RES_OK;
}

/* ================================================================
 * Accessors
 * ================================================================ */
res_T s3d_scene_get_device(s3d_scene* scn, s3d_device** dev) {
    if (!scn || !dev) return RES_BAD_ARG;
    *dev = scn->dev;
    s3d_device_ref_get(scn->dev);
    return RES_OK;
}

res_T s3d_scene_get_shapes_count(s3d_scene* scn, size_t* nshapes) {
    if (!scn || !nshapes) return RES_BAD_ARG;
    *nshapes = scn->shapes.size();
    return RES_OK;
}
