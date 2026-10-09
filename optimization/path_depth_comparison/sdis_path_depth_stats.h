/* Per-path depth statistics for CPU/GPU path depth comparison experiment.
 *
 * Activated by STARDIS_PATH_DEPTH_CSV=<output.csv> environment variable.
 * When active, each completed MC path writes one row to the CSV file with
 * detailed sub-path iteration counts.
 *
 * Usage:
 *   1. Add `struct path_depth_stats* depth_stats;` to rwalk_context.
 *   2. In solve_camera per-pixel loop: allocate a local path_depth_stats,
 *      set ctx.depth_stats = &stats; before launching the realisation.
 *   3. After the realisation completes, call path_depth_stats_write_row().
 *   4. At program end, call path_depth_stats_close().
 *
 * Thread safety: each OMP thread has its own FILE* via TLS or critical section.
 * Simplest approach: use a single #pragma omp critical around the write.
 *
 * Overhead: ~1 ns per size_t increment. For 10^9 iterations, ~1 s total.
 */
#ifndef SDIS_PATH_DEPTH_STATS_H
#define SDIS_PATH_DEPTH_STATS_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================= */
/* Per-path statistics record                                                */
/* ========================================================================= */
struct path_depth_stats {
  /* Level 0: top-level function calls in sample_coupled_path while loop */
  size_t total_func_calls;

  /* Level 1: sub-path physical iterations (inner loop counts) */
  size_t ds_steps;               /* delta-sphere do/while iterations */
  size_t wos_steps;              /* WoS for(;;) iterations */
  size_t rad_bounces;            /* radiative path bounce iterations */
  size_t cnv_steps;              /* convective null-collision iterations */

  /* Level 1b: sub-path entry counts (number of times each function entered) */
  size_t ds_entries;             /* conductive_path_delta_sphere called */
  size_t wos_entries;            /* conductive_path_wos called */
  size_t rad_entries;            /* trace_radiative_path called */
  size_t cnv_entries;            /* convective_path called */
  size_t bnd_entries;            /* boundary_path called */

  /* Total rays emitted */
  size_t rays_ds;                /* delta-sphere rays (2 per ds step) */
  size_t rays_rad;               /* radiative trace rays */
  size_t rays_ds_retry;          /* delta-sphere robust retry rays */

  /* Termination */
  int    done_reason;            /* 0=none, 1=rad_miss, 2=temp_known,
                                    3=boundary, 4=time_rewind, -1=failed */
};

#define PATH_DEPTH_STATS_NULL                                                  \
  { 0,0,0,0,0, 0,0,0,0,0, 0,0,0, 0 }

static const struct path_depth_stats PATH_DEPTH_STATS_ZERO =
  PATH_DEPTH_STATS_NULL;

/* ========================================================================= */
/* CSV file management                                                       */
/* ========================================================================= */

/* Global CSV file pointer (opened lazily on first write).
 * Set STARDIS_PATH_DEPTH_CSV environment variable to a file path to enable. */
static FILE*  s_depth_csv_fp      = NULL;
static int    s_depth_csv_checked = 0;

static FILE*
path_depth_csv_file(void)
{
  if(!s_depth_csv_checked) {
    const char* path = getenv("STARDIS_PATH_DEPTH_CSV");
    if(path && path[0]) {
      s_depth_csv_fp = fopen(path, "w");
      if(s_depth_csv_fp) {
        fprintf(s_depth_csv_fp,
          "px,py,spp,"
          "func_calls,"
          "ds_steps,wos_steps,rad_bounces,cnv_steps,"
          "ds_entries,wos_entries,rad_entries,cnv_entries,bnd_entries,"
          "rays_ds,rays_rad,rays_ds_retry,"
          "total_physical_iters,"
          "done_reason,"
          "T_value,T_done\n");
      }
    }
    s_depth_csv_checked = 1;
  }
  return s_depth_csv_fp;
}

/* Write one row for a completed path.  Thread-safe when called inside
 * #pragma omp critical.  */
static void
path_depth_stats_write_row(
  const struct path_depth_stats* s,
  size_t px, size_t py, size_t spp_idx,
  double T_value, int T_done)
{
  FILE* fp = path_depth_csv_file();
  if(!fp) return;
  {
    const size_t total = s->ds_steps + s->wos_steps
                       + s->rad_bounces + s->cnv_steps;
    fprintf(fp,
      "%lu,%lu,%lu,"           /* px, py, spp */
      "%lu,"                   /* func_calls */
      "%lu,%lu,%lu,%lu,"       /* ds/wos/rad/cnv steps */
      "%lu,%lu,%lu,%lu,%lu,"   /* entry counts */
      "%lu,%lu,%lu,"           /* rays */
      "%lu,"                   /* total_physical_iters */
      "%d,"                    /* done_reason */
      "%.17g,%d\n",            /* T_value, T_done */
      (unsigned long)px, (unsigned long)py, (unsigned long)spp_idx,
      (unsigned long)s->total_func_calls,
      (unsigned long)s->ds_steps,
      (unsigned long)s->wos_steps,
      (unsigned long)s->rad_bounces,
      (unsigned long)s->cnv_steps,
      (unsigned long)s->ds_entries,
      (unsigned long)s->wos_entries,
      (unsigned long)s->rad_entries,
      (unsigned long)s->cnv_entries,
      (unsigned long)s->bnd_entries,
      (unsigned long)s->rays_ds,
      (unsigned long)s->rays_rad,
      (unsigned long)s->rays_ds_retry,
      (unsigned long)total,
      s->done_reason,
      T_value, T_done);
  }
}

/* Close CSV file (call once at program end). */
static void
path_depth_stats_close(void)
{
  if(s_depth_csv_fp) {
    fclose(s_depth_csv_fp);
    s_depth_csv_fp = NULL;
    s_depth_csv_checked = 0;
  }
}

/* ========================================================================= */
/* Aggregation buffers for summary printing                                  */
/* ========================================================================= */
struct path_depth_summary {
  size_t count;
  size_t total_ds_steps;
  size_t total_wos_steps;
  size_t total_rad_bounces;
  size_t total_cnv_steps;
  size_t total_func_calls;
  size_t max_ds_steps;
  size_t max_wos_steps;
  size_t max_rad_bounces;
  size_t max_cnv_steps;
  size_t max_total_iters;
  size_t failed;
  size_t done_counts[5]; /* index 0..4 → done_reason 1..4; [4] = failed(-1) */

  /* Log2 histogram of total_physical_iters (bucket i = [2^i, 2^(i+1))) */
  size_t hist_log2[24]; /* covers up to 2^23 = 8,388,608 */
};

#define PATH_DEPTH_SUMMARY_NULL                                                \
  { 0,0,0,0,0,0, 0,0,0,0,0, 0, {0,0,0,0,0}, {0} }

/* Accumulate one path into the summary. */
static void
path_depth_summary_add(
  struct path_depth_summary* sum,
  const struct path_depth_stats* s)
{
  size_t total, bucket;
  if(!sum || !s) return;

  total = s->ds_steps + s->wos_steps + s->rad_bounces + s->cnv_steps;

  sum->count++;
  sum->total_ds_steps    += s->ds_steps;
  sum->total_wos_steps   += s->wos_steps;
  sum->total_rad_bounces += s->rad_bounces;
  sum->total_cnv_steps   += s->cnv_steps;
  sum->total_func_calls  += s->total_func_calls;

  if(s->ds_steps    > sum->max_ds_steps)    sum->max_ds_steps    = s->ds_steps;
  if(s->wos_steps   > sum->max_wos_steps)   sum->max_wos_steps   = s->wos_steps;
  if(s->rad_bounces > sum->max_rad_bounces) sum->max_rad_bounces = s->rad_bounces;
  if(s->cnv_steps   > sum->max_cnv_steps)   sum->max_cnv_steps   = s->cnv_steps;
  if(total          > sum->max_total_iters) sum->max_total_iters = total;

  if(s->done_reason == -1) { sum->failed++; sum->done_counts[4]++; }
  else if(s->done_reason >= 1 && s->done_reason <= 4)
    sum->done_counts[s->done_reason - 1]++;

  /* Log2 histogram */
  if(total == 0) { sum->hist_log2[0]++; }
  else {
    bucket = 0;
    { size_t v = total; while(v >>= 1) bucket++; }
    if(bucket >= 24) bucket = 23;
    sum->hist_log2[bucket]++;
  }
}

/* Print summary to stderr. */
static void
path_depth_summary_print(const struct path_depth_summary* sum)
{
  size_t i;
  if(!sum || sum->count == 0) return;

  fprintf(stderr,
    "\n=== Path Depth Statistics (CPU) ===\n"
    "total_paths:     %lu\n"
    "failed_paths:    %lu\n\n"
    "--- Maximums ---\n"
    "  max ds_steps:           %lu\n"
    "  max wos_steps:          %lu\n"
    "  max rad_bounces:        %lu\n"
    "  max cnv_steps:          %lu\n"
    "  max total_physical:     %lu\n\n"
    "--- Averages ---\n"
    "  avg ds_steps:           %.1f\n"
    "  avg wos_steps:          %.1f\n"
    "  avg rad_bounces:        %.1f\n"
    "  avg cnv_steps:          %.1f\n"
    "  avg func_calls:         %.1f\n\n"
    "--- Termination Reasons ---\n"
    "  rad_miss(1):    %lu  (%.2f%%)\n"
    "  temp_known(2):  %lu  (%.2f%%)\n"
    "  boundary(3):    %lu  (%.2f%%)\n"
    "  time_rewind(4): %lu  (%.2f%%)\n"
    "  failed(-1):     %lu  (%.2f%%)\n\n",
    (unsigned long)sum->count,
    (unsigned long)sum->failed,
    (unsigned long)sum->max_ds_steps,
    (unsigned long)sum->max_wos_steps,
    (unsigned long)sum->max_rad_bounces,
    (unsigned long)sum->max_cnv_steps,
    (unsigned long)sum->max_total_iters,
    (double)sum->total_ds_steps / (double)sum->count,
    (double)sum->total_wos_steps / (double)sum->count,
    (double)sum->total_rad_bounces / (double)sum->count,
    (double)sum->total_cnv_steps / (double)sum->count,
    (double)sum->total_func_calls / (double)sum->count,
    (unsigned long)sum->done_counts[0],
    100.0*(double)sum->done_counts[0]/(double)sum->count,
    (unsigned long)sum->done_counts[1],
    100.0*(double)sum->done_counts[1]/(double)sum->count,
    (unsigned long)sum->done_counts[2],
    100.0*(double)sum->done_counts[2]/(double)sum->count,
    (unsigned long)sum->done_counts[3],
    100.0*(double)sum->done_counts[3]/(double)sum->count,
    (unsigned long)sum->failed,
    100.0*(double)sum->failed/(double)sum->count);

  fprintf(stderr, "--- Histogram (total_physical_iters, log2 buckets) ---\n");
  for(i = 0; i < 24; i++) {
    if(sum->hist_log2[i] > 0) {
      fprintf(stderr, "  [%7lu, %7lu): %lu paths\n",
        (unsigned long)(i == 0 ? 0 : (1UL << i)),
        (unsigned long)(1UL << (i+1)),
        (unsigned long)sum->hist_log2[i]);
    }
  }
  fprintf(stderr, "\n");
}

#endif /* SDIS_PATH_DEPTH_STATS_H */
