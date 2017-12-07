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

#ifndef SDIS_DEVICE_C_H
#define SDIS_DEVICE_C_H

#include <rsys/ref_count.h>

struct sdis_device {
  struct logger* logger;
  struct mem_allocator* allocator;
  unsigned nthreads;
  int verbose;

  ref_T ref;
};

#endif /* SDIS_DEVICE_C_H */
