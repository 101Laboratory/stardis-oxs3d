/* Copyright (C) 2016-2023 |Méso|Star> (contact@meso-star.com)
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

#include "sdis_heat_path_boundary_c.h"
#include "sdis_interface_c.h"
#include "sdis_log.h"
#include "sdis_scene_c.h"
#include "sdis_source_c.h"

#include <rsys/cstr.h> /* res_to_cstr */

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Non generic data types and function
 ******************************************************************************/
#ifndef SDIS_HEAT_PATH_BOUNDARY_XD_HANDLE_EXTERNAL_NET_FLUX_H
#define SDIS_HEAT_PATH_BOUNDARY_XD_HANDLE_EXTERNAL_NET_FLUX_H

enum brdf_component {
  BRDF_SPECULAR,
  BRDF_DIFFUSE,
  BRDF_NONE
};

struct brdf_sample {
  double dir[3];
  double pdf;
  enum brdf_component cpnt;
};
#define BRDF_SAMPLE_NULL__ {{0}, 0, BRDF_NONE}
static const struct brdf_sample BRDF_SAMPLE_NULL = BRDF_SAMPLE_NULL__;

struct brdf {
  double emissivity;
  double specular_fraction;
};
#define BRDF_NULL__ {0, 0}
static const struct brdf BRDF_NULL = BRDF_NULL__;

/* Reflect the V wrt the normal N. By convention V points outward the surface.
 * In fact, this function is a double-precision version of the reflect_3d
 * function. TODO Clean this "repeat" */
static FINLINE double*
reflect(double res[3], const double V[3], const double N[3])
{
  double tmp[3];
  double cos_V_N;
  ASSERT(res && V && N);
  ASSERT(d3_is_normalized(V) && d3_is_normalized(N));
  cos_V_N = d3_dot(V, N);
  d3_muld(tmp, N, 2*cos_V_N);
  d3_sub(res, tmp, V);
  return res;
}

static void
sample_brdf
  (const struct brdf* brdf,
   struct ssp_rng* rng,
   const double wi[3], /* Incident direction. Point away from the surface */
   const double N[3], /* Surface normal */
   struct brdf_sample* sample)
{
  double r = 0; /* Random number */

  /* Preconditions */
  ASSERT(brdf && rng && wi && N && sample);
  ASSERT(d3_is_normalized(wi) && d3_is_normalized(N));
  ASSERT(d3_dot(wi, N) > 0);

  r = ssp_rng_canonical(rng);

  /* Sample the specular part */
  if(r < brdf->specular_fraction) {
    reflect(sample->dir, wi, N);
    sample->pdf = 1;
    sample->cpnt = BRDF_SPECULAR;

  /* Sample the diffuse part */
  } else {
    ssp_ran_hemisphere_cos(rng, N, sample->dir, NULL);
    sample->pdf = 1.0/PI;
    sample->cpnt = BRDF_DIFFUSE;
  }
}

/* Check that the trajectory reaches a valid interface, i.e. that it is on a
 * fluid/solid interface and has reached it from the fluid */
static res_T
check_interface
  (const struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag)
{
  enum sdis_medium_type mdm_frt_type = SDIS_MEDIUM_TYPES_COUNT__;
  enum sdis_medium_type mdm_bck_type = SDIS_MEDIUM_TYPES_COUNT__;
  enum sdis_side fluid_side = SDIS_SIDE_NULL__;
  res_T res = RES_OK;

  mdm_frt_type = sdis_medium_get_type(interf->medium_front);
  mdm_bck_type = sdis_medium_get_type(interf->medium_back);

  /* Semi-transparent materials are not supported. This means that a solid/solid
   * interface must not be intersected when tracing radiative paths */
  if(mdm_frt_type == SDIS_SOLID && mdm_bck_type == SDIS_SOLID) {
    log_err(interf->dev,
      "Error when sampling the trajectory to calculate the incident diffuse "
      "flux. The trajectory reaches a solid/solid interface, whereas this is "
      "supposed to be impossible (path position: %g, %g, %g).\n",
      SPLIT3(frag->P));
    res = RES_BAD_OP;
    goto error;
  }

  /* Find out which side of the interface the fluid is on */
  if(mdm_frt_type == SDIS_FLUID) {
    fluid_side = SDIS_FRONT;
  } else if(mdm_bck_type == SDIS_FLUID) {
    fluid_side = SDIS_BACK;
  } else {
    FATAL("Unreachable code\n");
  }

  /* Check that the current position is on the correct side of the interface */
  if(frag->side != fluid_side) {
    log_err(interf->dev,
      "Inconsistent intersection when sampling the trajectory to calculate the "
      "incident diffuse flux. The radiative path reaches an interface on "
      "its solid side, whereas this is supposed to be impossible "
      "(path position: %g, %g, %g).\n",
      SPLIT3(frag->P));
    res = RES_BAD_OP;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

#endif /* SDIS_HEAT_PATH_BOUNDARY_XD_HANDLE_EXTERNAL_NET_FLUX_H */

/*******************************************************************************
 * Generic helper functions
 ******************************************************************************/
static INLINE res_T
XD(check_handle_external_net_flux_args)
  (const struct sdis_scene* scn,
   const char* func_name,
   const struct XD(handle_external_net_flux_args)* args)
{
  int net_flux = 0;
  res_T res = RES_OK;

  /* Handle bugs */
  ASSERT(scn && func_name && args);
  ASSERT(args->interf && args->frag);
  ASSERT(!SXD_HIT_NONE(args->hit));
  ASSERT(args->h_cond >= 0 && args->h_conv >= 0 && args->h_radi >= 0);
  ASSERT(args->h_cond + args->h_conv + args->h_radi > 0);

  net_flux = interface_side_is_external_flux_handled(args->interf, args->frag);
  net_flux = net_flux && (scn->source != NULL);

  if(net_flux && args->picard_order != 1) {
    log_err(scn->dev,
      "%s: Impossible to process external fluxes when Picard order is not "
      "equal to 1; Picard order is currently set to %lu.\n",
      func_name, (unsigned long)args->picard_order);
    res = RES_BAD_ARG;
    return res;
  }

  if(sdis_medium_get_type(args->interf->medium_back)
  == sdis_medium_get_type(args->interf->medium_front)) {
    log_err(scn->dev,
      "%s: external fluxes can only be processed on fluid/solid interfaces.\n",
      func_name);
    res = RES_BAD_ARG;
    return res;
  }

  return RES_OK;
}

static INLINE void
XD(trace_ray)
  (const struct sdis_scene* scn,
   const double pos[DIM],
   const double dir[3],
   const double distance,
   const struct sXd(hit)* hit_from,
   struct sXd(hit)* hit)
{
  struct hit_filter_data filter_data = HIT_FILTER_DATA_NULL;
  float ray_org[DIM] = {0};
  float ray_dir[3] = {0};
  float ray_range[2] = {0};
  ASSERT(scn && pos && dir && distance >= 0 && hit_from && hit);

  fX_set_dX(ray_org, pos);
  f3_set_d3(ray_dir, dir);
  ray_range[0] = 0;
  ray_range[1] = (float)distance;
  filter_data.XD(hit) = *hit_from;
  filter_data.epsilon = 1.e-4;
#if DIM == 2
  SXD(scene_view_trace_ray_3d
    (scn->sXd(view), ray_org, ray_dir, ray_range, &filter_data, hit));
#else
  SXD(scene_view_trace_ray
    (scn->sXd(view), ray_org, ray_dir, ray_range, &filter_data, hit));
#endif
}

static INLINE double /* [W/m^2/sr] */
XD(direct_contribution)
  (const struct sdis_scene* scn,
   struct source_sample* sample,
   const double pos[DIM],
   const struct sXd(hit)* hit_from)
{
  struct sXd(hit) hit = SXD_HIT_NULL;
  ASSERT(scn && sample && pos && hit_from);

  /* Is the source hidden */
  XD(trace_ray)(scn, pos, sample->dir, sample->dst, hit_from, &hit);
  if(!SXD_HIT_NONE(&hit)) return 0; /* [W/m^2/sr] */

  return sample->radiance; /* [W/m^2/sr] */
}

static INLINE void
XD(setup_fragment)
  (struct sdis_interface_fragment* frag,
   const double pos[DIM],
   const double dir[DIM], /* Direction _toward_ the hit position */
   const double time, /* Current time */
   const double N[DIM],/* Surface normal */
   const struct sXd(hit)* hit)
{
  struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;
  enum sdis_side side = SDIS_SIDE_NULL__;
  ASSERT(frag && pos && dir && N);
  ASSERT(dX(is_normalized)(N));

  /* Setup the interface fragment at the intersection position */
  dX(set)(vtx.P, pos);
  vtx.time = time;
  side = dX(dot)(dir, N) < 0 ? SDIS_FRONT : SDIS_BACK;
  XD(setup_interface_fragment)(frag, &vtx, hit, side);
}

static INLINE res_T
XD(setup_brdf)
  (struct sdis_device* dev,
   struct brdf* brdf,
   const struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag)
{
  double epsilon = 0;
  double alpha = 0;
  res_T res = RES_OK;
  ASSERT(brdf && frag);
  ASSERT((frag->side == SDIS_FRONT
      && sdis_medium_get_type(interf->medium_front) == SDIS_FLUID)
      || sdis_medium_get_type(interf->medium_back) == SDIS_FLUID);

  epsilon = interface_side_get_emissivity(interf, frag);
  res = interface_side_check_emissivity(dev, epsilon, frag->P, frag->time);
  if(res != RES_OK) goto error;

  alpha = interface_side_get_specular_fraction(interf, frag);
  res = interface_side_check_specular_fraction(dev, alpha, frag->P, frag->time);
  if(res != RES_OK) goto error;

  brdf->emissivity = epsilon;
  brdf->specular_fraction = alpha;

exit:
  return res;
error:
  *brdf = BRDF_NULL;
  goto exit;
}

static res_T
XD(compute_incident_diffuse_flux)
  (const struct sdis_scene* scn,
   struct ssp_rng* rng,
   const double in_pos[DIM], /* position */
   const double in_N[DIM], /* Surface normal. (Away from the surface) */
   const double time,
   const struct sXd(hit)* in_hit, /* Current intersection */
   double* out_flux) /* [W/m^2] */
{
  struct sXd(hit) hit = SXD_HIT_NULL;
  double pos[3] = {0}; /* In 3D for ray tracing ray to the source */
  double dir[3] = {0}; /* Incident direction (toward the surface). Always 3D.*/
  double N[3] = {0}; /* Surface normal. Always 3D */
  double incident_diffuse_flux = 0; /* [W/m^2] */
  res_T res = RES_OK;
  ASSERT(in_pos && in_N && in_hit);

  /* Local copy of input argument */
  dX(set)(pos, in_pos);
  dX(set)(N, in_N);
  hit = *in_hit;

  /* Sample a diffusive direction in 3D */
  ssp_ran_hemisphere_cos(rng, N, dir, NULL);

  for(;;) {
    /* External sources */
    struct source_sample src_sample = SOURCE_SAMPLE_NULL;

    /* Interface */
    struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
    struct sdis_interface* interf = NULL;

    /* BRDF */
    struct brdf brdf = BRDF_NULL;
    struct brdf_sample brdf_sample = BRDF_SAMPLE_NULL;

    /* Miscellaneous */
    double L = 0; /* incident direct flux to bounce position */
    double wi[3] = {0}; /* Incident direction (outward the surface). Always 3D */
    double vec[DIM] = {0}; /* Temporary variable */

    dX(minus)(wi, dir);

    /* Find the following surface along the direction of propagation */
    XD(trace_ray)(scn, pos, dir, INF, &hit, &hit);
    if(SXD_HIT_NONE(&hit)) break; /* No surface */

    /* Retrieve the current position and normal */
    dX(add)(pos, pos, dX(muld)(vec, dir, hit.distance));
    dX_set_fX(N, hit.normal);
    dX(normalize(N, N));

    /* Retrieve the current interface properties */
    interf = scene_get_interface(scn, hit.prim.prim_id);
    XD(setup_fragment)(&frag, pos, dir, time, N, &hit);

    /* Check that the path reaches a valid interface */
    res = check_interface(interf, &frag);
    if(res != RES_OK) goto error;

    XD(setup_brdf)(scn->dev, &brdf, interf, &frag);

    /* Check if path is absorbed */
    if(ssp_rng_canonical(rng) < brdf.emissivity) break;

    /* Sample rebound direction */
    if(frag.side == SDIS_BACK) dX(minus)(N, N); /* Revert normal if necessary */
    sample_brdf(&brdf, rng, wi, N, &brdf_sample);
    dX(set)(dir, brdf_sample.dir);

    /* Calculate the direct contribution if the rebound is specular */
    if(brdf_sample.cpnt == BRDF_SPECULAR) {
      res = source_trace_to(scn->source, pos, brdf_sample.dir, time, &src_sample);
      if(res != RES_OK) goto error;

      if(!SOURCE_SAMPLE_NONE(&src_sample)) {
        const double Ld = XD(direct_contribution)(scn, &src_sample, pos, &hit);
        L = Ld; /* [W/m^2] */
      }

    /* Calculate the direct contribution of the rebound is diffuse */
    } else {
      double cos_theta = 0;
      ASSERT(brdf_sample.cpnt == BRDF_DIFFUSE);

      /* Sample an external source to handle its direct contribution at the
       * bounce position */
      res = source_sample(scn->source, rng, pos, time, &src_sample);
      CHK(res == RES_OK);
      cos_theta = d3_dot(src_sample.dir, N);

      /* The source is behind the surface */
      if(cos_theta <= 0) {
        L = 0; /* [W/m^2] */

      /* The source is above the surface */
      } else {
        const double Ld = XD(direct_contribution)(scn, &src_sample, pos, &hit);
        L = Ld * cos_theta / (PI * src_sample.pdf); /* [W/m^2] */
      }
    }
    incident_diffuse_flux += L;
  }

  incident_diffuse_flux *= PI;

exit:
  *out_flux = incident_diffuse_flux;
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(handle_external_net_flux)
  (const struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct XD(handle_external_net_flux_args)* args,
   struct XD(temperature)* T)
{
  /* Sampling external sources */
  struct source_sample src_sample = SOURCE_SAMPLE_NULL;

  /* External flux */
  double incident_flux = 0; /* [W/m^2] */
  double incident_flux_diffuse = 0; /* [W/m^2] */
  double incident_flux_direct = 0; /* [W/m^2] */
  double net_flux = 0; /* [W/m^2] */

  /* Sampled path */
  double N[3] = {0}; /* Normal. Always in 3D */

  /* On the fluid side */
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;

  /* Miscellaneous */
  double emissivity = 0; /* Emissivity */
  double Ld = 0; /* Incident radiance [W/m^2/sr] */
  double cos_theta = 0;
  int handle_flux = 0;
  res_T res = RES_OK;
  ASSERT(scn && args && T);

  res = XD(check_handle_external_net_flux_args)(scn, FUNC_NAME, args);
  if(res != RES_OK) goto error;

  /* Setup the interface fragment on flud side */
  frag = *args->frag;
  if(sdis_medium_get_type(args->interf->medium_front) == SDIS_FLUID) {
    frag.side = SDIS_FRONT;
  } else {
    ASSERT(sdis_medium_get_type(args->interf->medium_back) == SDIS_FLUID);
    frag.side = SDIS_BACK;
  }

  /* No external sources <=> no external fluxes. Nothing to do */
  handle_flux = interface_side_is_external_flux_handled(args->interf, &frag);
  handle_flux = net_flux && (scn->source != NULL);
  if(handle_flux) goto exit;

  /* Sample the external source */
  res = source_sample
    (scn->source, rng, frag.P, frag.time, &src_sample);
  if(res != RES_OK) goto error;

  /* Setup the normal to ensure that it points toward the fluid medium */
  dX(set)(N, frag.Ng);
  if(frag.side == SDIS_BACK) dX(minus)(N, N);

  /* Calculate the incident direct flux if the external source is above the
   * interface side */
  cos_theta = d3_dot(N, src_sample.dir);
  if(cos_theta > 0) {
    Ld = XD(direct_contribution)(scn, &src_sample, frag.P, args->hit);
    incident_flux_direct = cos_theta * Ld / src_sample.pdf; /* [W/m^2] */
  }

  /* Calculate the incident diffuse flux [W/m^2] */
  res = XD(compute_incident_diffuse_flux)
    (scn, rng, frag.P, N, frag.time, args->hit, &incident_flux_diffuse);
  if(res != RES_OK) goto error;

  /* Calculate the global incident flux */
  incident_flux = incident_flux_direct + incident_flux_diffuse; /* [W/m^2] */

  /* Calculate the net flux */
  emissivity = interface_side_get_emissivity(args->interf, &frag);
  res = interface_side_check_emissivity(scn->dev, emissivity, frag.P, frag.time);
  if(res != RES_OK) goto error;
  net_flux = incident_flux * emissivity; /* [W/m^2] */

  /* Update the Monte Carlo weight */
  T->value += net_flux / (args->h_radi + args->h_conv + args->h_cond);

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
