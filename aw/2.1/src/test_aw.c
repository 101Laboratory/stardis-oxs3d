/* Copyright (C) 2014-2017, 2020-2023 Vincent Forest (vaplv@free.fr)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include "aw.h"

#include <rsys/clock_time.h>
#include <rsys/mem_allocator.h>

int
main(int argc, char** argv)
{
  struct aw_obj* obj = NULL;
  struct aw_mtl* mtl = NULL;
  struct time t0, t1;
  struct aw_obj_desc desc;
  char buf[128];
  size_t i;

  if(argc < 2) {
    fprintf(stderr, "Usage: %s OBJ-FILENAME\n", argv[0]);
    return -1;
  }

  CHK(aw_obj_create(NULL, &mem_default_allocator, 1, &obj) == RES_OK);
  CHK(aw_mtl_create(NULL, &mem_default_allocator, 1, &mtl) == RES_OK);

  time_current(&t0);
  CHK(aw_obj_load(obj, argv[1]) == RES_OK);
  CHK(aw_obj_get_desc(obj, &desc) == RES_OK);
  FOR_EACH(i, 0, desc.mtllibs_count) {
    const char* mtllib = NULL;
    CHK(aw_obj_get_mtllib(obj, i, &mtllib) == RES_OK);
    aw_mtl_load(mtl, mtllib);
  }
  time_current(&t1);
  time_sub(&t0, &t1, &t0);
  time_dump(&t0, TIME_MIN|TIME_SEC|TIME_MSEC, NULL, buf, sizeof(buf));

  fprintf(stdout, "load `%s' in %s\n", argv[1], buf);
  fprintf(stdout, "faces count   = %lu\n", (unsigned long)desc.faces_count);
  fprintf(stdout, "groups count  = %lu\n", (unsigned long)desc.groups_count);
  fprintf(stdout, "usemtls count = %lu\n", (unsigned long)desc.usemtls_count);
  fprintf(stdout, "mtllibs count = %lu\n", (unsigned long)desc.mtllibs_count);

  CHK(aw_obj_ref_put(obj) == RES_OK);
  CHK(aw_mtl_ref_put(mtl) == RES_OK);

  CHK(MEM_ALLOCATED_SIZE(&mem_default_allocator) == 0);
  CHK(mem_allocated_size() == 0);
  return 0;
}
