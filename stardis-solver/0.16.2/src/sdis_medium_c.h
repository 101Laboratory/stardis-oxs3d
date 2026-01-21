/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef SDIS_MEDIUM_C_H
#define SDIS_MEDIUM_C_H

#include "sdis.h"
#include "sdis_log.h"

#include <rsys/free_list.h>
#include <rsys_math.h>
#include <rsys/ref_count.h>

struct sdis_medium {
  enum sdis_medium_type type;
  union {
    struct sdis_solid_shader solid;
    struct sdis_fluid_shader fluid;
  } shader;

  struct sdis_data* data;
  struct fid id; /* Unique identifier of the medium */

  ref_T ref;
  struct sdis_device* dev;
};

struct fluid_props {
  double rho; /* Volumic mass */
  double cp; /* Calorific capacity */
  double temperature;
  double t0; /* Initial time */
};
#define FLUID_PROPS_NULL__ {0,0,0,0}
static const struct fluid_props FLUID_PROPS_NULL = FLUID_PROPS_NULL__;

struct solid_props {
  double lambda; /* Conductivity */
  double rho; /* Volumic mass */
  double cp; /* Calorific capacity */
  double delta; /* Random walk step */
  double power; /* Volumic power */
  double temperature;
  double t0; /* Initial time */
};
#define SOLID_PROPS_NULL__ {0,0,0,0,0,0,0}
static const struct solid_props SOLID_PROPS_NULL = SOLID_PROPS_NULL__;

#define MDM_TYPE(Mdm) CONCAT(MDM_TYPE_, Mdm)
#define MDM_TYPE_solid SDIS_SOLID
#define MDM_TYPE_fluid SDIS_FLUID

#define PROP_STR(Prop) CONCAT(PROP_STR_, Prop)
#define PROP_STR_calorific_capacity "calorific capacity"
#define PROP_STR_conductivity "conductivity"
#define PROP_STR_delta "delta"
#define PROP_STR_temperature "temperature"
#define PROP_STR_thermal_conductivity "thermal conductivity"
#define PROP_STR_volumic_mass "volumic mass"
#define PROP_STR_volumic_power "volumic power"

#define DEFINE_MDM_GET_PROP_FUNC(Mdm, Prop)                                    \
  static INLINE double                                                         \
  Mdm##_get_##Prop                                                             \
    (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)       \
  {                                                                            \
    ASSERT(mdm && mdm->type == MDM_TYPE(Mdm));                                 \
    return mdm->shader.Mdm.Prop(vtx, mdm->data);                               \
  }

#define DEFINE_MDM_CHK_PROP_FUNC(Mdm, Prop, Low, Upp, LowIsInc, UppIsInc)      \
  static INLINE res_T                                                          \
  Mdm##_check_##Prop                                                           \
    (struct sdis_device* dev,                                                  \
     const double val, /* Value of the property */                             \
     const double pos[], /* Position at which the property was queried */      \
     const double time) /* Time at which the property was queried */           \
  {                                                                            \
    const int low_test = LowIsInc ? Low <= val : Low < val;                    \
    const int upp_test = UppIsInc ? Upp >= val : Upp > val;                    \
    const char low_char = LowIsInc ? '[' : ']';                                \
    const char upp_char = UppIsInc ? ']' : '[';                                \
    ASSERT(dev && pos);                                                        \
                                                                               \
    if(!low_test || !upp_test) {                                               \
      log_err(dev,                                                             \
        "invalid "STR(Mdm)" "PROP_STR(Prop)" '%g': it must be in %c%g, %g%c "  \
        "-- position=%g, %g, %g; time=%g\n",                                   \
        val, low_char, (double)Low, (double)Upp, upp_char, SPLIT3(pos), time); \
      return RES_BAD_ARG;                                                      \
    }                                                                          \
    return RES_OK;                                                             \
  }

/*******************************************************************************
 * Fluid local functions
 ******************************************************************************/
DEFINE_MDM_CHK_PROP_FUNC(fluid, calorific_capacity, 0, INF, 0, 1)
DEFINE_MDM_CHK_PROP_FUNC(fluid, volumic_mass, 0, INF, 0, 1)
DEFINE_MDM_CHK_PROP_FUNC(fluid, temperature, -INF, INF, 1, 1)

DEFINE_MDM_GET_PROP_FUNC(fluid, calorific_capacity)
DEFINE_MDM_GET_PROP_FUNC(fluid, volumic_mass)
DEFINE_MDM_GET_PROP_FUNC(fluid, temperature)

static INLINE double
fluid_get_t0(const struct sdis_medium* mdm)
{
  ASSERT(mdm && mdm->type == SDIS_FLUID);
  ASSERT(0 <= mdm->shader.fluid.t0 && mdm->shader.fluid.t0 < INF);
  return mdm->shader.fluid.t0;
}

static INLINE res_T
fluid_check_properties
  (struct sdis_device* dev,
   const struct fluid_props* props,
   const double pos[],
   const double time)
{
  res_T res = RES_OK;
  ASSERT(dev && props);

  #define CHK_PROP(Prop, Val) {                                                \
    res = fluid_check_##Prop(dev, Val, pos, time);                             \
    if(res != RES_OK) return res;                                              \
  } (void)0
  CHK_PROP(volumic_mass, props->rho);
  CHK_PROP(calorific_capacity, props->cp);
  #undef CHK_PROP

  return RES_OK;
}

static INLINE res_T
fluid_get_properties
  (const struct sdis_medium* mdm,
   const struct sdis_rwalk_vertex* vtx,
   struct fluid_props* props)
{
  ASSERT(mdm && mdm->type == SDIS_FLUID);
  props->rho = fluid_get_volumic_mass(mdm, vtx);
  props->cp = fluid_get_calorific_capacity(mdm, vtx);
  props->temperature = fluid_get_temperature(mdm, vtx);
  return fluid_check_properties(mdm->dev, props, vtx->P, vtx->time);
}

/*******************************************************************************
 * Solid local functions
 ******************************************************************************/
DEFINE_MDM_CHK_PROP_FUNC(solid, calorific_capacity, 0, INF, 0, 1)
DEFINE_MDM_CHK_PROP_FUNC(solid, thermal_conductivity, 0, INF, 0, 1)
DEFINE_MDM_CHK_PROP_FUNC(solid, volumic_mass, 0, INF, 0, 1)
DEFINE_MDM_CHK_PROP_FUNC(solid, delta, 0, INF, 0, 1)
DEFINE_MDM_CHK_PROP_FUNC(solid, volumic_power, -INF, INF, 1, 1)
DEFINE_MDM_CHK_PROP_FUNC(solid, temperature, -INF, INF, 1, 1)

DEFINE_MDM_GET_PROP_FUNC(solid, calorific_capacity)
DEFINE_MDM_GET_PROP_FUNC(solid, thermal_conductivity)
DEFINE_MDM_GET_PROP_FUNC(solid, volumic_mass)
DEFINE_MDM_GET_PROP_FUNC(solid, delta)
DEFINE_MDM_GET_PROP_FUNC(solid, temperature)

static INLINE double
solid_get_volumic_power
  (const struct sdis_medium* mdm,
   const struct sdis_rwalk_vertex* vtx)
{
  ASSERT(mdm && mdm->type == SDIS_SOLID);
  return mdm->shader.solid.volumic_power
    ? mdm->shader.solid.volumic_power(vtx, mdm->data)
    : SDIS_VOLUMIC_POWER_NONE;
}

static INLINE double
solid_get_t0(const struct sdis_medium* mdm)
{
  ASSERT(mdm && mdm->type == SDIS_SOLID);
  return mdm->shader.solid.t0;
}

static INLINE res_T
solid_check_properties
  (struct sdis_device* dev,
   const struct solid_props* props,
   const double pos[],
   const double time)
{
  res_T res = RES_OK;
  ASSERT(dev && props);

  #define CHK_PROP(Prop, Val) {                                                \
    res = solid_check_##Prop(dev, Val, pos, time);                             \
    if(res != RES_OK) return res;                                              \
  } (void)0
  CHK_PROP(calorific_capacity, props->cp);
  CHK_PROP(thermal_conductivity, props->lambda);
  CHK_PROP(volumic_mass, props->rho);
  CHK_PROP(delta, props->delta);
  CHK_PROP(volumic_power, props->power);
  #undef CHK_PROP

  /* Do not check the temperature. An invalid temperature means that the
   * temperature is unknown */

  return RES_OK;
}

static INLINE res_T
solid_get_properties
  (const struct sdis_medium* mdm,
   const struct sdis_rwalk_vertex* vtx,
   struct solid_props* props)
{
  ASSERT(mdm && mdm->type == SDIS_SOLID);
  props->lambda = solid_get_thermal_conductivity(mdm, vtx);
  props->rho = solid_get_volumic_mass(mdm, vtx);
  props->cp = solid_get_calorific_capacity(mdm, vtx);
  props->delta = solid_get_delta(mdm, vtx);
  props->power = solid_get_volumic_power(mdm, vtx);
  props->temperature = solid_get_temperature(mdm, vtx);
  props->t0 = solid_get_t0(mdm);
  return solid_check_properties(mdm->dev, props, vtx->P, vtx->time);
}

/*******************************************************************************
 * Generic functions
 ******************************************************************************/
static INLINE double
medium_get_temperature
  (const struct sdis_medium* mdm, const struct sdis_rwalk_vertex* vtx)
{
  double temp;
  ASSERT(mdm);
  switch(mdm->type) {
    case SDIS_FLUID: temp = fluid_get_temperature(mdm, vtx); break;
    case SDIS_SOLID: temp = solid_get_temperature(mdm, vtx); break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return temp;
}

static INLINE double
medium_get_t0(const struct sdis_medium* mdm)
{
  double t0;
  ASSERT(mdm);
  switch(mdm->type) {
    case SDIS_FLUID: t0 = fluid_get_t0(mdm); break;
    case SDIS_SOLID: t0 = solid_get_t0(mdm); break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return t0;
}

static INLINE unsigned
medium_get_id(const struct sdis_medium* mdm)
{
  ASSERT(mdm);
  return mdm->id.index;
}

static INLINE const char*
medium_type_to_string(const enum sdis_medium_type type)
{
  const char* str = "none";
  switch(type) {
    case SDIS_FLUID: str = "fluid"; break;
    case SDIS_SOLID: str = "solid"; break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return str;
}

#undef MDM_TYPE
#undef MDM_TYPE_solid
#undef MDM_TYPE_fluid
#undef PROP_STR
#undef PROP_STR_calorific_capacity
#undef PROP_STR_conductivity
#undef PROP_STR_delta
#undef PROP_STR_temperature
#undef PROP_STR_thermal_conductivity
#undef PROP_STR_volumic_mass
#undef PROP_STR_volumic_power
#undef DEFINE_MDM_CHK_PROP_FUNC
#undef DEFINE_MDM_GET_PROP_FUNC

#endif /* SDIS_MEDIUM_C_H */

