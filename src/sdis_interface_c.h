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

#ifndef SDIS_INTERFACE_C_H
#define SDIS_INTERFACE_C_H

#include "sdis.h"
#include <rsys/free_list.h>
#include <rsys/ref_count.h>
#include <float.h>

/* Forward declaration of external type */
struct s2d_hit;
struct s3d_hit;

struct sdis_interface {
  struct sdis_medium* medium_front;
  struct sdis_medium* medium_back;
  struct sdis_interface_shader shader;
  struct sdis_data* data;
  struct fid id; /* Unique identifier of the interface */

  ref_T ref;
  struct sdis_device* dev;
};

extern LOCAL_SYM struct sdis_medium*
interface_get_medium
  (const struct sdis_interface* interf,
   const enum sdis_side side);

static FINLINE unsigned
interface_get_id(const struct sdis_interface* interf)
{
  ASSERT(interf);
  return interf->id.index;
}

extern LOCAL_SYM void
setup_interface_fragment_2d
  (struct sdis_interface_fragment* frag,
   const struct sdis_rwalk_vertex* vertex,
   const struct s2d_hit* hit,
   const enum sdis_side side);

extern LOCAL_SYM void
setup_interface_fragment_3d
  (struct sdis_interface_fragment* frag,
   const struct sdis_rwalk_vertex* vertex,
   const struct s3d_hit* hit,
   const enum sdis_side side);

extern LOCAL_SYM res_T
build_interface_fragment_2d
  (struct sdis_interface_fragment* frag,
   const struct sdis_scene* scn,
   const unsigned iprim,
   const double uv[1],
   const enum sdis_side side);

extern LOCAL_SYM res_T
build_interface_fragment_3d
  (struct sdis_interface_fragment* frag,
   const struct sdis_scene* scn,
   const unsigned iprim,
   const double uv[2],
   const enum sdis_side side);

static INLINE double
interface_get_convection_coef
  (const struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag)
{
  ASSERT(interf && frag);
  return interf->shader.convection_coef
    ? interf->shader.convection_coef(frag, interf->data) : 0;
}

static INLINE double
interface_get_convection_coef_upper_bound
  (const struct sdis_interface* interf)
{
  ASSERT(interf);
  return interf->shader.convection_coef_upper_bound;
}

static INLINE double
interface_side_get_temperature
  (const struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag)
{
  const struct sdis_interface_side_shader* shader;
  ASSERT(interf && frag);
  switch(frag->side) {
    case SDIS_FRONT: shader = &interf->shader.front; break;
    case SDIS_BACK: shader = &interf->shader.back; break;
    default: FATAL("Unreachable code.\n");
  }
  return shader->temperature ? shader->temperature(frag, interf->data) : -1;
}

static INLINE double
interface_side_get_flux
  (const struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag)
{
  const struct sdis_interface_side_shader* shader;
  ASSERT(interf && frag);
  switch(frag->side) {
    case SDIS_FRONT: shader = &interf->shader.front; break;
    case SDIS_BACK: shader = &interf->shader.back; break;
    default: FATAL("Unreachable code.\n");
  }
  return shader->flux ? shader->flux(frag, interf->data) : SDIS_FLUX_NONE;
}

static INLINE double
interface_side_get_emissivity
  (const struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag)
{
  const struct sdis_interface_side_shader* shader;
  ASSERT(interf && frag);
  switch(frag->side) {
    case SDIS_FRONT: shader = &interf->shader.front; break;
    case SDIS_BACK: shader = &interf->shader.back; break;
    default: FATAL("Unreachable code\n"); break;
  }
  return shader->emissivity ? shader->emissivity(frag, interf->data) : 0;
}

static INLINE double
interface_side_get_specular_fraction
  (const struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag)
{
  const struct sdis_interface_side_shader* shader;
  ASSERT(interf && frag);
  switch(frag->side) {
    case SDIS_FRONT: shader = &interf->shader.front; break;
    case SDIS_BACK: shader = &interf->shader.back; break;
    default: FATAL("Unreachable code\n"); break;
  }
  return shader->specular_fraction
    ? shader->specular_fraction(frag, interf->data) : 0;
}

#endif /* SDIS_INTERFACE_C_H */

