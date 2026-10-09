/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef TEST_SDIS_UTILS_H
#define TEST_SDIS_UTILS_H

#include "sdis.h"

#include <rsys/double33.h>
#include <rsys/mem_allocator.h>
#include <stdio.h>
#include <string.h>

#ifdef SDIS_ENABLE_MPI
  #include <mpi.h>
#endif

#define BOLTZMANN_CONSTANT 5.6696e-8 /* W/m^2/K^4 */

#define OK(Cond) CHK((Cond) == RES_OK)
#define BA(Cond) CHK((Cond) == RES_BAD_ARG)
#define BO(Cond) CHK((Cond) == RES_BAD_OP)

/*******************************************************************************
 * Box geometry
 ******************************************************************************/
static const double box_vertices[8/*#vertices*/*3/*#coords per vertex*/] = {
  0.0, 0.0, 0.0,
  1.0, 0.0, 0.0,
  0.0, 1.0, 0.0,
  1.0, 1.0, 0.0,
  0.0, 0.0, 1.0,
  1.0, 0.0, 1.0,
  0.0, 1.0, 1.0,
  1.0, 1.0, 1.0
};
static const size_t box_nvertices = sizeof(box_vertices) / (sizeof(double)*3);

/* The following array lists the indices toward the 3D vertices of each
 * triangle.
 *        ,2---,3           ,2----3
 *      ,' | ,'/|         ,'/| \  |
 *    6----7' / |       6' / |  \ |        Y
 *    |',  | / ,1       | / ,0---,1        |
 *    |  ',|/,'         |/,' | ,'          o--X
 *    4----5'           4----5'           /
 *  Front, right      Back, left and     Z
 * and Top faces       bottom faces */
static const size_t box_indices[12/*#triangles*/*3/*#indices per triangle*/] = {
  0, 2, 1, 1, 2, 3, /* -Z */
  0, 4, 2, 2, 4, 6, /* -X */
  4, 5, 6, 6, 5, 7, /* +Z */
  3, 7, 5, 5, 1, 3, /* +X */
  2, 6, 7, 7, 3, 2, /* +Y */
  0, 1, 5, 5, 4, 0  /* -Y */
};
static const size_t box_ntriangles = sizeof(box_indices) / (sizeof(size_t)*3);

static INLINE void
box_get_indices(const size_t itri, size_t ids[3], void* context)
{
  (void)context;
  CHK(ids);
  CHK(itri < box_ntriangles);
  ids[0] = box_indices[itri*3+0];
  ids[1] = box_indices[itri*3+1];
  ids[2] = box_indices[itri*3+2];
}

static INLINE void
box_get_position(const size_t ivert, double pos[3], void* context)
{
  (void)context;
  CHK(pos);
  CHK(ivert < box_nvertices);
  pos[0] = box_vertices[ivert*3+0];
  pos[1] = box_vertices[ivert*3+1];
  pos[2] = box_vertices[ivert*3+2];
}

static INLINE void
box_get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  CHK(itri < box_ntriangles);
  *bound = interfaces[itri];
}

/*******************************************************************************
 * Square geometry
 ******************************************************************************/
static const double square_vertices[4/*#vertices*/*2/*#coords per vertex*/] = {
  1.0, 0.0,
  0.0, 0.0,
  0.0, 1.0,
  1.0, 1.0
};
static const size_t square_nvertices = sizeof(square_vertices)/(sizeof(double)*2);

static const size_t square_indices[4/*#segments*/*2/*#indices per segment*/]= {
  0, 1, /* Bottom */
  1, 2, /* Left */
  2, 3, /* Top */
  3, 0 /* Right */
};
static const size_t square_nsegments = sizeof(square_indices)/(sizeof(size_t)*2);

static INLINE void
square_get_indices(const size_t iseg, size_t ids[2], void* context)
{
  (void)context;
  CHK(ids);
  CHK(iseg < square_nsegments);
  ids[0] = square_indices[iseg*2+0];
  ids[1] = square_indices[iseg*2+1];
}

static INLINE void
square_get_position(const size_t ivert, double pos[2], void* context)
{
  (void)context;
  CHK(pos);
  CHK(ivert < square_nvertices);
  pos[0] = square_vertices[ivert*2+0];
  pos[1] = square_vertices[ivert*2+1];
}

static INLINE void
square_get_interface
  (const size_t iseg, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  CHK(iseg < square_nsegments);
  *bound = interfaces[iseg];
}

/*******************************************************************************
 * Medium, interface and ray
 ******************************************************************************/
static INLINE double
dummy_medium_getter
  (const struct sdis_rwalk_vertex* vert, struct sdis_data* data)
{
  (void)data;
  CHK(vert != NULL);
  return 0;
}

static INLINE double
dummy_interface_getter
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  (void)data;
  CHK(frag != NULL);
  return 0;
}

static INLINE double
dummy_radiative_interface_getter
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  (void)data, (void)source_id;
  CHK(frag != NULL);
  return 0;
}

static INLINE double
dummy_ray_getter(const struct sdis_radiative_ray* ray, struct sdis_data* data)
{
  (void)data;
  CHK(ray != NULL);
  return 0;
}

static const struct sdis_solid_shader DUMMY_SOLID_SHADER = {
  dummy_medium_getter, /* Calorific capacity */
  dummy_medium_getter, /* Thermal conductivity */
  dummy_medium_getter, /* Volumic mass */
  dummy_medium_getter, /* Delta */
  dummy_medium_getter, /* Volumic power */
  dummy_medium_getter, /* Temperature */
  NULL, /* sample path */
  0 /* Initial time */
};

static const struct sdis_fluid_shader DUMMY_FLUID_SHADER = {
  dummy_medium_getter, /* Calorific capacity */
  dummy_medium_getter, /* Volumic mass */
  dummy_medium_getter, /* Temperature */
  0 /* Initial time */
};

#define DUMMY_INTERFACE_SIDE_SHADER__ {                                        \
  dummy_interface_getter, /* Temperature */                                    \
  dummy_interface_getter, /* Flux */                                           \
  dummy_radiative_interface_getter, /* Emissivity */                           \
  dummy_radiative_interface_getter, /* Specular fraction */                    \
  dummy_interface_getter, /* Reference temperature */                          \
  1 /* Handle external flux */                                                 \
}
static const struct sdis_interface_shader DUMMY_INTERFACE_SHADER = {
  dummy_interface_getter, /* Convection coef */
  0, /* Upper bound of the convection coef */
  dummy_interface_getter, /* Thermal contact resistance */
  DUMMY_INTERFACE_SIDE_SHADER__, /* Front side */
  DUMMY_INTERFACE_SIDE_SHADER__ /* Back side */
};

static const struct sdis_radiative_env_shader DUMMY_RAY_SHADER = {
  dummy_ray_getter,
  dummy_ray_getter
};

/*******************************************************************************
 * Device creation
 ******************************************************************************/
#ifndef SDIS_ENABLE_MPI

static INLINE void
create_default_device
  (int* argc,
   char*** argv,
   int* is_master_process,
   struct sdis_device** dev)
{
  (void)argc, (void)argv;
  CHK(dev && is_master_process);
  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, dev));
  *is_master_process = 1;
}

#else

static INLINE void
create_default_device
  (int* pargc,
   char*** pargv,
   int* is_master_process,
   struct sdis_device** out_dev)
{
  struct sdis_device_create_args dev_args = SDIS_DEVICE_CREATE_ARGS_DEFAULT;
  struct sdis_device* dev = NULL;
  int mpi_thread_support;
  int mpi_rank;
  CHK(pargc && pargv && is_master_process && out_dev);

  CHK(MPI_Init_thread
    (pargc, pargv, MPI_THREAD_SERIALIZED, &mpi_thread_support) == MPI_SUCCESS);
  CHK(mpi_thread_support >= MPI_THREAD_SERIALIZED);

  dev_args.use_mpi = *pargc >= 2 && !strcmp((*pargv)[1], "mpi");
  OK(sdis_device_create(&dev_args, &dev));

  if(dev_args.use_mpi) {
    OK(sdis_device_get_mpi_rank(dev, &mpi_rank));
    *is_master_process = mpi_rank == 0;
  } else {
    CHK(sdis_device_get_mpi_rank(dev, &mpi_rank) == RES_BAD_OP);
    *is_master_process = 1;
  }
  *out_dev = dev;
}
#endif

static INLINE void
free_default_device(struct sdis_device* dev)
{
  OK(sdis_device_ref_put(dev));
#ifdef SDIS_ENABLE_MPI
  CHK(MPI_Finalize() == MPI_SUCCESS);
#endif
}

/*******************************************************************************
 * Miscellaneous
 ******************************************************************************/
static INLINE void
dump_mesh
  (FILE* stream,
   const double* pos,
   const size_t npos,
   const size_t* ids,
   const size_t nids)
{
  size_t i;
  CHK(pos != NULL && npos != 0);
  CHK(ids != NULL && nids != 0);
  FOR_EACH(i, 0, npos) {
    fprintf(stream, "v %g %g %g\n", SPLIT3(pos+i*3));
  }
  FOR_EACH(i, 0, nids) {
    fprintf(stream, "f %lu %lu %lu\n",
      (unsigned long)(ids[i*3+0] + 1),
      (unsigned long)(ids[i*3+1] + 1),
      (unsigned long)(ids[i*3+2] + 1));
  }
  fflush(stream);
}

static INLINE void
dump_segments
  (FILE* stream,
   const double* pos,
   const size_t npos,
   const size_t* ids,
   const size_t nids)
{
  size_t i;
  CHK(pos != NULL && npos != 0);
  CHK(ids != NULL && nids != 0);
  FOR_EACH(i, 0, npos) {
    fprintf(stream, "v %g %g 0\n", SPLIT2(pos+i*2));
  }
  FOR_EACH(i, 0, nids) {
    fprintf(stream, "l %lu %lu\n",
      (unsigned long)(ids[i*2+0] + 1),
      (unsigned long)(ids[i*2+1] + 1));
  }
  fflush(stream);
}

static INLINE void
check_estimator_eq
  (const struct sdis_estimator* e1, const struct sdis_estimator* e2)
{
  struct sdis_mc mc1, mc2;
  enum sdis_estimator_type type1, type2;
  ASSERT(e1 && e2);

  OK(sdis_estimator_get_type(e1, &type1));
  OK(sdis_estimator_get_type(e2, &type2));
  CHK(type1 == type2);

  switch(type1) {
    case SDIS_ESTIMATOR_TEMPERATURE:
      OK(sdis_estimator_get_temperature(e1, &mc1));
      OK(sdis_estimator_get_temperature(e2, &mc2));
      CHK(mc1.E + 3*mc1.SE >= mc2.E - 3*mc2.SE);
      CHK(mc1.E - 3*mc1.SE <= mc2.E + 3*mc2.SE);
      break;

    case SDIS_ESTIMATOR_FLUX:
      OK(sdis_estimator_get_convective_flux(e1, &mc1));
      OK(sdis_estimator_get_convective_flux(e2, &mc2));
      CHK(mc1.E + 3*mc1.SE >= mc2.E - 3*mc2.SE);
      CHK(mc1.E - 3*mc1.SE <= mc2.E + 3*mc2.SE);

      OK(sdis_estimator_get_radiative_flux(e1, &mc1));
      OK(sdis_estimator_get_radiative_flux(e2, &mc2));
      CHK(mc1.E + 3*mc1.SE >= mc2.E - 3*mc2.SE);
      CHK(mc1.E - 3*mc1.SE <= mc2.E + 3*mc2.SE);

      OK(sdis_estimator_get_total_flux(e1, &mc1));
      OK(sdis_estimator_get_total_flux(e2, &mc2));
      CHK(mc1.E + 3*mc1.SE >= mc2.E - 3*mc2.SE);
      CHK(mc1.E - 3*mc1.SE <= mc2.E + 3*mc2.SE);
      break;

    case SDIS_ESTIMATOR_POWER:
      OK(sdis_estimator_get_power(e1, &mc1));
      OK(sdis_estimator_get_power(e2, &mc2));
      CHK(mc1.E + 3*mc1.SE >= mc2.E - 3*mc2.SE);
      CHK(mc1.E - 3*mc1.SE <= mc2.E + 3*mc2.SE);
      break;

    default: FATAL("Unreachable code.\n"); break;
  }
}

static INLINE void
check_estimator_eq_strict
  (const struct sdis_estimator* e1, const struct sdis_estimator* e2)
{
  struct sdis_mc mc1, mc2;
  enum sdis_estimator_type type1, type2;
  ASSERT(e1 && e2);

  OK(sdis_estimator_get_type(e1, &type1));
  OK(sdis_estimator_get_type(e2, &type2));
  CHK(type1 == type2);

  switch(type1) {
    case SDIS_ESTIMATOR_TEMPERATURE:
      OK(sdis_estimator_get_temperature(e1, &mc1));
      OK(sdis_estimator_get_temperature(e2, &mc2));
      CHK(mc1.E == mc2.E && mc1.V == mc2.V && mc1.SE == mc2.SE);
      break;

    case SDIS_ESTIMATOR_FLUX:
      OK(sdis_estimator_get_convective_flux(e1, &mc1));
      OK(sdis_estimator_get_convective_flux(e2, &mc2));
      CHK(mc1.E == mc2.E && mc1.V == mc2.V && mc1.SE == mc2.SE);

      OK(sdis_estimator_get_radiative_flux(e1, &mc1));
      OK(sdis_estimator_get_radiative_flux(e2, &mc2));
      CHK(mc1.E == mc2.E && mc1.V == mc2.V && mc1.SE == mc2.SE);

      OK(sdis_estimator_get_total_flux(e1, &mc1));
      OK(sdis_estimator_get_total_flux(e2, &mc2));
      CHK(mc1.E == mc2.E && mc1.V == mc2.V && mc1.SE == mc2.SE);
      break;

    case SDIS_ESTIMATOR_POWER:
      OK(sdis_estimator_get_power(e1, &mc1));
      OK(sdis_estimator_get_power(e2, &mc2));
      CHK(mc1.E == mc2.E && mc1.V == mc2.V && mc1.SE == mc2.SE);
      break;

    default: FATAL("Unreachable code.\n"); break;
  }
}

static INLINE void
check_memory_allocator(struct mem_allocator* allocator)
{
  if(MEM_ALLOCATED_SIZE(allocator)) {
    char dump[1024];
    MEM_DUMP(allocator, dump, sizeof(dump));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks.\n");
  }
}

extern LOCAL_SYM void
check_green_function
  (struct sdis_green_function* green);

extern LOCAL_SYM void
dump_heat_paths
  (FILE* stream,
   const struct sdis_estimator* estimator);

extern LOCAL_SYM void
check_green_serialization
  (struct sdis_green_function* green,
   struct sdis_scene* scn);

#endif /* TEST_SDIS_UTILS_H */
