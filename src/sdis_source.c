/* Copyright (C) 2016-2024 |Méso|Star> (contact@meso-star.com)
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
#include "sdis_device_c.h"
#include "sdis_log.h"
#include "sdis_source_c.h"

#include <rsys/mem_allocator.h>
#include <rsys/ref_count.h>

#include <star/ssp.h>

struct sdis_source {
  struct sdis_spherical_source_create_args spherical;

  struct sdis_device* dev;
  ref_T ref;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
check_spherical_source_create_args
  (struct sdis_device* dev,
   const char* func_name,
   struct sdis_spherical_source_create_args* args)
{
  ASSERT(func_name);
  if(!args) return RES_BAD_ARG;

  if(!args->position) {
    log_err(dev, "%s: the position functor is missing.\n", func_name);
    return RES_BAD_ARG;
  }

  if(!args->power) {
    log_err(dev, "%s: the power functor is missing.\n", func_name);
    return RES_BAD_ARG;
  }

  if(args->radius < 0) {
    log_err(dev, "%s: invalid source radius '%g' m. It cannot be negative.\n",
      func_name, args->radius);
    return RES_BAD_ARG;
  }

  return RES_OK;
}

static void
release_source(ref_T* ref)
{
  struct sdis_device* dev = NULL;
  struct sdis_source* src = CONTAINER_OF(ref, struct sdis_source, ref);
  ASSERT(ref);
  dev = src->dev;
  if(src->spherical.data) SDIS(data_ref_put(src->spherical.data));
  MEM_RM(dev->allocator, src);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
res_T
sdis_spherical_source_create
  (struct sdis_device* dev,
   struct sdis_spherical_source_create_args* args,
   struct sdis_source** out_src)
{
  struct sdis_source* src = NULL;
  res_T res = RES_OK;

  if(!dev || !out_src) { res = RES_BAD_ARG; goto error; }
  res = check_spherical_source_create_args(dev, FUNC_NAME, args);
  if(res != RES_OK) goto error;

  src = MEM_CALLOC(dev->allocator, 1, sizeof(*src));
  if(!src) {
    log_err(dev, "%s: cannot allocate spherical source.\n", FUNC_NAME);
    res = RES_OK;
    goto error;
  }
  ref_init(&src->ref);
  SDIS(device_ref_get(dev));
  if(args->data) SDIS(data_ref_get(args->data));
  src->spherical = *args;
  src->dev = dev;

exit:
  if(out_src) *out_src = src;
  return res;
error:
  if(src) { SDIS(source_ref_put(src)); src = NULL; }
  goto exit;
}

res_T
sdis_source_ref_get(struct sdis_source* src)
{
  if(!src) return RES_BAD_ARG;
  ref_get(&src->ref);
  return RES_OK;
}

res_T
sdis_source_ref_put(struct sdis_source* src)
{
  if(!src) return RES_BAD_ARG;
  ref_put(&src->ref, release_source);
  return RES_OK;
}

double
sdis_source_get_power(struct sdis_source* src, const double time /* [s] */)
{
  ASSERT(src);
  return source_get_power(src, time);
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
source_sample
  (const struct sdis_source* src,
   struct ssp_rng* rng,
   const double pos[3],
   const double time,
   struct source_sample* sample)
{
  double src_pos[3]; /* [m] */
  double main_dir[3];
  double half_angle; /* [radians] */
  double cos_half_angle; /* [radians] */
  double dst; /* [m] */
  double radius; /* Source radius [m] */
  double power; /* Source power [W] */
  double area; /* Source area [m^2] */
  res_T res = RES_OK;
  ASSERT(src && rng && pos && sample);

  /* Retrieve current source position, radius and power */
  src->spherical.position(time, src_pos, src->spherical.data);
  power = src->spherical.power(time, src->spherical.data);
  radius = src->spherical.radius;

  if(power < 0) {
    log_err(src->dev, "%s: invalid source power '%g' W. It cannot be negative.\n",
      FUNC_NAME, power);
    res = RES_BAD_ARG;
    goto error;
  }

  area = 4*PI*radius*radius; /* [m^2] */

  /* compute the direction of `pos' toward the center of the source */
  d3_sub(main_dir, src_pos, pos);

  /* Normalize the direction and keep the distance from `pos' to the center of
   * the source */
  dst = d3_normalize(main_dir, main_dir);
  if(dst <= radius) {
    log_err(src->dev,
      "%s: the position from which the external source is sampled "
      "is included in the source:\n"
      "\tsource position = %g, %g, %g\n"
      "\tsource radius = %g\n"
      "\tposition = %g, %g, %g\n"
      "\ttime = %g\n"
      "\tdistance from position to source = %g\n",
      FUNC_NAME, SPLIT3(src_pos), radius, SPLIT3(pos), time, dst);
    res = RES_BAD_ARG;
    goto error;
  }

  /* Point source */
  if(area == 0) {
    d3_set(sample->dir, main_dir);
    sample->pdf = 1;
    sample->dst = dst;
    sample->power = power; /* [W] */
    sample->radiance_term = 1.0 / (4*PI*dst*dst); /* [W/m^2/sr] */
    sample->radiance = sample->power * sample->radiance_term; /* [W/m^2/sr] */

  /* Spherical source */
  } else {
    /* Sample the source according to its solid angle,
     * i.e. 2*PI*(1 - cos(half_angle)) */
    half_angle = asin(radius/dst);
    cos_half_angle = cos(half_angle);
    ssp_ran_sphere_cap_uniform /* pdf = 1/(2*PI*(1-cos(half_angle))) */
      (rng, main_dir, cos_half_angle, sample->dir, &sample->pdf);

    /* Set other sample variables */
    sample->dst = dst - radius; /* From pos to source boundaries [m] */
    sample->power = power; /* [W] */
    sample->radiance_term = 1.0 / (PI*area); /* [W/m^2/sr] */
    sample->radiance = sample->power * sample->radiance_term; /* [W/m^2/sr] */
  }

exit:
  return res;
error:
  goto exit;
}

res_T
source_trace_to
  (const struct sdis_source* src,
   const double pos[3], /* Ray origin */
   const double dir[3], /* Ray direction */
   const double time, /* Time at which ray is traced */
   struct source_sample* sample)
{
  double src_pos[3]; /* [m] */
  double main_dir[3];
  double radius; /* [m] */
  double power; /* [W] */
  double dst; /* Distance from pos to the source center [m] */
  double half_angle; /* [radian] */
  res_T res = RES_OK;
  ASSERT(src && pos && dir && sample);
  ASSERT(d3_is_normalized(dir));

  radius = src->spherical.radius;

  /* Point sources cannot be targeted */
  if(radius == 0) {
    *sample = SOURCE_SAMPLE_NULL;
    goto exit;
  }

  /* Retrieve current source position and power */
  src->spherical.position(time, src_pos, src->spherical.data);
  power = src->spherical.power(time, src->spherical.data);

  if(power < 0) {
    log_err(src->dev, "%s: invalid source power '%g' W. It cannot be negative.\n",
      FUNC_NAME, power);
    res = RES_BAD_ARG;
    goto error;
  }

  /* compute the direction of `pos' toward the center of the source */
  d3_sub(main_dir, src_pos, pos);

  /* Normalize the direction and keep the distance from `pos' to the center of
   * the source */
  dst = d3_normalize(main_dir, main_dir);
  if(dst <= radius) {
    log_err(src->dev,
      "%s: the position from which the external source is targeted "
      "is included in the source:\n"
      "\tsource position = %g, %g, %g\n"
      "\tsource radius = %g\n"
      "\tposition = %g, %g, %g\n"
      "\ttime = %g\n"
      "\tdistance from position to source = %g\n",
      FUNC_NAME, SPLIT3(src_pos), radius, SPLIT3(pos), time, dst);
    res = RES_BAD_ARG;
    goto error;
  }

  /* Compute the half angle of the source as seen from pos */
  half_angle = asin(radius/dst);

  /* The source is missed */
  if(d3_dot(dir, main_dir) < cos(half_angle)) {
    *sample = SOURCE_SAMPLE_NULL;

  /* The source is intersected */
  } else {
    const double area = 4*PI*radius*radius; /* [m^2] */

    d3_set(sample->dir, dir);
    sample->pdf = 1;
    sample->dst = dst - radius; /* From pos to source boundaries [m] */
    sample->power = power; /* [W] */
    sample->radiance_term = 1.0 / (PI*area); /* [W/m^2/sr] */
    sample->radiance = sample->power * sample->radiance_term; /* [W/m^2/sr] */
  }

exit:
  return res;
error:
  *sample = SOURCE_SAMPLE_NULL;
  goto exit;
}

double /* [W] */
source_get_power(const struct sdis_source* src, const double time /* [s] */)
{
  ASSERT(src);
  return src->spherical.power(time, src->spherical.data);
}

void
source_compute_signature(const struct sdis_source* src, hash256_T hash)
{
  struct sha256_ctx ctx;
  ASSERT(src && hash);

  /* Calculate the source signature. Currently, it is only the source radius.
   * But the Source API is designed to be independent of source type. In the
   * future, the source will not necessarily be spherical, so the data to be
   * hashed will depend on the type of source. This function anticipate this by
   * calculating a hash even if it is currently dispensable. */
  sha256_ctx_init(&ctx);
  sha256_ctx_update
    (&ctx, (const char*)&src->spherical.radius, sizeof(src->spherical.radius));
  sha256_ctx_finalize(&ctx, hash);
}
