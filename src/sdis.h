/* Copyright (C) |Meso|Star> 2016-2017 (contact@meso-star.com)
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

#ifndef SDIS_H
#define SDIS_H

#include <rsys/rsys.h>

/* Library symbol management */
#if defined(SDIS_SHARED_BUILD)
  #define SDIS_API extern EXPORT_SYM
#elif defined(SDIS_STATIC_BUILD)
  #define SDIS_API extern LOCAL_SYM
#else /* Use shared library */
  #define SDIS_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the Stardis function `Func'
 * returns an error. One should use this macro on Stardis function calls for
 * which no explicit error checking is performed. */
#ifndef NDEBUG
  #define SDIS(Func) ASSERT(sdis_ ## Func == RES_OK)
#else
  #define SDIS(Func) sdis_ ## Func
#endif

/* Syntactic sugar used to inform the library that it can use as many threads
 * as CPU cores */
#define SDIS_NTHREADS_DEFAULT (~0u)

/* Forward declaration of external opaque data types */
struct logger;
struct mem_allocator;

/* Forward declaration of the Stardis opaque data types. These data types are
 * ref counted. Once created with the appropriated `sdis_<TYPE>_create'
 * function, the caller implicitly owns the created data, i.e. its reference
 * counter is set to 1. The sdis_<TYPE>_ref_<get|put> functions get or release
 * a reference on the data, i.e. they increment or decrement the reference
 * counter, respectively. When this counter reach 0 the object is silently
 * destroyed and cannot be used anymore. */
struct sdis_device;
struct sdis_estimator;
struct sdis_interface;
struct sdis_medium;
struct sdis_data;
struct sdis_scene;

enum sdis_side_flag {
  SDIS_FRONT = BIT(0),
  SDIS_BACK = BIT(1),
  SDIS_SIDE_NULL__ = BIT(2)
};

enum sdis_medium_type {
  SDIS_MEDIUM_FLUID,
  SDIS_MEDIUM_SOLID,
  SDIS_MEDIUM_TYPES_COUNT__
};

/* Random walk vertex */
struct sdis_rwalk_vertex {
  double P[3]; /* World space position */
  double time; /* Time of the vertex */
};
#define SDIS_RWALK_VERTEX_NULL__ {{0}, -1}
static const struct sdis_rwalk_vertex SDIS_RWALK_VERTEX_NULL =
  SDIS_RWALK_VERTEX_NULL__;

/* Point onto an interface */
struct sdis_interface_fragment {
  double P[3]; /* World space position */
  double Ng[3]; /* Normalized world space geometry normal */
  double uv[2]; /* Texture coordinates */
  double time; /* Current time */
};
#define SDIS_INTERFACE_FRAGMENT_NULL__ {{0}, {0}, {0}, -1}
static const struct sdis_interface_fragment SDIS_INTERFACE_FRAGMENT_NULL =
  SDIS_INTERFACE_FRAGMENT_NULL__;

/* Monte-Carlo estimation */
struct sdis_mc {
  double E; /* Expected value */
  double V; /* Variance */
  double SE; /* Standard error */
};

/* Functor type to retrieve the medium properties. */
typedef void
(*sdis_medium_getter_T)
  (struct sdis_device* dev,
   struct sdis_data* data,
   const struct sdis_rwalk_vertex* vert,
   double* val);

/* Functor type to retrieve the interface properties. */
typedef void
(*sdis_interface_getter_T)
  (struct sdis_device* dev,
   struct sdis_data* data,
   const struct sdis_interface_fragment* frag,
   double* val);

struct sdis_solid_shader {
  /* Properties */
  sdis_medium_getter_T calorific_capacity;
  sdis_medium_getter_T thermal_conductivity;
  sdis_medium_getter_T volumic_mass;
  sdis_medium_getter_T delta_solid;
  sdis_medium_getter_T delta_boundary;

  /* Initial/limit condition. A temperature < 0 means that the temperature is
   * unknown for the submitted random walk vertex. */
  sdis_medium_getter_T temperature;
};
#define SDIS_SOLID_SHADER_NULL__ {NULL}
static const struct sdis_solid_shader SDIS_SOLID_SHADER_NULL =
  SDIS_SOLID_SHADER_NULL__;

struct sdis_fluid_shader {
  /* Properties */
  sdis_medium_getter_T calorific_capacity;
  sdis_medium_getter_T volumic_mass;

  /* Initial/limit condition. A temperature < 0 means that the temperature is
   * unknown for the submitted position and time. */
  sdis_medium_getter_T temperature;
};
#define SDIS_FLUID_SHADER_NULL__ {NULL}
static const struct sdis_fluid_shader SDIS_FLUID_SHADER_NULL =
  SDIS_FLUID_SHADER_NULL__;

struct sdis_interface_shader {
  sdis_interface_getter_T temperature; /* Limit condition. NULL <=> Unknown */
  sdis_interface_getter_T convection_coef; /* NULL <=> Solid/Solid interface */
};
#define SDIS_INTERFACE_SHADER_NULL__ {NULL}
static const struct sdis_interface_shader SDIS_INTERFACE_SHADER_NULL =
  SDIS_INTERFACE_SHADER_NULL__;

BEGIN_DECLS

/*******************************************************************************
 * Stardis Device. It is an handle toward the Stardis library. It manages the
 * Stardis resources.
 ******************************************************************************/
SDIS_API res_T
sdis_device_create
  (struct logger* logger, /* May be NULL <=> use default logger */
   struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const unsigned nthreads_hint, /* Hint on the number of threads to use */
   const int verbose,
   struct sdis_device** dev);

SDIS_API res_T
sdis_device_ref_get
  (struct sdis_device* dev);

SDIS_API res_T
sdis_device_ref_put
  (struct sdis_device* dev);

/*******************************************************************************
 * A data stores in the Stardis memory space a set of user defined data.
 ******************************************************************************/
SDIS_API res_T
sdis_data_create
  (struct sdis_device* dev,
   const size_t size, /* Size in bytes */
   const size_t align, /* Data alignment. Must be a power of 2 */
   void (*release)(void*),/* Invoked priorly to data destruction. May be NULL */
   struct sdis_data** data);

SDIS_API res_T
sdis_data_ref_get
  (struct sdis_data* data);

SDIS_API res_T
sdis_data_ref_put
  (struct sdis_data* data);

SDIS_API void*
sdis_data_get
  (struct sdis_data* data);

SDIS_API const void*
sdis_data_cget
  (const struct sdis_data* data);

/*******************************************************************************
 * A medium encapsulates the properties of either a fluid or a solid.
 ******************************************************************************/
SDIS_API res_T
sdis_fluid_create
  (struct sdis_device* dev,
   const struct sdis_fluid_shader* shader,
   struct sdis_data* data, /* Data sent to the shader. May be NULL */
   struct sdis_medium** fluid);

SDIS_API res_T
sdis_solid_create
  (struct sdis_device* dev,
   const struct sdis_solid_shader* shader,
   struct sdis_data* data, /* Data send to the shader. May be NULL */
   struct sdis_medium** solid);

SDIS_API res_T
sdis_medium_ref_get
  (struct sdis_medium* medium);

SDIS_API res_T
sdis_medium_ref_put
  (struct sdis_medium* medium);

SDIS_API enum sdis_medium_type
sdis_medium_get_type
  (const struct sdis_medium* medium);

/*******************************************************************************
 * An interface is the boundary between 2 mediums.
 ******************************************************************************/
SDIS_API res_T
sdis_interface_create
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const struct sdis_interface_shader* shader,
   struct sdis_data* data, /* Data sent to the shader. May be NULL */
   struct sdis_interface** interface);

SDIS_API res_T
sdis_interface_ref_get
  (struct sdis_interface* interface);

SDIS_API res_T
sdis_interface_ref_put
  (struct sdis_interface* interface);

/*******************************************************************************
 * A scene is a collection of triangles. Each triangle is the support of the
 * interface between 2 mediums.
 ******************************************************************************/
SDIS_API res_T
sdis_scene_create
  (struct sdis_device* dev,
   const size_t ntris, /* #triangles */
   void (*indices)(const size_t itri, size_t ids[3], void*),
   void (*interface)(const size_t itri, struct sdis_interface** bound, void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[3], void* ctx),
   void* ctx,
   struct sdis_scene** scn);

SDIS_API res_T
sdis_scene_ref_get
  (struct sdis_scene* scn);

SDIS_API res_T
sdis_scene_ref_put
  (struct sdis_scene* scn);

SDIS_API res_T
sdis_scene_get_aabb
  (const struct sdis_scene* scn,
   double lower[3],
   double upper[3]);

/*******************************************************************************
 * An estimator stores the state of a simulation
 ******************************************************************************/
SDIS_API res_T
sdis_estimator_ref_get
  (struct sdis_estimator* estimator);

SDIS_API res_T
sdis_estimator_ref_put
  (struct sdis_estimator* estimator);

SDIS_API res_T
sdis_estimator_get_realisation_count
  (const struct sdis_estimator* estimator,
   size_t* nrealisations);

SDIS_API res_T
sdis_estimator_get_failure_count
  (const struct sdis_estimator* estimator,
   size_t* nfailures);

SDIS_API res_T
sdis_estimator_get_temperature
  (const struct sdis_estimator* estimator,
   struct sdis_mc* temperature);

/*******************************************************************************
 * Miscellaneous functions
 ******************************************************************************/
SDIS_API res_T
sdis_solve_probe_temperature
  (struct sdis_scene* scn,
   const size_t nrealisations,
   const double position[3],
   const double time,
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   struct sdis_estimator** estimator);

END_DECLS

#endif /* SDIS_H */

