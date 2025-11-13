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

#include "sdis.h"
#include "sdis_brdf.h"
#include "sdis_interface_c.h"

#include <star/ssp.h>
#include <rsys/double3.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE res_T
check_brdf_setup_args(const struct brdf_setup_args* args)
{
  const struct sdis_medium* mdm = NULL;

  if(!args) return RES_BAD_ARG;

  if(args->interf == NULL || args->frag == NULL)
    return RES_BAD_ARG;

  switch(args->frag->side) {
    case SDIS_FRONT: mdm = args->interf->medium_front; break;
    case SDIS_BACK: mdm = args->interf->medium_back; break;
    default: FATAL("Unreachable code\n"); break;
  }

  if(sdis_medium_get_type(mdm) != SDIS_FLUID)
    return RES_BAD_ARG;

  return RES_OK;
}

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

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
brdf_setup
  (struct sdis_device* dev,
   const struct brdf_setup_args* args,
   struct brdf* brdf)
{
  res_T res = RES_OK;

  ASSERT(check_brdf_setup_args(args) == RES_OK);

  #define GET(Attr) {                                                          \
    brdf->Attr = interface_side_get_##Attr                                     \
      (args->interf, args->source_id, args->frag);                             \
                                                                               \
    res = interface_side_check_##Attr                                          \
      (dev, brdf->Attr, args->frag->P, args->frag->time);                      \
    if(res != RES_OK) goto error;                                              \
  } (void)0

  GET(emissivity);
  GET(specular_fraction);

  #undef GET

exit:
  return res;
error:
  *brdf = BRDF_NULL;
  goto exit;
}

void
brdf_sample
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
