/* Copyright (C) 2018-2025 |Méso|Star> (contact@meso-star.com)
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

#include "stardis-solid-prog.h"
#include "stardis-prog-properties.h"
#include "stardis-app.h"

#include <rsys/mem_allocator.h>

#include <sdis.h>

#include <limits.h>

/*******************************************************************************
 * Local Functions
 ******************************************************************************/

static double
solid_prog_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  const struct solid_prog* const* solid_props = sdis_data_cget(data);
  struct stardis_vertex v;
  d3_set(v.P, vtx->P);
  v.time = vtx->time;
  return (*solid_props)->lambda(&v, (*solid_props)->prog_data);
}

static double
solid_prog_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  const struct solid_prog* const* solid_props = sdis_data_cget(data);
  struct stardis_vertex v;
  d3_set(v.P, vtx->P);
  v.time = vtx->time;
  return (*solid_props)->rho(&v, (*solid_props)->prog_data);
}

static double
solid_prog_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  const struct solid_prog* const* solid_props = sdis_data_cget(data);
  struct stardis_vertex v;
  d3_set(v.P, vtx->P);
  v.time = vtx->time;
  return (*solid_props)->cp(&v, (*solid_props)->prog_data);
}

static double
solid_prog_get_delta
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  const struct solid_prog* const* solid_props = sdis_data_cget(data);
  struct stardis_vertex v;
  d3_set(v.P, vtx->P);
  v.time = vtx->time;
  return (*solid_props)->delta(&v, (*solid_props)->prog_data);
}

static double
solid_prog_get_volumic_power
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  const struct solid_prog* const* solid_props = sdis_data_cget(data);
  struct stardis_vertex v;
  double power;
  d3_set(v.P, vtx->P);
  v.time = vtx->time;
  power = (*solid_props)->vpower(&v, (*solid_props)->prog_data);
  return power != STARDIS_VOLUMIC_POWER_NONE ? power : SDIS_VOLUMIC_POWER_NONE;
}

static double
solid_prog_get_temperature
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  const struct solid_prog* const* solid_props = sdis_data_cget(data);
  struct stardis_vertex v;
  double temp;
  d3_set(v.P, vtx->P);
  v.time = vtx->time;
  temp = (*solid_props)->temp(&v, (*solid_props)->prog_data);
  return STARDIS_TEMPERATURE_IS_KNOWN(temp) ? temp : SDIS_TEMPERATURE_NONE;
}

static res_T
solid_prog_sample_path
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   struct sdis_path* path,
   struct sdis_data* data)
{
  const struct solid_prog* const* solid_props = sdis_data_cget(data);
  struct stardis_path p = STARDIS_PATH_NULL;
  int err = 0;
  res_T res = RES_OK;

  d3_set(p.vtx.P, path->vtx.P);
  p.vtx.time = path->vtx.time;

  err = (*solid_props)->sample_path(scn, rng, &p, (*solid_props)->prog_data);
  if(err) { res = RES_UNKNOWN_ERR; goto error; }

  d3_set(path->vtx.P, p.vtx.P);
  path->vtx.time = p.vtx.time;
  path->weight = p.weight;
  path->at_limit = p.at_limit;

  if(!STARDIS_TRIANGLE_NONE(&p.tri)) {
    struct sdis_primkey key = SDIS_PRIMKEY_NULL;

    sdis_primkey_setup(&key, p.tri.vtx0, p.tri.vtx1, p.tri.vtx2);
    res = sdis_scene_get_s3d_primitive(scn, &key, &path->prim_3d);

    /* In fact, we'd like to log an error notifying us that the returned
     * triangle doesn't exist in Stardis. But there's no easy way to retrieve
     * the logger since the program has no reference to Stardis. This lack stems
     * from design choices where internal data structures are not managed by
     * reference and don't take a reference on the Stardis structure. In any
     * case, until stardis' internal data structures have been thoroughly
     * redesigned, we're only returning an error */
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

res_T
create_solver_solid_prog
  (struct stardis* stardis,
   const struct solid_prog* solid_props)
{
  res_T res = RES_OK;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_data* data = NULL;
  const struct solid_prog** props;

  ASSERT(stardis && solid_props);
  solid_shader.calorific_capacity = solid_prog_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_prog_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_prog_get_volumic_mass;
  solid_shader.delta = solid_prog_get_delta;
  solid_shader.volumic_power = solid_prog_get_volumic_power;
  solid_shader.temperature = solid_prog_get_temperature;
  solid_shader.t0 = stardis->initial_time;
  if(solid_props->sample_path) {
    solid_shader.sample_path = solid_prog_sample_path;
  }
  ERR(sdis_data_create(stardis->dev, sizeof(struct solid_prog*),
    ALIGNOF(struct solid_prog*), NULL, &data));

  props = sdis_data_get(data); /* Fetch the allocated memory space */
  *props = solid_props;
  if(solid_props->solid_id >= darray_media_ptr_size_get(&stardis->media)) {
    ERR(darray_media_ptr_resize(&stardis->media, solid_props->solid_id + 1));
  }
  ASSERT(!darray_media_ptr_data_get(&stardis->media)[solid_props->solid_id]);
  ERR(sdis_solid_create(stardis->dev, &solid_shader, data,
    darray_media_ptr_data_get(&stardis->media) + solid_props->solid_id));

end:
  if(data) SDIS(data_ref_put(data));
  return res;
error:
  goto end;
}

res_T
init_solid_prog(struct mem_allocator* allocator, struct solid_prog** dst)
{
  res_T res = RES_OK;
  int str_initialized = 0;
  ASSERT(allocator && dst && *dst == NULL);
  *dst = MEM_CALLOC(allocator, 1, sizeof(**dst));
  if(! *dst) {
    res = RES_MEM_ERR;
    goto error;
  }
  str_init(allocator, &(*dst)->name);
  str_init(allocator, &(*dst)->prog_name);
  str_initialized = 1;
  (*dst)->desc_id = UINT_MAX;
  (*dst)->solid_id = UINT_MAX;
end:
  return res;
error:
  if(str_initialized) {
    str_release(&(*dst)->name);
    str_release(&(*dst)->prog_name);
  }
  if(*dst) MEM_RM(allocator, *dst);
  goto end;
}

void
release_solid_prog
  (struct solid_prog* solid,
   struct mem_allocator* allocator)
{
  size_t i;
  ASSERT(solid && allocator);
  str_release(&solid->name);
  str_release(&solid->prog_name);
  if(solid->prog_data)
    solid->release(solid->prog_data);
  for(i = 0; i < solid->argc; i++) MEM_RM(allocator, solid->argv[i]);
  MEM_RM(allocator, solid->argv);
  /* library_close call is managed at lib_data level */
  MEM_RM(allocator, solid);
}

res_T
str_print_solid_prog(struct str* str, const struct solid_prog* f)
{
  res_T res = RES_OK;
  ASSERT(str && f);
  ASSERT(f->argc >= 1); /* At least one argument which is the program name */

  ERR(str_append_printf(str,
    "programmed solid '%s': lib='%s', it is medium %u)",
    str_cget(&f->name), str_cget(&f->prog_name), f->solid_id));
  if(f->argc > 1) {
    size_t i;
    ERR(str_append_printf(str, ", provided arguments:\n"));
    for(i = 1; i < f->argc; i++) {
      ERR(str_append_printf(str, (i+1 == f->argc ? "\t%s" : "\t%s\n"), f->argv[i]));
    }
  }
end:
  return res;
error:
  goto end;
}
