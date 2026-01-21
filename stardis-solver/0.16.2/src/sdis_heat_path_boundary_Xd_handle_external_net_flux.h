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

#include "sdis_brdf.h"
#include "sdis_heat_path_boundary_c.h"
#include "sdis_interface_c.h"
#include "sdis_log.h"
#include "sdis_scene_c.h"
#include "sdis_source_c.h"

#include <rsys/cstr.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Non generic data types and function
 ******************************************************************************/
#ifndef SDIS_HEAT_PATH_BOUNDARY_XD_HANDLE_EXTERNAL_NET_FLUX_H
#define SDIS_HEAT_PATH_BOUNDARY_XD_HANDLE_EXTERNAL_NET_FLUX_H

/* Incident diffuse flux is made up of two components. One corresponds to the
 * diffuse flux due to the reflection of the source on surfaces. The other is
 * the diffuse flux due to the source's radiation scattering at least once in
 * the environment. */
struct incident_diffuse_flux {
  double reflected; /* [W/m^2] */
  double scattered; /* [W/m^2] */
  double dir[3]; /* Direction along wich the scattered part was retrieved */
};
#define INCIDENT_DIFFUSE_FLUX_NULL__ {0, 0, {0,0,0}}
static const struct incident_diffuse_flux INCIDENT_DIFFUSE_FLUX_NULL =
  INCIDENT_DIFFUSE_FLUX_NULL__;

#endif /* SDIS_HEAT_PATH_BOUNDARY_XD_HANDLE_EXTERNAL_NET_FLUX_H */

/*******************************************************************************
 * Generic helper functions
 ******************************************************************************/
static INLINE res_T
XD(check_handle_external_net_flux_args)
  (const struct sdis_scene* scn,
   const char* func_name,
   const struct handle_external_net_flux_args* args)
{
  int net_flux = 0;
  res_T res = RES_OK;

  /* Handle bugs */
  ASSERT(scn && func_name && args);
  ASSERT(args->interf && args->frag);
  ASSERT(!SXD_HIT_NONE(args->XD(hit)));
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

static INLINE double /* [W/m^2/sr] */
XD(direct_contribution)
  (struct sdis_scene* scn,
   struct source_sample* sample,
   const double pos[DIM],
   const unsigned enc_id, /* Current enclosure */
   const struct sXd(hit)* hit_from)
{
  struct sXd(hit) hit = SXD_HIT_NULL;
  ASSERT(scn && sample && pos && hit_from);

  /* Is the source hidden */
  XD(trace_ray)(scn, pos, sample->dir, sample->dst, enc_id, hit_from, &hit);
  if(!SXD_HIT_NONE(&hit)) return 0; /* [W/m^2/sr] */

  /* Note that the value returned is not the source's actual radiance, but the
   * radiance relative to the source's power. Care must therefore be taken to
   * multiply it by the power of the source to obtain its real contribution.
   * This trick makes it possible to manage the external flux in the green
   * function. */
  return sample->radiance_term; /* [W/m^2/sr] */
}

static res_T
XD(compute_incident_diffuse_flux)
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct source_props* props,
   const double in_pos[DIM], /* position */
   const double in_N[DIM], /* Surface normal. (Away from the surface) */
   const double time,
   const unsigned enc_id, /* Current enclosure */
   const struct sXd(hit)* in_hit, /* Current intersection */
   struct incident_diffuse_flux* diffuse_flux) /* [W/m^2] */
{
  struct sXd(hit) hit = SXD_HIT_NULL;
  double pos[3] = {0}; /* In 3D for ray tracing ray to the source */
  double dir[3] = {0}; /* Incident direction (toward the surface). Always 3D.*/
  double N[3] = {0}; /* Surface normal. Always 3D */
  size_t nbounces = 0; /* For debug */
  res_T res = RES_OK;
  ASSERT(props && in_pos && in_N && in_hit && diffuse_flux);

  /* Local copy of input argument */
  dX(set)(pos, in_pos);
  dX(set)(N, in_N);
  hit = *in_hit;

  /* Sample a diffusive direction in 3D */
  ssp_ran_hemisphere_cos(rng, N, dir, NULL);

  *diffuse_flux = INCIDENT_DIFFUSE_FLUX_NULL;

  for(;;) {
    /* External sources */
    struct source_sample samp = SOURCE_SAMPLE_NULL;

    /* Interface */
    struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
    struct sdis_interface* interf = NULL;

    /* BRDF */
    struct brdf brdf = BRDF_NULL;
    struct brdf_sample bounce = BRDF_SAMPLE_NULL;
    struct brdf_setup_args brdf_setup_args = BRDF_SETUP_ARGS_NULL;

    /* Miscellaneous */
    double L = 0; /* incident flux to bounce position */
    double wi[3] = {0}; /* Incident direction (outward the surface). Always 3D */

    d3_minus(wi, dir); /* Always in 3D */

    res = XD(find_next_fragment)
      (scn, pos, dir, &hit, time, enc_id, &hit, &interf, &frag);
    if(res != RES_OK) goto error;

    if(SXD_HIT_NONE(&hit)) {
      /* No surface. Handle the radiance emitted by the source and scattered at
       * least once in the environment. Note that the value returned is not the
       * actual scattered component of the incident diffuse flux: it relates
       * to the radiance of the source scattered along the input dir at the
       * given instant. It must therefore be multiplied by this radiance to
       * obtain its real contribution. This trick makes it possible to manage
       * the external flux in the green function. */
      diffuse_flux->scattered = PI;
      diffuse_flux->dir[0] = dir[0];
      diffuse_flux->dir[1] = dir[1];
      diffuse_flux->dir[2] = dir[2];
      break;
    }

    d3_set(pos, frag.P);

    /* Retrieve BRDF at current interface position */
    brdf_setup_args.interf = interf;
    brdf_setup_args.frag = &frag;
    brdf_setup_args.source_id = sdis_source_get_id(scn->source);
    res = brdf_setup(scn->dev, &brdf_setup_args, &brdf);
    if(res != RES_OK) goto error;

    /* Check if path is absorbed */
    if(ssp_rng_canonical(rng) < brdf.emissivity) break;

    /* Sample rebound direction */
    switch(frag.side) {
      case SDIS_FRONT: dX(set)(N, frag.Ng); break;
      case SDIS_BACK:  dX(minus)(N, frag.Ng); break;
      default: FATAL("Unreachable code\n");
    }
    brdf_sample(&brdf, rng, wi, N, &bounce);
    d3_set(dir, bounce.dir); /* Always in 3D */

    /* Calculate the direct contribution if the rebound is specular */
    if(bounce.cpnt == BRDF_SPECULAR) {
      res = source_trace_to(scn->source, props, pos, bounce.dir, &samp);
      if(res != RES_OK) goto error;

      if(!SOURCE_SAMPLE_NONE(&samp)) {
        double Ld = XD(direct_contribution)(scn, &samp, pos, enc_id, &hit);
        L = Ld; /* [W/m^2/sr] */
      }

    /* Calculate the direct contribution of the rebound is diffuse */
    } else {
      double cos_theta = 0;
      ASSERT(bounce.cpnt == BRDF_DIFFUSE);

      /* Sample an external source to handle its direct contribution at the
       * bounce position */
      res = source_sample(scn->source, props, rng, pos, &samp);
      CHK(res == RES_OK);
      cos_theta = d3_dot(samp.dir, N);

      /* The source is behind the surface */
      if(cos_theta <= 0) {
        L = 0; /* [W/m^2/sr] */

      /* The source is above the surface */
      } else {
        double Ld = XD(direct_contribution)(scn, &samp, pos, enc_id, &hit);
        L = Ld * cos_theta / (PI * samp.pdf); /* [W/m^2/sr] */
      }
    }
    diffuse_flux->reflected += L; /* [W/m^2/sr] */
    ++nbounces;
  }
  diffuse_flux->reflected *= PI; /* [W/m^2] */

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(handle_external_net_flux)
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct handle_external_net_flux_args* args,
   struct temperature* T)
{
  /* Terms to be registered in the green function */
  struct sdis_green_external_flux_terms green =
    SDIS_GREEN_EXTERNAL_FLUX_TERMS_NULL;

  /* External source */
  struct source_props src_props = SOURCE_PROPS_NULL;
  struct source_sample src_sample = SOURCE_SAMPLE_NULL;

  /* External flux */
  struct incident_diffuse_flux incident_flux_diffuse = INCIDENT_DIFFUSE_FLUX_NULL;
  double incident_flux = 0; /* [W/m^2] */
  double incident_flux_direct = 0; /* [W/m^2] */
  double net_flux = 0; /* [W/m^2] */
  double net_flux_sc = 0; /* [W/m^2] */

  /* Sampled path */
  double N[3] = {0}; /* Normal. Always in 3D */

  /* On the fluid side */
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;

  /* Miscellaneous */
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
  double sum_h = 0;
  double emissivity = 0; /* Emissivity */
  double Ld = 0; /* Incident radiance [W/m^2/sr] */
  double cos_theta = 0;
  unsigned src_id = 0;
  int handle_flux = 0;
  res_T res = RES_OK;
  ASSERT(scn && args && T);

  res = XD(check_handle_external_net_flux_args)(scn, FUNC_NAME, args);
  if(res != RES_OK) goto error;

  /* Setup the interface fragment on fluid side */
  frag = *args->frag;
  if(sdis_medium_get_type(args->interf->medium_front) == SDIS_FLUID) {
    frag.side = SDIS_FRONT;
  } else {
    ASSERT(sdis_medium_get_type(args->interf->medium_back) == SDIS_FLUID);
    frag.side = SDIS_BACK;
  }

  /* Retrieve the enclosures */
  scene_get_enclosure_ids(scn, args->XD(hit)->prim.prim_id, enc_ids);

  /* No external sources <=> no external fluxes. Nothing to do */
  handle_flux = interface_side_is_external_flux_handled(args->interf, &frag);
  handle_flux = handle_flux && (scn->source != NULL);
  if(!handle_flux) goto exit;

  /* Emissivity is null <=> external flux is null. Nothing to do */
  src_id = sdis_source_get_id(scn->source);
  emissivity = interface_side_get_emissivity(args->interf, src_id, &frag);
  res = interface_side_check_emissivity(scn->dev, emissivity, frag.P, frag.time);
  if(res != RES_OK) goto error;

  if(emissivity == 0) goto exit;

  res = source_get_props(scn->source, frag.time, &src_props);
  if(res != RES_OK) goto error;

  /* Sample a direction toward the source to add its direct contribution */
  res = source_sample(scn->source, &src_props, rng, frag.P, &src_sample);
  if(res != RES_OK) goto error;

  /* Setup the normal to ensure that it points toward the fluid medium */
  dX(set)(N, frag.Ng);
  if(frag.side == SDIS_BACK) dX(minus)(N, N);

  /* Calculate the incident direct flux if the external source is above the
   * interface side */
  cos_theta = d3_dot(N, src_sample.dir);
  if(cos_theta > 0) {
    Ld = XD(direct_contribution)
      (scn, &src_sample, frag.P, enc_ids[frag.side], args->XD(hit));
    incident_flux_direct = cos_theta * Ld / src_sample.pdf; /* [W/m^2] */
  }

  /* Calculate the incident diffuse flux [W/m^2] */
  res = XD(compute_incident_diffuse_flux)(scn, rng, &src_props, frag.P, N,
    frag.time, enc_ids[frag.side], args->XD(hit), &incident_flux_diffuse);
  if(res != RES_OK) goto error;

  /* Calculate the incident flux without the part scattered by the environment.
   * The latter depends on the source's diffuse radiance, not on its power. On
   * the other hand, both the direct incident flux and the diffuse incident flux
   * reflected by surfaces are linear with respect to the source power. This
   * term can therefore be recorded in the green function in relation to this
   * power, whereas the incident diffused flux coming from the scattered source
   * radiance depends on the diffuse radiance of the source */
  incident_flux = /* [W/m^2] */
    incident_flux_direct + incident_flux_diffuse.reflected;

  /* Calculate the net flux [W/m^2] */
  net_flux = incident_flux * emissivity; /* [W/m^2] */

  /* Calculate the net flux from the radiance source scattered at least once by
   * the medium */
  net_flux_sc = incident_flux_diffuse.scattered * emissivity; /* [W/m^2] */

  /* Until now, net flux has been calculated on the basis of source power and
   * source diffuse radiance. What is actually calculated are the external flux
   * terms of the green function. These must be multiplied by the source power
   * and the source diffuse radiance, then added together to give the actual
   * external flux */
  sum_h = (args->h_radi + args->h_conv + args->h_cond);
  green.term_wrt_power = net_flux / sum_h; /* [K/W] */
  green.term_wrt_diffuse_radiance = net_flux_sc / sum_h; /* [K/W/m^2/sr] */
  green.time = frag.time; /* [s] */
  green.dir[0] = incident_flux_diffuse.dir[0];
  green.dir[1] = incident_flux_diffuse.dir[1];
  green.dir[2] = incident_flux_diffuse.dir[2];

  T->value += green.term_wrt_power * src_props.power;
  if(green.term_wrt_diffuse_radiance) {
    T->value +=
        green.term_wrt_diffuse_radiance
      * source_get_diffuse_radiance(scn->source, green.time, green.dir);
  }

  /* Register the external net flux terms */
  if(args->green_path) {
    res = green_path_add_external_flux_terms(args->green_path, &green);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
