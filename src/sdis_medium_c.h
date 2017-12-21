/* Copyright (C) |Meso|Star> 2016-2017 (contact@meso-star.com)
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

#ifndef SDIS_MEDIUM_C_H
#define SDIS_MEDIUM_C_H

#include "sdis.h"

struct sdis_medium {
  enum sdis_medium_type type;
  union {
    struct sdis_solid_shader solid;
    struct sdis_fluid_shader fluid;
  } shader;

  struct sdis_data* data;

  ref_T ref;
  struct sdis_device* dev;
};

/*******************************************************************************
 * Fluid local functions
 ******************************************************************************/
static INLINE double
fluid_get_calorific_capacity
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  double cp = -1;
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_FLUID);
  mdm->shader.fluid.calorific_capacity(mdm->dev, mdm->data, vtx, &cp);
  return cp;
}

static INLINE double
fluid_get_volumic_mass
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  double rho = -1;
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_FLUID);
  mdm->shader.fluid.volumic_mass(mdm->dev, mdm->data, vtx, &rho);
  return rho;
}

static INLINE double
fluid_get_temperature
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  double T = -1;
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_FLUID);
  mdm->shader.fluid.temperature(mdm->dev, mdm->data, vtx, &T);
  return T;
}

/*******************************************************************************
 * Solid local functions
 ******************************************************************************/
static INLINE double
solid_get_calorific_capacity
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  double cp = -1;
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  mdm->shader.solid.calorific_capacity(mdm->dev, mdm->data, vtx, &cp);
  return cp;
}

static INLINE double
solid_get_thermal_conductivity
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  double lambda = -1;
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  mdm->shader.solid.thermal_conductivity(mdm->dev, mdm->data, vtx, &lambda);
  return lambda;
}

static INLINE double
solid_get_volumic_mass
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  double rho = -1;
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  mdm->shader.solid.volumic_mass(mdm->dev, mdm->data, vtx, &rho);
  return rho;
}

static INLINE double
solid_get_delta
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  double delta = -1;
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  mdm->shader.solid.delta_solid(mdm->dev, mdm->data, vtx, &delta);
  return delta;
}

static INLINE double
solid_get_delta_boundary
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  double delta_bound = -1;
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  mdm->shader.solid.delta_boundary(mdm->dev, mdm->data, vtx, &delta_bound);
  return delta_bound;
}

static INLINE double
solid_get_temperature
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  double T = -1;
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  mdm->shader.solid.temperature(mdm->dev, mdm->data, vtx, &T);
  return T;
}

#endif /* SDIS_MEDIUM_C_H */

