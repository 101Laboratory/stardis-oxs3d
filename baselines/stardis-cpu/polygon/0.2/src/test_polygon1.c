/* Copyright (C) 2014-2017, 2021-2023 Vincent Forest (vaplv@free.fr)
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

#include "polygon.h"
#include "test_polygon_utils.h"
#include <rsys/mem_allocator.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator_proxy;
  struct polygon* poly;
  union { uint32_t i; float f; } ucast;
  float pos[3] = {0, 0, 0};
  const uint32_t* ids;
  uint32_t nids;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator_proxy, &mem_default_allocator) == RES_OK);

  CHK(polygon_create(&allocator_proxy, &poly) == RES_OK);

  pos[0] = (ucast.i = 0xbeb504f3, ucast.f);
  pos[1] = (ucast.i = 0x3eb504f3, ucast.f);
  CHK(polygon_vertex_add(poly, pos) == RES_OK);
  pos[0] = (ucast.i = 0xbe977d76, ucast.f);
  pos[1] = (ucast.i = 0x3ec14031, ucast.f);
  CHK(polygon_vertex_add(poly, pos) == RES_OK);
  pos[0] = (ucast.i = 0xbe5dab96, ucast.f);
  pos[1] = (ucast.i = 0x3f05ca33, ucast.f);
  CHK(polygon_vertex_add(poly, pos) == RES_OK);
  pos[0] = (ucast.i = 0xbecccbef, ucast.f);
  pos[1] = (ucast.i = 0x3ecccbef, ucast.f);
  CHK(polygon_vertex_add(poly, pos) == RES_OK);
  pos[0] = (ucast.i = 0xbeb504f3, ucast.f);
  pos[1] = (ucast.i = 0x3eb504f3, ucast.f);
  CHK(polygon_vertex_add(poly, pos) == RES_OK);

  CHK(polygon_triangulate(poly, &ids, &nids) == RES_OK);
  CHK(nids == 6);
  CHK(polygon_ref_put(poly) == RES_OK);

  check_memory_allocator(&allocator_proxy);
  mem_shutdown_proxy_allocator(&allocator_proxy);
  CHK(mem_allocated_size() == 0);
  return 0;
}
