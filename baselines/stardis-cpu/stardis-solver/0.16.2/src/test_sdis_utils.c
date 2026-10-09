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

#include "test_sdis_utils.h"
#include <rsys_math.h>

enum heat_vertex_attrib {
  HEAT_VERTEX_BRANCH_ID,
  HEAT_VERTEX_WEIGHT,
  HEAT_VERTEX_TIME,
  HEAT_VERTEX_TYPE
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
struct green_accum { double sum, sum2; };

static res_T
count_green_paths(struct sdis_green_path* path, void* ctx)
{
  CHK(path && ctx);
  *((size_t*)ctx) += 1;
  return RES_OK;
}

static res_T
accum_power_terms(struct sdis_medium* mdm, const double power_term, void* ctx)
{
  struct sdis_solid_shader shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;
  struct sdis_data* data = NULL;
  double* power = ctx; /* Power contribution [K] */

  CHK(mdm && ctx);
  CHK(sdis_medium_get_type(mdm) == SDIS_SOLID);

  OK(sdis_solid_get_shader(mdm, &shader));
  data = sdis_medium_get_data(mdm);
  vtx.time = INF;

  *power += power_term * shader.volumic_power(&vtx, data);
  return RES_OK;
}

static res_T
accum_flux_terms
  (struct sdis_interface* interf,
   const enum sdis_side side,
   const double flux_term,
   void* ctx)
{
  struct sdis_interface_shader shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
  struct sdis_data* data = NULL;
  double* flux = ctx; /* Flux contribution [K] */
  double phi;

  CHK(interf && ctx);

  OK(sdis_interface_get_shader(interf, &shader));
  data = sdis_interface_get_data(interf);
  frag.time = INF;
  frag.side = side;

  phi = side == SDIS_FRONT
    ? shader.front.flux(&frag, data)
    : shader.back.flux(&frag, data);

  *flux += flux_term * phi;
  return RES_OK;
}

static res_T
accum_extflux
  (struct sdis_source* source,
   const struct sdis_green_external_flux_terms* terms,
   void* ctx)
{
  struct sdis_spherical_source_shader shader = SDIS_SPHERICAL_SOURCE_SHADER_NULL;
  struct sdis_data* data = NULL;
  double* extflux = ctx; /* External flux contribution [K] */
  double power = 0; /* [W] */
  double diffuse_radiance = 0; /* [W/m^2/sr] */

  CHK(source && terms && ctx);

  data = sdis_source_get_data(source);
  OK(sdis_spherical_source_get_shader(source, &shader));
  power = shader.power(terms->time, data);
  if(shader.diffuse_radiance) {
    diffuse_radiance = shader.diffuse_radiance(terms->time, terms->dir, data);
  }

  *extflux += terms->term_wrt_power * power;
  *extflux += terms->term_wrt_diffuse_radiance * diffuse_radiance;
  return RES_OK;
}

static res_T
solve_green_path(struct sdis_green_path* path, void* ctx)
{
  struct sdis_green_path_end end = SDIS_GREEN_PATH_END_NULL;

  struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
  struct sdis_radiative_ray ray = SDIS_RADIATIVE_RAY_NULL;

  struct sdis_solid_shader solid = SDIS_SOLID_SHADER_NULL;
  struct sdis_fluid_shader fluid = SDIS_FLUID_SHADER_NULL;
  struct sdis_interface_shader interf = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_radiative_env_shader radenv = SDIS_RADIATIVE_ENV_SHADER_NULL;

  struct sdis_green_function* green = NULL;
  struct sdis_scene* scn = NULL;
  struct green_accum* acc = NULL;
  struct sdis_data* data = NULL;
  enum sdis_medium_type type;
  double power = 0;
  double flux = 0;
  double extflux = 0;
  double time, temp = 0;
  double weight = 0;
  CHK(path && ctx);

  acc = ctx;

  BA(sdis_green_path_get_green_function(NULL, NULL));
  BA(sdis_green_path_get_green_function(path, NULL));
  BA(sdis_green_path_get_green_function(NULL, &green));
  OK(sdis_green_path_get_green_function(path, &green));

  BA(sdis_green_function_get_scene(NULL, NULL));
  BA(sdis_green_function_get_scene(NULL, &scn));
  BA(sdis_green_function_get_scene(green, NULL));
  OK(sdis_green_function_get_scene(green, &scn));

  BA(sdis_green_path_for_each_power_term(NULL, accum_power_terms, &power));
  BA(sdis_green_path_for_each_power_term(path, NULL, &acc));
  OK(sdis_green_path_for_each_power_term(path, accum_power_terms, &power));

  BA(sdis_green_path_for_each_flux_term(NULL, accum_flux_terms, &flux));
  BA(sdis_green_path_for_each_flux_term(path, NULL, &acc));
  OK(sdis_green_path_for_each_flux_term(path, accum_flux_terms, &flux));

  BA(sdis_green_path_for_each_external_flux_terms(NULL, &accum_extflux, &extflux));
  BA(sdis_green_path_for_each_external_flux_terms(path, NULL, &extflux));
  OK(sdis_green_path_for_each_external_flux_terms(path, &accum_extflux, &extflux));

  BA(sdis_green_path_get_elapsed_time(NULL, NULL));
  BA(sdis_green_path_get_elapsed_time(path, NULL));
  BA(sdis_green_path_get_elapsed_time(NULL, &time));
  OK(sdis_green_path_get_elapsed_time(path, &time));

  BA(sdis_green_path_get_end(NULL, NULL));
  BA(sdis_green_path_get_end(NULL, &end));
  BA(sdis_green_path_get_end(path, NULL));
  OK(sdis_green_path_get_end(path, &end));
  switch(end.type) {
    case SDIS_GREEN_PATH_END_AT_INTERFACE:
      frag = end.data.itfrag.fragment;
      OK(sdis_interface_get_shader(end.data.itfrag.intface, &interf));
      data = sdis_interface_get_data(end.data.itfrag.intface);
      temp = frag.side == SDIS_FRONT
        ? interf.front.temperature(&frag, data)
        : interf.back.temperature(&frag, data);
      break;

    case SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV:
      ray = end.data.radenvray.ray;
      OK(sdis_radiative_env_get_shader(end.data.radenvray.radenv, &radenv));
      data = sdis_radiative_env_get_data(end.data.radenvray.radenv);
      temp = radenv.temperature(&ray, data);
      break;

    case SDIS_GREEN_PATH_END_IN_VOLUME:
      vtx = end.data.mdmvert.vertex;
      type = sdis_medium_get_type(end.data.mdmvert.medium);
      data = sdis_medium_get_data(end.data.mdmvert.medium);
      if(type == SDIS_FLUID) {
        OK(sdis_fluid_get_shader(end.data.mdmvert.medium, &fluid));
        temp = fluid.temperature(&vtx, data);
      } else {
        OK(sdis_solid_get_shader(end.data.mdmvert.medium, &solid));
        temp = solid.temperature(&vtx, data);
      }
      break;

    default: FATAL("Unreachable code.\n"); break;
  }

  weight = temp + power + extflux + flux;
  acc->sum += weight;
  acc->sum2 += weight*weight;

  return RES_OK;
}

static size_t
heat_path_get_vertices_count(const struct sdis_heat_path* path)
{
  size_t istrip = 0;
  size_t nstrips = 0;
  size_t nvertices = 0;
  CHK(path);

  OK(sdis_heat_path_get_line_strips_count(path, &nstrips));
  FOR_EACH(istrip, 0, nstrips) {
    size_t n;
    OK(sdis_heat_path_line_strip_get_vertices_count(path, istrip, &n));
    nvertices += n;
  }
  return nvertices;
}

static void
dump_heat_path_positions(FILE* stream, const struct sdis_heat_path* path)
{
  size_t istrip, nstrips;
  size_t ivert, nverts;

  CHK(stream && path);

  OK(sdis_heat_path_get_line_strips_count(path, &nstrips));
  FOR_EACH(istrip, 0, nstrips) {
    OK(sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts));
    FOR_EACH(ivert, 0, nverts) {
      struct sdis_heat_vertex vtx;
      OK(sdis_heat_path_line_strip_get_vertex(path, istrip, ivert, &vtx));
      fprintf(stream, "%g %g %g\n", SPLIT3(vtx.P));
    }
  }
}

static void
dump_heat_path_line_strip
  (FILE* stream,
   const struct sdis_heat_path* path,
   const size_t istrip,
   const size_t offset)
{
  size_t ivert, nverts;

  CHK(stream);

  OK(sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts));
  fprintf(stream, "%lu", (unsigned long)nverts);
  FOR_EACH(ivert, 0, nverts) {
    fprintf(stream, " %lu", (unsigned long)(ivert + offset));
  }
  fprintf(stream, "\n");
}

static void
dump_heat_path_vertex_attribs
  (FILE* stream,
   const struct sdis_heat_path* path,
   const enum heat_vertex_attrib attr)
{
  size_t ivert, nverts;
  size_t istrip, nstrips;
  CHK(stream && path);

  OK(sdis_heat_path_get_line_strips_count(path, &nstrips));
  FOR_EACH(istrip, 0, nstrips) {
    OK(sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts));
    FOR_EACH(ivert, 0, nverts) {
      struct sdis_heat_vertex vtx;
      OK(sdis_heat_path_line_strip_get_vertex(path, istrip, ivert, &vtx));
      switch(attr) {
        case HEAT_VERTEX_BRANCH_ID:
          fprintf(stream, "%i\n", vtx.branch_id);
          break;
        case HEAT_VERTEX_WEIGHT:
          fprintf(stream, "%g\n", vtx.weight);
          break;
        case HEAT_VERTEX_TIME:
          fprintf(stream, "%g\n",
            IS_INF(vtx.time) || vtx.time > FLT_MAX ? -1 : vtx.time);
          break;
        case HEAT_VERTEX_TYPE:
          switch(vtx.type) {
            case SDIS_HEAT_VERTEX_CONDUCTION: fprintf(stream, "0.0\n"); break;
            case SDIS_HEAT_VERTEX_CONVECTION: fprintf(stream, "0.5\n"); break;
            case SDIS_HEAT_VERTEX_RADIATIVE:  fprintf(stream, "1.0\n"); break;
            default: FATAL("Unreachable code.\n"); break;
          }
          break;
        default: FATAL("Unreachable code.\n"); break;
      }
    }
  }
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
void
check_green_function(struct sdis_green_function* green)
{
  double time_range[2];
  struct sdis_estimator* estimator;
  struct sdis_mc mc;
  struct green_accum accum = {0, 0};
  double E, V, SE;
  size_t nreals;
  size_t nfails;
  size_t n;

  time_range[0] = time_range[1] = INF;

  OK(sdis_green_function_solve(green, &estimator));

  BA(sdis_green_function_get_paths_count(NULL, &n));
  BA(sdis_green_function_get_paths_count(green, NULL));
  OK(sdis_green_function_get_paths_count(green, &n));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  CHK(n == nreals);

  BA(sdis_green_function_get_invalid_paths_count(NULL, &n));
  BA(sdis_green_function_get_invalid_paths_count(green, NULL));
  OK(sdis_green_function_get_invalid_paths_count(green, &n));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  CHK(n == nfails);

  n = 0;
  BA(sdis_green_function_for_each_path(NULL, count_green_paths, &n));
  BA(sdis_green_function_for_each_path(green, NULL, &n));
  OK(sdis_green_function_for_each_path(green, count_green_paths, &n));
  CHK(n == nreals);

  OK(sdis_green_function_for_each_path(green, solve_green_path, &accum));

  E = accum.sum / (double)n;
  V = MMAX(0, accum.sum2 / (double)n - E*E);
  SE = sqrt(V/(double)n);
  OK(sdis_estimator_get_temperature(estimator, &mc));

  printf("Green: rebuild = %g +/- %g; solved = %g +/- %g\n",
    E, SE, mc.E, mc.SE);

  CHK(E + SE >= mc.E - mc.SE);
  CHK(E - SE <= mc.E + mc.SE);

  OK(sdis_estimator_get_realisation_time(estimator, &mc));
  printf("Green per realisation time (in usec) = %g +/- %g\n",
    mc.E, mc.SE);

  OK(sdis_estimator_ref_put(estimator));
}

void
dump_heat_paths(FILE* stream, const struct sdis_estimator* estimator)
{
  const struct sdis_heat_path* path;
  size_t ipath;
  size_t npaths;
  size_t nstrips_overall;
  size_t nvertices;
  size_t offset;
  CHK(stream && estimator);

  OK(sdis_estimator_get_paths_count(estimator, &npaths));
  CHK(npaths);

  /* Header */
  fprintf(stream, "# vtk DataFile Version 2.0\n");
  fprintf(stream, "Heat paths\n");
  fprintf(stream, "ASCII\n");
  fprintf(stream, "DATASET POLYDATA\n");

  /* Compute the overall number of vertices and the overall number of strips */
  nvertices = 0;
  nstrips_overall = 0;
  FOR_EACH(ipath, 0, npaths) {
    size_t n;
    OK(sdis_estimator_get_path(estimator, ipath, &path));
    nvertices += heat_path_get_vertices_count(path);
    OK(sdis_heat_path_get_line_strips_count(path, &n));
    nstrips_overall += n;
  }

  /* Write path positions */
  fprintf(stream, "POINTS %lu double\n", (unsigned long)nvertices);
  FOR_EACH(ipath, 0, npaths) {
    OK(sdis_estimator_get_path(estimator, ipath, &path));
    dump_heat_path_positions(stream, path);
  }

  /* Write the segment of the paths */
  fprintf(stream, "LINES %lu %lu\n",
    (unsigned long)(nstrips_overall),
    (unsigned long)(nstrips_overall + nvertices));
  offset = 0;
  FOR_EACH(ipath, 0, npaths) {
    size_t path_istrip;
    size_t path_nstrips;
    OK(sdis_estimator_get_path(estimator, ipath, &path));
    OK(sdis_heat_path_get_line_strips_count(path, &path_nstrips));
    FOR_EACH(path_istrip, 0, path_nstrips) {
      size_t n;
      dump_heat_path_line_strip(stream, path, path_istrip, offset);
      OK(sdis_heat_path_line_strip_get_vertices_count(path, path_istrip, &n));
      offset += n;
    }
  }

  fprintf(stream, "POINT_DATA %lu\n", (unsigned long)nvertices);

  /* Write the type of the random walk vertices */
  fprintf(stream, "SCALARS Vertex_Type float 1\n");
  fprintf(stream, "LOOKUP_TABLE vertex_type\n");
  FOR_EACH(ipath, 0, npaths) {
    OK(sdis_estimator_get_path(estimator, ipath, &path));
    dump_heat_path_vertex_attribs(stream, path, HEAT_VERTEX_TYPE);
  }
  fprintf(stream, "LOOKUP_TABLE vertex_type 3\n");
  fprintf(stream, "0.0 1.0 1.0 1.0\n"); /* 0.0 = Magenta: conduction */
  fprintf(stream, "1.0 1.0 0.0 1.0\n"); /* 0.5 = Yellow: convection */
  fprintf(stream, "1.0 0.0 1.0 1.0\n"); /* 1.0 = Purple: radiative */

  /* Write the weights of the random walk vertices */
  fprintf(stream, "SCALARS Weight double 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(ipath, 0, npaths) {
    OK(sdis_estimator_get_path(estimator, ipath, &path));
    dump_heat_path_vertex_attribs(stream, path, HEAT_VERTEX_WEIGHT);
  }

  /* Write the time of the random walk vertices */
  fprintf(stream, "SCALARS Time double 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(ipath, 0, npaths) {
    OK(sdis_estimator_get_path(estimator, ipath, &path));
    dump_heat_path_vertex_attribs(stream, path, HEAT_VERTEX_TIME);
  }

  /* Write the branch id of the random walk vertices */
  fprintf(stream, "SCALARS BranchID int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(ipath, 0, npaths) {
    OK(sdis_estimator_get_path(estimator, ipath, &path));
    dump_heat_path_vertex_attribs(stream, path, HEAT_VERTEX_BRANCH_ID);
  }

  /* Write path type */
  fprintf(stream, "CELL_DATA %lu\n", (unsigned long)nstrips_overall);
  fprintf(stream, "SCALARS Path_Type float 1\n");
  fprintf(stream, "LOOKUP_TABLE path_type\n");
  FOR_EACH(ipath, 0, npaths) {
    size_t path_istrip;
    size_t path_nstrips;
    enum sdis_heat_path_flag status = SDIS_HEAT_PATH_NONE;
    OK(sdis_estimator_get_path(estimator, ipath, &path));
    OK(sdis_heat_path_get_status(path, &status));
    OK(sdis_heat_path_get_line_strips_count(path, &path_nstrips));
    FOR_EACH(path_istrip, 0, path_nstrips) {
      switch(status) {
        case SDIS_HEAT_PATH_SUCCESS: fprintf(stream, "0.0\n"); break;
        case SDIS_HEAT_PATH_FAILURE: fprintf(stream, "1.0\n"); break;
        default: FATAL("Unreachable code.\n"); break;
      }
    }
  }
  fprintf(stream, "LOOKUP_TABLE path_type 2\n");
  fprintf(stream, "0.0 0.0 1.0 1.0\n"); /* 0.0 = Blue: success */
  fprintf(stream, "1.0 0.0 0.0 1.0\n"); /* 1.0 = Red: failure */
}

void
check_green_serialization
  (struct sdis_green_function* green,
   struct sdis_scene* scn)
{
  struct sdis_green_function_create_from_stream_args args =
    SDIS_GREEN_FUNCTION_CREATE_FROM_STREAM_ARGS_DEFAULT;
  FILE* stream = NULL;
  struct sdis_estimator *e1 = NULL;
  struct sdis_estimator *e2 = NULL;
  struct sdis_green_function* green2 = NULL;

  CHK(green && scn);
  stream = tmpfile();
  CHK(stream);

  OK(sdis_green_function_write(green, stream));

  rewind(stream);
  args.scene = scn;
  args.stream = stream;
  OK(sdis_green_function_create_from_stream(&args, &green2));
  CHK(!fclose(stream));
  check_green_function(green2);

  OK(sdis_green_function_solve(green, &e1));
  OK(sdis_green_function_solve(green2, &e2));
  check_estimator_eq_strict(e1, e2);

  OK(sdis_estimator_ref_put(e1));
  OK(sdis_estimator_ref_put(e2));
  OK(sdis_green_function_ref_put(green2));
}
