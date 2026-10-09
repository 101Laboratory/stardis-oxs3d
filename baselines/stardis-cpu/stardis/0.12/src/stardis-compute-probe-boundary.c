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

#include "stardis-app.h"
#include "stardis-compute.h"
#include "stardis-output.h"
#include "stardis-prog-properties.h"
#include "stardis-solid.h"
#include "stardis-solid-prog.h"
#include "stardis-ssconnect.h"

#include <sdis.h>

#include <star/senc3d.h>
#include <star/ssp.h>

#include <rsys/logger.h>
#include <rsys/str.h>
#include <rsys/clock_time.h>
#ifdef _WIN32
#include <string.h>
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

struct filter_ctx {
  float distance;
  enum sg3d_property_type side;
};
#define FILTER_CTX_DEFAULT__ {0.f, SG3D_INTFACE}
static const struct filter_ctx FILTER_CTX_DEFAULT = FILTER_CTX_DEFAULT__;

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE const char*
sdis_side_to_cstr(const enum sdis_side side)
{
  const char* cstr = NULL;
  switch(side) {
    case SDIS_FRONT: cstr = "FRONT"; break;
    case SDIS_BACK: cstr = "BACK"; break;
    case SDIS_SIDE_NULL__: cstr = "UNDEFINED"; break;
    default: FATAL("Unreachable code.\n");
  }
  return cstr;
}

static res_T
read_rng_state
  (struct stardis* stardis,
   const char* filename,
   struct ssp_rng* rng)
{
  FILE* fp = NULL;
  res_T res = RES_OK;
  ASSERT(stardis && filename && rng);

  if(stardis->mpi_initialized && stardis->mpi_rank != 0) {
    goto exit; /* Non master process. Nothing to do */
  }

  if((fp = fopen(filename, "r")) == NULL) {
    logger_print(stardis->logger, LOG_ERROR,
      "Cannot open generator's state file ('%s').\n", filename);
    res = RES_IO_ERR;
    goto error;
  }

  res =  ssp_rng_read(rng, fp);
  if(res != RES_OK) {
    logger_print(stardis->logger, LOG_ERROR,
      "Cannot read random generator's state ('%s').\n", filename);
    goto error;
  }

exit:
  if(fp) CHK(fclose(fp) == 0);
  return res;
error:
  goto exit;
}

static res_T
write_rng_state
  (struct stardis* stardis,
   const char* filename,
   struct ssp_rng* rng_state)
{
  FILE* fp = NULL;
  res_T res = RES_OK;
  ASSERT(stardis && filename && rng_state);

  if(stardis->mpi_initialized && stardis->mpi_rank != 0) {
    goto exit; /* Non master process. Nothing to do */
  }

  if((fp = fopen(filename, "wb")) == NULL) {
    logger_print(stardis->logger, LOG_ERROR,
      "Cannot open generator's state file ('%s').\n", filename);
    res = RES_IO_ERR;
    goto error;
  }

  res = ssp_rng_write(rng_state, fp);
  if(res != RES_OK) {
    logger_print(stardis->logger, LOG_ERROR,
      "Cannot write random generator's state ('%s').\n", filename);
    res = RES_IO_ERR;
    goto error;
  }

exit:
  if(fp) CHK(fclose(fp) == 0);
  return res;
error:
  goto exit;
}

/* Filter used from a point query to determine not only one of the closest
 * point, but the better one if there are more than one. In some circumstances
 * it is not possible to determine the medium we are in using a given hit, but
 * it is possible using another equidistant hit :
 *
 *
 *   P             C
 *   +.............+---trg 1---
 *                 |
 *   medium 1    trg 2  medium 2
 *                 |
 *
 * C is the closest point from P, and 2 different hits at C are possible (one
 * on each triangle). However, only hit on trg 2 allows to find out that P is
 * in medium 1 using sign(PC.Ntrg1) as PC.Ntrg2 = 0.
 * The following filter function aims at selecting the hit on trg2 regardless
 * of the order in which the 2 triangles are checked.
 * One unexpected case cannot be decided though, but it implies that the
 * closest triangle has 2 different media on its sides and that P lies on the
 * triangle's plane :
 *
 *   P       C  medium 1
 *   +       +---trg---
 *              medium 2 */
static int
hit_filter
  (const struct s3d_hit* hit,
   const float ray_org[3],
   const float ray_dir[3],
   const float ray_range[2],
   void* ray_data,
   void* filter_data)
{
  struct filter_ctx* filter_ctx = ray_data;

  (void)ray_org, (void)ray_range, (void)filter_data;
  ASSERT(hit && filter_ctx);
  ASSERT(hit->uv[0] == CLAMP(hit->uv[0], 0, 1));
  ASSERT(hit->uv[1] == CLAMP(hit->uv[1], 0, 1));

  /* That's not the closest point. Keep the previous one if it can be used to
   * detect the medium (i.e. side != SG3D_INTFACE) */
  if(filter_ctx->distance == hit->distance && filter_ctx->side != SG3D_INTFACE) {
    return 1; /* Skip */
  }

  filter_ctx->distance = hit->distance;

  if(filter_ctx->distance == 0) {
    filter_ctx->side = SG3D_INTFACE;
  } else {
    float sign = 0;
    float N[3] = {0,0,0}; /* Normalized normal */

    /* Calculate the dot product with normalized vectors limits the numerical
     * inaccuracies on its sign */
    f3_normalize(N, hit->normal);
    sign = f3_dot(ray_dir, N);

    /* Star3D hit normals are left-handed */
         if(sign < 0) filter_ctx->side = SG3D_FRONT;
    else if(sign > 0) filter_ctx->side = SG3D_BACK;
    else/*sign == 0*/ filter_ctx->side = SG3D_INTFACE;
  }

  return 0; /* Keep */
}

static INLINE res_T
find_closest_point
  (struct stardis* stardis,
   const double pos[3],
   struct filter_ctx* filter_ctx,
   size_t* iprim,
   double uv[2])
{
  struct sdis_scene_find_closest_point_args closest_pt_args =
    SDIS_SCENE_FIND_CLOSEST_POINT_ARGS_NULL;
  res_T res = RES_OK;
  ASSERT(stardis && pos && filter_ctx && iprim && uv);

  /* Find the surface point closest to the input position */
  closest_pt_args.position[0] = pos[0];
  closest_pt_args.position[1] = pos[1];
  closest_pt_args.position[2] = pos[2];
  closest_pt_args.radius = INF;
  closest_pt_args.filter_3d = hit_filter;
  closest_pt_args.filter_data = filter_ctx;
  ERR(sdis_scene_find_closest_point
    (stardis->sdis_scn , &closest_pt_args, iprim, uv));
  if(*iprim == SDIS_PRIMITIVE_NONE) {
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
check_move_to_solid_boundary
  (const struct stardis* stardis,
   const double pos[3], /* Original position */
   const double time, /* [s] */
   const struct description* desc, /* Solid medium in which pos lies */
   const size_t iprim, /* Triangle index to which to move */
   const double uv[2], /* Triangle coordinates to which to move */
   const double distance, /* Move distance */
   const int advice)
{
  struct stardis_vertex vtx = STARDIS_VERTEX_NULL;
  const char* prefix = "";
  const char* solid_name = "";
  double delta = 0;
  res_T res = RES_OK;

  /* Check pre-conditions */
  ASSERT(stardis && pos && time > 0 && desc && uv && distance >= 0);

  /* Retrieve the delta and define the prefix of the solid for log messages */
  switch(desc->type) {
    /* Regular solid, i.e. solid with constant properties */
    case DESC_MAT_SOLID:
      delta = desc->d.solid->delta;
      prefix = "";
      break;

    /* Solid with programmed properties */
    case DESC_MAT_SOLID_PROG:
      vtx.P[0] = pos[0];
      vtx.P[1] = pos[1];
      vtx.P[2] = pos[2];
      vtx.time = time;
      delta = desc->d.solid_prog->delta(&vtx, desc->d.solid_prog->prog_data);
      prefix = "programmed ";
      break;

    default: FATAL("Unreachable code.\n");
  }

  solid_name = str_cget(get_description_name(desc));
  logger_print(stardis->logger, LOG_OUTPUT, "Probe was in %ssolid '%s'.\n",
    prefix, solid_name);

  /* The position is close from the triangle */
  if(distance < 0.5*delta) {
    logger_print(stardis->logger, LOG_OUTPUT,
      "Probe was %g delta from closest boundary.\n",
      distance/delta);

  /* Notice that the position is a little far from the triangle */
  } else if(distance < 2*delta) {
    logger_print(stardis->logger, LOG_WARNING,
      "Probe was %g delta from closest boundary.%s\n",
      distance/delta,
      (advice ? " Consider using -p instead of -P.\n" : ""));

  /* The position is too far from the triangle */
  } else {
    logger_print(stardis->logger, LOG_ERROR,
      "Probe moved to (%g, %g, %g), primitive %lu, uv = (%g, %g). "
      "Move is %g delta long. Use -p instead of -P.\n",
      SPLIT3(pos), (unsigned long)iprim, SPLIT2(uv), distance/delta);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

/* This function checks nothing. It only records the status. It is named as the
 * one used to check the projection on the solid limit to make it symmetrical,
 * and thus simplify the reading of sources */
static res_T
check_move_to_fluid_boundary
  (struct stardis* stardis,
   const struct description* desc, /* Fluid medium in which pos lies */
   const double distance) /* Move distance */
{
  const char* prefix = "";
  const char* fluid_name = "";

  ASSERT(stardis && desc && distance >= 0);

  switch(desc->type) {
    case DESC_MAT_FLUID: prefix = ""; break;
    case DESC_MAT_FLUID_PROG: prefix = "programmed "; break;
    default: FATAL("Unreachable code.\n");
  }

  fluid_name = str_cget(get_description_name(desc));
  logger_print(stardis->logger, LOG_OUTPUT,
    "Probe was in %sfluid '%s'.\n", prefix, fluid_name);
  logger_print(stardis->logger, LOG_OUTPUT,
    "Probe distance from closest boundary was %g.\n", distance);

  return RES_OK;
}

static res_T
move_to_boundary
  (struct stardis* stardis,
   const double pos[3],
   const double time, /* [s] */
   const int is_probe_temp_computation,
   size_t* out_iprim,
   double uv[2])
{
  /* Position on boundary */
  struct filter_ctx filter_ctx = FILTER_CTX_DEFAULT;
  double proj_pos[3] = {0,0,0};
  size_t iprim = 0;

  /* Miscellaneous */
  size_t nvertices_close = 0;
  res_T res = RES_OK;

  /* Check pre-conditions */
  ASSERT(stardis && pos && time >= 0 && out_iprim && uv);

  ERR(find_closest_point(stardis, pos, &filter_ctx, &iprim, uv));

  if(filter_ctx.side != SG3D_INTFACE) {
    /* Properties */
    const struct description* desc_list = NULL;
    const struct description* desc = NULL;
    unsigned desc_ids[SG3D_PROP_TYPES_COUNT__];

    SG3D(geometry_get_unique_triangle_properties
      (stardis->geometry.sg3d, (unsigned)iprim, desc_ids));

    desc_list = darray_descriptions_cdata_get(&stardis->descriptions);

    /* Probe is outside the system */
    if(desc_ids[filter_ctx.side] == SG3D_UNSPECIFIED_PROPERTY) {
      logger_print(stardis->logger, LOG_WARNING,
        "Probe was outside the system.\n");

    /* Probe is in a medium */
    } else {
      desc = desc_list + desc_ids[filter_ctx.side];

      switch(desc->type) {
        case DESC_MAT_SOLID:
        case DESC_MAT_SOLID_PROG:
          ERR(check_move_to_solid_boundary
            (stardis, pos, time, desc, iprim, uv, filter_ctx.distance,
             is_probe_temp_computation));
          break;
        case DESC_MAT_FLUID:
        case DESC_MAT_FLUID_PROG:
          ERR(check_move_to_fluid_boundary
            (stardis, desc, filter_ctx.distance));
          break;
        default: FATAL("Unreachable code.\n");
      }
    }
  }

  SDIS(scene_get_boundary_position(stardis->sdis_scn, iprim, uv, proj_pos));

  /* Count the number of vertices that are close to the boundary position
   * and issue a warning if necessary */
  nvertices_close += CLAMP(uv[0], 0.0005, 0.9995) != uv[0];
  nvertices_close += CLAMP(uv[1], 0.0005, 0.9995) != uv[1];
  if(nvertices_close) {
    logger_print(stardis->logger, LOG_WARNING,
      "Probe %s close to / on %s. "
      "If computation fails, try moving it slightly.\n",
      filter_ctx.distance == 0 ? "is" : "moved",
      nvertices_close == 1 ? "an edge" : "a vertex");
  }

  /* Probe is on a boundary */
  if(filter_ctx.distance == 0) {
    logger_print(stardis->logger, LOG_OUTPUT,
      "Probe is on primitive %lu, uv = (%g, %g), not moved.\n",
      (unsigned long)iprim, SPLIT2(uv));

  /* Probe was projected on a boundary */
  } else {
    logger_print(stardis->logger, LOG_OUTPUT,
      "Probe moved to (%g, %g, %g), primitive %lu, uv = (%g, %g).\n",
      SPLIT3(proj_pos), (unsigned long)iprim, SPLIT2(uv));
  }

exit:
  *out_iprim = iprim;
  return res;
error:
  goto exit;
}

static res_T
setup_probe_side
  (struct stardis* stardis,
   const unsigned desc_ids[SG3D_PROP_TYPES_COUNT__],
   const char* side_str,
   const size_t iprim,
   enum sdis_side *out_side)
{
  const struct description* desc_list = NULL;
  const struct description* desc_front = NULL;
  const struct description* desc_back = NULL;
  size_t ndescs = 0;
  enum sdis_side side = SDIS_SIDE_NULL__;
  res_T res = RES_OK;
  (void)ndescs; /* Avoid "Unused variable" warnings in release */

  /* Check pre-conditions */
  ASSERT(stardis && side_str && desc_ids && out_side);

  /* Fetch the properties */
  desc_list = darray_descriptions_cdata_get(&stardis->descriptions);
  ndescs = darray_descriptions_size_get(&stardis->descriptions);
  desc_front = desc_list + desc_ids[SG3D_FRONT];
  desc_back = desc_list + desc_ids[SG3D_BACK];

  /* No side specified */
  if(!side_str || !strlen(side_str)) {
    side = SDIS_SIDE_NULL__;

  /* Set probe to front side */
  } else if(!strcasecmp(side_str, "FRONT")) {
    ASSERT(desc_ids[SG3D_FRONT] < ndescs && DESC_IS_MEDIUM(desc_front));
    side = SDIS_FRONT;

  /* Set probe to back side */
  } else if(!strcasecmp(side_str, "BACK")) {
    ASSERT(desc_ids[SG3D_BACK] < ndescs && DESC_IS_MEDIUM(desc_back));
    side = SDIS_BACK;

  /* Set the probe to the side that points to the submitted medium name */
  } else {
    unsigned med_id_probe = 0; /* Medium defined on the probe */
    unsigned med_id_front = 0; /* Medium on front side */
    unsigned med_id_back = 0; /* Medium on back side */
    ASSERT(DESC_IS_MEDIUM(desc_front) && DESC_IS_MEDIUM(desc_back));

    if(!find_medium_by_name(stardis, side_str, &med_id_probe)) {
      logger_print(stardis->logger, LOG_ERROR,
        "Cannot locate side from medium name '%s' (unknown medium)\n",
        side_str);
      res = RES_BAD_ARG;
      goto error;
    }

    description_get_medium_id(desc_front, &med_id_front);
    description_get_medium_id(desc_back, &med_id_back);

    /* Invalid probe medium wrt the boundary on which it is located */
    if(med_id_probe != med_id_front
    && med_id_probe != med_id_back) {
      logger_print(stardis->logger, LOG_ERROR,
        "Medium '%s' is not used at this interface (prim id=%lu)\n",
        side_str, (unsigned long)iprim);
      res = RES_BAD_ARG;
      goto error;
    }

    /* The same medium is used on both sides: cannot differentiate */
    if(med_id_front == med_id_back) {
      unsigned encs[2]; /* Identifier of the enclosures */

      ERR(senc3d_scene_get_triangle_enclosures
        (stardis->senc3d_scn, (unsigned)iprim, encs));
      logger_print(stardis->logger, LOG_ERROR,
        "Medium '%s' is used on both sides of this interface (prim id=%lu).\n",
        side_str, (unsigned long)iprim);
      logger_print(stardis->logger, LOG_ERROR,
        "Side must be defined using either FRONT or BACK.\n");
      logger_print(stardis->logger, LOG_ERROR,
        "FRONT side is related to enclosure %u, BACK side to enclosure %u.\n",
        encs[SENC3D_FRONT], encs[SENC3D_BACK]);

      res = RES_BAD_ARG;
      goto error;
    }

    side = med_id_probe == med_id_front ? SDIS_FRONT : SDIS_BACK;
  }

exit:
  *out_side = side;
  return res;
error:
  side = SDIS_SIDE_NULL__;
  goto exit;
}

/* This function checks the conformity between the potential thermal contact
 * resistance at the probe location and the specified probe side. */
static res_T
setup_thermal_contact_resistance
  (struct stardis* stardis,
   const unsigned desc_ids[SG3D_PROP_TYPES_COUNT__],
   const enum sdis_side probe_side)
{
  struct str log_msg;
  const struct description* desc_list = NULL;
  const struct description* desc_front = NULL;
  const struct description* desc_back = NULL;
  const struct description* desc_intface = NULL;
  size_t ndescs = 0;
  double tcr = 0;
  res_T res = RES_OK;
  (void)ndescs; /* Avoid "Unused variable" warnings in release */

  /* Check pre-conditions */
  ASSERT(stardis && desc_ids);

  str_init(stardis->allocator, &log_msg);

  /* Fetch the properties */
  desc_list = darray_descriptions_cdata_get(&stardis->descriptions);
  ndescs = darray_descriptions_size_get(&stardis->descriptions);
  desc_front = desc_list + desc_ids[SG3D_FRONT];
  desc_back = desc_list + desc_ids[SG3D_BACK];
  desc_intface = desc_list + desc_ids[SG3D_INTFACE];

  /* Get the thermal contact resistance between solid/solid connection if any */
  if(desc_ids[SG3D_INTFACE] != SG3D_UNSPECIFIED_PROPERTY
  && desc_intface->type == DESC_SOLID_SOLID_CONNECT) {
    ASSERT(desc_ids[SG3D_INTFACE] < ndescs);
    tcr = desc_intface->d.ss_connect->tcr;
  }

  /* Warn if side defined and no resistance defined */
  if(tcr == 0 && probe_side != SDIS_SIDE_NULL__) {
    logger_print(stardis->logger, LOG_WARNING,
      "Specifying a compute side at an interface with no contact resistance "
      "is meaningless.\n");
  }

  #define GET_DESC_NAME(Desc) str_cget(get_description_name(Desc))

  /* A thermal contact resistance cannot be defined if probe side is NULL */
  if(tcr != 0 && probe_side == SDIS_SIDE_NULL__) {
    logger_print(stardis->logger, LOG_ERROR,
      "Cannot let probe computation side unspecified on an interface with a "
      "non-nul thermal resistance.\n");

    /* Format the log string */
    if(desc_ids[SG3D_FRONT] != SG3D_UNSPECIFIED_PROPERTY) {
      ASSERT(desc_ids[SG3D_FRONT] < ndescs);
      ERR(str_append_printf(&log_msg, " FRONT: '%s'", GET_DESC_NAME(desc_front)));
    }
    if(desc_ids[SG3D_FRONT] != SG3D_UNSPECIFIED_PROPERTY
    && desc_ids[SG3D_BACK] != SG3D_UNSPECIFIED_PROPERTY) {
      ERR(str_append_char(&log_msg, ','));
    }
    if(desc_ids[SG3D_BACK] != SG3D_UNSPECIFIED_PROPERTY) {
      ASSERT(desc_ids[SG3D_BACK] < ndescs);
      ERR(str_append_printf(&log_msg, " BACK: '%s'", GET_DESC_NAME(desc_back)));
    }

    /* Print error message */
    logger_print(stardis->logger, LOG_ERROR,
      "Interface '%s',%s, resistance=%g K.m^2/W.\n",
      GET_DESC_NAME(desc_intface), str_cget(&log_msg), tcr);

    res = RES_BAD_ARG;
    goto error;
  }

  /* Log that a calculation is done on a boundary with tcr */
  if(tcr > 0) {
    const char* medium_name = probe_side == SDIS_FRONT
      ? GET_DESC_NAME(desc_front)
      : GET_DESC_NAME(desc_back);

    logger_print(stardis->logger, LOG_OUTPUT,
      "Probe computation on an interface with a thermal resistance = %g K.m^2/W "
      "on %s side (medium is '%s').\n",
      tcr, sdis_side_to_cstr(probe_side), medium_name);
  }

  #undef GET_DESC_NAME

exit:
  str_release(&log_msg);
  return res;
error:
  goto exit;
}

static res_T
solve
  (struct stardis* stardis,
   struct time* start,
   struct sdis_solve_probe_boundary_args* args,
   res_T output[2])
{
  struct time t0, t1;
  struct sdis_mc time = SDIS_MC_NULL;
  struct dump_path_context ctx = DUMP_PATH_CONTEXT_NULL;
  struct sdis_estimator* estimator = NULL;
  const struct str* rng_in = NULL;
  const struct str* rng_out = NULL;
  struct ssp_rng* rng = NULL;
  int is_master_process = 0;
  res_T res = RES_OK;
  ASSERT(stardis && args && output);

  is_master_process = !stardis->mpi_initialized || stardis->mpi_rank == 0;

  rng_in = &stardis->rndgen_state_in_filename;
  rng_out = &stardis->rndgen_state_out_filename;

  /* Read RNG state from file */
  if(!str_is_empty(rng_in)) {
    ERR(ssp_rng_create(stardis->allocator, SSP_RNG_THREEFRY, &rng));
    ERR(read_rng_state(stardis, str_cget(rng_in), rng));
    args->rng_state = rng;
  }

  /* Run the calculation */
  time_current(&t0);
  ERR(sdis_solve_probe_boundary(stardis->sdis_scn, args, &estimator));
  time_current(&t1);

  /* No more to do for non master processes */
  if(!is_master_process) goto exit;

  /* Per per realisation time */
  ERR(sdis_estimator_get_realisation_time(estimator, &time));
  ERR(print_computation_time(&time, stardis, start, &t0, &t1, NULL));

  /* Write outputs and save their status */
  ctx.stardis = stardis;
  ctx.rank = 0;
  output[0] = print_single_MC_result(estimator, stardis, stdout);
  output[1] = sdis_estimator_for_each_path(estimator, dump_path, &ctx);

  /* Write the resulting RNG state to a file */
  if(!str_is_empty(rng_out)) {
    struct ssp_rng* rng_state = NULL;
    ERR(sdis_estimator_get_rng_state(estimator, &rng_state));
    ERR(write_rng_state(stardis, str_cget(rng_out), rng_state));
  }

exit:
  if(estimator) SDIS(estimator_ref_put(estimator));
  if(rng) SSP(rng_ref_put(rng));
  return res;
error:
  goto exit;
}

static res_T
solve_list
  (struct stardis* stardis,
   struct time* start,
   struct sdis_solve_probe_boundary_list_args* args,
   res_T* output)
{
  struct time t0, t1;
  struct sdis_mc time = SDIS_MC_NULL; /* Time per realisation */
  struct sdis_estimator_buffer* buffer = NULL;
  size_t i = 0;
  size_t def[2] = {0, 0};
  int is_master_process = 0;
  res_T res = RES_OK;
  ASSERT(stardis && start && args);

  is_master_process = !stardis->mpi_initialized || stardis->mpi_rank == 0;

  /* Run the calculation */
  time_current(&t0);
  ERR(sdis_solve_probe_boundary_list(stardis->sdis_scn, args, &buffer));
  time_current(&t1);

  /* No more to do for non master processes */
  if(!is_master_process) goto exit;

  /* Retrieve the number of solved probes */
  ERR(sdis_estimator_buffer_get_definition(buffer, def));
  ASSERT(def[0] == darray_probe_boundary_size_get(&stardis->probe_boundary_list));
  ASSERT(def[1] == 1);

  ERR(sdis_estimator_buffer_get_realisation_time(buffer, &time));
  ERR(print_computation_time(&time, stardis, start, &t0, &t1, NULL));

  /* Print the estimated temperature of each probe */
  FOR_EACH(i, 0, def[0]) {
    const struct stardis_probe_boundary* probe = NULL;
    const struct sdis_estimator* estimator = NULL;
    res_T res2 = RES_OK;

    probe = darray_probe_boundary_cdata_get(&stardis->probe_boundary_list) + i;
    ERR(sdis_estimator_buffer_at(buffer, i, 0, &estimator));

    res2 = print_single_MC_result_probe_boundary
      (stardis, probe, estimator, stdout);
    if(res2 != RES_OK && *output == RES_OK) *output = res2;
  }

exit:
  if(buffer) SDIS(estimator_buffer_ref_put(buffer));
  return res;
error:
  goto exit;
}

static res_T
setup_solve_probe_boundary_flux_args
  (struct stardis* stardis,
   const struct stardis_probe_boundary* probe,
   struct sdis_solve_probe_boundary_flux_args* args)
{
  double uv[2] = {0, 0};
  size_t iprim = SIZE_MAX;
  unsigned desc_ids[SG3D_PROP_TYPES_COUNT__];
  res_T res = RES_OK;
  ASSERT(stardis && probe && args);

  /* Calculate the probe position on the boundary */
  ERR(move_to_boundary(stardis, probe->position, probe->time[0], 0, &iprim, uv));

  ERR(sg3d_geometry_get_unique_triangle_properties(stardis->geometry.sg3d,
    (unsigned)iprim, desc_ids));

  d3_set(stardis->probe, probe->position);
  /* Setup of solve input parameters */

  args->nrealisations = stardis->samples;
  args->iprim = iprim;
  d2_set(args->uv, uv);
  args->picard_order = stardis->picard_order;
  d2_set(args->time_range, stardis->time_range);
  args->diff_algo = stardis->diff_algo;

exit:
  return res;
error:
  goto exit;
}

static res_T
solve_flux_list
  (struct stardis* stardis,
   struct time* start,
   const struct stardis_probe_boundary* probes,
   size_t nprobes)
{
  struct time t0, t1;
  struct sdis_mc time = SDIS_MC_NULL; /* Time per realisation */
  struct sdis_estimator_buffer* buffer = NULL;
  size_t i = 0;
  struct sdis_estimator* estimator = NULL;
  FILE* stream_r = NULL;
  struct sdis_solve_probe_boundary_flux_args args;
  res_T res = RES_OK;
  ASSERT(stardis && start);

  /* Input random state? */
  READ_RANDOM_STATE(&stardis->rndgen_state_in_filename);

  time_current(&t0);
  for(i = 0; i < nprobes; i++) {
    args = SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT;
    ERR(setup_solve_probe_boundary_flux_args(stardis, &probes[i], &args));
    /* Run the calculation */
    ERR(sdis_solve_probe_boundary_flux(stardis->sdis_scn, &args, &estimator));
    /* Print the estimated temperature */
    ERR(print_single_MC_result(estimator, stardis, stdout));
    ERR(sdis_estimator_ref_put(estimator));
  }
  time_current(&t1);

  /* Output random state? */
  WRITE_RANDOM_STATE(&stardis->rndgen_state_out_filename);

  ERR(print_computation_time(&time, stardis, start, &t0, &t1, NULL));

exit:
  if(buffer) SDIS(estimator_buffer_ref_put(buffer));
  return res;
error:
  goto exit;
}

static res_T
solve_green
  (struct stardis* stardis,
   struct time* start,
   struct sdis_solve_probe_boundary_args* args)
{
  struct time t0/*calculation start*/, t1/*calculation end*/, t2/*output end*/;
  struct sdis_green_function* green = NULL;
  FILE* fp_green = NULL;
  FILE* fp_path = NULL;
  const struct str* rng_in = NULL;
  struct ssp_rng* rng = NULL;
  int is_master_process = 0;
  res_T res = RES_OK;

  ASSERT(stardis && args);

  is_master_process = !stardis->mpi_initialized || stardis->mpi_rank == 0;

  rng_in = &stardis->rndgen_state_in_filename;

  /* Read RNG state from file */
  if(!str_is_empty(rng_in)) {
    ERR(ssp_rng_create(stardis->allocator, SSP_RNG_THREEFRY, &rng));
    ERR(read_rng_state(stardis, str_cget(rng_in), rng));
    args->rng_state = rng;
  }

  /* Try to open output files to detect errors early */
  if(is_master_process && (stardis->mode & MODE_GREEN_BIN)) {
    const char* green_filename = str_cget(&stardis->bin_green_filename);
    const char* path_filename = str_cget(&stardis->end_paths_filename);

    if((fp_green = fopen(green_filename, "wb")) == NULL) {
      logger_print(stardis->logger, LOG_ERROR,
        "Cannot open file '%s' for binary writing.\n", green_filename);
      res = RES_IO_ERR;
      goto error;
    }

    if(strlen(path_filename) != 0) {
      if((fp_path = fopen(path_filename, "w")) == NULL) {
        logger_print(stardis->logger, LOG_ERROR,
          "Cannot open file '%s' for writing.\n", path_filename);
        res = RES_IO_ERR;
        goto error;
      }
    }
  }

  /* Run the Green estimation */
  time_current(&t0); /* Calculation starts */
  ERR(sdis_solve_probe_boundary_green_function(stardis->sdis_scn, args, &green));
  time_current(&t1); /* Calculation ends */

  /* No more to do for non master processes */
  if(is_master_process) goto exit;

  /* Write ASCII Green */
  if(stardis->mode & MODE_GREEN_ASCII) {
    ERR(dump_green_ascii(green, stardis, stdout));
  }

  /* Write binary Green */
  if(stardis->mode & MODE_GREEN_BIN) {
    ERR(dump_green_bin(green, stardis, fp_green));
    if(fp_path) {
      ERR(dump_paths_end(green, stardis, fp_path));
    }
  }

  time_current(&t2); /* Output ends */

  ERR(print_computation_time(NULL, stardis, start, &t0, &t1, &t2));

  /* Note that the resulting RNG state is not written in an output file because
   * the solver API does not provide a function to recover it. But in fact, the
   * green function saves the RNG state after its estimation. Therefore, the API
   * can be expected to provide such functionality soon.
   *
   * TODO write the RNG status of the Green function when it is available */

exit:
  if(fp_green) CHK(fclose(fp_green) == 0);
  if(fp_path) CHK(fclose(fp_path) == 0);
  if(green) SDIS(green_function_ref_put(green));
  if(rng) SSP(rng_ref_put(rng));
  return res;
error:
  goto exit;
}

static res_T
compute_single_flux_probe_on_interface
  (struct stardis* stardis,
   struct time* start,
   const struct stardis_probe_boundary* probe)
{
  res_T res = RES_OK;
  struct sdis_green_function* green = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_solve_probe_boundary_flux_args args
    = SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT;
  FILE* stream_r = NULL;
  struct time compute_start, compute_end;

  ASSERT(stardis && start && probe);

  ERR(setup_solve_probe_boundary_flux_args(stardis, probe, &args));

  /* Input random state? */
  READ_RANDOM_STATE(&stardis->rndgen_state_in_filename);

  if(stardis->mpi_initialized && stardis->mpi_rank != 0) {
    ERR(sdis_solve_probe_boundary_flux(stardis->sdis_scn, &args, &estimator));
  } else {
    struct sdis_mc time = SDIS_MC_NULL;
    time_current(&compute_start);
    ERR(sdis_solve_probe_boundary_flux(stardis->sdis_scn, &args, &estimator));
    time_current(&compute_end);
    ERR(sdis_estimator_get_realisation_time(estimator, &time));
    ERR(print_computation_time
      (&time, stardis, start, &compute_start, &compute_end, NULL));

    ERR(print_single_MC_result(estimator, stardis, stdout));
  }

  /* Output random state? */
  WRITE_RANDOM_STATE(&stardis->rndgen_state_out_filename);

end:
  if(estimator) SDIS(estimator_ref_put(estimator));
  if(green) SDIS(green_function_ref_put(green));
  if(args.rng_state) SSP(rng_ref_put(args.rng_state));
  return res;
error:
  goto end;
}

static res_T
setup_solve_probe_boundary_args
  (struct stardis* stardis,
   const struct stardis_probe_boundary* probe,
   struct sdis_solve_probe_boundary_args* args)
{
  enum sdis_side probe_side = SDIS_SIDE_NULL__;
  double uv[2] = {0, 0};
  size_t iprim = SIZE_MAX;
  unsigned desc_ids[SG3D_PROP_TYPES_COUNT__];
  res_T res = RES_OK;
  ASSERT(stardis && probe && args && (stardis->mode && MODE_COMPUTE_TEMP_MAP_ON_SURF));

  /* Calculate the probe position on the boundary */
  ERR(move_to_boundary(stardis, probe->position, probe->time[0], 1, &iprim, uv));

  ERR(sg3d_geometry_get_unique_triangle_properties(stardis->geometry.sg3d,
    (unsigned)iprim, desc_ids));

  ERR(setup_probe_side(stardis, desc_ids, probe->side, iprim, &probe_side));
  ERR(setup_thermal_contact_resistance(stardis, desc_ids, probe_side));

  /* Setup of solve input parameters */
  args->nrealisations = stardis->samples;
  args->iprim = iprim;
  args->uv[0] = uv[0];
  args->uv[1] = uv[1];
  args->time_range[0] = probe->time[0];
  args->time_range[1] = probe->time[1];
  args->picard_order = stardis->picard_order;
  args->side = probe_side;
  args->register_paths = stardis->dump_paths;
  args->diff_algo = stardis->diff_algo;

  /* The solver does not accept that the side of the interface on which the
   * probe is placed is invalid. Below, the side is arbitrarily defined because
   * at this point, Stardis has already arbitrated that this side does not
   * matter (i.e. there is no thermal contact resistance) */
  if(args->side == SDIS_SIDE_NULL__) {
    args->side = SDIS_FRONT;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
compute_single_probe_on_interface
  (struct stardis* stardis,
   struct time* start,
   const struct stardis_probe_boundary* probe)
{
  struct sdis_solve_probe_boundary_args args
    = SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT;
  res_T output_status[2] = {RES_OK, RES_OK};
  res_T res = RES_OK;
  ASSERT(stardis && start && probe);

  ERR(setup_solve_probe_boundary_args(stardis, probe, &args));

  /* Run the calculation */
  if(stardis->mode & (MODE_GREEN_ASCII | MODE_GREEN_BIN)) {
    ERR(solve_green(stardis, start, &args));
  } else {
    ERR(solve(stardis, start, &args, output_status));
  }

exit:
  res = (res != RES_OK ? res : output_status[0]);
  res = (res != RES_OK ? res : output_status[1]);
  return res;
error:
  goto exit;
}

static res_T
compute_multiple_probes_on_interface
  (struct stardis* stardis,
   struct time* start)
{
  /* Probes */
  const struct stardis_probe_boundary* probes = NULL;
  struct sdis_solve_probe_boundary_args* solve_args = NULL;
  struct sdis_solve_probe_boundary_list_args solve_list_args =
    SDIS_SOLVE_PROBE_BOUNDARY_LIST_ARGS_DEFAULT;
  size_t nprobes = 0;

  /* Miscellaneous */
  res_T output_status = RES_OK;
  res_T res = RES_OK;
  size_t i= 0;
  ASSERT(stardis && start);

  /* Fetch the list of probes arguments */
  probes = darray_probe_boundary_cdata_get(&stardis->probe_boundary_list);
  nprobes = darray_probe_boundary_size_get(&stardis->probe_boundary_list);
  ASSERT(nprobes > 1);

  solve_args = MEM_CALLOC(stardis->allocator, nprobes, sizeof(*solve_args));
  if(!probes) {
    logger_print(stardis->logger, LOG_ERROR,
      "Argument list allocation error for resolving multiple probes "
      "on the boundary.\n");
    res = RES_MEM_ERR;
    goto error;
  }

  /* Setup the solve arguments */
  FOR_EACH(i, 0, nprobes) {
    solve_args[i] = SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT;
    ERR(setup_solve_probe_boundary_args(stardis, &probes[i], &solve_args[i]));
  }
  solve_list_args.probes = solve_args;
  solve_list_args.nprobes = nprobes;

  /* Run calculations */
  ERR(solve_list(stardis, start, &solve_list_args, &output_status));

exit:
  if(probes) MEM_RM(stardis->allocator, solve_args);
  res = (res != RES_OK ? res : output_status);
  return res;
error:
  goto exit;
}

static res_T
compute_multiple_flux_probes_on_interface
  (struct stardis* stardis,
   struct time* start)
{
  /* Probes */
  const struct stardis_probe_boundary* probes = NULL;
  size_t nprobes = 0;
  res_T res = RES_OK;
  ASSERT(stardis && start);

  /* Fetch the list of probes arguments */
  probes = darray_probe_boundary_cdata_get(&stardis->probe_boundary_list);
  nprobes = darray_probe_boundary_size_get(&stardis->probe_boundary_list);
  ASSERT(nprobes > 1);

  /* Run calculations */
  ERR(solve_flux_list(stardis, start, probes, nprobes));

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
compute_probe_on_interface(struct stardis* stardis, struct time* start)
{
  int temp_flags = MODE_COMPUTE_PROBE_TEMP_ON_SURF
    | MODE_COMPUTE_LIST_PROBE_TEMP_ON_SURF;
  int flux_flags = MODE_COMPUTE_PROBE_FLUX_DNSTY_ON_SURF
    | MODE_COMPUTE_LIST_PROBE_FLUX_DNSTY_ON_SURF;
  res_T res = RES_OK;
  ASSERT(stardis && start);
  ASSERT((stardis->mode & temp_flags) != (stardis->mode & flux_flags)); /* xor */

  if(stardis->mode & temp_flags) {
    if(darray_probe_boundary_size_get(&stardis->probe_boundary_list) > 1) {
      res = compute_multiple_probes_on_interface(stardis, start);
      if(res != RES_OK) goto error;
    } else {
      const struct stardis_probe_boundary* probe
        = darray_probe_boundary_cdata_get(&stardis->probe_boundary_list);
      res = compute_single_probe_on_interface(stardis, start, probe);
      if(res != RES_OK) goto error;
    }
  } else if(stardis->mode & flux_flags) {
    if(darray_probe_boundary_size_get(&stardis->probe_boundary_list) > 1) {
      res = compute_multiple_flux_probes_on_interface(stardis, start);
      if(res != RES_OK) goto error;
    } else {
      const struct stardis_probe_boundary* probe
        = darray_probe_boundary_cdata_get(&stardis->probe_boundary_list);
      res = compute_single_flux_probe_on_interface(stardis, start, probe);
      if(res != RES_OK) goto error;
    }
  }

exit:
  return res;
error:
  goto exit;
}
