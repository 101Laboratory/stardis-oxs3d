/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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
  struct fid id; /* Unique identifier of the medium */

  ref_T ref;
  struct sdis_device* dev;
};

static FINLINE unsigned
medium_get_id(const struct sdis_medium* mdm)
{
  ASSERT(mdm);
  return mdm->id.index;
}

/*******************************************************************************
 * Fluid local functions
 ******************************************************************************/
static INLINE double
fluid_get_calorific_capacity
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_FLUID);
  return mdm->shader.fluid.calorific_capacity(vtx, mdm->data);
}

static INLINE double
fluid_get_volumic_mass
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_FLUID);
  return mdm->shader.fluid.volumic_mass(vtx, mdm->data);
}

static INLINE double
fluid_get_temperature
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_FLUID);
  return mdm->shader.fluid.temperature(vtx, mdm->data);
}

/*******************************************************************************
 * Solid local functions
 ******************************************************************************/
static INLINE double
solid_get_calorific_capacity
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  return mdm->shader.solid.calorific_capacity(vtx, mdm->data);
}

static INLINE double
solid_get_thermal_conductivity
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  return mdm->shader.solid.thermal_conductivity(vtx, mdm->data);
}

static INLINE double
solid_get_volumic_mass
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  return mdm->shader.solid.volumic_mass(vtx, mdm->data);
}

static INLINE double
solid_get_delta
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  return mdm->shader.solid.delta_solid(vtx, mdm->data);
}

static INLINE double
solid_get_delta_boundary
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  return mdm->shader.solid.delta_boundary(vtx, mdm->data);
}

static INLINE double
solid_get_volumic_power
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  return mdm->shader.solid.volumic_power 
    ? mdm->shader.solid.volumic_power(vtx, mdm->data)
    : 0;
}

static INLINE double
solid_get_temperature
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  ASSERT(mdm && mdm->type == SDIS_MEDIUM_SOLID);
  return mdm->shader.solid.temperature(vtx, mdm->data);
}

#endif /* SDIS_MEDIUM_C_H */

