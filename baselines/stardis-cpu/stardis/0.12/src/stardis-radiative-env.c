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
#include "stardis-radiative-env.h"

#include <sdis.h>
#include <rsys/cstr.h>
#include <rsys/logger.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static double
radenv_get_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  const struct radiative_env_const* props = sdis_data_cget(data);
  (void)ray;
  return props->temperature;
}

static double
radenv_get_reference_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  const struct radiative_env_const* props = sdis_data_cget(data);
  (void)ray;
  return props->reference_temperature;
}

static res_T
create_radenv_const
  (struct radiative_env* radenv,
   struct stardis* stardis)
{
  struct sdis_radiative_env_shader shader = SDIS_RADIATIVE_ENV_SHADER_NULL;
  struct sdis_data* data = NULL;
  struct radiative_env_const* props = NULL;
  res_T res = RES_OK;
  ASSERT(radenv && radenv->type == RADIATIVE_ENV_CONST && stardis);

  res = sdis_data_create(stardis->dev, sizeof(struct radiative_env_const),
    ALIGNOF(struct radiative_env_const), NULL, &data);
  if(res != RES_OK) goto error;
  props = sdis_data_get(data);
  *props = radenv->data.cst;

  shader.temperature = radenv_get_temperature;
  shader.reference_temperature = radenv_get_reference_temperature;
  res = sdis_radiative_env_create
    (stardis->dev, &shader, data, &radenv->sdis_radenv);
  if(res != RES_OK) goto error;

exit:
  /* Release the local reference on the data: it is kept by the sdis radenv */
  if(data) SDIS(data_ref_put(data));
  return res;
error:
  logger_print(stardis->logger, LOG_ERROR,
    "Error when creating the radiative environment for the solver -- %s\n",
    res_to_cstr(res));
  goto exit;
}

static double
radenv_prog_get_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  const struct radiative_env_prog* radenv = NULL;

  radenv = *((const struct radiative_env_prog* const*)sdis_data_cget(data));
  return radenv->temperature(ray->time, ray->dir, radenv->data);
}

static double
radenv_prog_get_reference_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  const struct radiative_env_prog* radenv = NULL;

  radenv = *((const struct radiative_env_prog* const*)sdis_data_cget(data));
  return radenv->reference_temperature(ray->time, ray->dir, radenv->data);
}

static res_T
create_radenv_prog
  (struct radiative_env* radenv,
   struct stardis* stardis)
{
  struct sdis_radiative_env_shader shader = SDIS_RADIATIVE_ENV_SHADER_NULL;
  struct sdis_data* data = NULL;
  res_T res = RES_OK;
  ASSERT(radenv && radenv->type == RADIATIVE_ENV_PROG && stardis);

  /* Register a pointer to the radiative environment with the solver radiative
   * environment */
  res = sdis_data_create(stardis->dev, sizeof(struct radiative_env_prog*),
    ALIGNOF(struct radiative_env_prog*), NULL, &data);
  if(res != RES_OK) goto error;
  *((struct radiative_env_prog**)sdis_data_get(data)) = &radenv->data.prg;

  /* Create the radiative environment */
  shader.temperature = radenv_prog_get_temperature;
  shader.reference_temperature = radenv_prog_get_reference_temperature;
  res = sdis_radiative_env_create
    (stardis->dev, &shader, data, &radenv->sdis_radenv);
  if(res != RES_OK) goto error;

exit:
  /* Release the local reference on the data: it is kept by the sdis radenv */
  if(data) SDIS(data_ref_put(data));
  return res;
error:
  logger_print(stardis->logger, LOG_ERROR,
    "Error when creating the radiative environment for the solver -- %s\n",
    res_to_cstr(res));
  goto exit;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
res_T
radiative_env_init_const
  (struct mem_allocator* allocator,
   struct radiative_env* radenv)
{
  ASSERT(radenv);
  (void)allocator;
  radenv->type = RADIATIVE_ENV_CONST;
  radenv->data.cst = RADIATIVE_ENV_CONST_DEFAULT;
  return RES_OK;
}

res_T
radiative_env_init_prog
  (struct mem_allocator* allocator,
   struct radiative_env* radenv)
{
  ASSERT(radenv);
  radenv->type = RADIATIVE_ENV_PROG;
  radenv->data.prg = RADIATIVE_ENV_PROG_NULL;
  radenv->data.prg.allocator = allocator;
  str_init(allocator, &radenv->data.prg.prog_name);
  return RES_OK;
}

void
radiative_env_release(struct radiative_env* radenv)
{
  ASSERT(radenv);

  if(radenv->type == RADIATIVE_ENV_PROG) {
    struct radiative_env_prog* radenv_prog = &radenv->data.prg;
    size_t i = 0;

    if(radenv_prog->data) {
      ASSERT(radenv_prog->release);
      radenv_prog->release(radenv_prog->data);
    }

    str_release(&radenv_prog->prog_name);
    FOR_EACH(i, 0, radenv_prog->argc) {
      MEM_RM(radenv_prog->allocator, radenv_prog->argv[i]);
    }
    MEM_RM(radenv_prog->allocator, radenv_prog->argv);
  }

  if(radenv->sdis_radenv) SDIS(radiative_env_ref_put(radenv->sdis_radenv));
  radenv->sdis_radenv = NULL;
}

res_T
radiative_env_create_solver_radiative_env
  (struct radiative_env* radenv,
   struct stardis* stardis)
{
  res_T res = RES_OK;
  ASSERT(radenv && stardis);

  switch(radenv->type) {
    case RADIATIVE_ENV_CONST:
      res = create_radenv_const(radenv, stardis);
      if(res != RES_OK) goto error;
      break;
    case RADIATIVE_ENV_PROG:
      res = create_radenv_prog(radenv, stardis);
      if(res != RES_OK) goto error;
      break;
    default: FATAL("Unreachable code\n"); break;
  }

exit:
  return res;
error:
  goto exit;
}
