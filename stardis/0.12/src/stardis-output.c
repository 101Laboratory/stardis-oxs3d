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

#define _POSIX_C_SOURCE 200112L /* snprintf */
#include "stardis-output.h"
#include "stardis-args.h"
#include "stardis-compute.h"
#include "stardis-fluid.h"
#include "stardis-solid.h"
#include "stardis-hbound.h"
#include "stardis-tbound.h"
#include "stardis-fbound.h"
#include "stardis-ssconnect.h"
#include "stardis-sfconnect.h"
#include "stardis-intface.h"
#include "stardis-app.h"
#include "stardis-green-types.h"

#include <sdis.h>

#include <star/ssp.h>
#include <star/senc3d.h>
#include <star/sg3d.h>

#include <rsys_math.h>
#include <rsys/mem_allocator.h>
#include <rsys/dynamic_array_uint.h>
#include <rsys/hash_table.h>
#include <rsys/logger.h>
#include <rsys/str.h>
#include <rsys/clock_time.h>

#include <limits.h>
#include <string.h>
#include <stdio.h>

#define HTABLE_NAME weigth
#define HTABLE_DATA double
#define HTABLE_KEY unsigned
#include <rsys/hash_table.h>

struct w_ctx {
  struct mem_allocator* alloc;
  const struct darray_descriptions* desc;
  struct htable_weigth pw;
  struct htable_weigth flux;
  FILE* stream;
};

struct e_ctx {
  const struct darray_descriptions* desc;
  FILE* stream;
};

/*******************************************************************************
 * Local Type; for documentation purpose
 * These values are used in dumps
 ******************************************************************************/
enum enclosure_errors_t {
  NO_ENCLOSURE_ERROR = BIT(0),
  ENCLOSURE_WITH_N_MEDIA = BIT(1),
  ENCLOSURE_WITH_UNDEF_MEDIUM = BIT(2)
};

static res_T
copy_desc_to_green_desc
  (struct green_description* gdesc,
   const struct darray_descriptions* descriptions,
   const size_t idx)
{
  size_t sz;
  const struct description* desc;
  ASSERT(gdesc && descriptions);
  sz = darray_descriptions_size_get(descriptions);
  CHK(idx < sz);
  desc = darray_descriptions_cdata_get(descriptions) + idx;
  switch(desc->type) {
    case DESC_MAT_SOLID:
      gdesc->type = GREEN_MAT_SOLID;
      strcpy(gdesc->d.solid.name, str_cget(&desc->d.solid->name));
      gdesc->d.solid.conductivity = desc->d.solid->lambda;
      gdesc->d.solid.volumic_mass = desc->d.solid->rho;
      gdesc->d.solid.calorific_capacity = desc->d.solid->cp;
      gdesc->d.solid.volumic_power = desc->d.solid->vpower;
      gdesc->d.solid.initial_temperature = desc->d.solid->tinit;
      gdesc->d.solid.imposed_temperature = desc->d.solid->imposed_temperature;
      break;
    case DESC_MAT_FLUID:
      gdesc->type = GREEN_MAT_FLUID;
      strcpy(gdesc->d.fluid.name, str_cget(&desc->d.fluid->name));
      gdesc->d.fluid.volumic_mass = desc->d.fluid->rho;
      gdesc->d.fluid.calorific_capacity = desc->d.fluid->cp;
      gdesc->d.fluid.initial_temperature = desc->d.fluid->tinit;
      gdesc->d.fluid.imposed_temperature = desc->d.fluid->imposed_temperature;
      break;
    case DESC_BOUND_H_FOR_FLUID:
    case DESC_BOUND_H_FOR_SOLID:
      gdesc->type = GREEN_BOUND_H;
      strcpy(gdesc->d.h_boundary.name, str_cget(&desc->d.h_boundary->name));
      gdesc->d.h_boundary.reference_temperature
	= desc->d.h_boundary->ref_temperature;
      gdesc->d.h_boundary.emissivity = desc->d.h_boundary->emissivity;
      gdesc->d.h_boundary.specular_fraction
	= desc->d.h_boundary->specular_fraction;
      gdesc->d.h_boundary.convection_coefficient = desc->d.h_boundary->hc;
      gdesc->d.h_boundary.imposed_temperature
	= desc->d.h_boundary->imposed_temperature;
      break;
    case DESC_BOUND_T_FOR_SOLID:
      gdesc->type = GREEN_BOUND_T;
      strcpy(gdesc->d.t_boundary.name, str_cget(&desc->d.t_boundary->name));
      gdesc->d.t_boundary.imposed_temperature
	= desc->d.t_boundary->imposed_temperature;
      break;
    case DESC_BOUND_F_FOR_SOLID:
      gdesc->type = GREEN_BOUND_F;
      strcpy(gdesc->d.f_boundary.name, str_cget(&desc->d.f_boundary->name));
      gdesc->d.f_boundary.imposed_flux
	= desc->d.f_boundary->imposed_flux;
      break;
    case DESC_SOLID_FLUID_CONNECT:
      gdesc->type = GREEN_SOLID_FLUID_CONNECT;
      strcpy(gdesc->d.sf_connect.name, str_cget(&desc->d.sf_connect->name));
      gdesc->d.sf_connect.reference_temperature
	= desc->d.sf_connect->ref_temperature;
      gdesc->d.sf_connect.emissivity = desc->d.sf_connect->emissivity;
      gdesc->d.sf_connect.specular_fraction
	= desc->d.sf_connect->specular_fraction;
      gdesc->d.sf_connect.convection_coefficient = desc->d.sf_connect->hc;
      break;
    case DESC_SOLID_SOLID_CONNECT:
      gdesc->type = GREEN_SOLID_SOLID_CONNECT;
      strcpy(gdesc->d.ss_connect.name, str_cget(&desc->d.ss_connect->name));
      gdesc->d.ss_connect.thermal_contact_resistance = desc->d.ss_connect->tcr;
      break;
    default: return RES_BAD_ARG;
  }
  return RES_OK;
}

/*******************************************************************************
 * Local Functions
 ******************************************************************************/

static res_T
merge_flux_terms
  (struct sdis_interface* interf,
   const enum sdis_side side,
   const double flux_term,
   void* ctx)
{
  res_T res = RES_OK;
  struct sdis_data* data = NULL;
  struct intface* d__;
  unsigned desc_id;
  struct w_ctx* w_ctx = ctx;
  const struct description* descs;

  ASSERT(interf && w_ctx);
  (void)side;

  data = sdis_interface_get_data(interf);
  d__ = sdis_data_get(data);
  desc_id = d__->desc_id;
  CHK(desc_id < darray_descriptions_size_get(w_ctx->desc));
  descs = darray_descriptions_cdata_get(w_ctx->desc);

  switch (descs[desc_id].type) {
  case DESC_BOUND_T_FOR_SOLID:
  case DESC_BOUND_H_FOR_SOLID:
  case DESC_BOUND_H_FOR_FLUID:
    FATAL("Cannot have a flux term here.\n"); break;
  case DESC_BOUND_F_FOR_SOLID: {
    double* w;
    w = htable_weigth_find(&w_ctx->flux, &desc_id);
    if(w) *w += flux_term;
    else ERR(htable_weigth_set(&w_ctx->flux, &desc_id, &flux_term));
    break;
  }
  default: FATAL("Unreachable code.\n"); break;
  }
end:
  return res;
error:
  goto end;
}

static res_T
merge_power_terms
  (struct sdis_medium* mdm,
   const double power_term,
   void* ctx)
{
  res_T res = RES_OK;
  struct sdis_data* data = NULL;
  enum sdis_medium_type type;
  struct w_ctx* w_ctx = ctx;
  size_t sz;

  ASSERT(mdm && w_ctx);

  sz = darray_descriptions_size_get(w_ctx->desc);
  data = sdis_medium_get_data(mdm);
  type = sdis_medium_get_type(mdm);

  switch (type) {
  case SDIS_FLUID: {
    /* Could be OK, but unimplemented in stardis */
    FATAL("Unexpected power term in fluid");
  }
  case SDIS_SOLID: {
    struct solid** psolid = sdis_data_get(data);
    double* w;
    unsigned id = (*psolid)->desc_id;
    CHK(id < sz);
    w = htable_weigth_find(&w_ctx->pw, &id);
    if(w) *w += power_term;
    else ERR(htable_weigth_set(&w_ctx->pw, &id, &power_term));
    break;
  }
  default: FATAL("Unreachable code.\n"); break;
  }
end:
  return res;
error:
  goto end;
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

res_T
dump_path
  (const struct sdis_heat_path* path,
   void* context)
{
  res_T res = RES_OK;
  struct dump_path_context* dump_ctx = context;
  FILE* stream = NULL;
  char* name = NULL;
  enum sdis_heat_path_flag status = SDIS_HEAT_PATH_NONE;
  size_t vcount_, scount_, offset, name_sz, istrip;
  unsigned long scount, vcount, strip_1, type_changes, *strip_type_changes = NULL;

  ASSERT(path && dump_ctx
    && dump_ctx->stardis
    && !str_is_empty(&dump_ctx->stardis->paths_filename));

  ERR(sdis_heat_path_get_status(path, &status));

  name_sz = 20 + str_len(&dump_ctx->stardis->paths_filename);
  name = MEM_CALLOC(dump_ctx->stardis->allocator, name_sz, sizeof(*name));
  if(!name) {
    res = RES_MEM_ERR;
    goto error;
  }
  snprintf(name, name_sz, "%s%08lu%s.vtk",
    str_cget(&dump_ctx->stardis->paths_filename), dump_ctx->rank++,
    (status == SDIS_HEAT_PATH_FAILURE ? "_err" : ""));

  stream = fopen(name, "w");
  if(!stream) {
    logger_print(dump_ctx->stardis->logger, LOG_ERROR,
      "cannot open file '%s' for writing.\n", name);
    res = RES_IO_ERR;
    goto error;
  }

  /* Get counts */
  ERR(sdis_heat_path_get_line_strips_count(path, &scount_));
  if(scount_ > ULONG_MAX) goto abort;
  scount = (unsigned long)scount_;
  vcount_ = 0;
  strip_1 = 0;
  type_changes = 0;
  strip_type_changes = MEM_CALLOC(dump_ctx->stardis->allocator, scount,
      sizeof(*strip_type_changes));
  if(!strip_type_changes) {
    res = RES_MEM_ERR;
    goto error;
  }

  FOR_EACH(istrip, 0, scount_) {
    size_t ivert, nverts;
    enum sdis_heat_vertex_type prev_type = SDIS_HEAT_VERTEX_CONDUCTION;
    ERR(sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts));
    if(nverts == 0 || nverts > ULONG_MAX) goto abort;
    if(nverts == 1) strip_1++;
    FOR_EACH(ivert, 0, nverts) {
      struct sdis_heat_vertex vtx;
      ERR(sdis_heat_path_line_strip_get_vertex(path, istrip, ivert, &vtx));
      /* Count changes in vertex type along strips
       * As we use vertex type instead of segment type, the vertices where type
       * change are duplicated (with the only difference of their types) so that
       * segments where type change are zero-length.
       * This way Paraview displays segments with no misleading color gradient */
      if(ivert != 0 && vtx.type != prev_type) {
        type_changes++;
        strip_type_changes[istrip]++;
      }
      prev_type = vtx.type;
    }
    vcount_+= nverts;
  }
  if(vcount_ > ULONG_MAX) goto abort;
  vcount = (unsigned long)vcount_;
  /* Header */
  fprintf(stream, "# vtk DataFile Version 2.0\n");
  fprintf(stream, "Heat path\n");
  fprintf(stream, "ASCII\n");
  fprintf(stream, "DATASET POLYDATA\n");
  /* Write path positions */
  fprintf(stream, "POINTS %lu double\n", vcount + type_changes);
  FOR_EACH(istrip, 0, scount_) {
    size_t ivert, nverts;
    enum sdis_heat_vertex_type prev_type = SDIS_HEAT_VERTEX_CONDUCTION;
    ERR(sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts));
    FOR_EACH(ivert, 0, nverts) {
      struct sdis_heat_vertex vtx;
      ERR(sdis_heat_path_line_strip_get_vertex(path, istrip, ivert, &vtx));
      if(ivert != 0 && vtx.type != prev_type) {
        /* duplicate the previous vertex */
        struct sdis_heat_vertex v;
        ERR(sdis_heat_path_line_strip_get_vertex(path, istrip, ivert-1, &v));
        fprintf(stream, "%g %g %g\n", SPLIT3(v.P));
      }
      fprintf(stream, "%g %g %g\n", SPLIT3(vtx.P));
      prev_type = vtx.type;
    }
  }
  /* Write the strips of the path
   * Workaround a Paraview crash by creating 2-vertices-long paths from
   * single-vertex paths */
  fprintf(stream, "LINES %lu %lu\n",
      scount, scount + vcount + strip_1 + type_changes);
  offset = 0;
  FOR_EACH(istrip, 0, scount_) {
    size_t nverts;
    ERR(sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts));
    if(nverts == 1) {
      fprintf(stream, "2 %lu %lu\n", (unsigned long)offset, (unsigned long)offset);
    } else {
      size_t ivert;
      fprintf(stream, "%lu", (unsigned long)nverts + strip_type_changes[istrip]);
      FOR_EACH(ivert, 0, nverts + strip_type_changes[istrip]) {
        if(ivert + offset > ULONG_MAX) goto abort;
        fprintf(stream, " %lu", (unsigned long)(ivert + offset));
      }
      fprintf(stream, "\n");
    }
    offset += nverts + strip_type_changes[istrip];
  }

  /* Write path status on strips */
  fprintf(stream, "CELL_DATA %lu\n", scount);
  fprintf(stream, "SCALARS Path_Failure unsigned_char 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(istrip, 0, scount_) {
    switch (status) {
      case SDIS_HEAT_PATH_SUCCESS: fprintf(stream, "0\n"); break;
      case SDIS_HEAT_PATH_FAILURE: fprintf(stream, "1\n"); break;
      default: FATAL("Unreachable code.\n"); break;
    }
  }
  fprintf(stream, "POINT_DATA %lu\n", vcount + type_changes);
  /* Write the type of the random walk vertices */
  fprintf(stream, "SCALARS Segment_Type unsigned_char 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(istrip, 0, scount_) {
    size_t ivert, nverts;
    enum sdis_heat_vertex_type prev_type = SDIS_HEAT_VERTEX_CONDUCTION;
    ERR(sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts));
    FOR_EACH(ivert, 0, nverts) {
      struct sdis_heat_vertex vtx;
      int t;
      ERR(sdis_heat_path_line_strip_get_vertex(path, istrip, ivert, &vtx));
      if((size_t)vtx.type > UCHAR_MAX) goto abort;
      switch(vtx.type) {
        case SDIS_HEAT_VERTEX_CONDUCTION: t = 0; break;
        case SDIS_HEAT_VERTEX_CONVECTION: t = 1; break;
        case SDIS_HEAT_VERTEX_RADIATIVE: t = 2; break;
        default: FATAL("Unreachable code.\n"); break;
      }
      fprintf(stream, "%d\n", t);
      if(ivert != 0 && vtx.type != prev_type) {
        /* duplicate the previous vertex, except its type which is defined as
         * the type of the current vertex */
        fprintf(stream, "%d\n", t);
      }
      prev_type = vtx.type;
    }
  }
  /* Write the weights of the random walk vertices */
  fprintf(stream, "SCALARS Weight double 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(istrip, 0, scount_) {
    size_t ivert, nverts;
    enum sdis_heat_vertex_type prev_type = SDIS_HEAT_VERTEX_CONDUCTION;
    double prev_w = 0;
    ERR(sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts));
    FOR_EACH(ivert, 0, nverts) {
      struct sdis_heat_vertex vtx;
      ERR(sdis_heat_path_line_strip_get_vertex(path, istrip, ivert, &vtx));
      fprintf(stream, "%g\n", vtx.weight);
      if(ivert != 0 && vtx.type != prev_type) {
        /* duplicate the previous vertex */
        fprintf(stream, "%g\n", prev_w);
      }
      prev_type = vtx.type;
      prev_w = vtx.weight;
    }
  }
  /* Write the branch_id of the random walk vertices */
  fprintf(stream, "SCALARS Branch_id int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(istrip, 0, scount_) {
    size_t ivert, nverts;
    enum sdis_heat_vertex_type prev_type = SDIS_HEAT_VERTEX_CONDUCTION;
    int prev_id = 0;
    ERR(sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts));
    FOR_EACH(ivert, 0, nverts) {
      struct sdis_heat_vertex vtx;
      ERR(sdis_heat_path_line_strip_get_vertex(path, istrip, ivert, &vtx));
      fprintf(stream, "%d\n", vtx.branch_id);
      if(ivert != 0 && vtx.type != prev_type) {
        /* duplicate the previous vertex */
        fprintf(stream, "%d\n", prev_id);
      }
      prev_type = vtx.type;
      prev_id = vtx.branch_id;
    }
  }
  /* If computation time is not INF,
   * write the time of the random walk vertices */
  FOR_EACH(istrip, 0, scount_) {
    size_t ivert, nverts;
    enum sdis_heat_vertex_type prev_type = SDIS_HEAT_VERTEX_CONDUCTION;
    double prev_time = 0;
    ERR(sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts));
    FOR_EACH(ivert, 0, nverts) {
      struct sdis_heat_vertex vtx;
      ERR(sdis_heat_path_line_strip_get_vertex(path, istrip, ivert, &vtx));
      if(IS_INF(vtx.time)) {
       if(istrip || ivert) goto abort;
       goto end_prt_time;
      }
      if(istrip == 0 && ivert == 0) {
        fprintf(stream, "SCALARS Time double 1\n");
        fprintf(stream, "LOOKUP_TABLE default\n");
      }
      fprintf(stream, "%g\n", vtx.time);
      if(ivert != 0 && vtx.type != prev_type) {
        /* duplicate the previous vertex */
        fprintf(stream, "%g\n", prev_time);
      }
      prev_type = vtx.type;
      prev_time = vtx.time;
    }
  }
end_prt_time:

end:
  MEM_RM(dump_ctx->stardis->allocator, name);
  MEM_RM(dump_ctx->stardis->allocator, strip_type_changes);
  if(stream) fclose(stream);
  return res;
error:
  goto end;
abort:
  res = RES_BAD_ARG;
  goto error;
}

res_T
dump_vtk_image
  (const struct sdis_estimator_buffer* buf,
   FILE* stream)
{
  res_T res = RES_OK;
  size_t def[2];
  unsigned long definition[2];
  double* temps = NULL;
  size_t ix, iy;

  ASSERT(buf && stream);
  ERR(sdis_estimator_buffer_get_definition(buf, def));
  if(def[0] == 0 || def[1] == 0 || def[0] * def[1] > ULONG_MAX) goto abort;
  definition[0] = (unsigned long)def[0];
  definition[1] = (unsigned long)def[1];

  temps = mem_alloc(definition[0] * definition[1] * sizeof(double));
  if(temps == NULL) {
    res = RES_MEM_ERR;
    goto error;
  }

  /* Compute the per pixel temperature */
  fprintf(stream, "# vtk DataFile Version 2.0\n");
  fprintf(stream, "Infrared Image\n");
  fprintf(stream, "ASCII\n");
  fprintf(stream, "DATASET STRUCTURED_POINTS\n");
  fprintf(stream, "DIMENSIONS %lu %lu 1\n", definition[0], definition[1]);
  fprintf(stream, "ORIGIN 0 0 0\n");
  fprintf(stream, "SPACING 1 1 1\n");
  fprintf(stream, "POINT_DATA %lu\n", definition[0] * definition[1]);
  fprintf(stream, "SCALARS temperature_estimate float 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(iy, 0, definition[1]) {
    FOR_EACH(ix, 0, definition[0]) {
      const struct sdis_estimator* estimator;
      struct sdis_mc T;
      ERR(sdis_estimator_buffer_at(buf, ix, iy, &estimator));
      ERR(sdis_estimator_get_temperature(estimator, &T));
      fprintf(stream, "%f\n", T.E);
    }
  }
  fprintf(stream, "SCALARS temperature_std_dev float 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(iy, 0, definition[1]) {
    FOR_EACH(ix, 0, definition[0]) {
      const struct sdis_estimator* estimator;
      struct sdis_mc T;
      ERR(sdis_estimator_buffer_at(buf, ix, iy, &estimator));
      ERR(sdis_estimator_get_temperature(estimator, &T));
      fprintf(stream, "%f\n", T.SE);
    }
  }
  fprintf(stream, "SCALARS computation_time float 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(iy, 0, definition[1]) {
    FOR_EACH(ix, 0, definition[0]) {
      const struct sdis_estimator* estimator;
      struct sdis_mc time;
      ERR(sdis_estimator_buffer_at(buf, ix, iy, &estimator));
      ERR(sdis_estimator_get_realisation_time(estimator, &time));
      fprintf(stream, "%f\n", time.E);
    }
  }
  fprintf(stream, "SCALARS computation_time_std_dev float 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(iy, 0, definition[1]) {
    FOR_EACH(ix, 0, definition[0]) {
      const struct sdis_estimator* estimator;
      struct sdis_mc time;
      ERR(sdis_estimator_buffer_at(buf, ix, iy, &estimator));
      ERR(sdis_estimator_get_realisation_time(estimator, &time));
      fprintf(stream, "%f\n", time.SE);
    }
  }
  fprintf(stream, "SCALARS failures_count unsigned_long_long 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(iy, 0, definition[1]) {
    FOR_EACH(ix, 0, definition[0]) {
      const struct sdis_estimator* estimator;
      size_t nfails;
      ERR(sdis_estimator_buffer_at(buf, ix, iy, &estimator));
      ERR(sdis_estimator_get_failure_count(estimator, &nfails));
      if(nfails > ULONG_MAX) goto abort;
      fprintf(stream, "%lu\n", (unsigned long)nfails);
    }
  }
  mem_rm(temps);

end:
  return res;
error:
  goto end;
abort:
  res = RES_BAD_ARG;
  goto error;
}

res_T
dump_ht_image
  (const struct sdis_estimator_buffer* buf,
   FILE* stream)
{
  res_T res = RES_OK;
  size_t def[2];
  unsigned long definition[2];
  size_t ix, iy;

  ASSERT(buf && stream);
  ERR(sdis_estimator_buffer_get_definition(buf, def));
  if(def[0] > ULONG_MAX || def[1] > ULONG_MAX) goto abort;
  definition[0] = (unsigned long)def[0];
  definition[1] = (unsigned long)def[1];

  fprintf(stream, "%lu %lu\n", definition[0], definition[1]);
  FOR_EACH(iy, 0, definition[1]) {
    FOR_EACH(ix, 0, definition[0]) {
      const struct sdis_estimator* estimator;
      struct sdis_mc T;
      struct sdis_mc time;
      ERR(sdis_estimator_buffer_at(buf, ix, iy, &estimator));
      ERR(sdis_estimator_get_realisation_time(estimator, &time));
      ERR(sdis_estimator_get_temperature(estimator, &T));
      fprintf(stream, "%f %f 0 0 0 0 %f %f\n",
        T.E, T.SE, time.E, time.SE);
    }
  };

end:
  return res;
error:
  goto end;
abort:
  res = RES_BAD_ARG;
  goto error;
}

#define FW(Ptr, Count) \
  if((Count) != fwrite((Ptr), sizeof(*(Ptr)), (Count), stream)) { \
    res = RES_IO_ERR; \
    goto error; \
  }

static FINLINE double
medium_get_t0
  (struct sdis_medium* medium)
{
  struct sdis_data* data = NULL;
  enum sdis_medium_type type;
  ASSERT(medium);
  type = sdis_medium_get_type(medium);
  data = sdis_medium_get_data(medium);
  if(type == SDIS_FLUID) {
    const struct fluid* fluid_props = *((const struct fluid**)sdis_data_cget(data));
    return fluid_props->t0;
  } else {
    const struct solid* solid_props = *((const struct solid**)sdis_data_cget(data));
    ASSERT(type == SDIS_SOLID);
    return solid_props->t0;
  }
}

static res_T
dump_sample_end(struct sdis_green_path* path, void* ctx)
{
  /* Stardis */
  struct sdis_green_path_end end = SDIS_GREEN_PATH_END_NULL;
  struct sdis_data* data = NULL;
  enum sdis_medium_type type;

  /* Stream */
  struct e_ctx* e_ctx = ctx;
  FILE* stream;

  /* Miscellaneous */
  const struct description* descs;
  double* pos;
  double elapsed;
  size_t sz;
  unsigned trad_id;
  unsigned id;
  res_T res = RES_OK;

  CHK(path && ctx);

  stream = e_ctx->stream;
  ERR(sdis_green_path_get_elapsed_time(path, &elapsed));
  ERR(sdis_green_path_get_end(path, &end));

  sz = darray_descriptions_size_get(e_ctx->desc);
  if(sz > UINT_MAX) { res = RES_BAD_ARG; goto error; }
  trad_id = (unsigned)sz;

  descs = darray_descriptions_cdata_get(e_ctx->desc);
  switch(end.type) {
    case SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV:
      /* End, End ID, X, Y, Z, Elapsed time */
      fprintf(stream, "TRAD, %u, 0, 0, 0, %g\n", trad_id, elapsed);
      break;
    case SDIS_GREEN_PATH_END_AT_INTERFACE: {
      struct intface* d__;
      data = sdis_interface_get_data(end.data.itfrag.intface);
      pos = end.data.itfrag.fragment.P;
      d__ = sdis_data_get(data);
      id = d__->desc_id;
      CHK(DESC_IS_T(descs+id) || DESC_IS_H(descs+id));
      /* End, End ID, X, Y, Z, Elapsed time */
      fprintf(stream, "%s, %u, %g, %g, %g, %g\n",
        str_cget(get_description_name(descs + id)), id, SPLIT3(pos), elapsed);
      break;
    }
    case SDIS_GREEN_PATH_END_IN_VOLUME:
      type = sdis_medium_get_type(end.data.mdmvert.medium);
      data = sdis_medium_get_data(end.data.mdmvert.medium);
      pos = end.data.mdmvert.vertex.P;
      if(end.data.mdmvert.vertex.P[0] == INF) {
        /* Radiative output (Trad) */
        id = trad_id;
      }
      else if(type == SDIS_FLUID) {
        struct fluid** pfluid = sdis_data_get(data);
        id = (*pfluid)->desc_id;
      } else {
        struct solid** psolid = sdis_data_get(data);
        ASSERT(type == SDIS_SOLID);
        ASSERT(!(*psolid)->is_outside); /* FIXME: what if in external solid? */
        id = (*psolid)->desc_id;
      }
      /* End, End ID, X, Y, Z, Elapsed time */
      fprintf(stream, "%s, %u, %g, %g, %g, %g\n",
        str_cget(get_description_name(descs + id)), id, SPLIT3(pos), elapsed);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

end:
  return res;
error:
  goto end;
}

static res_T
dump_sample
  (struct sdis_green_path* path,
   void* ctx)
{
  /* Stardis variables */
  struct sdis_green_path_end path_end = SDIS_GREEN_PATH_END_NULL;
  struct sdis_data* data = NULL;
  enum sdis_medium_type type;

  /* Miscellaneous variables */
  struct w_ctx* w_ctx = ctx;
  FILE* stream;
  const struct description* descs;
  struct htable_weigth_iterator it, end;
  struct green_sample_header header;
  unsigned* ids = NULL;
  double* weights = NULL;
  double t0;
  size_t sz, i;
  unsigned trad_id;
  res_T res = RES_OK;

  CHK(path && ctx);

  stream = w_ctx->stream;
  ERR(sdis_green_path_get_end(path, &path_end));
  sz = darray_descriptions_size_get(w_ctx->desc);
  if(sz > UINT_MAX) { res = RES_BAD_ARG; goto error; }
  trad_id = (unsigned)sz;

  /* For each path, dump:
   * # end_id #power_terms #flux_terms
   *    power_id_1  ... power_id_n flux_id_1 ... flux_id_n
   *    power_factor_1  ... power_factor_n flux_factor_1 ... flux_factor_n
   */

  descs = darray_descriptions_cdata_get(w_ctx->desc);
  switch(path_end.type) {
    case SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV:
      header.at_initial = 0;
      header.sample_end_description_id = trad_id;
      break;
    case SDIS_GREEN_PATH_END_AT_INTERFACE: {
      struct intface* d__;
      unsigned desc_id;
      data = sdis_interface_get_data(path_end.data.itfrag.intface);
      d__ = sdis_data_get(data);
      desc_id = d__->desc_id;
      CHK(DESC_IS_T(descs+desc_id) || DESC_IS_H(descs+desc_id));
      header.sample_end_description_id = desc_id;
      header.at_initial = 0;
      break;
    }
    case SDIS_GREEN_PATH_END_IN_VOLUME:
      type = sdis_medium_get_type(path_end.data.mdmvert.medium);
      data = sdis_medium_get_data(path_end.data.mdmvert.medium);
      t0 = medium_get_t0(path_end.data.mdmvert.medium);
      header.at_initial = (path_end.data.mdmvert.vertex.time <= t0);
      if(path_end.data.mdmvert.vertex.P[0] == INF) {
        /* Radiative output (Trad) */
        header.sample_end_description_id = trad_id;
      }
      else if(type == SDIS_FLUID) {
        struct fluid** pfluid = sdis_data_get(data);
        header.sample_end_description_id = (*pfluid)->desc_id;
      } else {
        struct solid** psolid = sdis_data_get(data);
        ASSERT(type == SDIS_SOLID);
        ASSERT(!(*psolid)->is_outside); /* FIXME: what if in external solid? */
        header.sample_end_description_id = (*psolid)->desc_id;
      }
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

  /* Merge power and flux terms */
  htable_weigth_clear(&w_ctx->pw);
  htable_weigth_clear(&w_ctx->flux);
  ERR(sdis_green_path_for_each_power_term(path, merge_power_terms, w_ctx));
  ERR(sdis_green_path_for_each_flux_term(path, merge_flux_terms, w_ctx));
  sz = htable_weigth_size_get(&w_ctx->pw);
  if(sz > UINT_MAX) { res = RES_BAD_ARG; goto error; }
  header.pw_count = (unsigned)sz;
  sz = htable_weigth_size_get(&w_ctx->flux);
  if(sz > UINT_MAX) { res = RES_BAD_ARG; goto error; }
  header.fx_count = (unsigned)sz;

  /* Write path's header */
  FW(&header, 1);

  /* Allocate buffers */
  sz = header.pw_count + header.fx_count;
  ids = MEM_CALLOC(w_ctx->alloc, sz, sizeof(*ids));
  weights = MEM_CALLOC(w_ctx->alloc, sz, sizeof(*weights));
  if(!ids || !weights) {
    res = RES_MEM_ERR;
    goto error;
  }

  /* Write terms */
  htable_weigth_begin(&w_ctx->pw, &it);
  htable_weigth_end(&w_ctx->pw, &end);
  i = 0;
  while(!htable_weigth_iterator_eq(&it, &end)) {
    double* w = htable_weigth_iterator_data_get(&it);
    unsigned* k = htable_weigth_iterator_key_get(&it);
    CHK(*k <= trad_id);
    ids[i] = *k;
    weights[i] = *w;
    htable_weigth_iterator_next(&it);
    i++;
  }
  CHK(i == header.pw_count);

  htable_weigth_begin(&w_ctx->flux, &it);
  htable_weigth_end(&w_ctx->flux, &end);
  while (!htable_weigth_iterator_eq(&it, &end)) {
    double* w = htable_weigth_iterator_data_get(&it);
    unsigned* k = htable_weigth_iterator_key_get(&it);
    CHK(*k <= trad_id);
    ids[i] = *k;
    weights[i] = *w;
    htable_weigth_iterator_next(&it);
    i++;
  }
  CHK(i == header.pw_count + header.fx_count);

  FW(ids, sz);
  FW(weights, sz);

end:
  MEM_RM(w_ctx->alloc, ids);
  MEM_RM(w_ctx->alloc, weights);
  return res;
error:
  goto end;
}

res_T
dump_green_bin
  (struct sdis_green_function* green,
   const struct stardis* stardis,
   FILE* stream)
{
  /* The following type must be identical to its stardis-green counterpart! */
  struct green_file_header header;
  const struct radiative_env_const* radenv_const = NULL;
  struct w_ctx w_ctx;
  size_t sz, i;
  int table_initialized = 0;
  res_T res = RES_OK;

  ASSERT(green && stardis && stream);

  /* Stardis can produce the green function on systems
   * with constant properties only */
  ASSERT(stardis->radenv.type == RADIATIVE_ENV_CONST);
  radenv_const = &stardis->radenv.data.cst;

  /* Init header */
  strcpy(header.green_string, BIN_FILE_IDENT_STRING);
  header.file_format_version = GREEN_FILE_FORMAT_VERSION;
  header.solid_count = stardis->counts.smed_count;
  header.fluid_count = stardis->counts.fmed_count;
  header.tbound_count = stardis->counts.tbound_count;
  header.hbound_count = stardis->counts.hbound_count;
  header.fbound_count = stardis->counts.fbound_count;
  header.sfconnect_count = stardis->counts.sfconnect_count;
  header.ssconnect_count = stardis->counts.ssconnect_count;
  ERR(sdis_green_function_get_paths_count(green, &header.ok_count));
  ERR(sdis_green_function_get_invalid_paths_count(green, &header.failed_count));
  sz = darray_descriptions_size_get(&stardis->descriptions);
  if(sz > UINT_MAX) goto abort;
  ASSERT(sz ==
    (stardis->counts.smed_count + stardis->counts.fmed_count
      + stardis->counts.tbound_count + stardis->counts.hbound_count
      + stardis->counts.fbound_count + stardis->counts.sfconnect_count
      + stardis->counts.ssconnect_count));
  header.description_count = (unsigned)sz;
  header.ambient_radiative_temperature = radenv_const->temperature;
  header.ambient_radiative_temperature_reference =
    radenv_const->reference_temperature;
  d2_set(header.time_range, stardis->time_range);

  /* Write header */
  FW(&header, 1);

  /* Write descriptions*/
  for(i = 0; i < sz; i++) {
    struct green_description desc;
    ERR(copy_desc_to_green_desc(&desc, &stardis->descriptions, i));
    FW(&desc, 1);
  }

  w_ctx.alloc = stardis->allocator;
  w_ctx.desc = &stardis->descriptions;
  htable_weigth_init(stardis->allocator, &w_ctx.pw);
  htable_weigth_init(stardis->allocator, &w_ctx.flux);
  w_ctx.stream = stream;
  table_initialized = 1;

  /* Write samples */
  ERR(sdis_green_function_for_each_path(green, dump_sample, &w_ctx));

end:
  if(table_initialized) htable_weigth_release(&w_ctx.pw);
  if(table_initialized) htable_weigth_release(&w_ctx.flux);
  return res;
error:
  goto end;
abort:
  res = RES_BAD_ARG;
  goto error;
}

res_T
dump_paths_end
  (struct sdis_green_function* green,
   const struct stardis* stardis,
   FILE* stream)
{
  res_T res = RES_OK;
  struct e_ctx e_ctx = { 0 };

  ASSERT(green && stardis && stream);

  e_ctx.desc = &stardis->descriptions;
  e_ctx.stream = stream;

  fprintf(stream, "\"End\", \"End ID\", \"X\", \"Y\", \"Z\", \"Elapsed time\"\n");
  ERR(sdis_green_function_for_each_path(green, dump_sample_end, &e_ctx));

end:
  return res;
error:
  goto end;
}

res_T
print_sample
  (struct sdis_green_path* path,
   void* ctx)
{
  res_T res = RES_OK;
  struct sdis_green_path_end path_end = SDIS_GREEN_PATH_END_NULL;
  struct sdis_data* data = NULL;
  enum sdis_medium_type type;
  struct htable_weigth_iterator it, end;
  unsigned desc_id;
  size_t pw_count, fx_count;
  struct w_ctx* w_ctx = ctx;
  const struct description* descs;
  CHK(path && ctx);

  ERR(sdis_green_path_get_end(path, &path_end));

  /* For each path, prints:
   * # end #power_terms #flux_terms power_term_1  ... power_term_n flux_term_1 ... flux_term_n
   * with:
   * - end = end_type end_id; end_type = T | H | R | F | S
   * - power_term_i = power_type_i power_id_i factor_i
   * - flux_term_i = flux_id_i factor_i
   */

  descs = darray_descriptions_cdata_get(w_ctx->desc);
  switch (path_end.type) {
  case SDIS_GREEN_PATH_END_AT_INTERFACE: {
    struct intface* d__;
    data = sdis_interface_get_data(path_end.data.itfrag.intface);
    d__ = sdis_data_get(data);
    desc_id = d__->desc_id;
    switch (descs[desc_id].type) {
    case DESC_BOUND_T_FOR_SOLID:
      fprintf(w_ctx->stream, "T\t%u", desc_id);
      break;
    case DESC_BOUND_H_FOR_SOLID:
    case DESC_BOUND_H_FOR_FLUID:
      fprintf(w_ctx->stream, "H\t%u", desc_id);
      break;
    case DESC_BOUND_F_FOR_SOLID:
      FATAL("Heat path cannot end at a flux boundary.\n"); break;
      break;
    default: FATAL("Unreachable code.\n"); break;
    }
    break;
  }
  case SDIS_GREEN_PATH_END_IN_VOLUME:
    type = sdis_medium_get_type(path_end.data.mdmvert.medium);
    data = sdis_medium_get_data(path_end.data.mdmvert.medium);
    if(path_end.data.mdmvert.vertex.P[0] == INF) {
      /* Radiative output (Trad)*/
      size_t sz = darray_descriptions_size_get(w_ctx->desc);
      if(sz > UINT_MAX) goto abort;
      fprintf(w_ctx->stream, "R\t%u", (unsigned)sz);
    }
    else if(type == SDIS_FLUID) {
      struct fluid** pfluid = sdis_data_get(data);
      desc_id = (*pfluid)->desc_id;
      if((*pfluid)->is_outside)
        /* If outside the model and in a fluid with known temperature,
         * its a fluid attached to a H boundary */
        fprintf(w_ctx->stream, "H\t%u", desc_id);
      /* In a standard fluid with known temperature */
      else fprintf(w_ctx->stream, "F\t%u", desc_id);
    } else {
      struct solid** psolid = sdis_data_get(data);
      ASSERT(type == SDIS_SOLID);
      ASSERT(!(*psolid)->is_outside); /* FIXME: what if in external solid? */
      desc_id = (*psolid)->desc_id;
      fprintf(w_ctx->stream, "S\t%u", desc_id);
    }
    break;
  default: FATAL("Unreachable code.\n"); break;
  }

  ERR(sdis_green_function_get_power_terms_count(path, &pw_count));

  htable_weigth_clear(&w_ctx->pw);
  htable_weigth_clear(&w_ctx->flux);
  ERR(sdis_green_path_for_each_power_term(path, merge_power_terms, w_ctx));
  ERR(sdis_green_path_for_each_flux_term(path, merge_flux_terms, w_ctx));
  fx_count = htable_weigth_size_get(&w_ctx->flux);

  if(pw_count > ULONG_MAX || fx_count > ULONG_MAX) goto abort;
  fprintf(w_ctx->stream, "\t%lu\t%lu",
    (unsigned long)pw_count, (unsigned long)fx_count);

  htable_weigth_begin(&w_ctx->pw, &it);
  htable_weigth_end(&w_ctx->pw, &end);
  while(!htable_weigth_iterator_eq(&it, &end)) {
    double* w = htable_weigth_iterator_data_get(&it);
    unsigned* k = htable_weigth_iterator_key_get(&it);
    fprintf(w_ctx->stream, "\t%u\t%g", *k, *w);
    htable_weigth_iterator_next(&it);
  }

  htable_weigth_begin(&w_ctx->flux, &it);
  htable_weigth_end(&w_ctx->flux, &end);
  while (!htable_weigth_iterator_eq(&it, &end)) {
    double* w = htable_weigth_iterator_data_get(&it);
    unsigned* k = htable_weigth_iterator_key_get(&it);
    fprintf(w_ctx->stream, "\t%u\t%g", *k, *w);
    htable_weigth_iterator_next(&it);
  }
  fprintf(w_ctx->stream, "\n");

end:
  return res;
error:
  goto end;
abort:
  res = RES_BAD_ARG;
  goto error;
}

res_T
dump_green_ascii
  (struct sdis_green_function* green,
   const struct stardis* stardis,
   FILE* stream)
{
  res_T res = RES_OK;
  const struct radiative_env_const* radenv_const = NULL;
  unsigned ok_count, failed_count;
  size_t sz;
  struct w_ctx w_ctx;
  int table_initialized = 0;
  unsigned i, szd;
  const struct description* descs;

  ASSERT(green && stardis && stream);

  /* Stardis can produce the green function on systems
   * with constant properties only */
  ASSERT(stardis->radenv.type == RADIATIVE_ENV_CONST);
  radenv_const = &stardis->radenv.data.cst;

  ERR(sdis_green_function_get_paths_count(green, &sz));
  if(sz > UINT_MAX) goto abort;
  ok_count = (unsigned)sz;
  ERR(sdis_green_function_get_invalid_paths_count(green, &sz));
  if(sz > UINT_MAX) goto abort;
  failed_count = (unsigned)sz;
  sz = darray_descriptions_size_get(&stardis->descriptions);
  if(sz > UINT_MAX) goto abort;
  szd = (unsigned)sz;
  descs = darray_descriptions_cdata_get(&stardis->descriptions);

  /* Output counts */
  fprintf(stream, "---BEGIN GREEN---\n");
  fprintf(stream, "# time range\n");
  fprintf(stream, "%g %g\n",
    SPLIT2(stardis->time_range));
  fprintf(stream,
    "# #solids #fluids #t_boundaries #h_boundaries #f_boundaries #ok #failures\n");
  fprintf(stream, "%u %u %u %u %u %u %u\n",
    stardis->counts.smed_count, stardis->counts.fmed_count,
    stardis->counts.tbound_count, stardis->counts.hbound_count,
    stardis->counts.fbound_count, ok_count, failed_count);

  /* List Media */
  if(stardis->counts.smed_count) {
    fprintf(stream, "# Solids\n");
    fprintf(stream, "# ID Name lambda rho cp power initial_temp imposed_temp\n");
    FOR_EACH(i, 0, szd) {
      const struct description* desc = descs + i;
      const struct solid* sl;
      if(desc->type != DESC_MAT_SOLID) continue;
      sl = desc->d.solid;
      fprintf(stream, "%u\t%s\t%g\t%g\t%g\t%g",
        i, str_cget(&sl->name), sl->lambda, sl->rho, sl->cp, sl->vpower);
      if(SDIS_TEMPERATURE_IS_KNOWN(sl->tinit)) {
        fprintf(stream, "\t%g", sl->tinit);
      } else {
        fprintf(stream, "\tNONE");
      }
      if(SDIS_TEMPERATURE_IS_KNOWN(sl->imposed_temperature)) {
        fprintf(stream, "\t%g\n", sl->imposed_temperature);
      } else {
        fprintf(stream, "\tNONE\n");
      }
    }
  }
  if(stardis->counts.fmed_count) {
    fprintf(stream, "# Fluids\n");
    fprintf(stream, "# ID Name rho cp initial_temp imposed_temp\n");
    FOR_EACH(i, 0, szd) {
      const struct description* desc = descs + i;
      const struct fluid* fl;
      if(desc->type != DESC_MAT_FLUID) continue;
      fl = desc->d.fluid;
      if(SDIS_TEMPERATURE_IS_KNOWN(fl->imposed_temperature)) {
        fprintf(stream, "%u\t%s\t%g\t%g",
          i, str_cget(&fl->name), fl->rho, fl->cp);
      } else {
        fprintf(stream, "%u\t%s\t%g\t%g",
          i, str_cget(&fl->name), fl->rho, fl->cp);
      }
      if(SDIS_TEMPERATURE_IS_KNOWN(fl->tinit)) {
        fprintf(stream, "\t%g", fl->tinit);
      } else {
        fprintf(stream, "\tNONE");
      }
      if(SDIS_TEMPERATURE_IS_KNOWN(fl->imposed_temperature)) {
        fprintf(stream, "\t%g\n", fl->imposed_temperature);
      } else {
        fprintf(stream, "\tNONE\n");
      }
    }
  }

  /* List Boundaries */
  if(stardis->counts.tbound_count) {
    fprintf(stream, "# T Boundaries\n");
    fprintf(stream, "# ID Name temperature\n");
    FOR_EACH(i, 0, szd) {
      const struct description* desc = descs + i;
      const struct t_boundary* bd;
      bd = desc->d.t_boundary;
      fprintf(stream, "%u\t%s\t%g\n",
        i, str_cget(&bd->name), bd->imposed_temperature);
    }
  }
  if(stardis->counts.hbound_count) {
    fprintf(stream, "# H Boundaries\n");
    fprintf(stream, "# ID Name ref_temperature emissivity specular_fraction hc T_env\n");
    FOR_EACH(i, 0, szd) {
      const struct description* desc = descs + i;
      const struct h_boundary* bd;
      if(desc->type != DESC_BOUND_H_FOR_SOLID
        && desc->type != DESC_BOUND_H_FOR_FLUID) continue;
      bd = desc->d.h_boundary;
      fprintf(stream, "%u\t%s\t%g\t%g\t%g\t%g\t%g\n",
        i, str_cget(&bd->name), bd->ref_temperature, bd->emissivity,
        bd->specular_fraction, bd->hc, bd->imposed_temperature);
    }
  }
  if(stardis->counts.fbound_count) {
    fprintf(stream, "# F Boundaries\n");
    fprintf(stream, "# ID Name flux\n");
    FOR_EACH(i, 0, szd) {
      const struct description* desc = descs + i;
      const struct f_boundary* bd;
      if(desc->type != DESC_BOUND_F_FOR_SOLID) continue;
      bd = desc->d.f_boundary;
      fprintf(stream, "%u\t%s\t%g\n",
        i, str_cget(&bd->name), bd->imposed_flux);
    }
  }

  /* Radiative Temperature */
  fprintf(stream, "# Radiative Temperatures\n");
  fprintf(stream, "# ID Trad Trad_Ref\n");
  fprintf(stream, "%u\t%g\t%g\n",
    szd, radenv_const->temperature, radenv_const->reference_temperature);

  fprintf(stream, "# Samples\n");
  fprintf(stream,
    "# end #power_terms #flux_terms power_term_1 ... power_term_n flux_term_1 ... flux_term_n\n");
  fprintf(stream, "# end = end_type end_id; end_type = T | H | R | F | S\n");
  fprintf(stream, "# power_term_i = power_id_i factor_i\n");
  fprintf(stream, "# flux_term_i = flux_id_i factor_i\n");

  w_ctx.alloc = stardis->allocator;
  w_ctx.desc = &stardis->descriptions;
  htable_weigth_init(stardis->allocator, &w_ctx.pw);
  htable_weigth_init(stardis->allocator, &w_ctx.flux);
  w_ctx.stream = stream;
  table_initialized = 1;

  ERR(sdis_green_function_for_each_path(green, print_sample, &w_ctx));

  fprintf(stream, "---END GREEN---\n");

end:
  if(table_initialized) htable_weigth_release(&w_ctx.pw);
  if(table_initialized) htable_weigth_release(&w_ctx.flux);
  return res;
error:
  goto end;
abort:
  res = RES_BAD_ARG;
  goto error;
}

res_T
dump_boundaries_at_the_end_of_vtk
  (const struct stardis* stardis,
   FILE* stream)
{
  res_T res = RES_OK;
  const struct description* descriptions;
  unsigned tsz, t;
  ASSERT(stardis && stream);

  ERR(sg3d_geometry_get_unique_triangles_count(stardis->geometry.sg3d, &tsz));
  descriptions = darray_descriptions_cdata_get(&stardis->descriptions);

  fprintf(stream, "SCALARS Boundaries unsigned_int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(t, 0, tsz) {
    unsigned descr[SG3D_PROP_TYPES_COUNT__];
    ERR(sg3d_geometry_get_unique_triangle_properties(stardis->geometry.sg3d, t,
      descr));
    if(descr[SG3D_INTFACE] != SG3D_UNSPECIFIED_PROPERTY
      && DESC_IS_BOUNDARY(descriptions+descr[SG3D_INTFACE]))
      /* Descriptions are numbered from 1 in the log (so the 1+ below) */
      fprintf(stream, "%u\n", 1 + descr[SG3D_INTFACE]);
    else fprintf(stream, "%u\n", SG3D_UNSPECIFIED_PROPERTY);
  }

exit:
  return res;
error:
  goto exit;
}

res_T
dump_enclosure_related_stuff_at_the_end_of_vtk
  (struct stardis* stardis,
   FILE* stream)
{
  res_T res = RES_OK;
  unsigned* trgs = NULL;
  struct senc3d_enclosure* enc = NULL;
  unsigned tsz, e, s, t, scount, ecount, ocount;
  int* enc_status = NULL;
  const struct description* descriptions;
  int undef_count = 0, multi_count = 0;
  struct str msg;
  ASSERT(stardis && stream);

  str_init(stardis->allocator, &msg);
  descriptions = darray_descriptions_cdata_get(&stardis->descriptions);
  ERR(sg3d_geometry_get_unique_triangles_count(stardis->geometry.sg3d, &tsz));
  trgs = MEM_CALLOC(stardis->allocator, tsz, sizeof(*trgs));
  if(!trgs) {
    res = RES_MEM_ERR;
    goto error;
  }

  /* If enclosure where not extracted, dump only errors */
  ERR(senc3d_scene_get_overlapping_triangles_count(stardis->senc3d_scn, &ocount));
  if(ocount) {
    FOR_EACH(t, 0, tsz) trgs[t] = 0;
    FOR_EACH(t, 0, ocount) {
      unsigned trid;
      ERR(senc3d_scene_get_overlapping_triangle(stardis->senc3d_scn, t, &trid));
      trgs[trid] = 1;
    }
    fprintf(stream, "SCALARS Overlapping_triangles unsigned_int 1\n");
    fprintf(stream, "LOOKUP_TABLE default\n");
    FOR_EACH(t, 0, tsz) fprintf(stream, "%u\n", trgs[t]);
    goto exit;
  }

  /* Keep the segments involved in holes (not the vertices) */
  ERR(senc3d_scene_get_frontier_segments_count(stardis->senc3d_scn, &scount));
  if(scount) {
    /* Room to store frontier triangles */
    FOR_EACH(s, 0, scount) {
      unsigned vrtc[2], trid;
      ERR(senc3d_scene_get_frontier_segment(stardis->senc3d_scn, s, vrtc, &trid));
      trgs[trid] = 1;
    }
    logger_print(stardis->logger, LOG_WARNING, "Model contains hole(s).\n");
    fprintf(stream, "SCALARS Hole_frontiers unsigned_int 1\n");
    fprintf(stream, "LOOKUP_TABLE default\n");
    FOR_EACH(t, 0, tsz) fprintf(stream, "%u\n", trgs[t]);
  }

  /* Dump enclosure information */
  ERR(senc3d_scene_get_enclosure_count(stardis->senc3d_scn, &ecount));
  enc_status = MEM_CALLOC(stardis->allocator, ecount, sizeof(*enc_status));
  if(!enc_status) {
    res = RES_MEM_ERR;
    goto error;
  }
  ASSERT(stardis->undefined_medium_behind_boundary_id != SENC3D_UNSPECIFIED_MEDIUM);
  FOR_EACH(e, 0, ecount) {
    struct senc3d_enclosure_header header;
    unsigned tid, med, enc_fst_med = SENC3D_UNSPECIFIED_MEDIUM;
    int is_fst_med = 1, is_err_cs = 0;

    enc_status[e] = NO_ENCLOSURE_ERROR;
    ERR(senc3d_scene_get_enclosure(stardis->senc3d_scn, e, &enc));
    ERR(senc3d_enclosure_get_header(enc, &header));

    FOR_EACH(t, 0, header.unique_primitives_count) {
      unsigned prop[SG3D_PROP_TYPES_COUNT__];
      enum senc3d_side side;
      size_t j;
      ERR(senc3d_enclosure_get_triangle_id(enc, t, &tid, &side));
      FOR_EACH(j, 0, darray_uint_size_get(&stardis->compute_surface.err_triangles))
      {
        unsigned prim
          = darray_uint_cdata_get(&stardis->compute_surface.err_triangles)[j];
        if(prim == tid) {
          is_err_cs = 1;
          break;
        }
      }
      if(is_err_cs)
        /* Don't flag an enclosure invalid because of a triangle that is
         * considered not member of it */
        continue;

      ERR(sg3d_geometry_get_unique_triangle_properties(stardis->geometry.sg3d,
        tid, prop));

      if(prop[side] != SG3D_UNSPECIFIED_PROPERTY) {
        ASSERT(prop[side] < darray_descriptions_size_get(&stardis->descriptions));
        description_get_medium_id(descriptions + prop[side], &med);
      } else  {
        /* If unspecified behind a boundary, use a specific ID to avoid to flag
         * using different boundaries for a given enclosure as invalid */
        int properties_conflict_status;
        validate_properties(t, prop, stardis, &properties_conflict_status);
        if(properties_conflict_status == NO_PROPERTY_CONFLICT)
          med = stardis->undefined_medium_behind_boundary_id;
        else {
          if(!(enc_status[e] & ENCLOSURE_WITH_UNDEF_MEDIUM)) undef_count++;
          enc_status[e] |= ENCLOSURE_WITH_UNDEF_MEDIUM;
          continue; /* Don't flag N_MEDIA at the same time */
        }
      }
      if(is_fst_med) {
        is_fst_med = 0;
        enc_fst_med = med;
      } else {
        if(enc_fst_med != med) {
          /* The external (infinite) enclosure can have multiple media */
          if(!header.is_infinite && !(enc_status[e] & ENCLOSURE_WITH_N_MEDIA))
            multi_count++;
          enc_status[e] |= ENCLOSURE_WITH_N_MEDIA;
        }
      }
    }
    ERR(senc3d_enclosure_ref_put(enc));
  }
  if(multi_count) {
    int fst = 1;
    str_printf(&msg,
        "Found %d enclosure(s) with more than 1 medium:", multi_count);
    FOR_EACH(e, 0, ecount) {
      if(!(enc_status[e] & ENCLOSURE_WITH_N_MEDIA)) continue;
      str_append_printf(&msg, (fst ? " %u" : ", %u"), e);
      fst = 0;
    }
    logger_print(stardis->logger, LOG_OUTPUT, "%s.\n",  str_cget(&msg));
  }
  if(undef_count) {
    int fst = 1;
    str_printf(&msg,
        "Found %d enclosure(s) with undefined medium:", undef_count);
    FOR_EACH(e, 0, ecount) {
      if(!(enc_status[e] & ENCLOSURE_WITH_UNDEF_MEDIUM)) continue;
      str_append_printf(&msg, (fst ? " %u" : ", %u"), e);
      fst = 0;
    }
    logger_print(stardis->logger, LOG_OUTPUT, "%s.\n",  str_cget(&msg));
  }
  fprintf(stream, "FIELD EnclosuresData 2\n");
  fprintf(stream, "Enclosures %d %d unsigned_char\n", ecount, tsz);
  FOR_EACH(t, 0, tsz) {
    unsigned encs[2], is_err_cs = 0;
    size_t j;
    FOR_EACH(j, 0, darray_uint_size_get(&stardis->compute_surface.err_triangles))
    {
      unsigned prim
        = darray_uint_cdata_get(&stardis->compute_surface.err_triangles)[j];
      if(prim == t) {
        is_err_cs = 1;
        break;
      }
    }
    if(is_err_cs) {
      /* Triangles in compute surface with error are considered member of no
       * enclosure */
      FOR_EACH(e, 1, ecount) fprintf(stream, "0 ");
      fprintf(stream, "0\n");
      continue;
    }
    ERR(senc3d_scene_get_triangle_enclosures(stardis->senc3d_scn, t, encs));
    FOR_EACH(e, 0, ecount) {
      unsigned c = (e == encs[SENC3D_FRONT] || e == encs[SENC3D_BACK])
        ? (unsigned char)enc_status[e] : 0;
      if(e == ecount - 1)
        fprintf(stream, "%u\n", c);
      else fprintf(stream, "%u ", c);
    }
  }

#define ENC_NOT_MEMBER SENC3D_UNSPECIFIED_MEDIUM
#define ENC_MEMBER_2_DISTINT_MEDIA (ENC_NOT_MEMBER - 1)
#define ENC_MEMBER_NO_MEDIUM (ENC_NOT_MEMBER - 2)

  fprintf(stream, "Enclosures_internal_media %d %d unsigned_int\n", ecount, tsz);
  FOR_EACH(t, 0, tsz) {
    unsigned descr[SG3D_PROP_TYPES_COUNT__];
    unsigned encs[2];
    unsigned is_err_cs = 0;
    size_t j;
    ERR(senc3d_scene_get_triangle_enclosures(stardis->senc3d_scn, t, encs));
    ERR(sg3d_geometry_get_unique_triangle_properties(stardis->geometry.sg3d, t,
      descr));

    /* Special value for triangles in compute surface with error */
    FOR_EACH(j, 0, darray_uint_size_get(&stardis->compute_surface.err_triangles))
    {
      unsigned prim
        = darray_uint_cdata_get(&stardis->compute_surface.err_triangles)[j];
      if(prim == t) {
        is_err_cs = 1;
        break;
      }
    }
    if(is_err_cs) {
      /* Triangles in compute surface with error are considered member of no
       * enclosure */
      FOR_EACH(e, 1, ecount) fprintf(stream, "%u ", ENC_NOT_MEMBER);
      fprintf(stream, "%u\n", ENC_NOT_MEMBER);
      continue;
    }
    FOR_EACH(e, 0, ecount) {
      unsigned mid;
      if(e == encs[SENC3D_FRONT] && e == encs[SENC3D_BACK]) {
        /* Both sides of this triangle are in enclosure #e */
        unsigned fmid, bmid;
        if(descr[SG3D_FRONT] != SG3D_UNSPECIFIED_PROPERTY)
          description_get_medium_id(descriptions + descr[SG3D_FRONT], &fmid);
        else if(descr[SG3D_INTFACE] != SG3D_UNSPECIFIED_PROPERTY
          && DESC_IS_BOUNDARY(descriptions+descr[SG3D_INTFACE]))
        {
          description_get_medium_id(descriptions + descr[SG3D_INTFACE], &fmid);
        }
        else fmid = ENC_MEMBER_NO_MEDIUM;
        if(descr[SENC3D_BACK] != SG3D_UNSPECIFIED_PROPERTY)
          description_get_medium_id(descriptions + descr[SENC3D_BACK], &bmid);
        else if(descr[SG3D_INTFACE] != SG3D_UNSPECIFIED_PROPERTY
          && DESC_IS_BOUNDARY(descriptions+descr[SG3D_INTFACE]))
        {
          description_get_medium_id(descriptions + descr[SG3D_INTFACE], &bmid);
        }
        else bmid = ENC_MEMBER_NO_MEDIUM;
        mid = (fmid == bmid) ? fmid : ENC_MEMBER_2_DISTINT_MEDIA;
      }
      else if(e == encs[SENC3D_FRONT]) {
        /* Member of enclosure #e (front side only) */
        if(descr[SG3D_FRONT] != SG3D_UNSPECIFIED_PROPERTY)
          description_get_medium_id(descriptions + descr[SG3D_FRONT], &mid);
        else if(descr[SG3D_INTFACE] != SG3D_UNSPECIFIED_PROPERTY
          && DESC_IS_BOUNDARY(descriptions+descr[SG3D_INTFACE]))
        {
          description_get_medium_id(descriptions + descr[SG3D_INTFACE], &mid);
        }
        else mid = ENC_MEMBER_NO_MEDIUM;
      }
      else if(e == encs[SENC3D_BACK]) {
        /* Member of enclosure #e  (back side only) */
        if(descr[SENC3D_BACK] != SG3D_UNSPECIFIED_PROPERTY)
          description_get_medium_id(descriptions + descr[SENC3D_BACK], &mid);
        else if(descr[SG3D_INTFACE] != SG3D_UNSPECIFIED_PROPERTY
          && DESC_IS_BOUNDARY(descriptions+descr[SG3D_INTFACE]))
        {
          description_get_medium_id(descriptions + descr[SG3D_INTFACE], &mid);
        }
        else mid = ENC_MEMBER_NO_MEDIUM;
      } else {
        /* Not member of enclosure #e */
        mid = ENC_NOT_MEMBER;
      }
      if(e == ecount - 1)
        fprintf(stream, "%u\n", mid);
      else fprintf(stream, "%u ", mid);
    }
  }

#undef ENC_NOT_MEMBER
#undef ENC_ERR_COMPUTE_SURFACE
#undef ENC_MEMBER_2_DISTINT_MEDIA
#undef ENC_MEMBER_NO_MEDIUM

exit:
  str_release(&msg);
  MEM_RM(stardis->allocator, trgs);
  MEM_RM(stardis->allocator, enc_status);
  return res;
error:
  goto exit;
}

res_T
print_computation_time
  (struct sdis_mc* time_per_realisation,
   struct stardis* stardis,
   struct time* start,
   struct time* compute_start,
   struct time* compute_end,
   struct time* output_end)
{
  struct time tmp;
  char buf[128];
  const int flag = TIME_MSEC | TIME_SEC | TIME_MIN | TIME_HOUR | TIME_DAY;

  ASSERT(stardis && start && compute_start && compute_end);

  /* Only master prints or reads estimators */
  ASSERT(!stardis->mpi_initialized || stardis->mpi_rank == 0);

  time_sub(&tmp, compute_start, start);
  time_dump(&tmp, flag, NULL, buf, sizeof(buf));
  logger_print(stardis->logger, LOG_OUTPUT,
    "Initialisation time = %s\n", buf);
  time_sub(&tmp, compute_end, compute_start);
  time_dump(&tmp, flag, NULL, buf, sizeof(buf));
  logger_print(stardis->logger, LOG_OUTPUT,
    "Computation time = %s\n", buf);
  if(output_end) {
    time_sub(&tmp, output_end, compute_end);
    time_dump(&tmp, flag, NULL, buf, sizeof(buf));
    logger_print(stardis->logger, LOG_OUTPUT,
      "Result output time = %s\n", buf);
  }

  if(time_per_realisation) {
    logger_print(stardis->logger, LOG_OUTPUT,
      "Time per realisation (in usec) = %g +/- %g\n",
        time_per_realisation->E, time_per_realisation->SE);
  }

  return RES_OK;
}

res_T
print_single_MC_result
  (struct sdis_estimator* estimator,
   struct stardis* stardis,
   FILE* stream)
{
  res_T res = RES_OK;
  struct sdis_mc result;
  size_t nfailures_;
  unsigned long nfailures, nsamples;

  ASSERT(estimator && stardis && stream);

  /* Only master prints or reads estimators */
  ASSERT(!stardis->mpi_initialized || stardis->mpi_rank == 0);

  /* Fetch the estimation data */
  ERR(sdis_estimator_get_temperature(estimator, &result));
  ERR(sdis_estimator_get_failure_count(estimator, &nfailures_));
  if(nfailures_ > ULONG_MAX || stardis->samples > ULONG_MAX) goto abort;
  nfailures = (unsigned long)nfailures_;
  nsamples = (unsigned long)stardis->samples;
  if(nfailures == nsamples) {
    logger_print(stardis->logger, LOG_ERROR,
      "All the %lu samples failed. No result to display.\n", nsamples);
    res = RES_BAD_OP;
    goto error;
  }

  /* Print the results */
  switch (stardis->mode & COMPUTE_MODES) {
  case MODE_COMPUTE_PROBE_TEMP_ON_VOL:
    if(stardis->mode & MODE_EXTENDED_RESULTS) {
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Temperature at [%g, %g, %g] at t=%g = %g K +/- %g\n",
          SPLIT3(stardis->probe), stardis->time_range[0],
          result.E, /* Expected value */
          result.SE); /* Standard error */
      else
        fprintf(stream,
          "Temperature at [%g, %g, %g] with t in [%g %g] = %g K +/- %g\n",
          SPLIT3(stardis->probe), SPLIT2(stardis->time_range),
          result.E, /* Expected value */
          result.SE); /* Standard error */
    }
    else fprintf(stream, "%g %g %lu %lu\n",
      result.E, result.SE, nfailures, nsamples);
    break;
  case MODE_COMPUTE_PROBE_TEMP_ON_SURF:
  case MODE_COMPUTE_LIST_PROBE_TEMP_ON_SURF:
    {
      const struct stardis_probe_boundary* probe = NULL;
      probe = darray_probe_boundary_cdata_get(&stardis->probe_boundary_list);
      ERR(print_single_MC_result_probe_boundary
        (stardis, probe, estimator, stream));
      break;
    }
  case MODE_COMPUTE_TEMP_MEAN_IN_MEDIUM:
    if(stardis->mode & MODE_EXTENDED_RESULTS) {
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Temperature in medium '%s' at t=%g = %g K +/- %g\n",
          str_cget(&stardis->solve_name), stardis->time_range[0],
          result.E, /* Expected value */
          result.SE); /* Standard error */
      else
        fprintf(stream,
          "Temperature in medium '%s' with t in [%g %g] = %g K +/- %g\n",
          str_cget(&stardis->solve_name), SPLIT2(stardis->time_range),
          result.E, /* Expected value */
          result.SE); /* Standard error */
    }
    else fprintf(stream, "%g %g %lu %lu\n",
      result.E, result.SE, nfailures, nsamples);
    break;
  case MODE_COMPUTE_TEMP_MEAN_ON_SURF:
    if(stardis->mode & MODE_EXTENDED_RESULTS) {
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Temperature at boundary '%s' at t=%g = %g K +/- %g\n",
          str_cget(&stardis->solve_name), stardis->time_range[0],
          result.E, /* Expected value */
          result.SE); /* Standard error */
      else
        fprintf(stream,
          "Temperature at boundary '%s' with t in [%g %g] = %g K +/- %g\n",
          str_cget(&stardis->solve_name), SPLIT2(stardis->time_range),
          result.E, /* Expected value */
          result.SE); /* Standard error */
    }
    else fprintf(stream, "%g %g %lu %lu\n",
      result.E, result.SE, nfailures, nsamples);
    break;

  case MODE_COMPUTE_PROBE_FLUX_DNSTY_ON_SURF:
  case MODE_COMPUTE_LIST_PROBE_FLUX_DNSTY_ON_SURF:
  {
    enum sdis_estimator_type type;
    ERR(sdis_estimator_get_type(estimator, &type));
    ASSERT(type == SDIS_ESTIMATOR_FLUX);

    if(stardis->mode & MODE_EXTENDED_RESULTS) {
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Temperature at [%g, %g, %g] at t=%g = %g K +/- %g\n",
          SPLIT3(stardis->probe), stardis->time_range[0],
          result.E, /* Expected value */
          result.SE); /* Standard error */
      else
        fprintf(stream,
          "Temperature at [%g, %g, %g] with t in [%g %g] = %g K +/- %g\n",
          SPLIT3(stardis->probe), SPLIT2(stardis->time_range),
          result.E, /* Expected value */
          result.SE); /* Standard error */
      ERR(sdis_estimator_get_convective_flux(estimator, &result));
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Convective flux density at [%g, %g, %g] at t=%g = %g W/m² +/- %g\n",
          SPLIT3(stardis->probe), stardis->time_range[0],
          result.E, /* Expected value */
          result.SE); /* Standard error */
      else
        fprintf(stream,
          "Convective flux density at [%g, %g, %g] with t in [%g %g] = %g W/m² +/- %g\n",
          SPLIT3(stardis->probe), SPLIT2(stardis->time_range),
          result.E, /* Expected value */
          result.SE); /* Standard error */
      ERR(sdis_estimator_get_radiative_flux(estimator, &result));
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Radiative flux density at [%g, %g, %g] at t=%g = %g W/m² +/- %g\n",
          SPLIT3(stardis->probe), stardis->time_range[0],
          result.E, /* Expected value */
          result.SE); /* Standard error */
      else
        fprintf(stream,
          "Radiative flux density at [%g, %g, %g] with t in [%g %g] = %g W/m² +/- %g\n",
          SPLIT3(stardis->probe), SPLIT2(stardis->time_range),
          result.E, /* Expected value */
          result.SE); /* Standard error */
      ERR(sdis_estimator_get_imposed_flux(estimator, &result));
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Imposed flux density at [%g, %g, %g] at t=%g = %g W/m² +/- %g\n",
          SPLIT3(stardis->probe), stardis->time_range[0],
          result.E, /* Expected value */
          result.SE); /* Standard error */
      else
        fprintf(stream,
          "Imposed flux density at [%g, %g, %g] with t in [%g %g] = %g W/m² +/- %g\n",
          SPLIT3(stardis->probe), SPLIT2(stardis->time_range),
          result.E, /* Expected value */
          result.SE); /* Standard error */
      ERR(sdis_estimator_get_total_flux(estimator, &result));
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Total flux density at [%g, %g, %g] at t=%g = %g W/m² +/- %g\n",
          SPLIT3(stardis->probe), stardis->time_range[0],
          result.E, /* Expected value */
          result.SE); /* Standard error */
      else
        fprintf(stream,
          "Total flux density at [%g, %g, %g] with t in [%g %g] = %g W/m² +/- %g\n",
          SPLIT3(stardis->probe), SPLIT2(stardis->time_range),
          result.E, /* Expected value */
          result.SE); /* Standard error */
    } else {
      fprintf(stream, "%g %g ", result.E, result.SE); /* Temperature */
      ERR(sdis_estimator_get_convective_flux(estimator, &result));
      fprintf(stream, "%g %g ",
        result.E,
        result.SE);
      ERR(sdis_estimator_get_radiative_flux(estimator, &result));
      fprintf(stream, "%g %g ",
        result.E,
        result.SE);
      ERR(sdis_estimator_get_imposed_flux(estimator, &result));
      fprintf(stream, "%g %g ",
        result.E,
        result.SE);
      ERR(sdis_estimator_get_total_flux(estimator, &result));
      fprintf(stream, "%g %g ",
        result.E,
        result.SE);
      fprintf(stream, "%lu %lu\n", nfailures, nsamples);
    }
    break;
  }

  case MODE_COMPUTE_FLUX_THROUGH_SURF:
  {
    enum sdis_estimator_type type;
    ERR(sdis_estimator_get_type(estimator, &type));
    ASSERT(type == SDIS_ESTIMATOR_FLUX);

    if(stardis->mode & MODE_EXTENDED_RESULTS) {
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Temperature at boundary '%s' at t=%g = %g K +/- %g\n",
          str_cget(&stardis->solve_name), stardis->time_range[0],
          result.E, /* Expected value */
          result.SE); /* Standard error */
      else
        fprintf(stream,
          "Temperature at boundary '%s' with t in [%g %g] = %g K +/- %g\n",
          str_cget(&stardis->solve_name), SPLIT2(stardis->time_range),
          result.E, /* Expected value */
          result.SE); /* Standard error */
      ERR(sdis_estimator_get_convective_flux(estimator, &result));
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Convective flux at boundary '%s' at t=%g = %g W +/- %g\n",
          str_cget(&stardis->solve_name), stardis->time_range[0],
          stardis->compute_surface.area * result.E, /* Expected value */
          stardis->compute_surface.area * result.SE); /* Standard error */
      else
        fprintf(stream,
          "Convective flux at boundary '%s' with t in [%g %g] = %g W +/- %g\n",
          str_cget(&stardis->solve_name), SPLIT2(stardis->time_range),
          stardis->compute_surface.area * result.E, /* Expected value */
          stardis->compute_surface.area * result.SE); /* Standard error */
      ERR(sdis_estimator_get_radiative_flux(estimator, &result));
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Radiative flux at boundary '%s' at t=%g = %g W +/- %g\n",
          str_cget(&stardis->solve_name), stardis->time_range[0],
          stardis->compute_surface.area * result.E, /* Expected value */
          stardis->compute_surface.area * result.SE); /* Standard error */
      else
        fprintf(stream,
          "Radiative flux at boundary '%s' with t in [%g %g] = %g W +/- %g\n",
          str_cget(&stardis->solve_name), SPLIT2(stardis->time_range),
          stardis->compute_surface.area * result.E, /* Expected value */
          stardis->compute_surface.area * result.SE); /* Standard error */
      ERR(sdis_estimator_get_imposed_flux(estimator, &result));
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Imposed flux at boundary '%s' at t=%g = %g W +/- %g\n",
          str_cget(&stardis->solve_name), stardis->time_range[0],
          stardis->compute_surface.area * result.E, /* Expected value */
          stardis->compute_surface.area * result.SE); /* Standard error */
      else
        fprintf(stream,
          "Imposed flux at boundary '%s' with t in [%g %g] = %g W +/- %g\n",
          str_cget(&stardis->solve_name), SPLIT2(stardis->time_range),
          stardis->compute_surface.area * result.E, /* Expected value */
          stardis->compute_surface.area * result.SE); /* Standard error */
      ERR(sdis_estimator_get_total_flux(estimator, &result));
      if(stardis->time_range[0] == stardis->time_range[1])
        fprintf(stream, "Total flux at boundary '%s' at t=%g = %g W +/- %g\n",
          str_cget(&stardis->solve_name), stardis->time_range[0],
          stardis->compute_surface.area * result.E, /* Expected value */
          stardis->compute_surface.area * result.SE); /* Standard error */
      else
        fprintf(stream,
          "Total flux at boundary '%s' with t in [%g %g] = %g W +/- %g\n",
          str_cget(&stardis->solve_name), SPLIT2(stardis->time_range),
          stardis->compute_surface.area * result.E, /* Expected value */
          stardis->compute_surface.area * result.SE); /* Standard error */
    } else {
      fprintf(stream, "%g %g ", result.E, result.SE); /* Temperature */
      ERR(sdis_estimator_get_convective_flux(estimator, &result));
      fprintf(stream, "%g %g ",
        stardis->compute_surface.area * result.E,
        stardis->compute_surface.area * result.SE);
      ERR(sdis_estimator_get_radiative_flux(estimator, &result));
      fprintf(stream, "%g %g ",
        stardis->compute_surface.area * result.E,
        stardis->compute_surface.area * result.SE);
      ERR(sdis_estimator_get_imposed_flux(estimator, &result));
      fprintf(stream, "%g %g ",
        stardis->compute_surface.area * result.E,
        stardis->compute_surface.area * result.SE);
      ERR(sdis_estimator_get_total_flux(estimator, &result));
      fprintf(stream, "%g %g ",
        stardis->compute_surface.area * result.E,
        stardis->compute_surface.area * result.SE);
      fprintf(stream, "%lu %lu\n", nfailures, nsamples);
    }
    break;
  }
  default: FATAL("Invalid mode\n.");
  }
  if(stardis->mode & MODE_EXTENDED_RESULTS)
    fprintf(stream, "#failures: %lu/%lu\n", nfailures, nsamples);
  if(nfailures)
    logger_print(stardis->logger, LOG_ERROR,
      "#failures: %lu/%lu\n", nfailures, nsamples);

end:
  return res;
error:
  goto end;
abort:
  res = RES_BAD_ARG;
  goto error;
}

res_T
print_single_MC_result_probe_boundary
  (struct stardis* stardis,
   const struct stardis_probe_boundary* probe,
   const struct sdis_estimator* estimator,
   FILE* stream)
{
  struct sdis_mc result = SDIS_MC_NULL;
  size_t nfailures = 0;
  size_t nsamples = 0;
  res_T res = RES_OK;

  ASSERT(stardis && probe && estimator);
  ASSERT((stardis->mode & MODE_COMPUTE_PROBE_TEMP_ON_SURF)
      || (stardis->mode & MODE_COMPUTE_LIST_PROBE_TEMP_ON_SURF));

  /* Only master prints or reads estimators */
  ASSERT(!stardis->mpi_initialized || stardis->mpi_rank == 0);

  /* Fetch the estimation data */
  ERR(sdis_estimator_get_temperature(estimator, &result));
  ERR(sdis_estimator_get_failure_count(estimator, &nfailures));
  nsamples = stardis->samples;

  if(nfailures == nsamples) {
    logger_print(stardis->logger, LOG_ERROR,
      "All the %lu samples failed. No result to display.\n", nsamples);
    res = RES_BAD_OP;
    goto error;
  }

  /* Raw output */
  if((stardis->mode & MODE_EXTENDED_RESULTS) == 0)  {
    fprintf(stream, "%g %g %lu %lu\n",
      result.E, result.SE, (unsigned long)nfailures, (unsigned long)nsamples);

  /* Extended output */
  } else if(stardis->time_range[0] == stardis->time_range[1]) {
    fprintf(stream,
      "Boundary temperature at [%g, %g, %g] at t=%g = %g K +/- %g\n",
        SPLIT3(probe->position), probe->time[0], result.E, result.SE);

  /* Extended output with time range */
  } else {
    fprintf(stream,
      "Boundary temperature at [%g, %g, %g] with t in [%g %g] = %g K +/- %g\n",
      SPLIT3(probe->position), SPLIT2(probe->time), result.E, result.SE);
  }

exit:
  return res;
error:
  goto exit;
}

res_T
dump_map
  (const struct stardis* stardis,
   const struct darray_estimators* estimators,
   FILE* stream)
{
  res_T res = RES_OK;
  unsigned i, vcount, tcount, last_v = 0;
  const size_t* idx;
  size_t sz;
  unsigned szp;
  struct sdis_estimator* const* est;

  ASSERT(stardis && estimators && stream);

  /* Only master prints or reads estimators */
  ASSERT(!stardis->mpi_initialized || stardis->mpi_rank == 0);

  est = darray_estimators_cdata_get(estimators);
  idx = darray_size_t_cdata_get(&stardis->compute_surface.primitives);
  sz = darray_size_t_size_get(&stardis->compute_surface.primitives);
  if(sz > UINT_MAX) goto abort;
  szp = (unsigned)sz;
  SG3D(geometry_get_unique_vertices_count(stardis->geometry.sg3d, &vcount));
  SG3D(geometry_get_unique_triangles_count(stardis->geometry.sg3d, &tcount));

  /* Find last used vertex */
  for(i = 0; i < szp; ++i) {
    unsigned t;
    unsigned indices[3];
    if(idx[i] > UINT_MAX) goto abort;
    t = (unsigned)idx[i];
    SG3D(geometry_get_unique_triangle_vertices(stardis->geometry.sg3d, t,
      indices));
    last_v = MMAX(MMAX(last_v, indices[0]), MMAX(indices[1], indices[2]));
  }

  /* Dump vertices up to last_v, even unused ones, to avoid reindexing */
  fprintf(stream, "# vtk DataFile Version 2.0\n");
  fprintf(stream, "Temperature Map\n");
  fprintf(stream, "ASCII\n");
  fprintf(stream, "DATASET POLYDATA\n");
  fprintf(stream, "POINTS %u float\n\n", last_v + 1);
  for(i = 0; i <= last_v; ++i) {
    double coord[3];
    SG3D(geometry_get_unique_vertex(stardis->geometry.sg3d, i, coord));
    fprintf(stream, "%f %f %f\n", SPLIT3(coord));
  }
  /* Dump only primitives in boundary */
  fprintf(stream, "\nPOLYGONS %u %u\n", szp, 4 * szp);
  for(i = 0; i < szp; ++i) {
    unsigned t;
    unsigned indices[3];
    if(idx[i] > UINT_MAX) goto abort;
    t = (unsigned)idx[i];
    SG3D(geometry_get_unique_triangle_vertices(stardis->geometry.sg3d, t,
      indices));
    fprintf(stream, "3 %u %u %u\n", SPLIT3(indices));
  }
  fprintf(stream, "\nCELL_DATA %u\n", szp);
  fprintf(stream, "SCALARS temperature_estimate float 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  for(i = 0; i < szp; ++i) {
    struct sdis_mc T;
    SDIS(estimator_get_temperature(est[i], &T));
    fprintf(stream, "%f\n", T.E);
  }
  fprintf(stream, "SCALARS temperature_std_dev float 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  for(i = 0; i < szp; ++i) {
    struct sdis_mc T;
    SDIS(estimator_get_temperature(est[i], &T));
    fprintf(stream, "%f\n", T.SE);
  }
  fprintf(stream, "SCALARS failures_count unsigned_long_long 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  for(i = 0; i < szp; ++i) {
    size_t nfails;
    SDIS(estimator_get_failure_count(est[i], &nfails));
    if(nfails > UINT_MAX) goto abort;
    fprintf(stream, "%u\n", (unsigned)nfails);
  }
  fprintf(stream, "SCALARS computation_time_estimate float 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  for(i = 0; i < szp; ++i) {
    struct sdis_mc time;
    SDIS(estimator_get_realisation_time(est[i], &time));
    fprintf(stream, "%f\n", time.E);
  }
  fprintf(stream, "SCALARS computation_time_std_dev float 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  for(i = 0; i < szp; ++i) {
    struct sdis_mc time;
    SDIS(estimator_get_realisation_time(est[i], &time));
    fprintf(stream, "%f\n", time.SE);
  }
end:
  return res;
error:
  goto end;
abort:
  res = RES_BAD_ARG;
  goto error;
}

res_T
dump_compute_region_at_the_end_of_vtk
  (struct stardis* stardis,
   FILE* stream)
{
  res_T res = RES_OK;
  unsigned char* v = NULL;
  unsigned tsz, i;
  size_t j, psz;
  ASSERT(stardis && stream);

  /* Only master prints or reads estimators */
  ASSERT(!stardis->mpi_initialized || stardis->mpi_rank == 0);

  psz = darray_size_t_size_get(&stardis->compute_surface.primitives);
  ASSERT(psz == darray_sides_size_get(&stardis->compute_surface.sides));

  ERR(sg3d_geometry_get_unique_triangles_count(stardis->geometry.sg3d, &tsz));
  /* For triangles not in compute region v==0 */
  v = MEM_CALLOC(stardis->allocator, tsz, sizeof(*v));
  if(!v) {
    res = RES_MEM_ERR;
    goto error;
  }

  if(stardis->mode & SURFACE_COMPUTE_MODES) {
    /* For triangles in compute surface, v==1 if FRONT or v==2 for BACK */
    FOR_EACH(j, 0, psz) {
      size_t prim
        = darray_size_t_cdata_get(&stardis->compute_surface.primitives)[j];
      enum sdis_side side
        = darray_sides_cdata_get(&stardis->compute_surface.sides)[j];
      ASSERT(prim <= tsz);
      v[(unsigned)prim] =
        (unsigned char)(v[(unsigned)prim] | (side == SDIS_FRONT ? 1 : 2));
    }

    /* For triangles in compute surface with error v==MAX */
    FOR_EACH(j, 0, darray_uint_size_get(&stardis->compute_surface.err_triangles))
    {
      unsigned prim
        = darray_uint_cdata_get(&stardis->compute_surface.err_triangles)[j];
      ASSERT(prim <= tsz);
      v[(unsigned)prim] = UCHAR_MAX;
    }
  } else {
    unsigned descr[SG3D_PROP_TYPES_COUNT__];
    struct sdis_medium* medium;
    const struct description* descriptions;
    unsigned medium_id;
    ASSERT(stardis->mode & MODE_COMPUTE_TEMP_MEAN_IN_MEDIUM);
    medium = find_medium_by_name
      (stardis, str_cget(&stardis->solve_name), &medium_id);
    ASSERT(medium != NULL); (void)medium;
    descriptions = darray_descriptions_cdata_get(&stardis->descriptions);
    FOR_EACH(i, 0, tsz) {
      unsigned f_mid, b_mid;
      /* Get the description IDs for this triangle */
      ERR(sg3d_geometry_get_unique_triangle_properties(stardis->geometry.sg3d,
        i, descr));
      /* For triangles in compute volume,
       * v==1 if FRONT, v==2 for BACK */
      if(descr[SG3D_FRONT] == SG3D_UNSPECIFIED_PROPERTY)
        f_mid = UINT_MAX;
      else description_get_medium_id(descriptions + descr[SG3D_FRONT], &f_mid);
      if(descr[SG3D_BACK] == SG3D_UNSPECIFIED_PROPERTY)
        b_mid = UINT_MAX;
      else description_get_medium_id(descriptions + descr[SG3D_BACK], &b_mid);
      if(f_mid == medium_id && b_mid == medium_id)
        ; /* Keep v==0, not really a boundary */
      else if(f_mid == medium_id)
        v[i] = 1;
      else if(b_mid == medium_id)
        v[i] = 2;
    }
  }

  fprintf(stream, "SCALARS Compute_region unsigned_int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(i, 0, tsz)
    fprintf(stream, "%u\n", v[i] == UCHAR_MAX ? UINT_MAX : v[i]);

exit:
  MEM_RM(stardis->allocator, v);
  return res;
error:
  goto exit;
}

res_T
dump_model_as_c_chunks
  (struct stardis* stardis,
   FILE* stream)
{
  res_T res = RES_OK;
  const char* prefix;
  unsigned n, vcount, tcount;

  ASSERT(stardis && stream);

  /* Only master prints or reads estimators */
  ASSERT(!stardis->mpi_initialized || stardis->mpi_rank == 0);

  prefix = str_cget(&stardis->chunks_prefix);
  ERR(sg3d_geometry_get_unique_vertices_count(stardis->geometry.sg3d, &vcount));
  ERR(sg3d_geometry_get_unique_triangles_count(stardis->geometry.sg3d, &tcount));

  fprintf(stream, "#define %s_UNSPECIFIED_PROPERTY %u\n\n",
    prefix, SG3D_UNSPECIFIED_PROPERTY);

  fprintf(stream, "static const unsigned\n");
  fprintf(stream, "%s_vertices_count = %u;\n\n", prefix, vcount);

  fprintf(stream, "static const unsigned\n");
  fprintf(stream, "%s_triangles_count = %u;\n\n", prefix, tcount);

  fprintf(stream, "static const double\n");
  fprintf(stream, "%s_vertices[%u][3] = {\n", prefix, vcount);
  for(n = 0; n < vcount; n++) {
    double vertex[3];
    ERR(sg3d_geometry_get_unique_vertex(stardis->geometry.sg3d, n,
      vertex));
    fprintf(stream, "   { %g, %g, %g }%c\n",
      SPLIT3(vertex), (n == vcount - 1 ? ' ' : ','));
  }
  fprintf(stream, "};\n\n");

  fprintf(stream, "static const unsigned\n");
  fprintf(stream, "%s_triangles[%u][3] = {\n", prefix, tcount);
  for(n = 0; n < tcount; n++) {
    unsigned triangle[3];
    ERR(sg3d_geometry_get_unique_triangle_vertices(stardis->geometry.sg3d, n,
      triangle));
    fprintf(stream, "   { %u, %u, %u }%c\n",
      SPLIT3(triangle), (n == tcount - 1 ? ' ' : ','));
  }
  fprintf(stream, "};\n\n");

  fprintf(stream, "static const unsigned\n");
  fprintf(stream, "%s_properties[%u][3] = {\n", prefix, tcount);
  for(n = 0; n < tcount; n++) {
    unsigned properties[SG3D_PROP_TYPES_COUNT__];
    ERR(sg3d_geometry_get_unique_triangle_properties(stardis->geometry.sg3d, n,
      properties));
    if(properties[0] == SG3D_UNSPECIFIED_PROPERTY)
      fprintf(stream, "   { %s_UNSPECIFIED_PROPERTY, ", prefix);
    else fprintf(stream, "   { %u, ", properties[0]);
    if(properties[1] == SG3D_UNSPECIFIED_PROPERTY)
      fprintf(stream, "%s_UNSPECIFIED_PROPERTY, ", prefix);
    else fprintf(stream, "%u, ", properties[1]);
    if(properties[2] == SG3D_UNSPECIFIED_PROPERTY)
      fprintf(stream, "%s_UNSPECIFIED_PROPERTY }", prefix);
    else fprintf(stream, "%u }", properties[2]);
    if(n == tcount - 1) fprintf(stream, "\n"); else fprintf(stream, ",\n");
  }
  fprintf(stream, "};\n\n");

exit:
  return res;
error:
  goto exit;
}

res_T
write_random_generator_state
  (struct sdis_estimator* estimator,
   FILE* stream)
{
  res_T res;
  struct ssp_rng* state;
  res = sdis_estimator_get_rng_state(estimator, &state);
  if(res != RES_OK) return res;
  return ssp_rng_write(state, stream);
}

res_T
read_random_generator_state
  (struct ssp_rng* state,
   FILE* stream)
{
  return ssp_rng_read(state, stream);
}
