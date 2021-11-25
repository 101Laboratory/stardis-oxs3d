/* Copyright (C) 2016-2021 |Meso|Star> (contact@meso-star.com)
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

#include "sdis.h"

/*******************************************************************************
 * Exported function
 ******************************************************************************/
res_T
sdis_get_info(struct sdis_info* info)
{
  if(!info) return RES_BAD_ARG;
  *info = SDIS_INFO_NULL;
#ifdef SDIS_USE_MPI
  info->mpi_enable = 1;
#else
  info->mpi_enable = 0;
#endif
  return RES_OK;
}
