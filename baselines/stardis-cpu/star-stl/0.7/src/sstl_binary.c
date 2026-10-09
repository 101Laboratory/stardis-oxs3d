/* Copyright (C) 2015, 2016, 2019, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#include "sstl_c.h"

#include <rsys/cstr.h>
#include <rsys/stretchy_array.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE res_T
parse_header(struct sstl* sstl, FILE* fp, const char* name)
{
  char header[80];
  ASSERT(sstl && fp && fp);

  if(fread(header, sizeof(header), 1, fp) != 1) {
    ERROR(sstl, "%s: invalid header\n", name);
    return RES_BAD_ARG;
  }
  return RES_OK;
}

static INLINE res_T
parse_triangle_count
  (struct sstl* sstl,
   FILE* fp,
   const char* name,
   uint32_t* ntri)
{
  uint8_t bytes[4]; /* Little endian */
  ASSERT(sstl && fp && name && ntri);

  if(fread(bytes, sizeof(bytes), 1, fp) != 1) {
    ERROR(sstl, "%s: invalid triangle count\n", name);
    return RES_BAD_ARG;
  }

  /* Ensure encoding in host byte order */
  *ntri =
   (uint32_t)(bytes[0]<<0)
 | (uint32_t)(bytes[1]<<8)
 | (uint32_t)(bytes[2]<<16)
 | (uint32_t)(bytes[3]<<24);
  return RES_OK;
}

static INLINE res_T
parse_triangle
  (struct sstl* sstl,
   FILE* fp,
   const char* name,
   const uint32_t itri) /* Triangle identifier */
{
  uint8_t bytes[4/*#bytes*/*12/*#vectors*/+2/*attribute*/];
  union { uint32_t ui32; float f; } ucast;
  float* N = NULL; /* Normal */
  float v[3][3] = {0}; /* Vertices */
  int i = 0;
  res_T res = RES_OK;
  ASSERT(sstl && fp && name);

  if(fread(bytes, sizeof(bytes), 1, fp) != 1) {
    ERROR(sstl, "%s: invalid triangle %i\n", name, itri);
    res = RES_BAD_ARG;
    goto error;
  }

  #define CAST(Bytes) \
    ((ucast.ui32 = \
      (uint32_t)((Bytes)[0]<<0) \
    | (uint32_t)((Bytes)[1]<<8) \
    | (uint32_t)((Bytes)[2]<<16) \
    | (uint32_t)((Bytes)[3]<<24)), \
    ucast.f)

  N = sa_add(sstl->normals, 3);
  N[0] = CAST(bytes+0);
  N[1] = CAST(bytes+4);
  N[2] = CAST(bytes+8);

  FOR_EACH(i, 0, 3) {
    v[i][0] = CAST(bytes+(i+1)*12+0);
    v[i][1] = CAST(bytes+(i+1)*12+4);
    v[i][2] = CAST(bytes+(i+1)*12+8);

    res = register_vertex(sstl, v[i]);
    if(res != RES_OK) {
      ERROR(sstl, "%s: triangle %i: vertex registration error -- %s\n",
        name, itri, res_to_cstr(res));
      res = RES_BAD_ARG;
      goto error;
    }
  }

  /* If necessary, automatically calculate the surface normal. */
  if(!f3_is_normalized(N)) calculate_normal(N, v[0], v[1], v[2]);

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
load_stream_binary(struct sstl* sstl, FILE* fp, const char* name)
{
  uint32_t ntris = 0;
  uint32_t i = 0;
  res_T res = RES_OK;
  ASSERT(sstl && fp && name);

  clear(sstl);

  if((res = parse_header(sstl, fp, name)) != RES_OK) goto error;
  if((res = parse_triangle_count(sstl, fp, name, &ntris)) != RES_OK) goto error;

  FOR_EACH(i, 0, ntris) {
    if((res = parse_triangle(sstl, fp, name, i)) != RES_OK) goto error;
  }

  sstl->type = SSTL_BINARY;

exit:
  htable_vertex_purge(&sstl->vertex2id); /* Purge the helper structure */
  return res;
error:
  clear(sstl);
  goto exit;
}
