/* Copyright (C) 2024 |Méso|Star> (contact@meso-star.com)
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

#include "swf.h"
#include <rsys/mem_allocator.h>

/* Define generic dimension helper functions */
#define TEST_SWF_DIMENSION 2
#include "test_swf_HXd.h"

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  (void)argc, (void)argv;

  check_tabulation_creation_2d();
  check_inversion_2d();
  CHK(mem_allocated_size() == 0);
  return 0;
}
