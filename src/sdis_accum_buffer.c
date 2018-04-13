/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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
#include "sdis_device_c.h"

struct sdis_accum_buffer {
  struct sdis_accum* accums;
  size_t width;
  size_t height;

  ref_T ref;
  struct sdis_device* dev;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
accum_buffer_release(ref_T* ref)
{
  struct sdis_accum_buffer* buf;
  struct sdis_device* dev;
  ASSERT(ref);
  buf = CONTAINER_OF(ref, struct sdis_accum_buffer, ref);
  dev = buf->dev;
  if(buf->accums) MEM_RM(dev->allocator, buf->accums);
  MEM_RM(dev->allocator, buf);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Export functions
 ******************************************************************************/
res_T
sdis_accum_buffer_create
  (struct sdis_device* dev,
   const size_t width,
   const size_t height,
   struct sdis_accum_buffer** out_buf)
{
  struct sdis_accum_buffer* buf = NULL;
  size_t sz;
  res_T res = RES_OK;

  if(!dev || !width || !height || !out_buf) {
    res = RES_BAD_ARG;
    goto error;
  }

  buf = MEM_CALLOC(dev->allocator, 1, sizeof(struct sdis_accum_buffer));
  if(!buf) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&buf->ref);
  SDIS(device_ref_get(dev));
  buf->dev = dev;
  buf->width = width;
  buf->height = height;

  sz = buf->width * buf->height * sizeof(struct sdis_accum);
  buf->accums = MEM_ALLOC_ALIGNED(dev->allocator, sz, 16);
  if(!buf->accums) {
    log_err(dev, "%s: could not allocate a buffer accumulations of %lux%lu.\n",
      FUNC_NAME, (unsigned long)width, (unsigned long)height);
    res = RES_MEM_ERR;
    goto error;
  }

exit:
  if(out_buf) *out_buf = buf;
  return res;
error:
  if(buf) {
    SDIS(accum_buffer_ref_put(buf));
    buf = NULL;
  }
  goto exit;
}

res_T
sdis_accum_buffer_ref_get(struct sdis_accum_buffer* buf)
{
  if(!buf) return RES_BAD_ARG;
  ref_get(&buf->ref);
  return RES_OK;
}

res_T
sdis_accum_buffer_ref_put(struct sdis_accum_buffer* buf)
{
  if(!buf) return RES_BAD_ARG;
  ref_put(&buf->ref, accum_buffer_release);
  return RES_OK;
}

res_T
sdis_accum_buffer_get_layout
  (const struct sdis_accum_buffer* buf,
   struct sdis_accum_buffer_layout* layout)
{
  if(!buf || !layout) return RES_BAD_ARG;
  layout->width = buf->width;
  layout->height = buf->height;
  return RES_OK;
}

res_T
sdis_accum_buffer_map
  (const struct sdis_accum_buffer* buf, const struct sdis_accum** accums)
{
  if(!buf || !accums) return RES_BAD_ARG;
  *accums = buf->accums;
  return RES_OK;
}

res_T
sdis_accum_buffer_unmap(const struct sdis_accum_buffer* buf)
{
  if(!buf) return RES_BAD_ARG;
  /* Do nothing */
  return RES_OK;
}

res_T
sdis_accum_buffer_write
  (void* buffer,
   const size_t origin[2],
   const size_t size[2],
   const struct sdis_accum* accums)
{
  struct sdis_accum_buffer* buf = buffer;
  const struct sdis_accum* src_row = accums;
  size_t dst_iy;

  if(UNLIKELY(!buffer || !origin || !size || !accums)) return RES_BAD_ARG;
  if(UNLIKELY((origin[0] + size[0]) > buf->width)) return RES_BAD_ARG;
  if(UNLIKELY((origin[1] + size[1]) > buf->height)) return RES_BAD_ARG;

  FOR_EACH(dst_iy, origin[1], origin[1] + size[1]) {
    const size_t dst_irow = dst_iy*buf->width + origin[0];
    memcpy(buf->accums + dst_irow, src_row, size[0] * sizeof(struct sdis_accum));
    src_row += size[0];
  }
  return RES_OK;
}

