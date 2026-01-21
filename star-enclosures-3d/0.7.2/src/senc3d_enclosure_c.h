/* Copyright (C) 2018-2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SENC3D_ENCLOSURE_C_H
#define SENC3D_ENCLOSURE_C_H

#include <rsys/ref_count.h>

#include "senc3d.h"
#include "senc3d_internal_types.h"

struct enclosure_data;
struct senc3d_scene;

struct senc3d_enclosure {
  const struct enclosure_data* data;
  struct senc3d_scene* scene;
  ref_T ref;
};

struct senc3d_enclosure*
enclosure_create
  (struct senc3d_scene* scene,
   const enclosure_id_t idx);

#endif /* SENC3D_ENCLOSURE_C_H */
