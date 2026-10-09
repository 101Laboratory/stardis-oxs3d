#ifndef GPU_WF_BENCH_H
#define GPU_WF_BENCH_H

#include <cstdint>
#include <cstdio>

/* ================================================================
 *  GPU Wavefront Feasibility Benchmark — shared definitions
 * ================================================================ */

/* ---------- path_hot_gpu: mirrors 8-byte hot header ------------ */
struct path_hot_gpu {
    uint8_t  phase;
    uint8_t  active;
    uint8_t  needs_ray;
    uint8_t  ray_bucket;
    uint8_t  ray_count_ext;
    uint8_t  pad[3];
};

/* ---------- experiment parameter set --------------------------- */
struct BenchParams {
    int   pool_size;          /* number of paths in the pool          */
    int   path_state_size;    /* AoS stride in bytes (256..2048)      */
    int   inner_iters;        /* cascade inner-loop iterations        */
    int   phase_count;        /* phases per wavefront round           */
    int   warmup_rounds;      /* warmup iterations (not timed)        */
    int   measure_rounds;     /* timed iterations                     */
    float active_ratio;       /* fraction of pool that is active [0,1]*/
};

/* ---------- timing result for a single configuration ----------- */
struct BenchResult {
    double kernel_us;         /* average kernel time in microseconds  */
    double bandwidth_GBs;     /* effective bandwidth (GB/s)           */
    double iters_per_sec;     /* iterations / second (Kernel B)       */
    double compact_us;        /* compact kernel time (Kernel C)       */
    double round_us;          /* per-round time (Kernel D)            */
};

/* ---------- default parameter sweeps from experiment_plan ------- */
static const int POOL_SIZES[]       = { 16384, 20000, 32768, 65536, 131072 };
static const int PATH_STATE_SIZES[] = { 256, 512, 1024, 2048 };
static const int INNER_ITERS[]      = { 1, 4, 16, 64 };
static const int NUM_POOL_SIZES     = 5;
static const int NUM_STATE_SIZES    = 4;
static const int NUM_INNER_ITERS    = 4;

/* ---------- reference constants from hybrid profiling ---------- */
static const double REF_TOTAL_RAYS          = 12.912e9;   /* total rays in porous scene         */
static const double REF_CASCADE_ITERS       = 5.379e9;    /* total cascade iterations            */
static const double REF_CPU_CASCADE_S       = 36.9;       /* CPU cascade wall-clock              */
static const double REF_CPU_EMBREE_TOTAL_S  = 100.0;      /* CPU Embree total wall-clock         */

/* ---------- GPU trace model (measured from hybrid profiling) ---- */
/* GPU-only trace kernel time at pool=20K: 15-25s (measured range).
   CPU postprocess (distribute) = 20-35s — captured separately by
   Kernel D, NOT included in T_trace.                               */
static const double REF_GPU_TRACE_S_LOW     = 15.0;       /* optimistic GPU trace at pool=20K   */
static const double REF_GPU_TRACE_S_MID     = 20.0;       /* midpoint GPU trace at pool=20K     */
static const double REF_GPU_TRACE_S_HIGH    = 25.0;       /* conservative GPU trace at pool=20K */
static const int    REF_TRACE_POOL          = 20000;       /* reference pool for trace timing    */
static const double RAY_NEED_RATIO          = 0.65;        /* fraction of active paths needing trace */

/* ---------- kernel launch prototypes --------------------------- */

/* Kernel A: pure bandwidth scan (read first cacheline, write last cacheline) */
void run_bandwidth_scan(const BenchParams& p, BenchResult& r);

/* Kernel B: cascade stub (hot+cold split, indirect access, inner loop) */
void run_cascade_stub(const BenchParams& p, BenchResult& r);

/* Kernel C: CUB stream compaction benchmark */
void run_compact_bench(const BenchParams& p, BenchResult& r);

/* Kernel D: multi-phase round simulation */
void run_multi_phase_sim(const BenchParams& p, BenchResult& r);

/* ---------- utility -------------------------------------------- */
inline void cuda_check(cudaError_t err, const char* file, int line) {
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA error at %s:%d — %s\n",
                file, line, cudaGetErrorString(err));
        exit(1);
    }
}
#define CUDA_CHECK(call) cuda_check((call), __FILE__, __LINE__)

#endif /* GPU_WF_BENCH_H */
