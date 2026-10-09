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

#include "stardis-app.h"
#include "stardis-extern-source.h"

#include <sdis.h>
#include <rsys/cstr.h>
#include <rsys/logger.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
sphere_get_position(const double time, double pos[3], struct sdis_data* data)
{
  const struct spherical_source* src = NULL;
  ASSERT(pos);
  (void)time;

  src = *((const struct spherical_source* const*)sdis_data_cget(data));
  pos[0] = src->position[0];
  pos[1] = src->position[1];
  pos[2] = src->position[2];
}

static double
sphere_get_power(const double time, struct sdis_data* data)
{
  const struct spherical_source* src = NULL;
  (void)time;

  src = *((const struct spherical_source* const*)sdis_data_cget(data));
  return src->power; /* [W] */
}

static double
sphere_get_diffuse_radiance
  (const double time,
   const double dir[3],
   struct sdis_data* data)
{
  const struct spherical_source* src = NULL;
  (void)time, (void)dir;

  src = *((const struct spherical_source* const*)sdis_data_cget(data));
  return src->diffuse_radiance; /* [W/m^2/sr] */
}

static res_T
create_solver_source_sphere
  (struct extern_source* src,
   struct stardis* stardis)
{
  struct sdis_spherical_source_shader shader = SDIS_SPHERICAL_SOURCE_SHADER_NULL;
  struct sdis_data* data = NULL;
  res_T res = RES_OK;
  ASSERT(src && src->type == EXTERN_SOURCE_SPHERE && stardis);

  /* Register with the solver source a pointer to the external source */
  res = sdis_data_create(stardis->dev, sizeof(struct spherical_source*),
    ALIGNOF(struct spherical_source*), NULL, &data);
  if(res != RES_OK) goto error;
  *((struct spherical_source**)sdis_data_get(data)) = &src->data.sphere;

  /* Create the spherical source */
  shader.position = sphere_get_position;
  shader.power = sphere_get_power;
  shader.diffuse_radiance = sphere_get_diffuse_radiance;
  shader.radius = src->data.sphere.radius;
  res = sdis_spherical_source_create(stardis->dev, &shader, data, &src->sdis_src);
  if(res != RES_OK) goto error;

exit:
  /* Release the local reference on the data: it is kept by the sdis source */
  if(data) SDIS(data_ref_put(data));
  return res;
error:
  logger_print(stardis->logger, LOG_ERROR,
    "Error when creating the external spherical source for the solver -- %s\n",
    res_to_cstr(res));
  goto exit;
}

static void
sphere_prog_get_position(const double time, double pos[3], struct sdis_data* data)
{
  const struct spherical_source_prog* src = NULL;
  ASSERT(pos);

  src = *((const struct spherical_source_prog* const*)sdis_data_cget(data));
  src->position(time, pos, src->data);;
}

static double
sphere_prog_get_power(const double time, struct sdis_data* data)
{
  const struct spherical_source_prog* src = NULL;

  src = *((const struct spherical_source_prog* const*)sdis_data_cget(data));
  return src->power(time, src->data);
}

static double
sphere_prog_get_diffuse_radiance
  (const double time,
   const double dir[3],
   struct sdis_data* data)
{
  const struct spherical_source_prog* src = NULL;

  src = *((const struct spherical_source_prog* const*)sdis_data_cget(data));
  return src->diffuse_radiance(time, dir, src->data);
}

static res_T
create_solver_source_sphere_prog
  (struct extern_source* src,
   struct stardis* stardis)
{
  struct sdis_spherical_source_shader shader = SDIS_SPHERICAL_SOURCE_SHADER_NULL;
  struct sdis_data* data = NULL;
  res_T res = RES_OK;
  ASSERT(src && src->type == EXTERN_SOURCE_SPHERE_PROG && stardis);

  /* Register a pointer to the external source with the solver source */
  res = sdis_data_create(stardis->dev, sizeof(struct spherical_source_prog*),
    ALIGNOF(struct spherical_source_prog*), NULL, &data);
  if(res != RES_OK) goto error;
  *((struct spherical_source_prog**)sdis_data_get(data)) = &src->data.sphere_prog;

  /* Create the spherical source */
  shader.position = sphere_prog_get_position;
  shader.power = sphere_prog_get_power;
  shader.diffuse_radiance = sphere_prog_get_diffuse_radiance;
  shader.radius = src->data.sphere_prog.radius;
  res = sdis_spherical_source_create(stardis->dev, &shader, data, &src->sdis_src);
  if(res != RES_OK) goto error;

exit:
  /* Release the local reference on the data: it is kept by the sdis source */
  if(data) SDIS(data_ref_put(data));
  return res;
error:
  logger_print(stardis->logger, LOG_ERROR,
    "Error when creating the external spherical source for the solver -- %s\n",
    res_to_cstr(res));
  goto exit;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
extern_source_init_sphere
  (struct mem_allocator* allocator,
   struct extern_source* src)
{
  ASSERT(src);
  (void)allocator;
  src->type = EXTERN_SOURCE_SPHERE;
  src->data.sphere = SPHERICAL_SOURCE_NULL;
  return RES_OK;
}

res_T
extern_source_init_sphere_prog
  (struct mem_allocator* allocator,
   struct extern_source* src)
{
  ASSERT(src);
  src->type = EXTERN_SOURCE_SPHERE_PROG;
  src->data.sphere_prog = SPHERICAL_SOURCE_PROG_NULL;
  src->data.sphere_prog.allocator = allocator;
  str_init(allocator, &src->data.sphere_prog.prog_name);
  return RES_OK;
}

void
extern_source_release(struct extern_source* src)
{
  ASSERT(src);
  if(src->type == EXTERN_SOURCE_SPHERE_PROG) {
    struct spherical_source_prog* src_prog = &src->data.sphere_prog;
    size_t i;

    if(src_prog->data) {
      ASSERT(src_prog->release);
      src_prog->release(src_prog->data);
    }

    str_release(&src_prog->prog_name);
    FOR_EACH(i, 0, src_prog->argc) {
      MEM_RM(src_prog->allocator, src_prog->argv[i]);
    }

    MEM_RM(src_prog->allocator, src_prog->argv);
  }

  if(src->sdis_src) SDIS(source_ref_put(src->sdis_src));
}

res_T
extern_source_create_solver_source
  (struct extern_source* src,
   struct stardis* stardis)
{
  res_T res = RES_OK;
  ASSERT(src);

  switch(src->type) {
    case EXTERN_SOURCE_SPHERE:
      res = create_solver_source_sphere(src, stardis);
      if(res != RES_OK) goto error;
      break;
    case EXTERN_SOURCE_SPHERE_PROG:
      res = create_solver_source_sphere_prog(src, stardis);
      if(res != RES_OK) goto error;
      break;
    default: FATAL("Unreachable code\n"); break;
  }

exit:
  return res;
error:
  goto exit;
}
