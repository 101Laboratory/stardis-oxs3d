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

#include "stardis-intface.h"
#include "stardis-app.h"
#include "stardis-compute.h"
#include "stardis-output.h"
#include "stardis-solid.h"
#include "stardis-hbound.h"
#include "stardis-hbound-prog.h"
#include "stardis-hfbound.h"
#include "stardis-hfbound-prog.h"
#include "stardis-tbound.h"
#include "stardis-tbound-prog.h"
#include "stardis-fbound.h"
#include "stardis-fbound-prog.h"
#include "stardis-ssconnect.h"
#include "stardis-ssconnect-prog.h"
#include "stardis-sfconnect.h"
#include "stardis-sfconnect-prog.h"

#include <sdis.h>

/*******************************************************************************
 * Local Functions
 ******************************************************************************/

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  (void)frag;
  return interface_props->hc;
}

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  (void)frag;
  return interface_props->imposed_temperature;
}

static double
interface_get_flux
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  (void)frag;
  return interface_props->imposed_flux;
}

static double
interface_get_ref_temperature
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  (void)frag;
  return interface_props->ref_temperature;
}

static double
interface_get_emissivity
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  (void)frag, (void)source_id;
  return interface_props->emissivity;
}

static double
interface_get_alpha
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  (void)frag, (void)source_id;
  return interface_props->alpha;
}

static double
interface_get_tcr
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  (void)frag;
  return interface_props->tcr;
}

static double
emissivity_1
  (const struct stardis_interface_fragment* frag,
   const unsigned src_id,
   void* data)
{
  (void)frag, (void)src_id, (void)data;
  return 1;
}

static double
intface_prog_get_temp
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  struct stardis_interface_fragment f;
  double temp;
  d3_set(f.P, frag->P);
  d3_set(f.Ng, frag->Ng);
  d3_set(f.uv, frag->uv);
  f.time = frag->time;
  ASSERT(frag->side == SDIS_FRONT || frag->side == SDIS_BACK);
  f.side = (frag->side == SDIS_FRONT) ? FRONT : BACK;
  temp = interface_props->get_temp(&f, interface_props->prog_data);
  return STARDIS_TEMPERATURE_IS_KNOWN(temp) ? temp : SDIS_TEMPERATURE_NONE;
}

static double
intface_prog_get_flux
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  struct stardis_interface_fragment f;
  double flux;
  d3_set(f.P, frag->P);
  d3_set(f.Ng, frag->Ng);
  d3_set(f.uv, frag->uv);
  f.time = frag->time;
  ASSERT(frag->side == SDIS_FRONT || frag->side == SDIS_BACK);
  f.side = (frag->side == SDIS_FRONT) ? FRONT : BACK;
  flux = interface_props->get_flux(&f, interface_props->prog_data);
  return flux != STARDIS_FLUX_NONE ? flux : SDIS_FLUX_NONE;
}

static double
intface_prog_get_hc
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  struct stardis_interface_fragment f;
  d3_set(f.P, frag->P);
  d3_set(f.Ng, frag->Ng);
  d3_set(f.uv, frag->uv);
  f.time = frag->time;
  ASSERT(frag->side == SDIS_FRONT || frag->side == SDIS_BACK);
  f.side = (frag->side == SDIS_FRONT) ? FRONT : BACK;
  return interface_props->get_hc(&f, interface_props->prog_data);
}

static double
intface_prog_get_emissivity
  (const struct sdis_interface_fragment* frag,
   const unsigned src_id,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  struct stardis_interface_fragment f;
  unsigned id;
  d3_set(f.P, frag->P);
  d3_set(f.Ng, frag->Ng);
  d3_set(f.uv, frag->uv);
  f.time = frag->time;
  ASSERT(frag->side == SDIS_FRONT || frag->side == SDIS_BACK);
  f.side = (frag->side == SDIS_FRONT) ? FRONT : BACK;
  id = src_id != SDIS_INTERN_SOURCE_ID ? src_id : STARDIS_INTERN_SOURCE_ID;
  return interface_props->get_emissivity(&f, id, interface_props->prog_data);
}

static double
intface_prog_get_alpha
  (const struct sdis_interface_fragment* frag,
   const unsigned src_id,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  struct stardis_interface_fragment f;
  unsigned id;
  d3_set(f.P, frag->P);
  d3_set(f.Ng, frag->Ng);
  d3_set(f.uv, frag->uv);
  f.time = frag->time;
  ASSERT(frag->side == SDIS_FRONT || frag->side == SDIS_BACK);
  f.side = (frag->side == SDIS_FRONT) ? FRONT : BACK;
  id = src_id != SDIS_INTERN_SOURCE_ID ? src_id : STARDIS_INTERN_SOURCE_ID;
  return interface_props->get_alpha(&f, id, interface_props->prog_data);
}

static double
intface_prog_get_ref_temp
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  struct stardis_interface_fragment f;
  double temp;
  d3_set(f.P, frag->P);
  d3_set(f.Ng, frag->Ng);
  d3_set(f.uv, frag->uv);
  f.time = frag->time;
  ASSERT(frag->side == SDIS_FRONT || frag->side == SDIS_BACK);
  f.side = (frag->side == SDIS_FRONT) ? FRONT : BACK;
  temp = interface_props->get_ref_temp(&f, interface_props->prog_data);
  return STARDIS_TEMPERATURE_IS_KNOWN(temp) ? temp : SDIS_TEMPERATURE_NONE;
}

static double
intface_prog_get_tcr
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct intface* interface_props = sdis_data_cget(data);
  struct stardis_interface_fragment f;
  d3_set(f.P, frag->P);
  d3_set(f.Ng, frag->Ng);
  d3_set(f.uv, frag->uv);
  f.time = frag->time;
  ASSERT(frag->side == SDIS_FRONT || frag->side == SDIS_BACK);
  f.side = (frag->side == SDIS_FRONT) ? FRONT : BACK;
  return interface_props->get_tcr(&f, interface_props->prog_data);
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

res_T
create_intface
  (struct stardis* stardis,
   unsigned itri,
   struct htable_intface* htable_interfaces)
{
  struct int_descs int_descs = INT_DESCS_NULL;
  struct sdis_interface** pp_intface;
  struct sdis_interface* p_intface;
  struct sdis_medium* front_med = NULL;
  struct sdis_medium* back_med = NULL;
  struct sdis_interface_side_shader* fluid_side_shader = NULL;
  struct sdis_data* data = NULL;
  unsigned fd, bd, id, descr[SG3D_PROP_TYPES_COUNT__];
  int fluid_count = 0, solid_count = 0;
  int solid_fluid_connection_count = 0, solid_solid_connection_count = 0;
  int intface_count = 0, boundary_count = 0, prog_count = 0;
  const struct description* descriptions;
  struct sdis_medium** media;
  res_T res = RES_OK;

  ASSERT(stardis && htable_interfaces);

  /* Get the description IDs for this triangle */
  ERR(sg3d_geometry_get_unique_triangle_properties(stardis->geometry.sg3d,
    itri, descr));

  descriptions = darray_descriptions_cdata_get(&stardis->descriptions);
  media = darray_media_ptr_data_get(&stardis->media);

  /* Create key */
  int_descs.front = fd = descr[SG3D_FRONT];
  int_descs.back = bd = descr[SG3D_BACK];
  int_descs.intface = id = descr[SG3D_INTFACE];

  /* Search if interface already exists, or create it */
  pp_intface = htable_intface_find(htable_interfaces, &int_descs);
  if(pp_intface) {
    p_intface = *pp_intface;
  } else {
    /* create new interface and register */
    struct sdis_interface_shader interface_shader = SDIS_INTERFACE_SHADER_NULL;
    struct intface* interface_props = NULL;
    unsigned mid;
    int front_defined = (fd != SG3D_UNSPECIFIED_PROPERTY);
    int back_defined = (bd != SG3D_UNSPECIFIED_PROPERTY);
    int intface_defined = (id != SG3D_UNSPECIFIED_PROPERTY);

    ERR(sdis_data_create(stardis->dev, sizeof(struct intface),
      ALIGNOF(struct intface), NULL, &data));
    interface_props = sdis_data_get(data);
    interface_props->imposed_temperature = SDIS_TEMPERATURE_NONE;
    interface_props->imposed_flux = SDIS_FLUX_NONE;
    interface_props->front_medium_id = UINT_MAX;
    interface_props->back_medium_id = UINT_MAX;
    interface_props->desc_id = UINT_MAX;
    interface_props->get_temp = NULL;
    interface_props->get_flux = NULL;
    interface_props->get_hc = NULL;
    interface_props->get_emissivity = NULL;
    interface_props->get_alpha = NULL;
    interface_props->get_ref_temp = NULL;
    interface_props->get_tcr = NULL;
    interface_props->prog_data = NULL;

    if(fluid_count == 2) {
      res = RES_BAD_ARG;
      goto error;
    }
    if(front_defined) {
      description_get_medium_id(&descriptions[fd], &mid);
      interface_props->front_medium_id = mid;
      front_med = media[mid];
      switch (descriptions[fd].type) {
      case DESC_MAT_SOLID_PROG:
        prog_count++;
      /* fall through */
      case DESC_MAT_SOLID:
        solid_count++;
        break;
      case DESC_MAT_FLUID_PROG:
        prog_count++;
      /* fall through */
      case DESC_MAT_FLUID:
        fluid_count++;
        fluid_side_shader = &interface_shader.front;
        break;
      default:
        FATAL("error:" STR(__FILE__) ":" STR(__LINE__)": Invalid type.\n");
      }
    }
    if(back_defined) {
      description_get_medium_id(&descriptions[bd], &mid);
      interface_props->back_medium_id = mid;
      back_med = media[mid];
      switch (descriptions[bd].type) {
      case DESC_MAT_SOLID_PROG:
        prog_count++;
      /* fall through */
      case DESC_MAT_SOLID:
        solid_count++;
        break;
      case DESC_MAT_FLUID_PROG:
        prog_count++;
      /* fall through */
      case DESC_MAT_FLUID:
        fluid_count++;
        /* cannot overwrite as the 2-fluid case is an error */
        fluid_side_shader = &interface_shader.back;
        break;
      default:
        FATAL("error:" STR(__FILE__) ":" STR(__LINE__)": Invalid type.\n");
      }
    }
    if(intface_defined) {
      const struct description* intface = descriptions + id;
      int for_fluid = 0;
      struct sdis_medium* def_medium = NULL;
      unsigned ext_id;
      if(!DESC_IS_CONNECTION(intface) && front_defined == back_defined) {
        /* not connection implies 1 and only 1 side defined */
        res = RES_BAD_ARG;
        goto error;
      }
      /* meaningless if connection (but unused in this case) */
      def_medium = front_defined ? front_med : back_med;
      interface_props->desc_id = id;
      switch(intface->type) {
      case DESC_BOUND_H_FOR_FLUID:
        if(sdis_medium_get_type(def_medium) != SDIS_FLUID) {
          res = RES_BAD_ARG;
          goto error;
        }
        for_fluid = 1;
        ASSERT(SDIS_TEMPERATURE_IS_KNOWN(intface->d.h_boundary->imposed_temperature));
        interface_props->imposed_temperature
          = intface->d.h_boundary->imposed_temperature;
        ASSERT(fluid_side_shader);
        fluid_side_shader->temperature = interface_get_temperature;
        /* fall through */
      case DESC_BOUND_H_FOR_SOLID:
        if(!for_fluid) {
          if(sdis_medium_get_type(def_medium) != SDIS_SOLID) {
            res = RES_BAD_ARG;
            goto error;
          }
          ASSERT(!fluid_side_shader);
          fluid_side_shader =
            front_defined ? &interface_shader.back : &interface_shader.front;
        }
        ext_id = intface->d.h_boundary->mat_id; /* External material id */
        ASSERT(ext_id < darray_media_ptr_size_get(&stardis->media));
        ASSERT(sdis_medium_get_type(media[ext_id]) ==
          (for_fluid ? SDIS_SOLID : SDIS_FLUID));
        intface_count++;
        boundary_count++;
        if(front_defined) back_med = media[ext_id];
        else front_med = media[ext_id];
        interface_shader.convection_coef_upper_bound = intface->d.h_boundary->hc;
        interface_props->hc = intface->d.h_boundary->hc;
        interface_props->ref_temperature = intface->d.h_boundary->ref_temperature;
        interface_props->emissivity = intface->d.h_boundary->emissivity;
        interface_props->alpha = intface->d.h_boundary->specular_fraction;
        if(intface->d.h_boundary->hc > 0) {
          interface_shader.convection_coef = interface_get_convection_coef;
        }
        fluid_side_shader->reference_temperature = interface_get_ref_temperature;
        if(intface->d.h_boundary->emissivity > 0) {
          fluid_side_shader->emissivity = interface_get_emissivity;
          fluid_side_shader->specular_fraction = interface_get_alpha;
        }
        break;
      case DESC_BOUND_HF_FOR_SOLID:
        if(!for_fluid) {
          if(sdis_medium_get_type(def_medium) != SDIS_SOLID) {
            res = RES_BAD_ARG;
            goto error;
          }
          ASSERT(!fluid_side_shader);
          fluid_side_shader =
            front_defined ? &interface_shader.back : &interface_shader.front;
        }
        ext_id = intface->d.hf_boundary->mat_id; /* External material id */
        ASSERT(ext_id < darray_media_ptr_size_get(&stardis->media));
        ASSERT(sdis_medium_get_type(media[ext_id]) ==
          (for_fluid ? SDIS_SOLID : SDIS_FLUID));
        intface_count++;
        boundary_count++;
        if(front_defined) back_med = media[ext_id];
        else front_med = media[ext_id];
        interface_shader.convection_coef_upper_bound = intface->d.hf_boundary->hc;
        interface_props->hc = intface->d.hf_boundary->hc;
        interface_props->ref_temperature = intface->d.hf_boundary->ref_temperature;
        interface_props->emissivity = intface->d.hf_boundary->emissivity;
        interface_props->alpha = intface->d.hf_boundary->specular_fraction;
        if(intface->d.hf_boundary->hc > 0) {
          interface_shader.convection_coef = interface_get_convection_coef;
        }
        fluid_side_shader->reference_temperature = interface_get_ref_temperature;
        ASSERT(intface->d.hf_boundary->imposed_flux != SDIS_FLUX_NONE);
        if(intface->d.hf_boundary->imposed_flux) {
          interface_props->imposed_flux = intface->d.hf_boundary->imposed_flux;
          interface_shader.front.flux = interface_get_flux;
          interface_shader.back.flux = interface_get_flux;
        }
        if(intface->d.hf_boundary->emissivity > 0) {
          fluid_side_shader->emissivity = interface_get_emissivity;
          fluid_side_shader->specular_fraction = interface_get_alpha;
        }
        break;
      case DESC_BOUND_H_FOR_FLUID_PROG:
        if(sdis_medium_get_type(def_medium) != SDIS_FLUID) {
          res = RES_BAD_ARG;
          goto error;
        }
        for_fluid = 1;
        interface_props->get_temp = intface->d.h_boundary_prog->boundary_temp;
        ASSERT(fluid_side_shader);
        fluid_side_shader->temperature = intface_prog_get_temp;
        /* fall through */
      case DESC_BOUND_H_FOR_SOLID_PROG:
        if(!for_fluid) {
          if(sdis_medium_get_type(def_medium) != SDIS_SOLID) {
            res = RES_BAD_ARG;
            goto error;
          }
          ASSERT(!fluid_side_shader);
          fluid_side_shader =
            front_defined ? &interface_shader.back : &interface_shader.front;
        }
        ext_id = intface->d.h_boundary_prog->mat_id; /* External material id */
        ASSERT(ext_id < darray_media_ptr_size_get(&stardis->media));
        ASSERT(sdis_medium_get_type(media[ext_id]) ==
          (for_fluid ? SDIS_SOLID : SDIS_FLUID));
        prog_count++;
        intface_count++;
        boundary_count++;
        if(front_defined) back_med = media[ext_id];
        else front_med = media[ext_id];
        interface_shader.convection_coef_upper_bound
          = h_bound_prog_get_hmax(intface->d.h_boundary_prog);
        ASSERT(interface_shader.convection_coef_upper_bound >= 0);
        interface_shader.convection_coef
          = intface_prog_get_hc;
        fluid_side_shader->reference_temperature
          = intface_prog_get_ref_temp;
        fluid_side_shader->emissivity = intface_prog_get_emissivity;
        fluid_side_shader->specular_fraction = intface_prog_get_alpha;
        interface_props->get_hc = intface->d.h_boundary_prog->hc;
        interface_props->get_emissivity = intface->d.h_boundary_prog->emissivity;
        interface_props->get_alpha = intface->d.h_boundary_prog->alpha;
        interface_props->get_ref_temp = intface->d.h_boundary_prog->ref_temp;
        interface_props->prog_data = intface->d.h_boundary_prog->prog_data;
        break;
      case DESC_BOUND_HF_FOR_SOLID_PROG:
        if(sdis_medium_get_type(def_medium) != SDIS_SOLID) {
          res = RES_BAD_ARG;
          goto error;
        }
        ASSERT(!fluid_side_shader);
        fluid_side_shader =
          front_defined ? &interface_shader.back : &interface_shader.front;
        ext_id = intface->d.hf_boundary_prog->mat_id; /* External material id */
        ASSERT(ext_id < darray_media_ptr_size_get(&stardis->media));
        ASSERT(sdis_medium_get_type(media[ext_id]) == SDIS_FLUID);
        prog_count++;
        intface_count++;
        boundary_count++;
        if(front_defined) back_med = media[ext_id];
        else front_med = media[ext_id];
        interface_shader.convection_coef_upper_bound
          = hf_bound_prog_get_hmax(intface->d.hf_boundary_prog);
        ASSERT(interface_shader.convection_coef_upper_bound >= 0);
        interface_shader.convection_coef = intface_prog_get_hc;
        fluid_side_shader->reference_temperature = intface_prog_get_ref_temp;
        fluid_side_shader->emissivity = intface_prog_get_emissivity;
        fluid_side_shader->specular_fraction = intface_prog_get_alpha;
        interface_shader.front.flux = intface_prog_get_flux;
        interface_shader.back.flux = intface_prog_get_flux;
        interface_props->get_hc = intface->d.hf_boundary_prog->hc;
        interface_props->get_emissivity = intface->d.hf_boundary_prog->emissivity;
        interface_props->get_alpha = intface->d.hf_boundary_prog->alpha;
        interface_props->get_flux = intface->d.hf_boundary_prog->flux;
        interface_props->get_ref_temp = intface->d.hf_boundary_prog->ref_temp;
        interface_props->prog_data = intface->d.hf_boundary_prog->prog_data;
        break;
      case DESC_BOUND_T_FOR_SOLID:
        if(sdis_medium_get_type(def_medium) != SDIS_SOLID) {
          res = RES_BAD_ARG;
          goto error;
        }
        fluid_side_shader =
          front_defined ? &interface_shader.back : &interface_shader.front;
        ext_id = intface->d.t_boundary->mat_id; /* External material id */
        ASSERT(ext_id < darray_media_ptr_size_get(&stardis->media));
        ASSERT(sdis_medium_get_type(media[ext_id]) == SDIS_FLUID);
        intface_count++;
        boundary_count++;
        if(front_defined) {
          ASSERT(!back_med);
          back_med = media[ext_id];
        } else {
          ASSERT(!front_med);
          front_med = media[ext_id];
        }
        /* The imposed T is for the 2 sides (there is no contact resistance) */
        interface_shader.front.temperature = interface_get_temperature;
        interface_shader.back.temperature = interface_get_temperature;
        /* Set emissivity to 1 to allow radiative paths comming from
         * a possible external fluid to 'see' the imposed T */
        fluid_side_shader->emissivity = interface_get_emissivity;
        interface_props->emissivity = 1;
        ASSERT(SDIS_TEMPERATURE_IS_KNOWN(intface->d.t_boundary->imposed_temperature));
        interface_props->imposed_temperature = intface->d.t_boundary->imposed_temperature;
        /* Temporarily set the reference temperature to the set temperature.
         * TODO use a different reference temperature when the file format is
         * updated to allow explicit definition by the user. */
        fluid_side_shader->reference_temperature = interface_get_ref_temperature;
        interface_props->ref_temperature = intface->d.t_boundary->imposed_temperature;
        break;
      case DESC_BOUND_T_FOR_SOLID_PROG:
        if(sdis_medium_get_type(def_medium) != SDIS_SOLID) {
          res = RES_BAD_ARG;
          goto error;
        }
        fluid_side_shader =
          front_defined ? &interface_shader.back : &interface_shader.front;
        ext_id = intface->d.t_boundary_prog->mat_id; /* External material id */
        ASSERT(ext_id < darray_media_ptr_size_get(&stardis->media));
        ASSERT(sdis_medium_get_type(media[ext_id]) == SDIS_FLUID);
        prog_count++;
        intface_count++;
        boundary_count++;
        if(front_defined) {
          ASSERT(!back_med);
          back_med = media[ext_id];
        } else {
          ASSERT(!front_med);
          front_med = media[ext_id];
        }
        /* The imposed T is for the 2 sides (there is no contact resistance) */
        interface_shader.front.temperature = intface_prog_get_temp;
        interface_shader.back.temperature = intface_prog_get_temp;
        /* Set emissivity to 1 to allow radiative paths comming from
         * a possible external fluid to 'see' the imposed T */
        fluid_side_shader->emissivity = intface_prog_get_emissivity;
        interface_props->get_emissivity = emissivity_1;
        interface_props->get_temp = intface->d.t_boundary_prog->temperature;
        interface_props->prog_data = intface->d.t_boundary_prog->prog_data;
        /* Temporarily set the reference temperature to the set temperature.
         * TODO use a different reference temperature when the file format is
         * updated to allow explicit definition by the user. */
        fluid_side_shader->reference_temperature = intface_prog_get_temp;
        break;
      case DESC_BOUND_F_FOR_SOLID:
        if(sdis_medium_get_type(def_medium) != SDIS_SOLID) {
          res = RES_BAD_ARG;
          goto error;
        }
        intface_count++;
        boundary_count++;
        if(front_defined) {
          back_med = media[intface->d.f_boundary->mat_id];
        } else {
          front_med = media[intface->d.f_boundary->mat_id];
        }
        interface_shader.front.flux = interface_get_flux;
        interface_shader.back.flux = interface_get_flux;
        ASSERT(intface->d.f_boundary->imposed_flux != SDIS_FLUX_NONE);
        interface_props->imposed_flux = intface->d.f_boundary->imposed_flux;
        break;
      case DESC_BOUND_F_FOR_SOLID_PROG:
        if(sdis_medium_get_type(def_medium) != SDIS_SOLID) {
          res = RES_BAD_ARG;
          goto error;
        }
        prog_count++;
        intface_count++;
        boundary_count++;
        if(front_defined) {
          back_med = media[intface->d.f_boundary_prog->mat_id];
        } else {
          front_med = media[intface->d.f_boundary_prog->mat_id];
        }
        interface_shader.front.flux = intface_prog_get_flux;
        interface_shader.back.flux = intface_prog_get_flux;
        interface_props->get_flux = intface->d.f_boundary_prog->flux;
        interface_props->prog_data = intface->d.f_boundary_prog->prog_data;
        break;
      case DESC_SOLID_FLUID_CONNECT:
        /* Both front and back should be defined */
        if(solid_count != 1 || fluid_count != 1) {
          res = RES_BAD_ARG;
          goto error;
        }
        ASSERT(front_defined && back_defined);
        intface_count++;
        solid_fluid_connection_count++;
        interface_shader.convection_coef_upper_bound = intface->d.sf_connect->hc;
        interface_props->hc = intface->d.sf_connect->hc;
        interface_props->ref_temperature = intface->d.sf_connect->ref_temperature;
        interface_props->emissivity = intface->d.sf_connect->emissivity;
        interface_props->alpha = intface->d.sf_connect->specular_fraction;
        interface_props->imposed_flux = intface->d.sf_connect->flux;
        if(intface->d.sf_connect->hc > 0) {
          interface_shader.convection_coef = interface_get_convection_coef;
        }
        if(intface->d.sf_connect->flux != SDIS_FLUX_NONE) {
          interface_shader.front.flux = interface_get_flux;
          interface_shader.back.flux = interface_get_flux;
        }
        ASSERT(fluid_side_shader);
        fluid_side_shader->reference_temperature = interface_get_ref_temperature;
        fluid_side_shader->specular_fraction = interface_get_alpha;
        if(intface->d.sf_connect->emissivity > 0) {
          fluid_side_shader->emissivity = interface_get_emissivity;
        }
        break;
      case DESC_SOLID_FLUID_CONNECT_PROG:
        /* Both front and back should be defined */
        if(solid_count != 1 || fluid_count != 1) {
          res = RES_BAD_ARG;
          goto error;
        }
        ASSERT(front_defined && back_defined);
        prog_count++;
        intface_count++;
        solid_fluid_connection_count++;
        interface_shader.convection_coef_upper_bound
          = sf_connect_prog_get_hmax(intface->d.sf_connect_prog);
        ASSERT(interface_shader.convection_coef_upper_bound >= 0);
        interface_props->get_ref_temp = intface->d.sf_connect_prog->ref_temp;
        interface_props->get_hc = intface->d.sf_connect_prog->hc;
        interface_props->get_emissivity
          = intface->d.sf_connect_prog->emissivity;
        interface_props->get_alpha = intface->d.sf_connect_prog->alpha;
        interface_shader.convection_coef = intface_prog_get_hc;
        if(intface->d.sf_connect_prog->flux) {
          interface_props->get_flux = intface->d.sf_connect_prog->flux;
          interface_shader.front.flux = intface_prog_get_flux;
          interface_shader.back.flux = intface_prog_get_flux;
        }
        ASSERT(fluid_side_shader);
        fluid_side_shader->emissivity = intface_prog_get_emissivity;
        fluid_side_shader->specular_fraction = intface_prog_get_alpha;
        fluid_side_shader->reference_temperature = intface_prog_get_ref_temp;
        interface_props->prog_data = intface->d.sf_connect_prog->prog_data;
        break;
      case DESC_SOLID_SOLID_CONNECT:
        /* Both front and back should be defined */
        if(solid_count != 2 || fluid_count != 0) {
          res = RES_BAD_ARG;
          goto error;
        }
        ASSERT(front_defined && back_defined);
        intface_count++;
        solid_solid_connection_count++;
        interface_props->tcr = intface->d.ss_connect->tcr;
        if(intface->d.ss_connect->tcr > 0) {
          interface_shader.thermal_contact_resistance = interface_get_tcr;
        }
        break;
      case DESC_SOLID_SOLID_CONNECT_PROG:
        /* Both front and back should be defined */
        if(solid_count != 2 || fluid_count != 0) {
          res = RES_BAD_ARG;
          goto error;
        }
        ASSERT(front_defined && back_defined);
        prog_count++;
        intface_count++;
        solid_solid_connection_count++;
        interface_props->get_tcr = intface->d.ss_connect_prog->tcr;
        interface_shader.thermal_contact_resistance = intface_prog_get_tcr;
        interface_props->prog_data = intface->d.ss_connect_prog->prog_data;
        break;
      default:
        FATAL("error:" STR(__FILE__) ":" STR(__LINE__)": Invalid type.\n");
      }
    }

    if((fluid_count + solid_count + intface_count < 2)
      || (boundary_count && (fluid_count + solid_count != 1)))
    {
      res = RES_BAD_ARG;
      goto error;
    }

    ERR(sdis_interface_create(stardis->dev, front_med, back_med,
      &interface_shader, data, &p_intface));
    SDIS(data_ref_put(data)); data = NULL;
    ERR(darray_interface_ptrs_push_back(&stardis->geometry.interfaces, &p_intface));
    ERR(htable_intface_set(htable_interfaces, &int_descs, &p_intface));
  }
  ERR(darray_interface_ptrs_push_back(&stardis->geometry.interf_bytrg, &p_intface));

end:
  if(data) SDIS(data_ref_put(data));
  return res;
error:
  goto end;
}

