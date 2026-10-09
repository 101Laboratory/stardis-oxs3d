/* Copyright (C) 2018-2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#ifndef SDIS_OUTPUT_H
#define SDIS_OUTPUT_H

#include <rsys/rsys.h>
#include <stdio.h>

struct sdis_green_path;
struct sdis_heat_path;
struct sdis_estimator_buffer;
struct sdis_green_function;
struct sdis_mc;
struct counts;
struct description;
struct mem_allocator;
struct sdis_estimator;
struct stardis;
struct stardis_probe_boundary;
struct geometry;
struct vertex;
struct darray_estimators;
struct time;
struct ssp_rng;

struct dump_path_context {
  unsigned long rank;
  struct stardis* stardis;
};
#define DUMP_PATH_CONTEXT_NULL__ {0,NULL}
static const struct dump_path_context DUMP_PATH_CONTEXT_NULL =
  DUMP_PATH_CONTEXT_NULL__;

extern LOCAL_SYM res_T
dump_path
  (const struct sdis_heat_path* path,
   void* context);

extern LOCAL_SYM res_T
print_sample
  (struct sdis_green_path* path,
   void* ctx);

extern LOCAL_SYM res_T
dump_vtk_image
  (const struct sdis_estimator_buffer* buf,
   FILE* stream);

extern LOCAL_SYM res_T
dump_ht_image
  (const struct sdis_estimator_buffer* buf,
   FILE* stream);

extern LOCAL_SYM res_T
dump_green_ascii
  (struct sdis_green_function* green,
   const struct stardis* stardis,
   FILE* stream);

extern LOCAL_SYM res_T
dump_green_bin
  (struct sdis_green_function* green,
   const struct stardis* stardis,
   FILE* stream);

extern LOCAL_SYM res_T
dump_paths_end
  (struct sdis_green_function* green,
   const struct stardis* stardis,
   FILE* stream);

extern LOCAL_SYM res_T
dump_enclosure_related_stuff_at_the_end_of_vtk
  (struct stardis* stardis,
   FILE* stream);

extern LOCAL_SYM res_T
print_computation_time
  (struct sdis_mc* time_per_realisation, /* Can be NULL */
   struct stardis* stardis,
   struct time* start,
   struct time* computation_start,
   struct time* computation_end,
   struct time* output_end); /* Can be NULL */

extern LOCAL_SYM res_T
print_single_MC_result
  (struct sdis_estimator* estimator,
   struct stardis* stardis,
   FILE* stream);

extern LOCAL_SYM res_T
print_single_MC_result_probe_boundary
  (struct stardis* stardis,
   const struct stardis_probe_boundary* probe,
   const struct sdis_estimator* estimator,
   FILE* stream);

extern LOCAL_SYM res_T
dump_map
  (const struct stardis* stardis,
   const struct darray_estimators* estimators,
   FILE* stream);

extern LOCAL_SYM res_T
dump_boundaries_at_the_end_of_vtk
  (const struct stardis* stardis,
   FILE* stream);

res_T
dump_compute_region_at_the_end_of_vtk
  (struct stardis* stardis,
   FILE* stream);

res_T
dump_model_as_c_chunks
  (struct stardis* stardis,
   FILE* stream);

res_T
write_random_generator_state
  (struct sdis_estimator* estimator,
   FILE* stream);

res_T
read_random_generator_state
  (struct ssp_rng* state,
   FILE* stream);

#endif
