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

#ifndef SDIS_SOURCE_C_H
#define SDIS_SOURCE_C_H

#include <rsys/hash.h>
#include <rsys/rsys.h>

struct sdis_source;
struct ssp_rng;

struct source_props {
  double pos[3]; /* [m/fp_to_meter] */
  double radius; /* [m/fp_to_meter] */
  double power; /* [W] */
  double area; /* [m^2/fp_to_meter] */
  double time; /* [s] */
};
#define SOURCE_PROPS_NULL__ {0}
static const struct source_props SOURCE_PROPS_NULL = SOURCE_PROPS_NULL__;

struct source_sample {
  double dir[3]; /* Direction _to_ the source */
  double pdf; /* pdf of sampled direction */
  double dst; /* Distance to the source [m] */

  double radiance; /* [W/m^2/sr] */

  /* Radiance relative to power, i.e. the source power is assumed to be equal to
   * 1. It must be multiplied by the source power to obtain the actual radiance
   * of the source. In other words, this variable defines the contribution of
   * the source independently of its power, and can therefore be recorded in the
   * green function */
  double radiance_term; /* [W/m^2/sr] */
};
#define SOURCE_SAMPLE_NULL__ {0}
static const struct source_sample SOURCE_SAMPLE_NULL = SOURCE_SAMPLE_NULL__;

/* Helper macro used to define whether a sample is valid or not */
#define SOURCE_SAMPLE_NONE(Sample) ((Sample)->pdf == 0)

extern LOCAL_SYM res_T
source_get_props
  (const struct sdis_source* source,
   const double time, /* Time at which props are retrieved [s] */
   struct source_props* props);

extern LOCAL_SYM res_T
source_sample
  (const struct sdis_source* source,
   const struct source_props* props,
   struct ssp_rng* rng,
   const double pos[3], /* Position from which the source is sampled */
   struct source_sample* sample);

/* Trace a ray toward the source. The returned sample has a pdf of 1 or 0
 * whether the source is intersected by the ray or not, respectively. You can
 * use the SOURCE_SAMPLE_NONE macro to check this */
extern LOCAL_SYM res_T
source_trace_to
  (const struct sdis_source* source,
   const struct source_props* props,
   const double pos[3], /* Ray origin */
   const double dir[3], /* Ray direction */
   struct source_sample* sample); /* pdf == 0 if no source is reached */

extern LOCAL_SYM double /* [W] */
source_get_power
  (const struct sdis_source* source,
   const double time); /* [s] */

extern LOCAL_SYM double /* [W/m^2/sr] */
source_get_diffuse_radiance
  (const struct sdis_source* source,
   const double time, /* [s] */
   const double dir[3]);

extern LOCAL_SYM void
source_compute_signature
  (const struct sdis_source* source,
   hash256_T hash);

#endif /* SDIS_SOURCE_C_H */
