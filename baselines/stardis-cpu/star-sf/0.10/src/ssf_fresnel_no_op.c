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

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static res_T
fresnel_no_op_init(struct mem_allocator* allocator, void* fresnel)
{
  (void)allocator, (void)fresnel;
  return RES_OK;
}

static void
fresnel_no_op_release(void* fresnel)
{
  (void)fresnel;
}

static double
fresnel_no_op_eval(void* fresnel, const double cos_theta)
{
  (void)fresnel, (void)cos_theta;
  return 1.0;
}

/*******************************************************************************
 * Exported symbol
 ******************************************************************************/
const struct ssf_fresnel_type ssf_fresnel_no_op = {
  fresnel_no_op_init,
  fresnel_no_op_release,
  fresnel_no_op_eval,
  0, 1
};

