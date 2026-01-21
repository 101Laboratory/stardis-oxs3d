/* Copyright (C) 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef TEST_SCAM_UTILS_H
#define TEST_SCAM_UTILS_H

#include <rsys/rsys.h>
#include <stdlib.h>

static INLINE double
rand_canonical(void)
{
  return (double)rand() / (double)((int64_t)RAND_MAX + 1);
}

#endif /* TEST_SCAM_UTILS_H */
