/* Copyright (C) 2015-2018, 2021, 2024 |Méso|Star> (contact@meso-star.com)
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

#include <rsys/float3.h>

#include <star/s3d.h>
#include <star/ssp.h>
#include <star/smc.h>

#include "s4vs_realization.h"

/*******************************************************************************
 * Helper function
 ******************************************************************************/
int
s4vs_discard_self_hit
  (const struct s3d_hit* hit,
   const float ray_org[3],
   const float ray_dir[3],
   const float ray_range[2],
   void* ray_data,
   void* filter_data)
{
  const struct s3d_primitive* prim_from = ray_data;

  /* Avoid unused variable warn */
  (void)ray_org, (void)ray_dir, (void)ray_range, (void)filter_data;
  return prim_from ? S3D_PRIMITIVE_EQ(prim_from, &hit->prim) : 0;
}

/*******************************************************************************
 * 4V/S integrand
 ******************************************************************************/
res_T
s4vs_realization
  (void* out_length,
   struct ssp_rng* rng,
   const unsigned ithread,
   const uint64_t irealisation,
   void* context)
{
  struct s4vs_context* ctx = (struct s4vs_context*)context;
  struct s3d_attrib attrib;
  struct s3d_primitive prim;
  double sample[3];
  double normal[3];
  float u[3], x[3], st[2];
  const float range[2] = {0.f, FLT_MAX};
  struct s3d_hit hit;
  double w = 0;
  double sigma = 0;
  int keep_running = 0;
  float r0, r1, r2;
  (void)ithread, (void)irealisation; /* Avoid "unused variable" warning */

  /* Sample a surface location, i.e. primitive ID and parametric coordinates */
  r0 = ssp_rng_canonical_float(rng);
  r1 = ssp_rng_canonical_float(rng);
  r2 = ssp_rng_canonical_float(rng);
  S3D(scene_view_sample(ctx->view, r0, r1, r2, &prim, st));

  /* retrieve the sampled geometric normal and position */
  S3D(primitive_get_attrib(&prim, S3D_GEOMETRY_NORMAL, st, &attrib));
  d3_normalize(normal, d3_set_f3(normal, attrib.value));
  S3D(primitive_get_attrib(&prim, S3D_POSITION, st, &attrib));
  f3_set(x, attrib.value);

  /* Cosine weighted sampling of the hemisphere around the sampled normal */
  ssp_ran_hemisphere_cos(rng, normal, sample, NULL);
  f3_set_d3(u, sample);

  /* Find the 1st hit from the sampled location along the sampled direction */
  S3D(scene_view_trace_ray(ctx->view, x, u, range, &prim, &hit));

  /* No intersection <=> numerical imprecision or geometry leakage */
  if(S3D_HIT_NONE(&hit)) return RES_UNKNOWN_ERR;

  keep_running = 1;
  while(keep_running) { /* Here we go for the diffuse random walk */

    /* Sample a length according to ks */
    sigma = ssp_ran_exp(rng, ctx->ks);

    if(sigma < hit.distance) {
      int i;
      FOR_EACH(i, 0, 3) x[i] = x[i] + (float)sigma*u[i];
      d3_normalize(sample, d3_set_f3(sample, u));
      f3_set_d3(u, ssp_ran_sphere_hg(rng, sample, ctx->g, sample, NULL));

      /* sample a new direction */
      S3D(scene_view_trace_ray(ctx->view, x, u, range, NULL, &hit));

      w = w + sigma;

      /* No intersection <=> numerical imprecision or geometry leakage */
      if(S3D_HIT_NONE(&hit)) return RES_UNKNOWN_ERR;

    } else { /* Stop the random walk */
      w = w + hit.distance;
      keep_running = 0;
    }
  }

  SMC_DOUBLE(out_length) = w;
  return RES_OK;
}
