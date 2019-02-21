/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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
#include <rsys/math.h>

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
  double* power = ctx;

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
  double* flux = ctx;
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
solve_green_path(struct sdis_green_path* path, void* ctx)
{
  struct sdis_point pt = SDIS_POINT_NULL;
  struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
  struct sdis_solid_shader solid = SDIS_SOLID_SHADER_NULL;
  struct sdis_fluid_shader fluid = SDIS_FLUID_SHADER_NULL;
  struct sdis_interface_shader interf = SDIS_INTERFACE_SHADER_NULL;
  struct green_accum* acc = NULL;
  struct sdis_data* data = NULL;
  enum sdis_medium_type type;
  double power = 0;
  double flux = 0;
  double temp = 0;
  double weight = 0;
  CHK(path && ctx);

  acc = ctx;

  BA(sdis_green_path_for_each_power_term(NULL, accum_power_terms, &power));
  BA(sdis_green_path_for_each_power_term(path, NULL, &acc));
  OK(sdis_green_path_for_each_power_term(path, accum_power_terms, &power));

  BA(sdis_green_path_for_each_flux_term(NULL, accum_flux_terms, &flux));
  BA(sdis_green_path_for_each_flux_term(path, NULL, &acc));
  OK(sdis_green_path_for_each_flux_term(path, accum_flux_terms, &flux));

  BA(sdis_green_path_get_limit_point(NULL, &pt));
  BA(sdis_green_path_get_limit_point(path, NULL));
  OK(sdis_green_path_get_limit_point(path, &pt));

  switch(pt.type) {
    case SDIS_FRAGMENT:
      frag = pt.data.itfrag.fragment;
      frag.time = INF;
      OK(sdis_interface_get_shader(pt.data.itfrag.interface, &interf));
      data = sdis_interface_get_data(pt.data.itfrag.interface);
      temp = frag.side == SDIS_FRONT
        ? interf.front.temperature(&frag, data)
        : interf.back.temperature(&frag, data);
      break;
    case SDIS_VERTEX:
      vtx = pt.data.mdmvert.vertex;
      vtx.time = INF;
      type = sdis_medium_get_type(pt.data.mdmvert.medium);
      data = sdis_medium_get_data(pt.data.mdmvert.medium);
      if(type == SDIS_FLUID) {
        OK(sdis_fluid_get_shader(pt.data.mdmvert.medium, &fluid));
        temp = fluid.temperature(&vtx, data);
      } else {
        OK(sdis_solid_get_shader(pt.data.mdmvert.medium, &solid));
        temp = solid.temperature(&vtx, data);
      }
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

  weight = temp + power + flux;
  acc->sum += weight;
  acc->sum2 += weight*weight;

  return RES_OK;
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

  OK(sdis_green_function_solve(green, time_range, &estimator));

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

  OK(sdis_estimator_ref_put(estimator));
}

