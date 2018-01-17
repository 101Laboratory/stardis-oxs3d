/* Copyright (C) |Meso|Star> 2016-2018 (contact@meso-star.com)
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
#include "sdis_interface_c.h"

#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/mem_allocator.h>

#include <star/s2d.h>
#include <star/s3d.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static int
check_interface_shader
  (const struct sdis_interface_shader* shader,
   const struct sdis_medium* front,
   const struct sdis_medium* back)
{
  enum sdis_medium_type type0;
  enum sdis_medium_type type1;
  ASSERT(shader && front && back);

  type0 = sdis_medium_get_type(front);
  type1 = sdis_medium_get_type(back);

  /* Fluid<->solid interface */
  if(type0 != type1 && shader->convection_coef == NULL) {
    return 0;
  }

  /* Solid<->solid interface */
  if(type0 == SDIS_MEDIUM_SOLID
  && type1 == SDIS_MEDIUM_SOLID
  && shader->convection_coef) {
    return 0;
  }

  return 1;
}

static void
interface_release(ref_T* ref)
{
  struct sdis_interface* interf = NULL;
  struct sdis_device* dev = NULL;
  ASSERT(ref);
  interf = CONTAINER_OF(ref, struct sdis_interface, ref);
  dev = interf->dev;
  if(interf->medium_front) SDIS(medium_ref_put(interf->medium_front));
  if(interf->medium_back) SDIS(medium_ref_put(interf->medium_back));
  if(interf->data) SDIS(data_ref_put(interf->data));
  flist_name_del(&dev->names, interf->id);
  MEM_RM(dev->allocator, interf);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_interface_create
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const struct sdis_interface_shader* shader,
   struct sdis_data* data,
   struct sdis_interface** out_interface)
{
  struct sdis_interface* interf = NULL;
  res_T res = RES_OK;

  if(!dev || !front || !back || !shader || !out_interface) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(sdis_medium_get_type(front) == SDIS_MEDIUM_FLUID
  && sdis_medium_get_type(back) == SDIS_MEDIUM_FLUID) {
    log_err(dev, "%s: invalid fluid<->fluid interface.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  if(!check_interface_shader(shader, front, back)) {
    log_err(dev, "%s: invalid interface shader.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  interf = MEM_CALLOC(dev->allocator, 1, sizeof(struct sdis_interface));
  if(!interf) {
    log_err(dev, "%s: could not create the interface.\n", FUNC_NAME);
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&interf->ref);
  SDIS(medium_ref_get(front));
  SDIS(medium_ref_get(back));
  SDIS(device_ref_get(dev));
  interf->medium_front = front;
  interf->medium_back = back;
  interf->dev = dev;
  interf->shader = *shader;
  interf->id = flist_name_add(&dev->names);

  if(data) {
    SDIS(data_ref_get(data));
    interf->data = data;
  }

exit:
  if(out_interface) *out_interface = interf;
  return res;
error:
  if(interf) {
    SDIS(interface_ref_put(interf));
    interf = NULL;
  }
  goto exit;
}

res_T
sdis_interface_ref_get(struct sdis_interface* interf)
{
  if(!interf) return RES_BAD_ARG;
  ref_get(&interf->ref);
  return RES_OK;
}

res_T
sdis_interface_ref_put(struct sdis_interface* interf)
{
  if(!interf) return RES_BAD_ARG;
  ref_put(&interf->ref, interface_release);
  return RES_OK;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
const struct sdis_medium*
interface_get_medium
  (const struct sdis_interface* interf, const enum sdis_side_flag side)
{
  struct sdis_medium* mdm = NULL;
  ASSERT(interf);
  switch(side) {
    case SDIS_FRONT: mdm = interf->medium_front; break;
    case SDIS_BACK:  mdm = interf->medium_back; break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return mdm;
}

unsigned
interface_get_id(const struct sdis_interface* interf)
{
  ASSERT(interf);
  return interf->id.index;
}

void
setup_interface_fragment_2d
  (struct sdis_interface_fragment* frag,
   const struct sdis_rwalk_vertex* vertex,
   const struct s2d_hit* hit)
{
  ASSERT(frag && vertex && hit && !S2D_HIT_NONE(hit));
  d2_set(frag->P, vertex->P);
  frag->P[2] = 0;
  d2_normalize(frag->Ng, d2_set_f2(frag->Ng, hit->normal));
  frag->Ng[2] = 0;
  frag->uv[0] = hit->u;
  frag->time = vertex->time;
}

void
setup_interface_fragment_3d
  (struct sdis_interface_fragment* frag,
   const struct sdis_rwalk_vertex* vertex,
   const struct s3d_hit* hit)
{
  ASSERT(frag && vertex && hit && !S3D_HIT_NONE(hit));
  d3_set(frag->P, vertex->P);
  d3_normalize(frag->Ng, d3_set_f3(frag->Ng, hit->normal));
  d2_set_f2(frag->uv, hit->uv);
  frag->time = vertex->time;
}

