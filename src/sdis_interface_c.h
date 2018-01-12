/* Copyright (C) |Meso|Star> 2016-2018 (contact@meso-star.com)
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

extern LOCAL_SYM const struct sdis_medium*
interface_get_medium
  (const struct sdis_interface* interf,
   const enum sdis_side_flag side);

extern LOCAL_SYM unsigned
interface_get_id
  (const struct sdis_interface* interf);

extern LOCAL_SYM void
setup_interface_fragment
  (struct sdis_interface_fragment* frag,
   const struct sdis_rwalk_vertex* vertex,
   const struct s3d_hit* hit);

static INLINE double
interface_get_temperature
  (const struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag)
{
  ASSERT(interf && frag);
  if(!interf->shader.temperature) return -DBL_MAX;
  return interf->shader.temperature(frag, interf->data);
}

static INLINE double
interface_get_convection_coef
  (const struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag)
{
  ASSERT(interf && frag);
  return interf->shader.convection_coef(frag, interf->data);
}

#endif /* SDIS_INTERFACE_C_H */

