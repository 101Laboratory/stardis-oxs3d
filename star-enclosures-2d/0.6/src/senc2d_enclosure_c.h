/* Copyright (C) 2018-2021, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SENC2D_ENCLOSURE_C_H
#define SENC2D_ENCLOSURE_C_H

#include <rsys/ref_count.h>

#include "senc2d.h"
#include "senc2d_internal_types.h"

struct enclosure_data;
struct senc2d_scene;

struct senc2d_enclosure {
  const struct enclosure_data* data;
  struct senc2d_scene* scene;
  ref_T ref;
};

struct senc2d_enclosure*
enclosure_create
  (struct senc2d_scene* scene,
   const enclosure_id_t idx);

#endif /* SENC2D_ENCLOSURE_C_H */
