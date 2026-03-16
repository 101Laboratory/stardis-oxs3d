/* s3d.h - OptiX-backed s3d public API (cus3d-compatible)
 *
 * Drop-in replacement for cus3d's s3d.h.  All types, macros, and function
 * signatures match the original header, but the implementation is backed by
 * the OptiX Unified Tracer instead of cuBQL.
 *
 * Usage:
 *   #include <s3d.h>             // this file
 *   Link against s3d library  // CMake: target_link_libraries(... s3d)
 *
 * Differences from the cuBQL-backed cus3d:
 *   - BIT() macro defined locally if not available
 *   - struct logger / mem_allocator are void* (not used)
 */

#ifndef S3D_H
#define S3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <float.h>
#include <stdint.h>
#include <stddef.h>

/* ---- Portability ---- */
#ifndef BIT
#define BIT(n) (1 << (n))
#endif

/* ---- Result Type and platform macros (from rsys) ---- */
#include <rsys/rsys.h>

/* ---- Library symbol management (uses rsys EXPORT_SYM/IMPORT_SYM/LOCAL_SYM) ---- */
#if defined(S3D_SHARED_BUILD)
  #define S3D_API extern EXPORT_SYM
#elif defined(S3D_STATIC)
  #define S3D_API extern LOCAL_SYM
#else
  #define S3D_API extern IMPORT_SYM
#endif

/* ---- Helper Macros ---- */
#ifndef NDEBUG
  #include <assert.h>
  #define S3D(Func) assert(s3d_ ## Func == RES_OK)
#else
  #define S3D(Func) s3d_ ## Func
#endif

#define S3D_KEEP NULL

#define S3D_INVALID_ID ((unsigned)-1)

/* ---- Enums ---- */
enum s3d_rays_flag {
  S3D_RAYS_SINGLE_ORIGIN    = BIT(0),
  S3D_RAYS_SINGLE_DIRECTION = BIT(1),
  S3D_RAYS_SINGLE_RANGE     = BIT(2),
  S3D_RAYS_SINGLE_DATA      = BIT(3)
};

enum s3d_attrib_usage {
  S3D_POSITION,
  S3D_ATTRIB_0,
  S3D_ATTRIB_1,
  S3D_ATTRIB_2,
  S3D_ATTRIB_3,
  S3D_ATTRIBS_COUNT__,
  S3D_GEOMETRY_NORMAL
};

enum s3d_type {
  S3D_FLOAT,
  S3D_FLOAT2,
  S3D_FLOAT3,
  S3D_FLOAT4
};

enum s3d_transform_space {
  S3D_LOCAL_TRANSFORM,
  S3D_WORLD_TRANSFORM
};

enum s3d_scene_view_flag {
  S3D_TRACE         = BIT(0),
  S3D_SAMPLE        = BIT(1),
  S3D_GET_PRIMITIVE = BIT(2)
};

enum s3d_accel_struct_quality {
  S3D_ACCEL_STRUCT_QUALITY_LOW,
  S3D_ACCEL_STRUCT_QUALITY_MEDIUM,
  S3D_ACCEL_STRUCT_QUALITY_HIGH
};

enum s3d_accel_struct_flag {
  S3D_ACCEL_STRUCT_FLAG_ROBUST  = BIT(0),
  S3D_ACCEL_STRUCT_FLAG_DYNAMIC = BIT(1),
  S3D_ACCEL_STRUCT_FLAG_COMPACT = BIT(2)
};

/* ---- Primitive ---- */
struct s3d_primitive {
  unsigned prim_id;
  unsigned geom_id;
  unsigned inst_id;
  unsigned scene_prim_id;
  void* shape__;
  void* inst__;
};

#define S3D_PRIMITIVE_NULL__ { \
  S3D_INVALID_ID, S3D_INVALID_ID, S3D_INVALID_ID, S3D_INVALID_ID, NULL, NULL \
}
static const struct s3d_primitive S3D_PRIMITIVE_NULL = S3D_PRIMITIVE_NULL__;

#define S3D_PRIMITIVE_EQ(Prim0, Prim1) \
  (  (Prim0)->prim_id == (Prim1)->prim_id \
  && (Prim0)->geom_id == (Prim1)->geom_id \
  && (Prim0)->inst_id == (Prim1)->inst_id)

/* ---- Attrib ---- */
struct s3d_attrib {
  float value[4];
  enum s3d_type type;
  enum s3d_attrib_usage usage;
};

/* ---- Vertex Data ---- */
struct s3d_vertex_data {
  enum s3d_attrib_usage usage;
  enum s3d_type type;
  void (*get)(const unsigned ivert, float* value, void* ctx);
};

#define S3D_VERTEX_DATA_NULL__ { S3D_ATTRIBS_COUNT__, S3D_FLOAT, NULL }
static const struct s3d_vertex_data S3D_VERTEX_DATA_NULL = S3D_VERTEX_DATA_NULL__;

/* ---- Hit ---- */
struct s3d_hit {
  struct s3d_primitive prim;
  float normal[3];
  float uv[2];
  float distance;
};

#define S3D_HIT_NULL__ { \
  {S3D_INVALID_ID, S3D_INVALID_ID, S3D_INVALID_ID, S3D_INVALID_ID, NULL, NULL}, \
  {0.f, 0.f, 0.f}, \
  {0.f, 0.f}, \
  FLT_MAX \
}
static const struct s3d_hit S3D_HIT_NULL = S3D_HIT_NULL__;

#define S3D_HIT_NONE(Hit) ((Hit)->distance >= FLT_MAX)

/* ---- Accel Struct Config ---- */
struct s3d_accel_struct_conf {
  enum s3d_accel_struct_quality quality;
  int mask;
};

#define S3D_ACCEL_STRUCT_CONF_DEFAULT__ { \
  S3D_ACCEL_STRUCT_QUALITY_MEDIUM, \
  S3D_ACCEL_STRUCT_FLAG_ROBUST \
}
static const struct s3d_accel_struct_conf S3D_ACCEL_STRUCT_CONF_DEFAULT =
  S3D_ACCEL_STRUCT_CONF_DEFAULT__;

/* ---- Filter Function ---- */
typedef int (*s3d_hit_filter_function_T)(
  const struct s3d_hit* hit,
  const float org[3],
  const float dir[3],
  const float range[2],
  void* query_data,
  void* filter_data);

/* ---- Forward Declarations ---- */
struct s3d_device;
struct s3d_scene;
struct s3d_scene_view;
struct s3d_shape;

/* logger / mem_allocator not used — accept void* for compatibility */
struct logger;
struct mem_allocator;

/*******************************************************************************
 * Device API
 ******************************************************************************/
S3D_API res_T s3d_device_create(
  struct logger* logger, struct mem_allocator* allocator,
  const int verbose, struct s3d_device** dev);

S3D_API res_T s3d_device_ref_get(struct s3d_device* dev);
S3D_API res_T s3d_device_ref_put(struct s3d_device* dev);
S3D_API int   s3d_device_get_gpu_sm_count(struct s3d_device* dev);

/*******************************************************************************
 * Scene API
 ******************************************************************************/
S3D_API res_T s3d_scene_create(struct s3d_device* dev, struct s3d_scene** scn);
S3D_API res_T s3d_scene_ref_get(struct s3d_scene* scn);
S3D_API res_T s3d_scene_ref_put(struct s3d_scene* scn);
S3D_API res_T s3d_scene_instantiate(struct s3d_scene* scn, struct s3d_shape** shape);

S3D_API res_T s3d_scene_attach_shape(struct s3d_scene* scn, struct s3d_shape* shape);
S3D_API res_T s3d_scene_detach_shape(struct s3d_scene* scn, struct s3d_shape* shape);
S3D_API res_T s3d_scene_clear(struct s3d_scene* scn);
S3D_API res_T s3d_scene_get_device(struct s3d_scene* scn, struct s3d_device** dev);
S3D_API res_T s3d_scene_get_shapes_count(struct s3d_scene* scn, size_t* nshapes);

/*******************************************************************************
 * Scene View API
 ******************************************************************************/
S3D_API res_T s3d_scene_view_create(
  struct s3d_scene* scn, const int mask, struct s3d_scene_view** scnview);

S3D_API res_T s3d_scene_view_create2(
  struct s3d_scene* scn, const int mask,
  const struct s3d_accel_struct_conf* cfg, struct s3d_scene_view** scnview);

S3D_API res_T s3d_scene_view_ref_get(struct s3d_scene_view* scnview);
S3D_API res_T s3d_scene_view_ref_put(struct s3d_scene_view* scnview);
S3D_API res_T s3d_scene_view_get_mask(struct s3d_scene_view* scnview, int* mask);

S3D_API res_T s3d_scene_view_trace_ray(
  struct s3d_scene_view* scnview,
  const float origin[3], const float direction[3], const float range[2],
  void* ray_data, struct s3d_hit* hit);

S3D_API res_T s3d_scene_view_trace_rays(
  struct s3d_scene_view* scnview,
  const size_t nrays, const int mask,
  const float* origins, const float* directions, const float* ranges,
  void* rays_data, const size_t sizeof_ray_data, struct s3d_hit* hits);

S3D_API res_T s3d_scene_view_closest_point(
  struct s3d_scene_view* scnview,
  const float pos[3], const float radius, void* query_data,
  struct s3d_hit* hit);

S3D_API res_T s3d_scene_view_sample(
  struct s3d_scene_view* scnview,
  const float u, const float v, const float w,
  struct s3d_primitive* primitive, float st[2]);

S3D_API res_T s3d_scene_view_get_primitive(
  struct s3d_scene_view* scnview, const unsigned iprim,
  struct s3d_primitive* prim);

S3D_API res_T s3d_scene_view_primitives_count(
  struct s3d_scene_view* scnview, size_t* primitives_count);

S3D_API res_T s3d_scene_view_compute_area(
  struct s3d_scene_view* scnview, float* area);

S3D_API res_T s3d_scene_view_compute_volume(
  struct s3d_scene_view* scnview, float* volume);

S3D_API res_T s3d_scene_view_get_aabb(
  struct s3d_scene_view* scnview, float lower[3], float upper[3]);

/*******************************************************************************
 * Shape API
 ******************************************************************************/
S3D_API res_T s3d_shape_ref_get(struct s3d_shape* shape);
S3D_API res_T s3d_shape_ref_put(struct s3d_shape* shape);
S3D_API res_T s3d_shape_get_id(const struct s3d_shape* shape, unsigned* id);
S3D_API res_T s3d_shape_enable(struct s3d_shape* shape, const char enable);
S3D_API res_T s3d_shape_is_enabled(const struct s3d_shape* shape, char* is_enabled);
S3D_API res_T s3d_shape_flip_surface(struct s3d_shape* shape);

/*******************************************************************************
 * Primitive API
 ******************************************************************************/
S3D_API res_T s3d_primitive_get_attrib(
  const struct s3d_primitive* prim, const enum s3d_attrib_usage attr,
  const float st[2], struct s3d_attrib* attrib);

S3D_API res_T s3d_primitive_has_attrib(
  const struct s3d_primitive* prim, const enum s3d_attrib_usage attr,
  char* has_attrib);

S3D_API res_T s3d_primitive_sample(
  const struct s3d_primitive* prim, const float u, const float v, float st[2]);

S3D_API res_T s3d_primitive_compute_area(
  const struct s3d_primitive* prim, float* area);

S3D_API res_T s3d_primitive_get_transform(
  const struct s3d_primitive* prim, float transform[12]);

S3D_API res_T s3d_triangle_get_vertex_attrib(
  const struct s3d_primitive* prim, const size_t ivertex,
  const enum s3d_attrib_usage usage, struct s3d_attrib* attrib);

/*******************************************************************************
 * Sphere API
 ******************************************************************************/
S3D_API res_T s3d_shape_create_sphere(
  struct s3d_device* dev, struct s3d_shape** sphere);

S3D_API res_T s3d_sphere_setup(
  struct s3d_shape* shape, const float position[3], const float radius);

S3D_API res_T s3d_sphere_set_hit_filter_function(
  struct s3d_shape* shape, s3d_hit_filter_function_T func, void* filter_data);

S3D_API res_T s3d_sphere_get_hit_filter_data(
  struct s3d_shape* shape, void** data);

/*******************************************************************************
 * Mesh API
 ******************************************************************************/
S3D_API res_T s3d_shape_create_mesh(
  struct s3d_device* dev, struct s3d_shape** shape);

S3D_API res_T s3d_mesh_setup_indexed_vertices(
  struct s3d_shape* shape,
  const unsigned ntris,
  void (*get_indices)(const unsigned itri, unsigned ids[3], void* ctx),
  const unsigned nverts,
  struct s3d_vertex_data attribs[], const unsigned nattribs, void* data);

S3D_API res_T s3d_mesh_copy(const struct s3d_shape* src, struct s3d_shape* dst);

S3D_API res_T s3d_mesh_get_vertices_count(
  const struct s3d_shape* shape, unsigned* nverts);

S3D_API res_T s3d_mesh_get_vertex_attrib(
  const struct s3d_shape* shape, const unsigned ivert,
  const enum s3d_attrib_usage usage, struct s3d_attrib* attrib);

S3D_API res_T s3d_mesh_get_triangles_count(
  const struct s3d_shape* shape, unsigned* ntris);

S3D_API res_T s3d_mesh_get_triangle_indices(
  const struct s3d_shape* shape, const unsigned itri, unsigned ids[3]);

S3D_API res_T s3d_mesh_set_hit_filter_function(
  struct s3d_shape* shape, s3d_hit_filter_function_T func, void* filter_data);

S3D_API res_T s3d_mesh_get_hit_filter_data(
  struct s3d_shape* shape, void** data);

/*******************************************************************************
 * Instance API
 ******************************************************************************/
S3D_API res_T s3d_instance_set_position(
  struct s3d_shape* shape, const float position[3]);

S3D_API res_T s3d_instance_translate(
  struct s3d_shape* shape, const enum s3d_transform_space space,
  const float translation[3]);

S3D_API res_T s3d_instance_set_transform(
  struct s3d_shape* shape, const float transform[12]);

S3D_API res_T s3d_instance_transform(
  struct s3d_shape* shape, const enum s3d_transform_space space,
  const float transform[12]);

/*******************************************************************************
 * Batch Ray Tracing API (GPU-accelerated)
 ******************************************************************************/
struct s3d_ray_request {
  float    origin[3];
  float    direction[3];
  float    range[2];
  void*    filter_data;
  uint32_t user_id;
};

/* C-visible pinned Ray layout matching CUDA Ray struct (32 bytes).
 * Used for direct-write optimization: collect writes here directly,
 * skipping the intermediate s3d_ray_request buffer + AoS→SoA copy. */
struct s3d_ray_pinned {
  float origin_x, origin_y, origin_z;
  float tmin;
  float direction_x, direction_y, direction_z;
  float tmax;
};

struct s3d_batch_trace_stats {
  size_t  total_rays;
  size_t  batch_accepted;
  size_t  filter_rejected;
  size_t  retrace_accepted;
  size_t  retrace_missed;
  double  batch_time_ms;
  double  postprocess_time_ms;
  double  retrace_time_ms;
};

struct s3d_batch_trace_context;

S3D_API res_T s3d_scene_view_trace_rays_batch(
  struct s3d_scene_view* scnview,
  const struct s3d_ray_request* requests, size_t nrays,
  struct s3d_hit* hits, struct s3d_batch_trace_stats* stats);

S3D_API res_T s3d_batch_trace_context_create(
  struct s3d_batch_trace_context** ctx, size_t max_rays);

S3D_API void s3d_batch_trace_context_destroy(
  struct s3d_batch_trace_context* ctx);

S3D_API res_T s3d_scene_view_trace_rays_batch_ctx(
  struct s3d_scene_view* scnview,
  struct s3d_batch_trace_context* ctx,
  const struct s3d_ray_request* requests, size_t nrays,
  struct s3d_hit* hits, struct s3d_batch_trace_stats* stats);

/* P0: async ray tracing — launch GPU work and return immediately */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_async(
  struct s3d_scene_view* scnview,
  struct s3d_batch_trace_context* ctx,
  const struct s3d_ray_request* requests, size_t nrays);

/* P0: wait for async ray tracing to complete, run CPU post-process */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_wait(
  struct s3d_scene_view* scnview,
  struct s3d_batch_trace_context* ctx,
  const struct s3d_ray_request* requests, size_t nrays,
  struct s3d_hit* hits, struct s3d_batch_trace_stats* stats);

/* L3: sync compute kernel only — no D2H transfer */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_sync_kernel(
  struct s3d_batch_trace_context* ctx);

/* L3: launch async D2H download — returns immediately */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_start_d2h(
  struct s3d_batch_trace_context* ctx, size_t nrays);

/* L3: wait for D2H transfer, then CPU post-process + retrace */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_wait_d2h(
  struct s3d_scene_view* scnview,
  struct s3d_batch_trace_context* ctx,
  const struct s3d_ray_request* requests, size_t nrays,
  struct s3d_hit* hits, struct s3d_batch_trace_stats* stats);

/*******************************************************************************
 * L4: GPU Inline Filter API (Mode A — filtered multi-hit)
 ******************************************************************************/

/* Per-ray filter data for GPU-side self-intersection / epsilon / enclosure
 * rejection. Must match FilterPerRayData layout (16 bytes). */
struct s3d_filter_per_ray {
  uint32_t hit_from_prim_id;   /* GAS-local prim of origin surface; UINT32_MAX = none */
  uint32_t hit_from_geom_id;   /* geom_id of origin surface;        UINT32_MAX = none */
  uint32_t enc_id;             /* ray's current enclosure ID;       UINT32_MAX = skip  */
  float    epsilon;            /* near-distance self-intersection threshold            */
};

/* Upload per-shape enclosure IDs (front/back per prim) to GPU.
 * Must be called before filtered trace; takes effect after rebuild.  */
S3D_API res_T s3d_scene_view_set_enclosure_data(
  struct s3d_scene_view* scnview,
  unsigned int shape_id,
  const unsigned int* enc_front, const unsigned int* enc_back,
  size_t num_prims);

/* L4: filtered async launch (Mode A) — GPU filter + single-hit output.
 * filter_per_ray array [nrays] provides per-ray filter params. */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_filtered_async(
  struct s3d_scene_view* scnview,
  struct s3d_batch_trace_context* ctx,
  const struct s3d_ray_request* requests,
  const struct s3d_filter_per_ray* filter_per_ray,
  size_t nrays);

/* L4: sync filtered kernel */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_filtered_sync_kernel(
  struct s3d_batch_trace_context* ctx);

/* Query true GPU kernel elapsed time (ms) from last sync_kernel call.
 * Uses CUDA event timing — valid only after sync_kernel returns. */
S3D_API float s3d_batch_trace_context_get_last_kernel_ms(
  struct s3d_batch_trace_context* ctx);

/* L4: start filtered D2H (downloads HitResult, not MultiHitResult) */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_filtered_start_d2h(
  struct s3d_batch_trace_context* ctx, size_t nrays);

/* L4: wait filtered D2H, convert to s3d_hit (no retrace needed) */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_filtered_wait_d2h(
  struct s3d_scene_view* scnview,
  struct s3d_batch_trace_context* ctx,
  const struct s3d_ray_request* requests, size_t nrays,
  struct s3d_hit* hits, struct s3d_batch_trace_stats* stats);

/*******************************************************************************
 * Plan E: Pinned Direct-Write API
 * Collect writes directly into pinned buffers, eliminating intermediate copy.
 ******************************************************************************/

/* Get borrowed pointers to batch_ctx internal pinned buffers.
 * Caller must not free these — ownership stays with batch_ctx. */
S3D_API void s3d_batch_trace_context_get_pinned_buffers(
  struct s3d_batch_trace_context* ctx,
  struct s3d_ray_pinned**       out_rays,
  struct s3d_filter_per_ray**   out_filter,
  size_t*                        out_capacity,
  struct s3d_hit**               out_hits);

/* Launch GPU trace directly from pinned buffers (no AoS→SoA copy).
 * Caller must have written ray/filter data into the pinned buffers
 * obtained via s3d_batch_trace_context_get_pinned_buffers(). */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_filtered_pinned_async(
  struct s3d_scene_view* scnview,
  struct s3d_batch_trace_context* ctx,
  size_t nrays);

/*******************************************************************************
 * Batch Closest Point API (GPU-accelerated)
 ******************************************************************************/
struct s3d_cp_request {
  float    pos[3];
  float    radius;
  void*    query_data;
  uint32_t user_id;
};

struct s3d_batch_cp_stats {
  size_t  total_queries;
  size_t  batch_accepted;
  size_t  filter_rejected;
  size_t  requery_accepted;
  size_t  requery_missed;
  double  batch_time_ms;
  double  postprocess_time_ms;
  double  requery_time_ms;
};

struct s3d_batch_cp_context;

S3D_API res_T s3d_scene_view_closest_point_batch(
  struct s3d_scene_view* scnview,
  const struct s3d_cp_request* requests, size_t nqueries,
  struct s3d_hit* hits, struct s3d_batch_cp_stats* stats);

S3D_API res_T s3d_batch_cp_context_create(
  struct s3d_batch_cp_context** ctx, size_t max_queries);

S3D_API void s3d_batch_cp_context_destroy(
  struct s3d_batch_cp_context* ctx);

S3D_API res_T s3d_scene_view_closest_point_batch_ctx(
  struct s3d_scene_view* scnview,
  struct s3d_batch_cp_context* ctx,
  const struct s3d_cp_request* requests, size_t nqueries,
  struct s3d_hit* hits, struct s3d_batch_cp_stats* stats);

/* ---- Async CP pipeline (mirrors RT async pattern) ---- */

S3D_API res_T s3d_scene_view_closest_point_batch_ctx_async(
  struct s3d_scene_view* scnview,
  struct s3d_batch_cp_context* ctx,
  const struct s3d_cp_request* requests, size_t nqueries);

S3D_API res_T s3d_scene_view_closest_point_batch_ctx_sync_kernel(
  struct s3d_batch_cp_context* ctx);

S3D_API res_T s3d_scene_view_closest_point_batch_ctx_start_d2h(
  struct s3d_batch_cp_context* ctx, size_t nqueries);

S3D_API res_T s3d_scene_view_closest_point_batch_ctx_wait_d2h(
  struct s3d_scene_view* scnview,
  struct s3d_batch_cp_context* ctx,
  const struct s3d_cp_request* requests, size_t nqueries,
  struct s3d_hit* hits, struct s3d_batch_cp_stats* stats);

/*******************************************************************************
 * Batch Point-in-Enclosure Query
 ******************************************************************************/
struct s3d_enc_locate_request {
  float    pos[3];
  uint32_t user_id;
};

struct s3d_enc_locate_result {
  int32_t  prim_id;
  float    distance;
  int32_t  side;
  unsigned enc_id;
};

struct s3d_batch_enc_stats {
  size_t  total_queries;
  size_t  resolved;
  size_t  degenerate;
  size_t  missed;
  double  batch_time_ms;
  double  postprocess_time_ms;
};

struct s3d_batch_enc_context;

S3D_API res_T s3d_scene_view_find_enclosure_batch(
  struct s3d_scene_view* scnview,
  const struct s3d_enc_locate_request* requests, size_t nqueries,
  struct s3d_enc_locate_result* results, struct s3d_batch_enc_stats* stats);

S3D_API res_T s3d_batch_enc_context_create(
  struct s3d_batch_enc_context** ctx, size_t max_queries);

S3D_API void s3d_batch_enc_context_destroy(
  struct s3d_batch_enc_context* ctx);

S3D_API res_T s3d_scene_view_find_enclosure_batch_ctx(
  struct s3d_scene_view* scnview,
  struct s3d_batch_enc_context* ctx,
  const struct s3d_enc_locate_request* requests, size_t nqueries,
  struct s3d_enc_locate_result* results, struct s3d_batch_enc_stats* stats);

/* ---- Async enc_locate pipeline (mirrors RT async pattern) ---- */

S3D_API res_T s3d_scene_view_find_enclosure_batch_ctx_async(
  struct s3d_scene_view* scnview,
  struct s3d_batch_enc_context* ctx,
  const struct s3d_enc_locate_request* requests, size_t nqueries);

S3D_API res_T s3d_scene_view_find_enclosure_batch_ctx_sync_kernel(
  struct s3d_batch_enc_context* ctx);

S3D_API res_T s3d_scene_view_find_enclosure_batch_ctx_start_d2h(
  struct s3d_batch_enc_context* ctx, size_t nqueries);

S3D_API res_T s3d_scene_view_find_enclosure_batch_ctx_wait_d2h(
  struct s3d_scene_view* scnview,
  struct s3d_batch_enc_context* ctx,
  const struct s3d_enc_locate_request* requests, size_t nqueries,
  struct s3d_enc_locate_result* results, struct s3d_batch_enc_stats* stats);

#ifdef __cplusplus
}
#endif
#endif /* S3D_H */
