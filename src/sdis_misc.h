/* Copyright (C) 2016-2020 |Meso|Star> (contact@meso-star.com)
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

#ifndef SDIS_MISC_H
#define SDIS_MISC_H

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <star/ssp.h>

struct accum {
  double sum; /* Sum of MC weights */
  double sum2; /* Sum of square MC weights */
  size_t count; /* #accumulated MC weights */
};
#define ACCUM_NULL__ {0,0,0}
static const struct accum ACCUM_NULL = ACCUM_NULL__;

/* Empirical scale factor to apply to the upper bound of the ray range in order
 * to handle numerical imprecisions */
#define RAY_RANGE_MAX_SCALE 1.001f

/* Define a new result code from RES_BAD_OP saying that the bad operation is
 * definitive, i.e. in the current state, the realisation will inevitably fail.
 * It is thus unecessary to retry a specific section of the random walk */
#define RES_BAD_OP_IRRECOVERABLE (-RES_BAD_OP)

#define BOLTZMANN_CONSTANT 5.6696e-8 /* W/m^2/K^4 */

static INLINE void
sum_accums
  (const struct accum accums[],
   const size_t naccums,
   struct accum* accum)
{
  struct accum acc = ACCUM_NULL;
  size_t i;
  ASSERT(accums && naccums && accum);

  FOR_EACH(i, 0, naccums) {
    acc.sum += accums[i].sum;
    acc.sum2 += accums[i].sum2;
    acc.count += accums[i].count;
  }
  *accum = acc;
}

/* Reflect the V wrt the normal N. By convention V points outward the surface */
static FINLINE float*
reflect_2d(float res[2], const float V[2], const float N[2])
{
  float tmp[2];
  float cos_V_N;
  ASSERT(res && V && N);
  ASSERT(f2_is_normalized(V) && f2_is_normalized(N));
  cos_V_N = f2_dot(V, N);
  f2_mulf(tmp, N, 2*cos_V_N);
  f2_sub(res, tmp, V);
  return res;
}

/* Reflect the V wrt the normal N. By convention V points outward the surface */
static FINLINE float*
reflect_3d(float res[3], const float V[3], const float N[3])
{
  float tmp[3];
  float cos_V_N;
  ASSERT(res && V && N);
  ASSERT(f3_is_normalized(V) && f3_is_normalized(N));
  cos_V_N = f3_dot(V, N);
  f3_mulf(tmp, N, 2*cos_V_N);
  f3_sub(res, tmp, V);
  return res;
}

static FINLINE double*
move_pos_2d(double pos[2], const float dir[2], const float delta)
{
  ASSERT(pos && dir);
  pos[0] += dir[0] * delta;
  pos[1] += dir[1] * delta;
  return pos;
}

static FINLINE double*
move_pos_3d(double pos[3], const float dir[3], const float delta)
{
  ASSERT(pos && dir);
  pos[0] += dir[0] * delta;
  pos[1] += dir[1] * delta;
  pos[2] += dir[2] * delta;
  return pos;
}

static INLINE double
sample_time(struct ssp_rng* rng, const double time_range[2])
{
  ASSERT(time_range && time_range[0] >= 0 && time_range[1] >= time_range[0]);
  ASSERT(rng);
  if(time_range[0] == time_range[1]) return time_range[0];
  return ssp_rng_uniform_double(rng, time_range[0], time_range[1]);
}

static INLINE res_T
register_heat_vertex
  (struct sdis_heat_path* path,
   const struct sdis_rwalk_vertex* vtx,
   const double weight,
   const enum sdis_heat_vertex_type type)
{
  struct sdis_heat_vertex heat_vtx = SDIS_HEAT_VERTEX_NULL;
  ASSERT(vtx);

  if(!path) return RES_OK;

  heat_vtx.P[0] = vtx->P[0];
  heat_vtx.P[1] = vtx->P[1];
  heat_vtx.P[2] = vtx->P[2];
  heat_vtx.time = vtx->time;
  heat_vtx.weight = weight;
  heat_vtx.type = type;
  return heat_path_add_vertex(path, &heat_vtx);
}

extern LOCAL_SYM res_T
time_rewind_2d
  (struct sdis_medium* mdm, /* Medium into which the time is rewinded */
   struct ssp_rng* rng,
   const double dist_in_meter,
   const struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct temperature_2d* T);

extern LOCAL_SYM res_T
time_rewind_3d
  (struct sdis_medium* mdm, /* Medium into which the time is rewinded */
   struct ssp_rng* rng,
   const double dist_in_meter,
   const struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct temperature_3d* T);

#endif /* SDIS_MISC_H */
