/* Copyright (C) 2018-2021, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#include "senc2d.h"
#include "senc2d_device_c.h"
#include "senc2d_scene_c.h"
#include "senc2d_scene_analyze_c.h"

#include <rsys/rsys.h>
#include <rsys/double2.h>
#include<rsys/mem_allocator.h>

#include <limits.h>

/******************************************************************************
 * Helper function
 *****************************************************************************/
static void
scene_release(ref_T * ref)
{
  struct senc2d_device* dev = NULL;
  struct senc2d_scene* scn = NULL;
  ASSERT(ref);
  scn = CONTAINER_OF(ref, struct senc2d_scene, ref);
  dev = scn->dev;
  darray_segment_in_release(&scn->segments_in);
  darray_position_release(&scn->vertices);
  darray_side_range_release(&scn->media_use);

  darray_segment_enc_release(&scn->analyze.segments_enc);
  darray_enclosure_release(&scn->analyze.enclosures);
  darray_enc_ids_array_release(&scn->analyze.enc_ids_array_by_medium);
  darray_frontier_vertex_release(&scn->analyze.frontiers);
  darray_seg_id_release(&scn->analyze.overlapping_ids);

  MEM_RM(dev->allocator, scn);
  SENC2D(device_ref_put(dev));
}

static INLINE int
compatible_medium
  (const medium_id_t m1,
   const medium_id_t m2)
{
  if(m1 == SENC2D_UNSPECIFIED_MEDIUM || m2 == SENC2D_UNSPECIFIED_MEDIUM)
    return 1;
  return (m1 == m2);
}

/******************************************************************************
 * Exported functions
 *****************************************************************************/
res_T
senc2d_scene_create
  (struct senc2d_device* dev,
   const int conv,
   const seg_id_t nsegs,
   void(*indices)(const seg_id_t, vrtx_id_t*, void*),
   void(*media)(const seg_id_t, medium_id_t*, void*),
   const vrtx_id_t nverts,
   void(*position)(const vrtx_id_t, double*, void* ctx),
   void* ctx,
   struct senc2d_scene** out_scn)
{
  struct senc2d_scene* scn = NULL;
  /* Tables to detect duplicates */
  struct htable_vrtx unique_vertices;
  struct htable_seg unique_segments;
  vrtx_id_t nv;
  seg_id_t ns;
  res_T res = RES_OK;

  if(!dev || !out_scn || !indices || !position || !nverts || !nsegs
    /* Convention must be set both regarding FRONT/BACK and INSIDE/OUTSIDE */
    || !(conv & (SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_BACK))
    || !(conv & (SENC2D_CONVENTION_NORMAL_INSIDE | SENC2D_CONVENTION_NORMAL_OUTSIDE)))
    return RES_BAD_ARG;

  htable_vrtx_init(dev->allocator, &unique_vertices);
  htable_seg_init(dev->allocator, &unique_segments);

  scn = MEM_CALLOC(dev->allocator, 1, sizeof(struct senc2d_scene));
  if(!scn) {
    log_err(dev, LIB_NAME":%s: could not allocate the star-enclosures-2d scene.\n",
      FUNC_NAME);
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&scn->ref);
  SENC2D(device_ref_get(dev));
  scn->dev = dev;
  scn->convention = conv;
  scn->nsegs = nsegs;
  scn->next_medium_idx = 0;
  scn->nverts = nverts;
  darray_segment_in_init(dev->allocator, &scn->segments_in);
  darray_position_init(dev->allocator, &scn->vertices);
  darray_side_range_init(dev->allocator, &scn->media_use);

  darray_segment_enc_init(scn->dev->allocator, &scn->analyze.segments_enc);
  darray_enclosure_init(scn->dev->allocator, &scn->analyze.enclosures);
  darray_enc_ids_array_init(scn->dev->allocator,
    &scn->analyze.enc_ids_array_by_medium);
  darray_frontier_vertex_init(scn->dev->allocator, &scn->analyze.frontiers);
  /* Enclosure 0 is always defined for infinite */
  OK(darray_enclosure_resize(&scn->analyze.enclosures, 1));
  scn->analyze.enclosures_count = 1;
  darray_seg_id_init(scn->dev->allocator, &scn->analyze.overlapping_ids);

  OK(darray_position_reserve(&scn->vertices, scn->nverts));
  OK(darray_segment_in_reserve(&scn->segments_in, scn->nsegs));
  OK(htable_vrtx_reserve(&unique_vertices, scn->nverts));
  OK(htable_seg_reserve(&unique_segments, scn->nsegs));

  /* Get vertices */
  FOR_EACH(nv, 0, nverts) {
    vrtx_id_t* p_vrtx;
    union double2 tmp;
    position(nv, tmp.vec, ctx);
    p_vrtx = htable_vrtx_find(&unique_vertices, &tmp);
    if(p_vrtx) {
      /* Duplicate vertex */
      log_err(scn->dev, LIB_NAME":%s: vertex "PRTF_VRTX" is a duplicate.\n",
        FUNC_NAME, nv);
      res = RES_BAD_ARG;
      goto error;
    }
    /* New vertex */
    ASSERT(nv == htable_vrtx_size_get(&unique_vertices));
    OK(darray_position_push_back(&scn->vertices, &tmp));
    OK(htable_vrtx_set(&unique_vertices, &tmp, &nv));
  }
  /* Get segments */
  FOR_EACH(ns, 0, nsegs) {
    int j;
    seg_id_t s;
    medium_id_t med[2]
      = { SENC2D_UNSPECIFIED_MEDIUM, SENC2D_UNSPECIFIED_MEDIUM };
    vrtx_id_t ind[2];
    union vrtx_id2 seg_key;
    struct segment_in tmp;
    seg_id_t* p_seg;
    indices(ns, ind, ctx);
    FOR_EACH(j, 0, 2) {
      if(ind[j] >= nverts) {
        log_err(scn->dev,
          LIB_NAME":%s: segment "PRTF_SEG" uses invalid vertex id "PRTF_VRTX".\n",
          FUNC_NAME, ns, ind[j]);
        res = RES_BAD_ARG;
        goto error;
      }
      ASSERT(ind[j] <= VRTX_MAX__);
      tmp.vertice_id[j] = ind[j];
    }
    if(tmp.vertice_id[0] == tmp.vertice_id[1]) {
      log_err(scn->dev, LIB_NAME":%s: segment "PRTF_SEG" is degenerated.\n",
        FUNC_NAME, ns);
      res = RES_BAD_ARG;
      goto error;
    }
    /* Get media */
    if(media) {
      media(ns, med, ctx);
      for(s = SENC2D_FRONT; s <= SENC2D_BACK; s += SENC2D_BACK - SENC2D_FRONT) {
        if(med[s] == SENC2D_UNSPECIFIED_MEDIUM || med[s] <= MEDIUM_MAX__)
          continue;
        res = RES_BAD_ARG;
        goto error;
      }
    }
    seg_make_key(&seg_key, tmp.vertice_id);
    p_seg = htable_seg_find(&unique_segments, &seg_key);
    if(p_seg) {
      /* Duplicate segment */
      log_err(scn->dev, LIB_NAME":%s: segment "PRTF_SEG" is a duplicate.\n",
        FUNC_NAME, ns);
      res = RES_BAD_ARG;
      goto error;
    }
    /* New segment */
    ASSERT(ns == htable_seg_size_get(&unique_segments));
    OK(htable_seg_set(&unique_segments, &seg_key, &ns));
    for(s = SENC2D_FRONT; s <= SENC2D_BACK; s += SENC2D_BACK - SENC2D_FRONT) {
      struct side_range* media_use;
      size_t m_idx = medium_id_2_medium_idx(med[s]);
      tmp.medium[s] = med[s];
      if(m_idx >= scn->next_medium_idx) {
        medium_id_t medium;
        medium = (medium_id_t)m_idx;
        scn->next_medium_idx = medium;
        OK(darray_side_range_resize(&scn->media_use, 1 + m_idx));
      }
      /* media_use 0 is for SENC2D_UNSPECIFIED_MEDIUM */
      media_use = darray_side_range_data_get(&scn->media_use) + m_idx;
      media_use->first =
        MMIN(media_use->first, SEGIDxSIDE_2_SEGSIDE(ns, s));
      ASSERT(media_use->first < 2 * (scn->nsegs + 1));
      media_use->last =
        MMAX(media_use->last, SEGIDxSIDE_2_SEGSIDE(ns, s));
      ASSERT(media_use->last < 2 * (scn->nsegs + 1));
      ASSERT(media_use->first <= media_use->last);
    }
    OK(darray_segment_in_push_back(&scn->segments_in, &tmp));
  }

  OK(darray_enc_ids_array_resize(&scn->analyze.enc_ids_array_by_medium,
    1 + scn->next_medium_idx)); /* +1 is for undef */
  /* Proceed to the analyze */
  OK(scene_analyze(scn));

exit:
  htable_vrtx_release(&unique_vertices);
  htable_seg_release(&unique_segments);
  if(scn) *out_scn = scn;
  return res;

error:
  if(scn) {
    SENC2D(scene_ref_put(scn));
    scn = NULL;
  }
  goto exit;
}

res_T
senc2d_scene_get_convention
  (const struct senc2d_scene* scn,
   int* convention)
{
  if(!scn || !convention) return RES_BAD_ARG;
  *convention = scn->convention;
  return RES_OK;
}

res_T
senc2d_scene_get_segments_count
  (const struct senc2d_scene* scn,
   seg_id_t* count)
{
  if(!scn || !count) return RES_BAD_ARG;
  *count = scn->nsegs;
  return RES_OK;
}

res_T
senc2d_scene_get_segment
  (const struct senc2d_scene* scn,
   const seg_id_t iseg,
   vrtx_id_t indices[2])
{
  const struct segment_in* seg;
  int i;
  if(!scn || !indices
    || iseg >= darray_segment_in_size_get(&scn->segments_in))
    return RES_BAD_ARG;
  seg = darray_segment_in_cdata_get(&scn->segments_in) + iseg;
  FOR_EACH(i, 0, 2) indices[i] = seg->vertice_id[i];
  return RES_OK;
}

res_T
senc2d_scene_get_segment_media
  (const struct senc2d_scene* scn,
   const seg_id_t iseg,
   medium_id_t media[2])
{
  const struct segment_in* seg;
  int i;
  if(!scn || !media
    || iseg >= darray_segment_in_size_get(&scn->segments_in))
    return RES_BAD_ARG;
  seg = darray_segment_in_cdata_get(&scn->segments_in) + iseg;
  FOR_EACH(i, 0, 2) media[i] = seg->medium[i];
  return RES_OK;
}

res_T
senc2d_scene_get_vertices_count
  (const struct senc2d_scene* scn,
   vrtx_id_t* count)
{
  if(!scn || !count) return RES_BAD_ARG;
  *count = scn->nverts;
  return RES_OK;
}

res_T
senc2d_scene_get_vertex
  (const struct senc2d_scene* scn,
   const vrtx_id_t ivert,
   double coord[2])
{
  const union double2* v;
  if(!scn || !coord
    || ivert >= darray_position_size_get(&scn->vertices))
    return RES_BAD_ARG;
  v = darray_position_cdata_get(&scn->vertices) + ivert;
  d2_set(coord, v->vec);
  return RES_OK;
}

res_T
senc2d_scene_ref_get(struct senc2d_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_get(&scn->ref);
  return RES_OK;
}

res_T
senc2d_scene_ref_put(struct senc2d_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_put(&scn->ref, scene_release);
  return RES_OK;
}
