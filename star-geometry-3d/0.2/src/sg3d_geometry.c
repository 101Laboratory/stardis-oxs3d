/* Copyright (C) 2019, 2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#include "sg3d.h"
#include "sg3d_geometry.h"
#include "sg3d_device.h"

#include <rsys/double3.h>

#include <limits.h>

/******************************************************************************
 * Helper functions
 *****************************************************************************/
static void
geometry_release(ref_T* ref)
{
  struct sg3d_geometry* geom;
  struct sg3d_device* dev;
  ASSERT(ref);
  geom = CONTAINER_OF(ref, struct sg3d_geometry, ref);
  dev = geom->dev;
  darray_triangle_release(&geom->unique_triangles);
  darray_vertex_release(&geom->unique_vertices);
  htable_trg_release(&geom->unique_triangles_ids);
  htable_vrtx_release(&geom->unique_vertices_ids);
  darray_trg_descriptions_release(&geom->trg_descriptions);
  MEM_RM(dev->allocator, geom);
  SG3D(device_ref_put(dev));
}

static FINLINE int /* Return 1 if reversed */
trg_make_key(struct vrtx_id3* k, const vrtx_id_t t[3])
{
  ASSERT(t);
  ASSERT(t[0] != t[1] && t[0] != t[2] && t[1] != t[2]);
  if(t[0] < t[2]) {
    if(t[0] < t[1]) {
      k->x[0] = t[0];
      if(t[1] < t[2]) {
        k->x[1] = t[1];
        k->x[2] = t[2];
        return 0;
      } else {
        k->x[1] = t[2];
        k->x[2] = t[1];
        return 1;
      }
    } else {
      k->x[0] = t[1];
      if(t[0] < t[2]) {
        k->x[1] = t[0];
        k->x[2] = t[2];
        return 1;
      } else {
        k->x[1] = t[2];
        k->x[2] = t[0];
        return 0;
      }
    }
  }
  else if(t[2] < t[1]) {
    k->x[0] = t[2];
    if(t[0] < t[1]) {
      k->x[1] = t[0];
      k->x[2] = t[1];
      return 0;
    } else {
      k->x[1] = t[1];
      k->x[2] = t[0];
      return 1;
    }
  } else {
    k->x[0] = t[1];
    if(t[0] < t[2]) {
      k->x[1] = t[0];
      k->x[2] = t[2];
      return 1;
    } else {
      k->x[1] = t[2];
      k->x[2] = t[0];
      return 0;
    }
  }
}

static void
dump_trg_property
  (const struct sg3d_geometry* geom,
   FILE* stream,
   const enum sg3d_property_type type)
{
  size_t i;
  const struct trg_descriptions* descriptions;
  ASSERT(geom && stream && type < SG3D_PROP_TYPES_COUNT__);

  descriptions
    = darray_trg_descriptions_cdata_get(&geom->trg_descriptions);
  FOR_EACH(i, 0, darray_triangle_size_get(&geom->unique_triangles)) {
    prop_id_t property = SG3D_UNSPECIFIED_PROPERTY;
    size_t tdefs_count
      = darray_definition_size_get(&descriptions[i].defs[type]);
    if(tdefs_count && descriptions[i].property_defined[type]) {
      const struct definition* tdefs
        = darray_definition_cdata_get(&descriptions[i].defs[type]);
      size_t j;
      FOR_EACH(j, 0, tdefs_count) {
        if(tdefs->property_value != SG3D_UNSPECIFIED_PROPERTY) {
          property = tdefs->property_value;
          break; /* Found the defined value */
        }
        tdefs++; /* Next value */
      }
    }
    /* In VTK dumps UINT_MAX is used for unspecified */
    if(property == SG3D_UNSPECIFIED_PROPERTY)
      fprintf(stream, "%u\n", UINT_MAX);
    else fprintf(stream, "%u\n", (unsigned)property);
  }
}

/******************************************************************************
 * Local functions
 *****************************************************************************/
res_T
geometry_register_triangle
  (struct sg3d_geometry* geom,
   const struct triangle* triangle,
   const trg_id_t triangle_unique_id,
   const unsigned set_id,
   const int merge_conflict)
{
  res_T res = RES_OK;
  struct trg_descriptions* trg_d;
  int i;
  char keep_prop_def[SG3D_PROP_TYPES_COUNT__];

  ASSERT(geom && triangle);

  ERR(geometry_enlarge_trg_descriptions(geom, triangle_unique_id + 1));
  trg_d = (darray_trg_descriptions_data_get(&geom->trg_descriptions)
    + triangle_unique_id);
  /* Record information */
  FOR_EACH(i, 0, SG3D_PROP_TYPES_COUNT__) {
    struct darray_definition* definitions;
    struct definition* defs;
    int done = 0;
    size_t j;
    keep_prop_def[i] = trg_d->property_defined[i];
    if(triangle->properties[i] == SG3D_UNSPECIFIED_PROPERTY)
      trg_d->defs_include_unspecified = 1;
    else trg_d->property_defined[i] = 1;
    definitions = trg_d->defs + i;
    defs = darray_definition_data_get(definitions);
    FOR_EACH(j, 0, darray_definition_size_get(definitions)) {
      if(defs[j].property_value == triangle->properties[i]) {
        /* This property_value is already registered: no conflict */
        const unsigned* ids = darray_uint_cdata_get(&defs[j].set_ids);
        size_t k;
        /* Search if property_value already includes set_id */
        FOR_EACH(k, 0, darray_uint_size_get(&defs[j].set_ids)) {
          if(ids[k] == set_id) {
            /* Same value+set_id was there already */
            done = 1;
            break;
          }
        }
        if(!done) {
          /* Need to add the set_id for this property_value */
          ERR(darray_uint_push_back(&defs[j].set_ids, &set_id));
          done = 1;
        }
        break;
      }
    }
    if(!done) {
      /* This property_value was not recorded already */
      size_t defs_sz = darray_definition_size_get(definitions);
      struct definition* new_def;
      ERR(darray_definition_resize(definitions, 1 + defs_sz));
      new_def = darray_definition_data_get(definitions) + defs_sz;
      ERR(darray_uint_push_back(&new_def->set_ids, &set_id));
      new_def->property_value = triangle->properties[i];
      if(!trg_d->merge_conflict && merge_conflict) {
        /* If more than 1 merge_conflict occur, the first one remains */
        trg_d->merge_conflict = merge_conflict;
        geom->merge_conflict_count++;
      }
    }
  }

  if((!keep_prop_def[SG3D_FRONT] || !keep_prop_def[SG3D_BACK])
    && trg_d->property_defined[SG3D_FRONT] && trg_d->property_defined[SG3D_BACK])
  {
    /* Both sides are now defined */
    ASSERT(geom->trg_with_unspecified_sides_count > 0);
    geom->trg_with_unspecified_sides_count--;
  }

  if(!keep_prop_def[SG3D_INTFACE] && trg_d->property_defined[SG3D_INTFACE]) {
    /* Interface is now defined */
    ASSERT(geom->trg_with_unspecified_intface_count > 0);
    geom->trg_with_unspecified_intface_count--;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
geometry_enlarge_trg_descriptions
  (struct sg3d_geometry* geom,
   const trg_id_t sz)
{
  res_T res = RES_OK;
  size_t old_sz =
    darray_trg_descriptions_size_get(&geom->trg_descriptions);
  if(sz <= old_sz) return RES_OK;
  ERR(darray_trg_descriptions_resize(&geom->trg_descriptions, sz));
  ASSERT(geom->trg_with_unspecified_sides_count + sz - old_sz <= TRG_MAX__);
  ASSERT(geom->trg_with_unspecified_intface_count + sz - old_sz <= TRG_MAX__);
  geom->trg_with_unspecified_sides_count += (trg_id_t)(sz - old_sz);
  geom->trg_with_unspecified_intface_count += (trg_id_t)(sz - old_sz);

exit:
  return res;
error:
  goto exit;
}

static void
dump_partition
  (const struct sg3d_geometry* geom,
   FILE* stream,
   const char* group_name,
   enum sg3d_obj_dump_content partition)
{
  const struct trg_descriptions* trg_descriptions;
  const struct triangle* triangles;
  size_t sz, i;
  ASSERT(geom && stream && group_name);
  ASSERT(partition == SG3D_OBJ_DUMP_MERGE_CONFLICTS
    || partition == SG3D_OBJ_DUMP_PROPERTY_CONFLICTS
    || partition == SG3D_OBJ_DUMP_VALID_PRIMITIVE);
  trg_descriptions
    = darray_trg_descriptions_cdata_get(&geom->trg_descriptions);
  sz = darray_trg_descriptions_size_get(&geom->trg_descriptions);
  triangles = darray_triangle_cdata_get(&geom->unique_triangles);
  fprintf(stream, "g %s\n", group_name);
  FOR_EACH(i, 0, sz) {
    int dump;
    if(partition == SG3D_OBJ_DUMP_VALID_PRIMITIVE)
      dump = !(trg_descriptions[i].merge_conflict
        || trg_descriptions[i].properties_conflict);
    else if(partition == SG3D_OBJ_DUMP_MERGE_CONFLICTS)
      dump = trg_descriptions[i].merge_conflict;
    else {
      ASSERT(partition == SG3D_OBJ_DUMP_PROPERTY_CONFLICTS);
      dump = trg_descriptions[i].properties_conflict;
    }
    if(!dump) continue;
    fprintf(stream, "f "PRTF_VRTX" "PRTF_VRTX" "PRTF_VRTX"\n",
      /* OBJ indexing starts at 1 */
      1 + triangles[i].vertex_ids[0],
      1 + triangles[i].vertex_ids[1],
      1 + triangles[i].vertex_ids[2]);
  }
}

/******************************************************************************
 * Exported functions
 *****************************************************************************/
res_T
sg3d_geometry_create
  (struct sg3d_device* dev,
   struct sg3d_geometry** out_geometry)
{
  struct sg3d_geometry* geom = NULL;
  res_T res = RES_OK;

  if(!dev || !out_geometry) {
    res = RES_BAD_ARG;
    goto error;
  }

  geom = MEM_CALLOC(dev->allocator, 1, sizeof(struct sg3d_geometry));
  if(!geom) {
    log_err(dev,
        LIB_NAME":%s: could not allocate the sg3d.\n", FUNC_NAME);
    res = RES_MEM_ERR;
    goto error;
  }

  SG3D(device_ref_get(dev));
  darray_triangle_init(dev->allocator, &geom->unique_triangles);
  darray_vertex_init(dev->allocator, &geom->unique_vertices);
  htable_trg_init(dev->allocator, &geom->unique_triangles_ids);
  htable_vrtx_init(dev->allocator, &geom->unique_vertices_ids);
  darray_trg_descriptions_init(dev->allocator, &geom->trg_descriptions);
  geom->triangle_count_including_duplicates = 0;
  geom->sides_with_defined_medium_count = 0;
  geom->set_id = 0;
  geom->trg_with_unspecified_sides_count = 0;
  geom->trg_with_unspecified_intface_count = 0;
  geom->merge_conflict_count = 0;
  geom->properties_conflict_count = 0;
  geom->dev = dev;

  ref_init(&geom->ref);

exit:
  if(out_geometry) *out_geometry = geom;
  return res;
error:
  if(geom) {
    SG3D(geometry_ref_put(geom));
    geom = NULL;
  }
  goto exit;
}

res_T
sg3d_geometry_reserve
  (struct sg3d_geometry* geom,
   const vrtx_id_t vertices_count,
   const trg_id_t triangles_count,
   const prop_id_t properties_count)
{
  res_T res = RES_OK;
  if(!geom) return RES_BAD_ARG;

  ERR(darray_triangle_reserve(&geom->unique_triangles, triangles_count));
  ERR(darray_vertex_reserve(&geom->unique_vertices, vertices_count));
  ERR(htable_vrtx_reserve(&geom->unique_vertices_ids, vertices_count));
  ERR(htable_trg_reserve(&geom->unique_triangles_ids, triangles_count));
  ERR(darray_trg_descriptions_reserve(&geom->trg_descriptions,
    properties_count));

end:
  return res;
error:
  goto end;
}

res_T
sg3d_geometry_add
  (struct sg3d_geometry* geom,
   const vrtx_id_t nverts,
   const trg_id_t ntris,
   const struct sg3d_geometry_add_callbacks* callbacks,
   void* ctx) /* Can be NULL */
{
  res_T res = RES_OK;
  struct mem_allocator* alloc;
  size_t nutris, nuverts;
  vrtx_id_t nv, n_new_uverts = 0;
  trg_id_t nt, n_new_utris = 0;
  struct triangle* trg;
  /* Tmp table of IDs to record unique IDs of the currently added vertices */
  struct darray_vertice_ids unique_vertice_ids;
  int unique_vertice_ids_initialized = 0;
  get_indices_t get_ind;
  get_properties_t get_prop;
  get_position_t get_pos;
  add_triangle_t add_trg;
  merge_triangle_t mrg_trg;
  degenerated_triangle_t dege_trg;

  if(!geom || !callbacks || !callbacks->get_indices || !callbacks->get_position)
  {
    res = RES_BAD_ARG;
    goto error;
  }

  get_ind = callbacks->get_indices;
  get_prop = callbacks->get_properties;
  get_pos = callbacks->get_position;
  add_trg = callbacks->add_triangle;
  mrg_trg = callbacks->merge_triangle;
  dege_trg = callbacks->degenerated_triangle;
  alloc = geom->dev->allocator;
  nuverts = darray_vertex_size_get(&geom->unique_vertices);
  nutris = darray_triangle_size_get(&geom->unique_triangles);

  /* Make room for new geometry; suppose no more duplicates */
  darray_vertice_ids_init(alloc, &unique_vertice_ids);
  unique_vertice_ids_initialized = 1;
  ERR(darray_vertice_ids_reserve(&unique_vertice_ids, nverts));
  ERR(darray_vertex_reserve(&geom->unique_vertices, nuverts + nverts));
  ERR(darray_triangle_reserve(&geom->unique_triangles, nutris + ntris));
  ERR(htable_vrtx_reserve(&geom->unique_vertices_ids, nuverts + nverts));
  ERR(htable_trg_reserve(&geom->unique_triangles_ids, nutris + ntris));
  ASSERT(nutris == darray_trg_descriptions_size_get(&geom->trg_descriptions));
  ERR(darray_trg_descriptions_reserve(&geom->trg_descriptions, nutris + ntris));
  /* Get vertices and deduplicate */
  FOR_EACH(nv, 0, nverts) {
    vrtx_id_t* p_vrtx;
    struct vertex tmp;
    vrtx_id_t v_idx;
    get_pos(nv, tmp.coord, ctx);
    p_vrtx = htable_vrtx_find(&geom->unique_vertices_ids, &tmp);
    if(p_vrtx) {
      /* Duplicate vertex */
      v_idx = *p_vrtx;
    } else {
      /* New vertex */
      ASSERT(nuverts + n_new_uverts <= VRTX_MAX__);
      v_idx = (vrtx_id_t)(nuverts + n_new_uverts);
      ASSERT(v_idx == htable_vrtx_size_get(&geom->unique_vertices_ids));
      ERR(darray_vertex_push_back(&geom->unique_vertices, &tmp));
      ERR(htable_vrtx_set(&geom->unique_vertices_ids, &tmp, &v_idx));
      ++n_new_uverts;
    }
    /* Keep the unique ID for vertex nv */
    ERR(darray_vertice_ids_push_back(&unique_vertice_ids, &v_idx));
  }

  /* Get triangles and deduplicate */
  trg = darray_triangle_data_get(&geom->unique_triangles);
  FOR_EACH(nt, 0, ntris) {
    int j, reversed, dg;
    struct vrtx_id3 trg_key;
    struct triangle tmp = TRG_UNDEF__;
    trg_id_t* p_trg;
    struct trg_descriptions* trg_descriptions = NULL;
    trg_id_t unique_id;

    get_ind(nt, tmp.vertex_ids, ctx);
    FOR_EACH(j, 0, 3) {
      if(tmp.vertex_ids[j] >= nverts) {
        res = RES_BAD_ARG;
        goto error;
      }
      /* Replace the vertex ID by its the unique ID */
      tmp.vertex_ids[j]
        = darray_vertice_ids_cdata_get(&unique_vertice_ids)[tmp.vertex_ids[j]];
    }
    dg = (tmp.vertex_ids[0] == tmp.vertex_ids[1]
      || tmp.vertex_ids[0] == tmp.vertex_ids[2]
      || tmp.vertex_ids[1] == tmp.vertex_ids[2]);
    if(!dg) {
      double edge1[3], edge2[3], n[3];
      const struct vertex* vertices = darray_vertex_cdata_get(&geom->unique_vertices);
      d3_sub(edge1, vertices[tmp.vertex_ids[1]].coord, vertices[tmp.vertex_ids[0]].coord);
      d3_sub(edge2, vertices[tmp.vertex_ids[2]].coord, vertices[tmp.vertex_ids[0]].coord);
      d3_cross(n, edge1, edge2);
      dg = d3_len(n) == 0;
    }
    if(dg) {
      int abort = 0;
      if(dege_trg) {
        /* Let the client app rule. */
        ERR(dege_trg(nt, ctx, &abort));
      } else {
        log_warn(geom->dev,
          LIB_NAME":%s: triangle "PRTF_TRG" is degenerated (removed).\n",
          FUNC_NAME, nt);
      }
      if(abort) {
        res = RES_BAD_ARG;
        goto error;
      }
      else continue;
    }
    /* Get properties */
    if(get_prop) get_prop(nt, tmp.properties, ctx);
    /* Find duplicate triangles */
    reversed = trg_make_key(&trg_key, tmp.vertex_ids);
    p_trg = htable_trg_find(&geom->unique_triangles_ids, &trg_key);
    if(p_trg) {
      /* Duplicate triangle. Need to check duplicate validity */
      trg_id_t trg_id = *p_trg;
      struct vrtx_id3 utrg_key;
      int ureversed = trg_make_key(&utrg_key, trg[trg_id].vertex_ids);
      int same = (reversed == ureversed);
      int already_conflict;
      ASSERT(trg_key_eq(&trg_key, &utrg_key));
      unique_id = trg_id;
      ERR(geometry_enlarge_trg_descriptions(geom, 1 + trg_id));
      trg_descriptions
        = darray_trg_descriptions_data_get(&geom->trg_descriptions);
      if(!same)
        SWAP(prop_id_t, tmp.properties[SG3D_FRONT], tmp.properties[SG3D_BACK]);
      already_conflict = trg_descriptions[trg_id].merge_conflict;
      if(mrg_trg) {
        /* Let the client app rule. */
        ERR(mrg_trg(trg_id, nt, !same, trg[trg_id].properties,
          tmp.properties, ctx, &trg_descriptions[trg_id].merge_conflict));
      } else {
        FOR_EACH(j, 0, SG3D_PROP_TYPES_COUNT__) {
          if(!sg3d_compatible_property(trg[trg_id].properties[j],
            tmp.properties[j]))
          {
            trg_descriptions[trg_id].merge_conflict = 1;
            break;
          }
        }
      }
      if(trg_descriptions[trg_id].merge_conflict && !already_conflict)
        geom->merge_conflict_count++;
      /* Replace SG3D_UNSPECIFIED_PROPERTY properties */
      FOR_EACH(j, 0, SG3D_PROP_TYPES_COUNT__) {
        if(trg[trg_id].properties[j] == SG3D_UNSPECIFIED_PROPERTY
          && tmp.properties[j] != SG3D_UNSPECIFIED_PROPERTY) {
          trg[trg_id].properties[j] = tmp.properties[j];
          if(j == SG3D_FRONT || j == SG3D_BACK)
            geom->sides_with_defined_medium_count++;
        }
      }
    } else {
      /* New triangle */
      trg_id_t new_id = (trg_id_t)(nutris + n_new_utris);
      ASSERT(new_id <= TRG_MAX__);
      unique_id = new_id;
      tmp.user_id = geom->triangle_count_including_duplicates + nt;
      if(add_trg) ERR(add_trg(new_id, nt, ctx));
      ERR(geometry_enlarge_trg_descriptions(geom, 1 + new_id));
      trg_descriptions
        = darray_trg_descriptions_data_get(&geom->trg_descriptions);
      ERR(darray_triangle_push_back(&geom->unique_triangles, &tmp));
      trg = darray_triangle_data_get(&geom->unique_triangles);
      FOR_EACH(j, 0, SG3D_PROP_TYPES_COUNT__) {
        if((j == SG3D_FRONT || j == SG3D_BACK)
          && tmp.properties[j] != SG3D_UNSPECIFIED_PROPERTY)
          geom->sides_with_defined_medium_count++;
      }
      ASSERT(new_id == htable_trg_size_get(&geom->unique_triangles_ids));
      ERR(htable_trg_set(&geom->unique_triangles_ids, &trg_key, &new_id));
      n_new_utris++;
    }
    ERR(geometry_register_triangle(geom, &tmp, unique_id, geom->set_id,
      trg_descriptions[unique_id].properties_conflict));
    if(trg_descriptions[unique_id].properties_conflict)
      geom->merge_conflict_count++;
  }

  ASSERT(nuverts + n_new_uverts
    == htable_vrtx_size_get(&geom->unique_vertices_ids));
  ASSERT(nutris + n_new_utris
    == htable_trg_size_get(&geom->unique_triangles_ids));
exit:
  if(geom) {
    geom->set_id++;
    geom->triangle_count_including_duplicates += ntris;
  }
  if(unique_vertice_ids_initialized)
    darray_vertice_ids_release(&unique_vertice_ids);
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_validate_properties
  (struct sg3d_geometry* geom,
   res_T(*validate)(const trg_id_t, const prop_id_t*, void*, int*),
   void* ctx)
{
  size_t sz__;
  trg_id_t i, sz;
  struct trg_descriptions* trg_descriptions;
  res_T res = RES_OK;

  if(!geom || !validate) {
    res = RES_BAD_ARG;
    goto error;
  }

  sz__ = darray_trg_descriptions_size_get(&geom->trg_descriptions);
  ASSERT(sz__ <= PROP_MAX__);
  sz = (unsigned)sz__;
  trg_descriptions
    = darray_trg_descriptions_data_get(&geom->trg_descriptions);
  geom->properties_conflict_count = 0; /* Reset count */
  FOR_EACH(i, 0, sz) {
    int p;
    prop_id_t props[SG3D_PROP_TYPES_COUNT__];
    struct trg_descriptions* trgd = trg_descriptions + i;
    /* Validate only triangle not flagged with merge_conflict */
    if(trgd->merge_conflict) {
      trgd->properties_conflict = 0;
      continue;
    }
    /* Get properties for non-conflict triangles */
    FOR_EACH(p, 0, SG3D_PROP_TYPES_COUNT__) {
      size_t j;
      const struct definition* defs = darray_definition_cdata_get(trgd->defs + p);
      props[p] = SG3D_UNSPECIFIED_PROPERTY;
      FOR_EACH(j, 0, darray_definition_size_get(trgd->defs + p)) {
        if(defs[j].property_value != SG3D_UNSPECIFIED_PROPERTY) {
          props[p] = defs[j].property_value;
          break;
        }
      }
    }
    /* Call validation */
    ERR(validate(i, props, ctx, &trgd->properties_conflict));
    if(trgd->properties_conflict)
      geom->properties_conflict_count++;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_get_unique_vertices_count
  (const struct sg3d_geometry* geom,
   vrtx_id_t* count)
{
  res_T res = RES_OK;
  size_t sz;
  if(!geom || !count) {
    res = RES_BAD_ARG;
    goto error;
  }
  sz = darray_vertex_size_get(&geom->unique_vertices);
  ASSERT(sz <= VRTX_MAX__);
  *count = (vrtx_id_t)sz;
exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_get_unique_vertex
  (const struct sg3d_geometry* geom,
   const vrtx_id_t ivtx,
   double coord[SG3D_GEOMETRY_DIMENSION])
{
  res_T res = RES_OK;
  const struct vertex* vertices;
  if(!geom || !coord
    || ivtx >= darray_vertex_size_get(&geom->unique_vertices))
  {
    res = RES_BAD_ARG;
    goto error;
  }
  vertices = darray_vertex_cdata_get(&geom->unique_vertices);
  d3_set(coord, vertices[ivtx].coord);
exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_get_added_triangles_count
  (const struct sg3d_geometry* geom,
   trg_id_t* count)
{
  res_T res = RES_OK;
  if(!geom || !count) {
    res = RES_BAD_ARG;
    goto error;
  }
  *count = geom->triangle_count_including_duplicates;
exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_get_unique_triangles_count
  (const struct sg3d_geometry* geom,
   trg_id_t* count)
{
  res_T res = RES_OK;
  size_t sz;
  if(!geom || !count) {
    res = RES_BAD_ARG;
    goto error;
  }
  sz = darray_triangle_size_get(&geom->unique_triangles);
  ASSERT(sz <= TRG_MAX__);
  *count = (unsigned)sz;
exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_get_unique_triangle_vertices
  (const struct sg3d_geometry* geom,
   const trg_id_t itri,
   vrtx_id_t indices[SG3D_GEOMETRY_DIMENSION])
{
  res_T res = RES_OK;
  const struct triangle* triangles;
  size_t i;
  if(!geom || !indices
    || itri >= darray_triangle_size_get(&geom->unique_triangles))
  {
    res = RES_BAD_ARG;
    goto error;
  }
  triangles = darray_triangle_cdata_get(&geom->unique_triangles);
  FOR_EACH(i, 0, SG3D_GEOMETRY_DIMENSION)
    indices[i] = triangles[itri].vertex_ids[i];
exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_get_unique_triangle_properties
  (const struct sg3d_geometry* geom,
   const trg_id_t itri,
   prop_id_t properties[SG3D_PROP_TYPES_COUNT__])
{
  res_T res = RES_OK;
  const struct triangle* triangles;
  size_t i;
  if(!geom || !properties
    || itri >= darray_triangle_size_get(&geom->unique_triangles))
  {
    res = RES_BAD_ARG;
    goto error;
  }
  triangles = darray_triangle_cdata_get(&geom->unique_triangles);
  FOR_EACH(i, 0, SG3D_PROP_TYPES_COUNT__)
    properties[i] = triangles[itri].properties[i];
exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_get_unique_triangle_user_id
  (const struct sg3d_geometry* geom,
   const trg_id_t itri,
   trg_id_t* user_id)
{
  res_T res = RES_OK;
  const struct triangle* triangles;
  if(!geom || !user_id
    || itri >= darray_triangle_size_get(&geom->unique_triangles))
  {
    res = RES_BAD_ARG;
    goto error;
  }
  triangles = darray_triangle_cdata_get(&geom->unique_triangles);
  *user_id = triangles[itri].user_id;
exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_get_unique_triangles_with_unspecified_side_count
  (const struct sg3d_geometry* geom,
   trg_id_t* count)
{
  res_T res = RES_OK;
  if(!geom || !count) {
    res = RES_BAD_ARG;
    goto error;
  }
  *count = geom->trg_with_unspecified_sides_count;
exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_get_unique_triangles_with_unspecified_interface_count
  (const struct sg3d_geometry* geom,
   trg_id_t* count)
{
  res_T res = RES_OK;
  if(!geom || !count) {
    res = RES_BAD_ARG;
    goto error;
  }
  *count = geom->trg_with_unspecified_intface_count;
exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_get_unique_triangles_with_merge_conflict_count
  (const struct sg3d_geometry* geom,
   trg_id_t* count)
{
  res_T res = RES_OK;
  if(!geom || !count) {
    res = RES_BAD_ARG;
    goto error;
  }
  *count = geom->merge_conflict_count;
exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_get_unique_triangles_with_properties_conflict_count
  (const struct sg3d_geometry* geom,
   trg_id_t* count)
{
  res_T res = RES_OK;
  if(!geom || !count) {
    res = RES_BAD_ARG;
    goto error;
  }
  *count = geom->properties_conflict_count;
exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_dump_as_obj
  (const struct sg3d_geometry* geom,
   FILE* stream,
   int flags)
{
  res_T res = RES_OK;
  const struct vertex* vertices;
  size_t vsz, tsz, i;
  if(!geom || !stream || !flags
    || !geom->triangle_count_including_duplicates)
  {
    if(geom && !geom->triangle_count_including_duplicates)
      log_err(geom->dev,
        LIB_NAME":%s: cannot dump empty geometries as OBJ\n",
        FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }
  /* Headers */
  fprintf(stream, "# Dump of star-geometry-3d\n");
  fprintf(stream, "# Geometry counts:\n");
  vsz = darray_vertex_size_get(&geom->unique_vertices);
  ASSERT(vsz <= VRTX_MAX__);
  fprintf(stream, "# . "PRTF_VRTX" vertices\n", (vrtx_id_t)vsz);
  tsz = darray_triangle_size_get(&geom->unique_triangles);
  ASSERT(tsz <= TRG_MAX__);
  fprintf(stream, "# . "PRTF_TRG" triangles\n", (trg_id_t)tsz);
  fprintf(stream,
    "# . "PRTF_TRG" triangles flagged with a merge conflict\n",
    geom->merge_conflict_count);
  fprintf(stream,
    "# . "PRTF_TRG" triangles flagged with a property conflict\n",
    geom->merge_conflict_count);

  /* Dump vertices */
  vertices = darray_vertex_cdata_get(&geom->unique_vertices);
  FOR_EACH(i, 0, vsz)
    fprintf(stream, "v %.16g %.16g %.16g\n", SPLIT3(vertices[i].coord));

  /* Dump triangles by groups */
  if(flags & SG3D_OBJ_DUMP_VALID_PRIMITIVE) {
    dump_partition(geom, stream,
        "Valid_triangles", SG3D_OBJ_DUMP_VALID_PRIMITIVE);
  }
  if(flags & SG3D_OBJ_DUMP_MERGE_CONFLICTS) {
    dump_partition(geom, stream,
        "Merge_conflicts", SG3D_OBJ_DUMP_MERGE_CONFLICTS);
  }
  if(flags & SG3D_OBJ_DUMP_PROPERTY_CONFLICTS) {
    dump_partition(geom, stream,
        "Property_conflicts", SG3D_OBJ_DUMP_PROPERTY_CONFLICTS);
  }

exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_dump_as_vtk
  (const struct sg3d_geometry* geom,
   FILE* stream)
{
  res_T res = RES_OK;
  const struct vertex* vertices;
  const struct triangle* triangles;
  const struct trg_descriptions* descriptions;
  size_t vsz, tsz, i;
  if(!geom || !stream || !geom->triangle_count_including_duplicates) {
    if(geom && !geom->triangle_count_including_duplicates)
      log_err(geom->dev,
        LIB_NAME":%s: cannot dump empty geometries as VTK\n",
        FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }
  /* Headers */
  fprintf(stream, "# vtk DataFile Version 2.0\n");
  fprintf(stream, "Dump of star-geometry-3d geometry\n");
  fprintf(stream, "ASCII\n");
  fprintf(stream, "DATASET POLYDATA\n");

  /* Dump vertices */
  vsz = darray_vertex_size_get(&geom->unique_vertices);
  ASSERT(vsz <= VRTX_MAX__);
  fprintf(stream, "POINTS "PRTF_VRTX" double\n", (vrtx_id_t)vsz);
  vertices = darray_vertex_cdata_get(&geom->unique_vertices);
  FOR_EACH(i, 0, vsz)
    fprintf(stream, "%.16g %.16g %.16g\n", SPLIT3(vertices[i].coord));

  /* Dump triangles */
  tsz = darray_triangle_size_get(&geom->unique_triangles);
  ASSERT(4 * tsz <= TRG_MAX__);
  fprintf(stream, "POLYGONS "PRTF_TRG" "PRTF_TRG"\n",
    (trg_id_t)tsz, (trg_id_t)(4 * tsz));
  triangles = darray_triangle_cdata_get(&geom->unique_triangles);
  FOR_EACH(i, 0, tsz)
    fprintf(stream, "3 "PRTF_VRTX" "PRTF_VRTX" "PRTF_VRTX"\n",
      SPLIT3(triangles[i].vertex_ids));

  /* Start triangles properties */
  fprintf(stream, "CELL_DATA "PRTF_TRG"\n", (trg_id_t)tsz);
  descriptions = darray_trg_descriptions_cdata_get(&geom->trg_descriptions);

  /* Dump front medium */
  fprintf(stream, "SCALARS Front_medium unsigned_int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  dump_trg_property(geom, stream, SG3D_FRONT);

  /* Dump back medium */
  fprintf(stream, "SCALARS Back_medium unsigned_int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  dump_trg_property(geom, stream, SG3D_BACK);

  /* Dump interface */
  fprintf(stream, "SCALARS Interface unsigned_int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  dump_trg_property(geom, stream, SG3D_INTFACE);

  /* Dump unique_id */
  fprintf(stream, "SCALARS Unique_ID unsigned_int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(i, 0, tsz) fprintf(stream, PRTF_TRG"\n", (trg_id_t)i);

  /* Dump user_id */
  fprintf(stream, "SCALARS User_ID unsigned_int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(i, 0, tsz) fprintf(stream, PRTF_TRG"\n", triangles[i].user_id);

  /* Dump merge conflict status (if any) */
  if(geom->merge_conflict_count) {
    fprintf(stream, "SCALARS Merge_conflict int 1\n");
    fprintf(stream, "LOOKUP_TABLE default\n");
    FOR_EACH(i, 0, tsz)
      fprintf(stream, "%d\n", descriptions[i].merge_conflict);
  }

  /* Dump property conflict status (if any) */
  if(geom->properties_conflict_count) {
    fprintf(stream, "SCALARS Property_conflict int 1\n");
    fprintf(stream, "LOOKUP_TABLE default\n");
    FOR_EACH(i, 0, tsz)
      fprintf(stream, "%d\n", descriptions[i].properties_conflict);
  }

  /* Dump rank of the sg3d_geometry_add that created the triangle */
  fprintf(stream, "SCALARS Created_at_sg3d_geometry_add unsigned_int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");
  FOR_EACH(i, 0, tsz) {
    const struct definition* tdefs;
    const unsigned* ranks;
    ASSERT(darray_definition_size_get(&descriptions[i].defs[SG3D_FRONT]) > 0);
    /* Rank is the first set_id of the first definition of any property */
    tdefs = darray_definition_cdata_get(&descriptions[i].defs[SG3D_FRONT]);
    ranks = darray_uint_cdata_get(&tdefs[0].set_ids);
    fprintf(stream, "%u\n", ranks[0]);
  }

exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_dump_as_c_code
  (const struct sg3d_geometry* geom,
   FILE* stream,
   const char* name_prefix,
   const int flags)
{
  res_T res = RES_OK;
  const struct vertex* vertices;
  const struct triangle* triangles;
  const char* qualifiers;
  size_t vsz, tsz, i;
  if(!geom || !stream
    || geom->merge_conflict_count
    || geom->properties_conflict_count
    || !geom->triangle_count_including_duplicates)
  {
    if(geom
      && (geom->merge_conflict_count
        || geom->properties_conflict_count))
      log_err(geom->dev,
        LIB_NAME":%s: cannot dump geometries with conflict as C code\n",
        FUNC_NAME);
    if(geom && !geom->triangle_count_including_duplicates)
      log_err(geom->dev,
        LIB_NAME":%s: cannot dump empty geometries as C code\n",
        FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }
  if(!name_prefix) name_prefix = "";
  /* Headers */
  if(name_prefix && name_prefix[0] != '\0')
    fprintf(stream, "/* Dump of star-geometry-3d '%s'. */\n", name_prefix);
  else
    fprintf(stream, "/* Dump of star-geometry-3d. */\n");
  vsz = darray_vertex_size_get(&geom->unique_vertices);
  ASSERT(3 * vsz <= VRTX_MAX__);
  tsz = darray_triangle_size_get(&geom->unique_triangles);
  ASSERT(3 * tsz <= TRG_MAX__);

  if(vsz == 0 || tsz == 0) {
    log_err(geom->dev,
      "%s: no geometry to dump\n",
      FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  if(flags & SG3D_C_DUMP_CONST && flags & SG3D_C_DUMP_STATIC)
    qualifiers = "static const ";
  else if(flags & SG3D_C_DUMP_CONST)
    qualifiers = "const ";
  else if(flags & SG3D_C_DUMP_STATIC)
    qualifiers = "static ";
  else qualifiers = "";

  /* Dump vertices */
  fprintf(stream, "%s"VRTX_TYPE_NAME" %s_vertices_count = "PRTF_VRTX";\n",
    qualifiers, name_prefix, (vrtx_id_t)vsz);

  vertices = darray_vertex_cdata_get(&geom->unique_vertices);
  fprintf(stream,
    "%sdouble %s_vertices["PRTF_VRTX"] =\n"
    "{\n",
    qualifiers, name_prefix, (vrtx_id_t)(3 * vsz));
  FOR_EACH(i, 0, vsz - 1)
    fprintf(stream,
      "   %.16g, %.16g, %.16g,\n", SPLIT3(vertices[i].coord));
  fprintf(stream,
    "   %.16g, %.16g, %.16g\n", SPLIT3(vertices[vsz - 1].coord));
  fprintf(stream,
    "};\n");

  /* Dump triangles */
  fprintf(stream, "%s"TRG_TYPE_NAME" %s_triangles_count = "PRTF_TRG";\n",
    qualifiers, name_prefix, (trg_id_t)tsz);

  triangles = darray_triangle_cdata_get(&geom->unique_triangles);
  fprintf(stream,
    "%s"TRG_TYPE_NAME" %s_triangles["PRTF_TRG"] =\n"
    "{\n",
    qualifiers, name_prefix, (trg_id_t)(3 * tsz));
  FOR_EACH(i, 0, tsz - 1)
    fprintf(stream,
      "   "PRTF_VRTX", "PRTF_VRTX", "PRTF_VRTX",\n",
      SPLIT3(triangles[i].vertex_ids));
  fprintf(stream,
    "   "PRTF_VRTX", "PRTF_VRTX", "PRTF_VRTX"\n",
    SPLIT3(triangles[tsz - 1].vertex_ids));
  fprintf(stream,
    "};\n");

  /* Dump properties */
  fprintf(stream,
    "%s"PROP_TYPE_NAME" %s_properties["PRTF_PROP"] =\n"
    "{\n",
    qualifiers, name_prefix, (prop_id_t)(SG3D_PROP_TYPES_COUNT__ * tsz));
  FOR_EACH(i, 0, tsz) {
    int p;
    fprintf(stream, "  ");
    FOR_EACH(p, 0, SG3D_PROP_TYPES_COUNT__) {
      if(triangles[i].properties[p] == SG3D_UNSPECIFIED_PROPERTY)
        fprintf(stream, " SG3D_UNSPECIFIED_PROPERTY");
      else fprintf(stream," "PRTF_PROP"", triangles[i].properties[p]);
      if(i < tsz-1 || p < 2) fprintf(stream, ",");
      if(p == 2) fprintf(stream, "\n");
    }
  }
  fprintf(stream,
    "};\n");

exit:
  return res;
error:
  goto exit;
}

res_T
sg3d_geometry_ref_get(struct sg3d_geometry* geom)
{
  if(!geom) return RES_BAD_ARG;
  ref_get(&geom->ref);
  return RES_OK;
}

res_T
sg3d_geometry_ref_put(struct sg3d_geometry* geom)
{
  if(!geom) return RES_BAD_ARG;
  ref_put(&geom->ref, geometry_release);
  return RES_OK;
}
