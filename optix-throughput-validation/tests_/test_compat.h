/*
 * test_compat.h — rsys compatibility shim for ox_s3d tests
 *
 * Replaces all rsys macros, types and functions used by the cus3d test suite.
 * This allows the original test logic to compile against ox_s3d without rsys.
 */
#ifndef TEST_COMPAT_H
#define TEST_COMPAT_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cfloat>

/* ================================================================
 * Result codes (supplement s3d.h which has RES_OK … RES_ERROR)
 * ================================================================ */
#ifndef RES_BAD_OP
#define RES_BAD_OP 4
#endif

/* ================================================================
 * Assertion macros
 * ================================================================ */
#define CHK(Expr) \
    do { \
        if (!(Expr)) { \
            fprintf(stderr, "CHK FAILED: %s\n  at %s:%d\n", \
                    #Expr, __FILE__, __LINE__); \
            fflush(stderr); \
            exit(1); \
        } \
    } while (0)

#define ASSERT(Expr) CHK(Expr)

#define FATAL(Msg) \
    do { \
        fprintf(stderr, "FATAL: %s\n  at %s:%d\n", (Msg), __FILE__, __LINE__); \
        fflush(stderr); \
        exit(1); \
    } while (0)

/* ================================================================
 * Iteration macro
 * ================================================================ */
#define FOR_EACH(Var, Begin, End) \
    for ((Var) = (Begin); (Var) < (End); ++(Var))

/* ================================================================
 * INLINE
 * ================================================================ */
#ifndef INLINE
#define INLINE inline
#endif

/* ================================================================
 * Constants
 * ================================================================ */
#ifndef PI
#define PI 3.14159265358979323846
#endif

/* ================================================================
 * Epsilon comparison
 * ================================================================ */
static inline int eq_epsf(float a, float b, float eps) {
    float d = fabsf(a - b);
    float m = fmaxf(1.0f, fmaxf(fabsf(a), fabsf(b)));
    return d <= eps * m ? 1 : 0;
}

static inline int eq_eps(float a, float b, float eps) {
    return eq_epsf(a, b, eps);
}

static inline float absf(float x) { return fabsf(x); }

/* ================================================================
 * Float3 helpers (return-pointer style like rsys/float3.h)
 * ================================================================ */
static inline float* f3(float out[3], float x, float y, float z) {
    out[0] = x; out[1] = y; out[2] = z;
    return out;
}

static inline float* f3_set(float dst[3], const float src[3]) {
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
    return dst;
}

static inline float* f3_sub(float out[3], const float a[3], const float b[3]) {
    out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2];
    return out;
}

static inline float* f3_add(float out[3], const float a[3], const float b[3]) {
    out[0] = a[0] + b[0]; out[1] = a[1] + b[1]; out[2] = a[2] + b[2];
    return out;
}

static inline float* f3_mulf(float out[3], const float a[3], float s) {
    out[0] = a[0] * s; out[1] = a[1] * s; out[2] = a[2] * s;
    return out;
}

static inline float* f3_divf(float out[3], const float a[3], float s) {
    float inv = 1.0f / s;
    out[0] = a[0] * inv; out[1] = a[1] * inv; out[2] = a[2] * inv;
    return out;
}

static inline float f3_dot(const float a[3], const float b[3]) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static inline float* f3_cross(float out[3], const float a[3], const float b[3]) {
    float x = a[1]*b[2] - a[2]*b[1];
    float y = a[2]*b[0] - a[0]*b[2];
    float z = a[0]*b[1] - a[1]*b[0];
    out[0] = x; out[1] = y; out[2] = z;
    return out;
}

static inline float f3_length(const float v[3]) {
    return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

static inline float f3_len(const float v[3]) { return f3_length(v); }

static inline float f3_normalize(float out[3], const float in[3]) {
    float len = f3_length(in);
    if (len > 0.0f) {
        float inv = 1.0f / len;
        out[0] = in[0] * inv;
        out[1] = in[1] * inv;
        out[2] = in[2] * inv;
    } else {
        out[0] = out[1] = out[2] = 0.0f;
    }
    return len;
}

static inline int f3_eq(const float a[3], const float b[3]) {
    return (a[0] == b[0] && a[1] == b[1] && a[2] == b[2]) ? 1 : 0;
}

static inline int f3_eq_eps(const float a[3], const float b[3], float eps) {
    return (fabsf(a[0]-b[0]) <= eps &&
            fabsf(a[1]-b[1]) <= eps &&
            fabsf(a[2]-b[2]) <= eps) ? 1 : 0;
}

static inline float* f3_minus(float out[3], const float v[3]) {
    out[0] = -v[0]; out[1] = -v[1]; out[2] = -v[2];
    return out;
}

static inline float* f3_splat(float out[3], float val) {
    out[0] = out[1] = out[2] = val;
    return out;
}

static inline float* f3_min(float out[3], const float a[3], const float b[3]) {
    out[0] = a[0] < b[0] ? a[0] : b[0];
    out[1] = a[1] < b[1] ? a[1] : b[1];
    out[2] = a[2] < b[2] ? a[2] : b[2];
    return out;
}

static inline float* f3_max(float out[3], const float a[3], const float b[3]) {
    out[0] = a[0] > b[0] ? a[0] : b[0];
    out[1] = a[1] > b[1] ? a[1] : b[1];
    out[2] = a[2] > b[2] ? a[2] : b[2];
    return out;
}

static inline float* f3_subf(float out[3], const float a[3], float s) {
    out[0] = a[0] - s; out[1] = a[1] - s; out[2] = a[2] - s;
    return out;
}

static inline float* f3_addf(float out[3], const float a[3], float s) {
    out[0] = a[0] + s; out[1] = a[1] + s; out[2] = a[2] + s;
    return out;
}

/* ================================================================
 * Float2 helpers (return-pointer style like rsys/float2.h)
 * ================================================================ */
static inline float* f2(float out[2], float x, float y) {
    out[0] = x; out[1] = y;
    return out;
}

static inline float* f2_set(float dst[2], const float src[2]) {
    dst[0] = src[0]; dst[1] = src[1];
    return dst;
}

static inline int f2_eq(const float a[2], const float b[2]) {
    return (a[0] == b[0] && a[1] == b[1]) ? 1 : 0;
}

static inline int f2_eq_eps(const float a[2], const float b[2], float eps) {
    return (fabsf(a[0]-b[0]) <= eps &&
            fabsf(a[1]-b[1]) <= eps) ? 1 : 0;
}

static inline float* f2_minus(float out[2], const float v[2]) {
    out[0] = -v[0]; out[1] = -v[1];
    return out;
}

#define SPLIT3(v) (v)[0], (v)[1], (v)[2]

/* ================================================================
 * Float33 helpers (3×3+translation rotation matrices)
 * Rotation stored as row-major 3×3 in [0..8], translation in [9..11]
 * ================================================================ */
static inline void f33_rotation_pitch(float m[12], float angle) {
    /* Rotation around X-axis (pitch) */
    float c = cosf(angle);
    float s = sinf(angle);
    m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f;
    m[3] = 0.0f; m[4] = c;    m[5] = -s;
    m[6] = 0.0f; m[7] = s;    m[8] = c;
    m[9] = 0.0f; m[10] = 0.0f; m[11] = 0.0f;
}

/* ================================================================
 * Stub: struct mem_allocator (no-op)
 * ================================================================ */
struct mem_allocator {
    int dummy;
};

static struct mem_allocator mem_default_allocator = {0};

static inline int mem_init_proxy_allocator(struct mem_allocator* a,
                                           struct mem_allocator* /*parent*/) {
    if (a) a->dummy = 0;
    return 0; /* RES_OK */
}

static inline size_t MEM_ALLOCATED_SIZE(struct mem_allocator* /*a*/) {
    return 0;
}

static inline void MEM_DUMP(struct mem_allocator*, char*, size_t) {}

static inline void mem_shutdown_proxy_allocator(struct mem_allocator*) {}

static inline size_t mem_allocated_size(void) { return 0; }

#define MEM_CALLOC(Alloc, Count, Size) calloc((Count), (Size))
#define MEM_RM(Alloc, Ptr) do { free(Ptr); (Ptr) = nullptr; } while(0)

/* ================================================================
 * Stub: struct time (no-op timing)
 * ================================================================ */
#ifndef TIME_ALL
#define TIME_ALL 0
#endif

struct time {
    double seconds;
};

static inline struct time* time_current(struct time* t) {
    t->seconds = 0.0;
    return t;
}

static inline void time_sub(struct time* result,
                            const struct time* a,
                            const struct time* b) {
    result->seconds = a->seconds - b->seconds;
}

static inline void time_dump(const struct time* t, int /*flags*/,
                             void* /*unused*/, char* buf, size_t bufsz) {
    snprintf(buf, bufsz, "%.6f s", t->seconds);
}

/* ================================================================
 * Stub: struct logger (no-op)
 * ================================================================ */
enum { LOG_OUTPUT = 0, LOG_ERROR = 1, LOG_WARNING = 2 };

struct logger {
    int dummy;
};

static inline int logger_init(struct mem_allocator*, struct logger* l) {
    if (l) l->dummy = 0;
    return 0; /* RES_OK */
}

static inline void logger_set_stream(struct logger*, int,
                                      void (*)(const char*, void*), void*) {}

static inline void logger_release(struct logger*) {}

/* ================================================================
 * Stub: struct image (minimal)
 * ================================================================ */
enum { IMAGE_RGB8 = 0 };

struct image {
    void*   pixels;
    size_t  width;
    size_t  height;
    size_t  stride;
    int     format;
};

static inline void image_init(struct mem_allocator*, struct image* img) {
    memset(img, 0, sizeof(*img));
}

static inline int image_setup(struct image* img,
                               size_t w, size_t h, size_t stride,
                               int format, void* /*allocator*/) {
    img->width  = w;
    img->height = h;
    img->stride = stride;
    img->format = format;
    img->pixels = calloc(h * stride, 1);
    return img->pixels ? 0 : 2; /* RES_OK or RES_MEM */
}

static inline int image_write_ppm_stream(struct image* /*img*/,
                                          int /*flag*/, FILE* /*fp*/) {
    /* No-op: skip image output in ox_s3d tests */
    return 0;
}

static inline void image_release(struct image* img) {
    free(img->pixels);
    img->pixels = nullptr;
}

#endif /* TEST_COMPAT_H */
