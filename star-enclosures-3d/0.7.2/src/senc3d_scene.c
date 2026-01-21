/* Copyright (C) 2018-2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#include "senc3d.h"
#include "senc3d_device_c.h"
#include "senc3d_scene_c.h"
#include "senc3d_scene_analyze_c.h"

#include <rsys/rsys.h>
#include <rsys/double3.h>
#include <rsys/mem_allocator.h>

#include <limits.h>

/******************************************************************************
 * Helper function
 *****************************************************************************/
static void
scene_release(ref_T * ref)
{
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  ASSERT(ref);
  scn = CONTAINER_OF(ref, struct senc3d_scene, ref);
  dev = scn->dev;
  darray_triangle_in_release(&scn->triangles_in);
  darray_position_release(&scn->vertices);
  darray_side_range_release(&scn->media_use);

  darray_triangle_enc_release(&scn->analyze.triangles_enc);
  darray_enclosure_release(&scn->analyze.enclosures);
  darray_enc_ids_array_release(&scn->analyze.enc_ids_array_by_medium);
  darray_frontier_edge_release(&scn->analyze.frontiers);
  darray_trg_id_release(&scn->analyze.overlapping_ids);

  MEM_RM(dev->allocator, scn);
  SENC3D(device_ref_put(dev));
}

static INLINE int
compatible_medium
  (const medium_id_t m1,
   const medium_id_t m2)
{
  if(m1 == SENC3D_UNSPECIFIED_MEDIUM || m2 == SENC3D_UNSPECIFIED_MEDIUM) return 1;
  return (m1 == m2);
}

/******************************************************************************
 * Exported functions
 *****************************************************************************/
res_T
senc3d_scene_create
  (struct senc3d_device* dev,
   const int conv,
   const trg_id_t ntris,
   void(*indices)(const trg_id_t, vrtx_id_t*, void*),
   void(*media)(const trg_id_t, medium_id_t*, void*),
   const vrtx_id_t nverts,
   void(*position)(const vrtx_id_t, double*, void* ctx),
   void* ctx,
   struct senc3d_scene** out_scn)
{
  struct senc3d_scene* scn = NULL;
  /* Tables to detect duplicates */
  struct htable_vrtx unique_vertices;
  struct htable_trg unique_triangles;
  vrtx_id_t nv;
  trg_id_t nt;
  res_T res = RES_OK;

  if(!dev || !out_scn || !indices || !position || !nverts || !ntris
    /* Convention must be set both regarding FRONT/BACK and INSIDE/OUTSIDE */
    || !(conv & (SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_BACK))
    || !(conv & (SENC3D_CONVENTION_NORMAL_INSIDE | SENC3D_CONVENTION_NORMAL_OUTSIDE)))
    return RES_BAD_ARG;

  htable_vrtx_init(dev->allocator, &unique_vertices);
  htable_trg_init(dev->allocator, &unique_triangles);

  scn = MEM_CALLOC(dev->allocator, 1, sizeof(struct senc3d_scene));
  if(!scn) {
    log_err(dev, LIB_NAME":%s: could not allocate the star_enclosures-3d scene.\n",
      FUNC_NAME);
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&scn->ref);
  SENC3D(device_ref_get(dev));
  scn->dev = dev;
  scn->convention = conv;
  scn->ntris = ntris;
  scn->nverts = nverts;
  darray_triangle_in_init(dev->allocator, &scn->triangles_in);
  darray_position_init(dev->allocator, &scn->vertices);
  darray_side_range_init(dev->allocator, &scn->media_use);

  darray_triangle_enc_init(scn->dev->allocator, &scn->analyze.triangles_enc);
  darray_enclosure_init(scn->dev->allocator, &scn->analyze.enclosures);
  darray_enc_ids_array_init(scn->dev->allocator,
    &scn->analyze.enc_ids_array_by_medium);
  darray_frontier_edge_init(scn->dev->allocator, &scn->analyze.frontiers);
  /* Enclosure 0 is always defined for infinite */
  OK(darray_enclosure_resize(&scn->analyze.enclosures, 1));
  scn->analyze.enclosures_count = 1;
  darray_trg_id_init(scn->dev->allocator, &scn->analyze.overlapping_ids);

  OK(darray_position_reserve(&scn->vertices, scn->nverts));
  OK(darray_triangle_in_reserve(&scn->triangles_in, scn->ntris));
  OK(htable_vrtx_reserve(&unique_vertices, scn->nverts));
  OK(htable_trg_reserve(&unique_triangles, scn->ntris));

  /* Get vertices */
  FOR_EACH(nv, 0, nverts) {
    vrtx_id_t* p_vrtx;
    union double3 tmp;
    /* API: position needs an api_t */
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
  /* Get triangles */
  FOR_EACH(nt, 0, ntris) {
    int j, dg, s;
    medium_id_t med[2]
      = { SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM };
    vrtx_id_t ind[3];
    union vrtx_id3 trg_key;
    struct triangle_in tmp;
    trg_id_t* p_trg;
    indices(nt, ind, ctx);
    FOR_EACH(j, 0, 3) {
      if(ind[j] >= nverts) {
        log_err(scn->dev,
          LIB_NAME":%s: triangle "PRTF_TRG" uses invalid vertex id "PRTF_VRTX".\n",
          FUNC_NAME, nt, ind[j]);
        res = RES_BAD_ARG;
        goto error;
      }
      ASSERT(ind[j] <= VRTX_MAX__);
      tmp.vertice_id[j] = (vrtx_id_t)ind[j];
    }
    dg = tmp.vertice_id[0] == tmp.vertice_id[1]
      || tmp.vertice_id[0] == tmp.vertice_id[2]
      || tmp.vertice_id[1] == tmp.vertice_id[2];
    if(!dg) {
      double edge1[3], edge2[3], n[3];
      const union double3* vertices = darray_position_cdata_get(&scn->vertices);
      d3_sub(edge1, vertices[tmp.vertice_id[1]].vec, vertices[tmp.vertice_id[0]].vec);
      d3_sub(edge2, vertices[tmp.vertice_id[2]].vec, vertices[tmp.vertice_id[0]].vec);
      d3_cross(n, edge1, edge2);
      dg = d3_len(n) == 0;
    }
    if(dg) {
      log_err(scn->dev, LIB_NAME":%s: triangle "PRTF_TRG" is degenerated.\n",
        FUNC_NAME, nt);
      res = RES_BAD_ARG;
      goto error;
    }
    /* Get media */
    if(media) {
      media(nt, med, ctx);
      FOR_EACH(s, 0, 2) {
        if(med[s] == SENC3D_UNSPECIFIED_MEDIUM || med[s] <= MEDIUM_MAX__)
          continue;
        res = RES_BAD_ARG;
        goto error;
      }
    }
    trg_make_key(&trg_key, tmp.vertice_id);
    p_trg = htable_trg_find(&unique_triangles, &trg_key);
    if(p_trg) {
      /* Duplicate triangle */
      log_err(scn->dev, LIB_NAME":%s: triangle "PRTF_TRG" is a duplicate.\n",
        FUNC_NAME, nt);
      res = RES_BAD_ARG;
      goto error;
    }
    /* New triangle */
    ASSERT(nt == htable_trg_size_get(&unique_triangles));
    OK(htable_trg_set(&unique_triangles, &trg_key, &nt));
    FOR_EACH(s, 0, 2) {
      const enum senc3d_side side[2] = { SENC3D_FRONT, SENC3D_BACK };
      struct side_range* media_use;
      size_t m_idx = medium_id_2_medium_idx(med[s]);
      tmp.medium[s] = med[s];
      if(m_idx >= darray_side_range_size_get(&scn->media_use)) {
        OK(darray_side_range_resize(&scn->media_use, 1 + m_idx));
      }
      /* media_use 0 is for SENC3D_UNSPECIFIED_MEDIUM */
      media_use = darray_side_range_data_get(&scn->media_use) + m_idx;
      media_use->first =
        MMIN(media_use->first, TRGIDxSIDE_2_TRGSIDE((trg_id_t)nt, side[s]));
      ASSERT(media_use->first < 2 * (scn->ntris + 1));
      media_use->last =
        MMAX(media_use->last, TRGIDxSIDE_2_TRGSIDE((trg_id_t)nt, side[s]));
      ASSERT(media_use->last < 2 * (scn->ntris + 1));
      ASSERT(media_use->first <= media_use->last);
    }
    OK(darray_triangle_in_push_back(&scn->triangles_in, &tmp));
  }

  OK(darray_enc_ids_array_resize(&scn->analyze.enc_ids_array_by_medium,
    darray_side_range_size_get(&scn->media_use)));
  /* Proceed to the analyze */
  OK(scene_analyze(scn));

exit:
  htable_vrtx_release(&unique_vertices);
  htable_trg_release(&unique_triangles);
  if(scn) *out_scn = scn;
  return res;

error:
  if(scn) {
    SENC3D(scene_ref_put(scn));
    scn = NULL;
  }
  goto exit;
}

res_T
senc3d_scene_get_convention
  (const struct senc3d_scene* scn,
   int* convention)
{
  if(!scn || !convention) return RES_BAD_ARG;
  *convention = scn->convention;
  return RES_OK;
}

res_T
senc3d_scene_get_triangles_count
  (const struct senc3d_scene* scn,
   trg_id_t* count)
{
  if(!scn || !count) return RES_BAD_ARG;
  *count = scn->ntris;
  return RES_OK;
}

res_T
senc3d_scene_get_triangle
  (const struct senc3d_scene* scn,
   const trg_id_t itri,
   vrtx_id_t indices[3])
{
  const struct triangle_in* trg;
  int i;
  if(!scn || !indices
    || itri >= darray_triangle_in_size_get(&scn->triangles_in))
    return RES_BAD_ARG;
  trg = darray_triangle_in_cdata_get(&scn->triangles_in) + itri;
  FOR_EACH(i, 0, 3) indices[i] = trg->vertice_id[i];
  return RES_OK;
}

res_T
senc3d_scene_get_triangle_media
  (const struct senc3d_scene* scn,
   const trg_id_t itri,
   medium_id_t media[2])
{
  const struct triangle_in* trg;
  int i;
  if(!scn || !media
    || itri >= darray_triangle_in_size_get(&scn->triangles_in))
    return RES_BAD_ARG;
  trg = darray_triangle_in_cdata_get(&scn->triangles_in) + itri;
  FOR_EACH(i, 0, 2) media[i] = trg->medium[i];
  return RES_OK;
}

res_T
senc3d_scene_get_vertices_count
  (const struct senc3d_scene* scn,
   vrtx_id_t* count)
{
  if(!scn || !count) return RES_BAD_ARG;
  *count = scn->nverts;
  return RES_OK;
}

res_T
senc3d_scene_get_vertex
  (const struct senc3d_scene* scn,
   const vrtx_id_t ivert,
   double coord[3])
{
  const union double3* v;
  if(!scn || !coord
    || ivert >= darray_position_size_get(&scn->vertices))
    return RES_BAD_ARG;
  v = darray_position_cdata_get(&scn->vertices) + ivert;
  d3_set(coord, v->vec);
  return RES_OK;
}

res_T
senc3d_scene_dump_enclosure_obj
  (struct senc3d_scene* scn,
   const unsigned enc,
   const char* name)
{
  struct senc3d_enclosure* enclosure = NULL;
  struct senc3d_enclosure_header header;
  FILE* stream = NULL;
  unsigned count, i;
  res_T res = RES_OK;

  if(!scn || !name) {
    res = RES_BAD_ARG;
    goto error;
  }

  OK(senc3d_scene_get_enclosure_count(scn, &count));
  if(enc >= count) {
    res = RES_BAD_ARG;
    goto error;
  }

  OK(senc3d_scene_get_enclosure(scn, enc, &enclosure));
  OK(senc3d_enclosure_get_header(enclosure, &header));

  stream = fopen(name, "w");
  if(!stream) {
    res = RES_BAD_ARG;
    goto error;
  }

  FOR_EACH(i, 0, header.vertices_count) {
    double tmp[3];
    OK(senc3d_enclosure_get_vertex(enclosure, i, tmp));
    fprintf(stream, "v %g %g %g\n", SPLIT3(tmp));
  }
  FOR_EACH(i, 0, header.primitives_count) {
    unsigned indices[3];
    OK(senc3d_enclosure_get_triangle(enclosure, i, indices));
    fprintf(stream, "f %u %u %u\n",
      1+indices[0], 1+indices[1], 1+indices[2]);
  }

exit:
  if(enclosure) SENC3D(enclosure_ref_put(enclosure));
  if(stream) fclose(stream);
  return res;
error:
  goto exit;
}

res_T
senc3d_scene_ref_get(struct senc3d_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_get(&scn->ref);
  return RES_OK;
}

res_T
senc3d_scene_ref_put(struct senc3d_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_put(&scn->ref, scene_release);
  return RES_OK;
}
