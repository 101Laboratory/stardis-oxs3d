/* Copyright (C) 2016-2018, 2021-2025 |Méso|Star> (contact@meso-star.com)
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

#include "ssf.h"
#include "ssf_fresnel_c.h"

struct fresnel_constant {
  double k; /* in [0, 1] */
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static res_T
fresnel_constant_init(struct mem_allocator* allocator, void* fresnel)
{
  struct fresnel_constant* f = fresnel;
  (void)allocator;
  ASSERT(fresnel);
  f->k = 1; /* <=> No op */
  return RES_OK;
}

static void
fresnel_constant_release(void* fresnel)
{
  (void)fresnel;
}

static double
fresnel_constant_eval(void* fresnel, const double cos_theta_i)
{
  struct fresnel_constant* f = fresnel;
  (void)cos_theta_i;
  return f->k;
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_fresnel_type ssf_fresnel_constant = {
  fresnel_constant_init,
  fresnel_constant_release,
  fresnel_constant_eval,
  sizeof(struct fresnel_constant),
  ALIGNOF(struct fresnel_constant)
};

res_T
ssf_fresnel_constant_setup(struct ssf_fresnel* fresnel, const double value)
{
  struct fresnel_constant* f;
  if(!fresnel || !FRESNEL_TYPE_EQ(&fresnel->type, &ssf_fresnel_constant))
    return RES_BAD_ARG;
  if(value < 0 || value > 1)
    return RES_BAD_ARG;
  f = fresnel->data;
  f->k = value;
  return RES_OK;
}

