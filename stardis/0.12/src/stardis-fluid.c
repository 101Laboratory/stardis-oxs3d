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

#include "stardis-fluid.h"
#include "stardis-app.h"

#include <sdis.h>

#include <rsys/mem_allocator.h>

#include <limits.h>

/*******************************************************************************
 * Local Functions
 ******************************************************************************/

static double
fluid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  const struct fluid* const* fluid_props = sdis_data_cget(data);
  (void)vtx;
  return (*fluid_props)->cp;
}

static double
fluid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  const struct fluid* const* fluid_props = sdis_data_cget(data);
  (void)vtx;
  return (*fluid_props)->rho;
}

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  const struct fluid* const* fluid_props = sdis_data_cget(data);
  if(SDIS_TEMPERATURE_IS_KNOWN((*fluid_props)->imposed_temperature))
    /* If there is an imposed temp, it is imposed regardless of time */
    return (*fluid_props)->imposed_temperature;
  if(vtx->time <= (*fluid_props)->t0) {
    /* If time is <= t0: use tinit */
    if(SDIS_TEMPERATURE_IS_UNKNOWN((*fluid_props)->tinit)) {
      if(str_is_empty(&(*fluid_props)->name)) {
        FATAL("fluid_get_temperature: getting undefined Tinit\n");
      } else {
        VFATAL("fluid_get_temperature: getting undefined Tinit (fluid '%s')\n",
          ARG1(str_cget(&(*fluid_props)->name)));
      }
    }
    return (*fluid_props)->tinit;
  }
  return SDIS_TEMPERATURE_NONE; /* Unknown temperature */
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

res_T
create_solver_fluid
  (struct stardis* stardis,
   const struct fluid* fluid_props)
{
  res_T res = RES_OK;
  struct sdis_fluid_shader fluid_shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_data* data = NULL;
  const struct fluid** props;

  ASSERT(stardis && fluid_props);
  fluid_shader.calorific_capacity = fluid_get_calorific_capacity;
  fluid_shader.volumic_mass = fluid_get_volumic_mass;
  fluid_shader.temperature = fluid_get_temperature;
  fluid_shader.t0 = stardis->initial_time;
  ERR(sdis_data_create(stardis->dev, sizeof(struct fluid*), ALIGNOF(struct fluid*),
    NULL, &data));

  props = sdis_data_get(data); /* Fetch the allocated memory space */
  *props = fluid_props;
  if(fluid_props->fluid_id >= darray_media_ptr_size_get(&stardis->media)) {
    ERR(darray_media_ptr_resize(&stardis->media, fluid_props->fluid_id + 1));
  }
  ASSERT(!darray_media_ptr_data_get(&stardis->media)[fluid_props->fluid_id]);
  ERR(sdis_fluid_create(stardis->dev, &fluid_shader, data,
    darray_media_ptr_data_get(&stardis->media) + fluid_props->fluid_id));

end:
  if(data) SDIS(data_ref_put(data));
  return res;
error:
  goto end;
}

res_T
init_fluid(struct mem_allocator* allocator, struct fluid** dst)
{
  res_T res = RES_OK;
  int str_initialized = 0;
  ASSERT(allocator && dst && *dst == NULL);
  *dst = MEM_ALLOC(allocator, sizeof(struct fluid));
  if(! *dst) {
    res = RES_MEM_ERR;
    goto error;
  }
  str_init(allocator, &(*dst)->name);
  str_initialized = 1;
  (*dst)->rho = 1;
  (*dst)->cp = 1;
  (*dst)->tinit = SDIS_TEMPERATURE_NONE;
  (*dst)->imposed_temperature = SDIS_TEMPERATURE_NONE;
  (*dst)->t0 = 0;
  (*dst)->is_outside = 0;
  (*dst)->is_green = 0;
  (*dst)->desc_id = UINT_MAX;
  (*dst)->fluid_id = UINT_MAX;
end:
  return res;
error:
  if(str_initialized) str_release(&(*dst)->name);
  if(*dst) MEM_RM(allocator, *dst);
  goto end;
}

void
release_fluid
  (struct fluid* fluid,
   struct mem_allocator* allocator)
{
  ASSERT(fluid && allocator);
  str_release(&fluid->name);
  MEM_RM(allocator, fluid);
}

res_T
str_print_fluid(struct str* str, const struct fluid* f)
{
  res_T res = RES_OK;
  ASSERT(str && f);
  ERR(str_append_printf(str, "Fluid '%s': rho=%g cp=%g",
    str_cget(&f->name), f->rho, f->cp));
  if(SDIS_TEMPERATURE_IS_KNOWN(f->tinit)) {
    ERR(str_append_printf(str, " Tinit=%g", f->tinit));
  }
  if(SDIS_TEMPERATURE_IS_KNOWN(f->imposed_temperature)) {
    ERR(str_append_printf(str, " Temp=%g", f->imposed_temperature));
  }
  ERR(str_append_printf(str, " (it is medium %u)", f->fluid_id));
end:
  return res;
error:
  goto end;
}
