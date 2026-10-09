/* ================================================================
 *  GPU Wavefront Feasibility Benchmark — Driver
 * ================================================================
 *  Runs Kernel A-D across the parameter matrix, writes CSV
 *  results, and prints a final Go/No-Go projection.
 * ================================================================ */

#include "gpu_wf_bench.h"
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

/* ── helpers ──────────────────────────────────────────────────── */

static void print_gpu_info()
{
    int dev = 0;
    cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, dev));
    printf("=== GPU: %s ===\n", prop.name);
    printf("    SM count          : %d\n",   prop.multiProcessorCount);
    printf("    L2 cache          : %.1f MB\n", prop.l2CacheSize / 1e6);
    printf("    Global memory     : %.1f GB\n", prop.totalGlobalMem / 1e9);
    printf("    Memory clock      : %.0f MHz\n", prop.memoryClockRate / 1e3);
    printf("    Memory bus width  : %d bit\n", prop.memoryBusWidth);

    /* theoretical bandwidth */
    double bw_GBs = 2.0 * prop.memoryClockRate * 1e3 * (prop.memoryBusWidth / 8.0) / 1e9;
    printf("    Theoretical BW    : %.0f GB/s\n\n", bw_GBs);
}

static FILE* open_csv(const char* path)
{
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open %s for writing\n", path);
        exit(1);
    }
    return f;
}

/* ── Kernel A sweep ──────────────────────────────────────────── */

static void sweep_kernel_a(const char* csv_dir)
{
    printf("────── Kernel A: bandwidth_scan ──────\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/kernel_a_bandwidth.csv", csv_dir);
    FILE* f = open_csv(path);
    fprintf(f, "pool_size,stride_bytes,kernel_us,bandwidth_GBs\n");

    for (int pi = 0; pi < NUM_POOL_SIZES; pi++) {
        for (int si = 0; si < NUM_STATE_SIZES; si++) {
            BenchParams bp = {};
            bp.pool_size       = POOL_SIZES[pi];
            bp.path_state_size = PATH_STATE_SIZES[si];
            bp.warmup_rounds   = 20;
            bp.measure_rounds  = 100;

            BenchResult br = {};
            run_bandwidth_scan(bp, br);

            printf("  pool=%6d  stride=%5dB  kernel=%.2f us  BW=%.1f GB/s\n",
                   bp.pool_size, bp.path_state_size, br.kernel_us, br.bandwidth_GBs);
            fprintf(f, "%d,%d,%.3f,%.2f\n",
                    bp.pool_size, bp.path_state_size, br.kernel_us, br.bandwidth_GBs);
        }
    }
    fclose(f);
    printf("  → saved %s\n\n", path);
}

/* ── Kernel B sweep ──────────────────────────────────────────── */

static void sweep_kernel_b(const char* csv_dir)
{
    printf("────── Kernel B: cascade_stub ──────\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/kernel_b_cascade.csv", csv_dir);
    FILE* f = open_csv(path);
    fprintf(f, "pool_size,stride_bytes,inner_iters,kernel_us,Giters_per_sec,speedup_vs_cpu\n");

    const double cpu_iters_s = REF_CASCADE_ITERS / REF_CPU_CASCADE_S;  /* ~145.8 M */

    for (int pi = 0; pi < NUM_POOL_SIZES; pi++) {
        for (int si = 0; si < NUM_STATE_SIZES; si++) {
            for (int ii = 0; ii < NUM_INNER_ITERS; ii++) {
                BenchParams bp = {};
                bp.pool_size       = POOL_SIZES[pi];
                bp.path_state_size = PATH_STATE_SIZES[si];
                bp.inner_iters     = INNER_ITERS[ii];
                bp.active_ratio    = 0.65f;    /* 65% active, matches hybrid data */
                bp.warmup_rounds   = 10;
                bp.measure_rounds  = 50;

                BenchResult br = {};
                run_cascade_stub(bp, br);

                double Giters = br.iters_per_sec / 1e9;
                double speedup = br.iters_per_sec / cpu_iters_s;

                printf("  pool=%6d  stride=%5dB  iters=%2d  kernel=%8.2f us  %.3f Giter/s  (%.1fx CPU)\n",
                       bp.pool_size, bp.path_state_size, bp.inner_iters,
                       br.kernel_us, Giters, speedup);
                fprintf(f, "%d,%d,%d,%.3f,%.6f,%.2f\n",
                        bp.pool_size, bp.path_state_size, bp.inner_iters,
                        br.kernel_us, Giters, speedup);
            }
        }
    }
    fclose(f);
    printf("  → saved %s\n\n", path);
}

/* ── Kernel C sweep ──────────────────────────────────────────── */

static void sweep_kernel_c(const char* csv_dir)
{
    printf("────── Kernel C: compact_bench ──────\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/kernel_c_compact.csv", csv_dir);
    FILE* f = open_csv(path);
    fprintf(f, "pool_size,active_ratio,compact_us\n");

    float ratios[] = { 0.50f, 0.65f, 0.80f, 0.95f };
    int n_ratios   = 4;

    for (int pi = 0; pi < NUM_POOL_SIZES; pi++) {
        for (int ri = 0; ri < n_ratios; ri++) {
            BenchParams bp = {};
            bp.pool_size     = POOL_SIZES[pi];
            bp.active_ratio  = ratios[ri];
            bp.warmup_rounds = 20;
            bp.measure_rounds = 200;

            BenchResult br = {};
            run_compact_bench(bp, br);

            printf("  pool=%6d  active=%.0f%%  compact=%.2f us\n",
                   bp.pool_size, bp.active_ratio * 100.0f, br.compact_us);
            fprintf(f, "%d,%.2f,%.3f\n",
                    bp.pool_size, bp.active_ratio, br.compact_us);
        }
    }
    fclose(f);
    printf("  → saved %s\n\n", path);
}

/* ── Kernel D sweep ──────────────────────────────────────────── */

static void sweep_kernel_d(const char* csv_dir)
{
    printf("────── Kernel D: multi_phase_sim ──────\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/kernel_d_multi_phase.csv", csv_dir);
    FILE* f = open_csv(path);
    fprintf(f, "pool_size,stride_bytes,inner_iters,round_us\n");

    /* focused sweep: key configurations */
    int pool_focus[]   = { 20000, 32768, 65536, 131072 };
    int stride_focus[] = { 256, 512, 2048 };
    int iters_focus[]  = { 4, 16 };
    int n_pool   = 4;
    int n_stride = 3;
    int n_iters  = 2;

    for (int pi = 0; pi < n_pool; pi++) {
        for (int si = 0; si < n_stride; si++) {
            for (int ii = 0; ii < n_iters; ii++) {
                BenchParams bp = {};
                bp.pool_size       = pool_focus[pi];
                bp.path_state_size = stride_focus[si];
                bp.inner_iters     = iters_focus[ii];
                bp.active_ratio    = 0.65f;
                bp.warmup_rounds   = 5;
                bp.measure_rounds  = 50;

                BenchResult br = {};
                run_multi_phase_sim(bp, br);

                printf("  pool=%6d  stride=%5dB  iters=%2d  round=%.2f us\n",
                       bp.pool_size, bp.path_state_size, bp.inner_iters, br.round_us);
                fprintf(f, "%d,%d,%d,%.3f\n",
                        bp.pool_size, bp.path_state_size, bp.inner_iters, br.round_us);
            }
        }
    }
    fclose(f);
    printf("  → saved %s\n\n", path);
}

/* ── Projection ──────────────────────────────────────────────── */

/*
 * GPU trace time model:
 *
 *   At pool=20K (reference), measured GPU trace = 15-25s.
 *   Per-round trace time = T_trace_ref / total_rounds_ref.
 *   Decompose into:  per_round = launch_overhead + rays_in_batch / peak_throughput
 *   → derive launch_overhead and peak_throughput from reference point.
 *
 *   For different pool sizes, rays_per_round changes, so:
 *     per_round(pool) = launch_overhead + rays_per_round(pool) / peak_throughput
 *     T_trace(pool)   = total_rounds(pool) × per_round(pool)
 *
 *   This naturally captures: larger pool → bigger batch → fewer rounds →
 *   launch overhead amortised → lower T_trace.
 */
struct TraceModel {
    double launch_overhead_us;   /* per-round fixed cost (μs) */
    double peak_throughput;      /* rays/μs at infinite batch  */
    const char* label;

    /* solve from reference: T_ref, pool_ref, ray_need_ratio */
    static TraceModel from_reference(double T_ref_s, const char* lbl)
    {
        double rays_per_round_ref = REF_TRACE_POOL * RAY_NEED_RATIO;
        double total_rounds_ref   = REF_TOTAL_RAYS / rays_per_round_ref;
        double per_round_ref_us   = T_ref_s * 1e6 / total_rounds_ref;

        /* Assume launch overhead = 5 μs (typical CUDA kernel launch).
           Remainder is compute.  peak_throughput = rays / compute_us. */
        double launch_us = 5.0;
        double compute_us = per_round_ref_us - launch_us;
        if (compute_us < 1.0) compute_us = 1.0;  /* floor */
        double peak = rays_per_round_ref / compute_us;   /* rays/μs */

        TraceModel m;
        m.launch_overhead_us = launch_us;
        m.peak_throughput    = peak;
        m.label              = lbl;
        return m;
    }

    double estimate(int pool_size) const
    {
        double rays_per_round  = pool_size * RAY_NEED_RATIO;
        double total_rounds    = REF_TOTAL_RAYS / rays_per_round;
        double compute_us      = rays_per_round / peak_throughput;
        double per_round_us    = launch_overhead_us + compute_us;
        return total_rounds * per_round_us / 1e6;       /* seconds */
    }
};

static void compute_projection(const char* csv_dir)
{
    printf("══════ PROJECTION: GPU Wavefront wall-clock ══════\n\n");

    /* 3 trace scenarios derived from measured GPU trace at pool=20K */
    TraceModel models[3] = {
        TraceModel::from_reference(REF_GPU_TRACE_S_LOW,  "low(15s)"),
        TraceModel::from_reference(REF_GPU_TRACE_S_MID,  "mid(20s)"),
        TraceModel::from_reference(REF_GPU_TRACE_S_HIGH, "high(25s)"),
    };

    /* print model parameters */
    printf("  Trace model (derived from measured GPU trace at pool=20K):\n");
    for (int mi = 0; mi < 3; mi++) {
        printf("    %-9s  launch=%.1f us  peak_throughput=%.0f Mrays/s\n",
               models[mi].label,
               models[mi].launch_overhead_us,
               models[mi].peak_throughput);
    }
    printf("\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/projection.csv", csv_dir);
    FILE* f = open_csv(path);
    fprintf(f, "pool_size,stride_bytes,inner_iters,trace_scenario,"
               "T_cascade_s,T_trace_s,T_compact_s,T_launch_OH_s,T_other_s,T_total_s,"
               "vs_cpu_embree,verdict\n");

    const double launch_us = 5.0;
    const int phases_per_round = 5;

    int pool_proj[]   = { 20000, 32768, 65536, 131072 };
    int stride_proj[] = { 256, 512, 2048 };
    int n_pp = 4, n_sp = 3;

    for (int pi = 0; pi < n_pp; pi++) {
        for (int si = 0; si < n_sp; si++) {
            int pool   = pool_proj[pi];
            int stride = stride_proj[si];
            int iters  = 16;

            BenchParams bp = {};
            bp.pool_size       = pool;
            bp.path_state_size = stride;
            bp.inner_iters     = iters;
            bp.active_ratio    = 0.65f;
            bp.warmup_rounds   = 5;
            bp.measure_rounds  = 30;

            BenchResult br_cascade = {};
            run_cascade_stub(bp, br_cascade);

            BenchResult br_compact = {};
            run_compact_bench(bp, br_compact);

            BenchResult br_round = {};
            run_multi_phase_sim(bp, br_round);

            int n_active = (int)(pool * 0.65);
            double iters_per_launch = (double)n_active * iters;
            double total_rounds = REF_CASCADE_ITERS / iters_per_launch;

            double T_cascade = REF_CASCADE_ITERS / br_cascade.iters_per_sec;
            double T_compact = br_compact.compact_us * total_rounds / 1e6;
            double T_launch  = total_rounds * phases_per_round * launch_us / 1e6;

            double cascade_per_round = br_cascade.kernel_us;
            double compact_per_round = br_compact.compact_us;
            double other_per_round   = br_round.round_us - cascade_per_round - compact_per_round;
            if (other_per_round < 0.0) other_per_round = 0.0;
            double T_other = other_per_round * total_rounds / 1e6;

            printf("  pool=%6d  stride=%5dB  iters=%2d\n", pool, stride, iters);

            for (int mi = 0; mi < 3; mi++) {
                double T_trace = models[mi].estimate(pool);
                double T_total = T_cascade + T_trace + T_compact + T_launch + T_other;
                double vs_cpu  = T_total / REF_CPU_EMBREE_TOTAL_S;

                const char* verdict;
                if (T_total < 60.0)        verdict = "GO_STRONG";
                else if (T_total < 90.0)   verdict = "GO_MARGINAL";
                else if (T_total < 120.0)  verdict = "NOGO_EVEN";
                else                        verdict = "NOGO_WORSE";

                printf("    [%-9s] T_cas=%5.1f  T_trace=%5.1f  T_cmp=%4.1f  T_lnch=%4.1f  T_oth=%4.1f"
                       "  │ T_total=%5.1fs  (%.2f× CPU)  [%s]\n",
                       models[mi].label,
                       T_cascade, T_trace, T_compact, T_launch, T_other,
                       T_total, vs_cpu, verdict);

                fprintf(f, "%d,%d,%d,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%s\n",
                        pool, stride, iters, models[mi].label,
                        T_cascade, T_trace, T_compact, T_launch, T_other, T_total,
                        vs_cpu, verdict);
            }
            printf("\n");
        }
    }

    fclose(f);
    printf("  → saved %s\n", path);
}

/* ── Go/No-Go summary ────────────────────────────────────────── */

static void print_success_criteria()
{
    printf("\n══════ SUCCESS CRITERIA CHECK ══════\n\n");
    printf("  1. Kernel A: pool=20K stride=2048  →  kernel < 100 us ?  (check kernel_a CSV)\n");
    printf("  2. Kernel B: 16 iters  →  GPU iters/s >= 5× CPU (729 Miter/s) ?  (check kernel_b CSV)\n");
    printf("  3. Kernel D: pool=64K  →  projected T_total < 90s ?  (check projection CSV)\n\n");
    printf("If all three criteria pass → GPU Wavefront is feasible.\n");
    printf("═══════════════════════════════════\n\n");
}

/* ── main ─────────────────────────────────────────────────────── */

int main(int argc, char** argv)
{
    /* optional: specify output directory */
    const char* csv_dir = "../results";
    if (argc > 1)
        csv_dir = argv[1];

    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║   GPU Wavefront Feasibility Benchmark                 ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");

    print_gpu_info();

    sweep_kernel_a(csv_dir);
    sweep_kernel_b(csv_dir);
    sweep_kernel_c(csv_dir);
    sweep_kernel_d(csv_dir);
    compute_projection(csv_dir);
    print_success_criteria();

    printf("All benchmarks complete.\n");
    return 0;
}
