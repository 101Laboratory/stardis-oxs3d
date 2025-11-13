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

#ifndef SDIS_RADIATIVE_ENV_C_H
#define SDIS_RADIATIVE_ENV_C_H

#include "sdis.h"
#include <rsys/ref_count.h>

struct sdis_radiative_env {
  struct sdis_radiative_env_shader shader;
  struct sdis_data* data;

  ref_T ref;
  struct sdis_device* dev;
};

static INLINE double
radiative_env_get_temperature
  (const struct sdis_radiative_env* radenv,
   const struct sdis_radiative_ray* ray)
{
  ASSERT(ray && d3_is_normalized(ray->dir));
  return radenv && radenv->shader.temperature
    ? radenv->shader.temperature(ray, radenv->data)
    : SDIS_TEMPERATURE_NONE;
}

static INLINE double
radiative_env_get_reference_temperature
  (const struct sdis_radiative_env* radenv,
   const struct sdis_radiative_ray* ray)
{
  ASSERT(ray && d3_is_normalized(ray->dir));
  return radenv && radenv->shader.reference_temperature
    ? radenv->shader.reference_temperature(ray, radenv->data)
    : SDIS_TEMPERATURE_NONE;
}

#endif /* SDIS_RADIATIVE_ENV_C_H */
