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

#ifndef STARDIS_APP_H
#define STARDIS_APP_H

#include "stardis-args.h"
#include "stardis-description.h"
#include "stardis-extern-source.h"
#include "stardis-radiative-env.h"

#include <star/sg3d.h>

#include <rsys/rsys.h>
#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/dynamic_array_size_t.h>
#include <rsys/dynamic_array_uint.h>
#include <rsys/dynamic_array.h>
#include <rsys/str.h>

#include <sdis.h>

#ifdef STARDIS_ENABLE_MPI
  #include <mpi.h>
#endif

/* Forward declarations */
struct logger;
struct mem_allocator;
struct sdis_medium;

struct args;
struct solid;
struct fluid;

/* Utility macros */
#define ERR(Expr) if((res = (Expr)) != RES_OK) goto error; else (void)0

#define VFATAL(Fmt, Args) \
  { \
    fprintf(stderr, Fmt COMMA_##Args LIST_##Args); \
    ASSERT(0); \
    abort(); \
  } (void)0


#define DELTA_AUTO INF /* Placeholder until actual value is substituted */
#define UNKNOWN_MEDIUM_TEMPERATURE SDIS_TEMPERATURE_NONE

enum properties_conflict_t {
  NO_PROPERTY_CONFLICT,
  BOUND_H_FOR_FLUID_BETWEEN_2_DEFS,
  BOUND_H_FOR_FLUID_BETWEEN_2_UNDEFS,
  BOUND_H_FOR_FLUID_ENCLOSING_SOLID,
  BOUND_H_FOR_SOLID_BETWEEN_2_DEFS,
  BOUND_H_FOR_SOLID_BETWEEN_2_UNDEFS,
  BOUND_H_FOR_SOLID_ENCLOSING_FLUID,
  BOUND_HF_FOR_SOLID_BETWEEN_2_DEFS,
  BOUND_HF_FOR_SOLID_BETWEEN_2_UNDEFS,
  BOUND_HF_FOR_SOLID_ENCLOSING_FLUID,
  BOUND_T_FOR_SOLID_BETWEEN_2_DEFS,
  BOUND_T_FOR_SOLID_BETWEEN_2_UNDEFS,
  BOUND_T_FOR_SOLID_ENCLOSING_FLUID,
  BOUND_F_FOR_SOLID_BETWEEN_2_DEFS,
  BOUND_F_FOR_SOLID_BETWEEN_2_UNDEFS,
  BOUND_F_FOR_SOLID_ENCLOSING_FLUID,
  SFCONNECT_BETWEEN_2_SOLIDS,
  SFCONNECT_BETWEEN_2_FLUIDS,
  SFCONNECT_USED_AS_BOUNDARY,
  SFCONNECT_BETWEEN_2_UNDEFS,
  NO_CONNECTION_BETWEEN_2_FLUIDS,
  NO_CONNECTION_BETWEEN_SOLID_AND_FLUID,
  NO_BOUND_BETWEEN_FLUID_AND_UNDEF,
  NO_BOUND_BETWEEN_SOLID_AND_UNDEF,
  TRG_WITH_NO_PROPERTY,
  PROPERTIES_CONFLICT_COUNT__
};

#define DARRAY_NAME interface_ptrs
#define DARRAY_DATA struct sdis_interface*
#include <rsys/dynamic_array.h>

struct dummies {
  struct sdis_medium* dummy_fluid;
  unsigned dummy_fluid_id;
  struct fluid* stardis_fluid;
  struct sdis_medium* dummy_solid;
  unsigned dummy_solid_id;
  struct solid* stardis_solid;
};
#define DUMMIES_NULL__ {\
  NULL, UINT_MAX, NULL, NULL, UINT_MAX, NULL\
}
static const struct dummies DUMMIES_NULL;

static FINLINE void
init_media_ptr
  (struct mem_allocator* alloc,
   struct sdis_medium** data)
{
  ASSERT(data); (void)alloc;
  *data = NULL;
}

#define DARRAY_NAME media_ptr
#define DARRAY_DATA struct sdis_medium*
#define DARRAY_FUNCTOR_INIT init_media_ptr
#include <rsys/dynamic_array.h>

struct geometry {
  struct sg3d_geometry* sg3d;
  struct darray_interface_ptrs interf_bytrg;
  struct darray_interface_ptrs interfaces;
};

void
release_geometry
  (struct geometry* geom);

res_T
init_geometry
  (struct logger* logger,
   struct mem_allocator* allocator,
   const int verbose,
   struct geometry* geom);

/******************************************************************************/
enum stardis_output_fmt {
  STARDIS_RENDERING_OUTPUT_FILE_FMT_VTK,
  STARDIS_RENDERING_OUTPUT_FILE_FMT_HT
};

struct camera {
  double pos[3];
  double tgt[3];
  double up[3];
  enum stardis_output_fmt fmt;
  double fov;
  double time_range[2];
  unsigned spp;
  unsigned img_width, img_height;
  int auto_look_at;
  struct str file_name;
};

void
init_camera
  (struct mem_allocator* alloc, struct camera* cam);

/******************************************************************************/
void
release_camera(struct camera* cam);

void
log_err_fn(const char* msg, void* ctx);

void
log_warn_fn(const char* msg, void* ctx);

void
log_prt_fn(const char* msg, void* ctx);

struct counts {
  unsigned smed_count, fmed_count, tbound_count, hbound_count,
    fbound_count, sfconnect_count, ssconnect_count;
};
#define COUNTS_NULL__ {\
  0, 0, 0, 0, 0, 0, 0\
 }

/* Type to store the primitives of a compute region */
#define DARRAY_NAME sides
#define DARRAY_DATA enum sdis_side
#include <rsys/dynamic_array.h>

#define DARRAY_NAME descriptions
#define DARRAY_DATA struct description
#define DARRAY_FUNCTOR_INIT init_description
#include <rsys/dynamic_array.h>

struct compute_surface {
  struct darray_size_t primitives;
  struct darray_sides sides;
  struct darray_uint err_triangles;
  double area; /* in m2, regardless of scale factor */
};

struct stardis {
  struct dummies dummies; /* dummy meterials for boundaries' outside */
  struct geometry geometry;
  struct sdis_scene* sdis_scn; /* The solver scene */
  struct darray_descriptions descriptions;  /* Materials and boundaries */
  struct darray_media_ptr media;
  struct senc3d_scene* senc3d_scn;
  struct counts counts;

  double probe[3]; /* x,y,z of probe when mode is PROBE_COMPUTE */
  double time_range[2]; /* compute time */
  struct camera camera; /* camera when mode is IR_COMPUTE */
  struct str solve_name; /* medium name when mode is MEDIUM_COMPUTE,
                            boundary file name when [FLUX_]BOUNDARY_COMPUTE
                            map file name when MAP_COMPUTE */
  struct compute_surface compute_surface; /* 2D compute region when mode is
                                             [FLUX_]BOUNDARY_COMPUTE
                                             or MAP_COMPUTE */

  /* Min/Max temperatures used to linearize radiative transfer */
  double t_range[2]; /* [K] */

  struct str dump_model_filename;
  struct str paths_filename;
  struct str bin_green_filename;
  struct str end_paths_filename;
  struct str rndgen_state_in_filename;
  struct str rndgen_state_out_filename;
  struct str chunks_prefix;
  struct mem_allocator* allocator;
  struct logger* logger;
  struct sdis_device* dev;
  struct radiative_env radenv;
  int radenv_def;
  size_t samples;
  double scale_factor;
  double initial_time; /* [s] */
  int mode;
  unsigned nthreads;
  unsigned picard_order;
  unsigned next_medium_id;
  unsigned undefined_medium_behind_boundary_id;
  enum sdis_diffusion_algorithm diff_algo;
  int dump_paths;
  int verbose;
  int geometry_initialized;
  int mpi_initialized;
  int mpi_rank;

  struct darray_probe_boundary probe_boundary_list;
  struct extern_source extsrc;
  int disable_intrad; /* Disable internal radiative exchanges */
};

unsigned
allocate_stardis_medium_id
  (struct stardis* stardis);

#ifdef STARDIS_ENABLE_MPI
extern LOCAL_SYM res_T
init_mpi
  (int* pargc,
   char** pargv[],
   void (*prt_err_fn)(const char* msg, void* ctx),
   void (*prt_warn_fn)(const char* msg, void* ctx));

extern LOCAL_SYM void
finalize_mpi(void);
#endif

res_T
stardis_init
  (const struct args* args,
   struct logger* logger,
   struct mem_allocator* allocator,
   struct stardis* stardis);

void
stardis_release
  (struct stardis* stardis);

res_T
init_enclosures
  (struct stardis* stardis);

res_T
validate_properties
  (const unsigned itri,
   const unsigned properties[SG3D_PROP_TYPES_COUNT__],
   void* context,
   int* properties_conflict_status);

#endif /*STARDIS-APP_H*/
