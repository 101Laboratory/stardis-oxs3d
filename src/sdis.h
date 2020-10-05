/* Copyright (C) 2016-2020 |Meso|Star> (contact@meso-star.com)
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
#include <float.h>

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

#define SDIS_VOLUMIC_POWER_NONE 0 /* <=> No volumic power */
#define SDIS_FLUX_NONE DBL_MAX /* <=> No flux */
#define SDIS_PRIMITIVE_NONE SIZE_MAX /* Invalid primitive */

/* Forward declaration of external opaque data types */
struct logger;
struct mem_allocator;
struct senc2d_scene;
struct senc3d_scene;
struct ssp_rng;

/* Forward declaration of the Stardis opaque data types. These data types are
 * ref counted. Once created the caller implicitly owns the created data, i.e.
 * its reference counter is set to 1. The sdis_<TYPE>_ref_<get|put> functions
 * get or release a reference on the data, i.e. they increment or decrement the
 * reference counter, respectively. When this counter reaches 0, the object is
 * silently destroyed and cannot be used anymore. */
struct sdis_camera;
struct sdis_data;
struct sdis_device;
struct sdis_estimator;
struct sdis_estimator_buffer;
struct sdis_green_function;
struct sdis_interface;
struct sdis_medium;
struct sdis_scene;

/* Forward declaration of non ref counted types */
struct sdis_heat_path;

/*******************************************************************************
 * Miscellaneous data types
 ******************************************************************************/
enum sdis_side {
  SDIS_FRONT,
  SDIS_BACK,
  SDIS_SIDE_NULL__
};

enum sdis_scene_dimension {
  SDIS_SCENE_2D,
  SDIS_SCENE_3D
};

/* Random walk vertex, i.e. a spatiotemporal position at a given step of the
 * random walk. */
struct sdis_rwalk_vertex {
  double P[3]; /* World space position */
  double time; /* "Time" of the vertex */
};
#define SDIS_RWALK_VERTEX_NULL__ {{0}, -1}
static const struct sdis_rwalk_vertex SDIS_RWALK_VERTEX_NULL =
  SDIS_RWALK_VERTEX_NULL__;

/* Spatiotemporal position onto an interface. As a random walk vertex, it
 * stores the position and time of the random walk, but since it lies onto an
 * interface, it has additionnal parameters as the normal of the interface and
 * the parametric coordinate of the position onto the interface */
struct sdis_interface_fragment {
  double P[3]; /* World space position */
  double Ng[3]; /* Normalized world space geometry normal at the interface */
  double uv[2]; /* Parametric coordinates of the interface */
  double time; /* Current time */
  enum sdis_side side;
};
#define SDIS_INTERFACE_FRAGMENT_NULL__ {{0}, {0}, {0}, -1, SDIS_SIDE_NULL__}
static const struct sdis_interface_fragment SDIS_INTERFACE_FRAGMENT_NULL =
  SDIS_INTERFACE_FRAGMENT_NULL__;

/*******************************************************************************
 * Estimation data types
 ******************************************************************************/
enum sdis_estimator_type {
  SDIS_ESTIMATOR_TEMPERATURE, /* In Kelvin */
  SDIS_ESTIMATOR_FLUX, /* In Watt/m^2 */
  SDIS_ESTIMATOR_POWER, /* In Watt */
  SDIS_ESTIMATOR_TYPES_COUNT__
};

/* Monte-Carlo estimation */
struct sdis_mc {
  double E; /* Expected value */
  double V; /* Variance */
  double SE; /* Standard error */
};
#define SDIS_MC_NULL__ {0, 0, 0}
static const struct sdis_mc SDIS_MC_NULL = SDIS_MC_NULL__;

/*******************************************************************************
 * Data type used to describe physical properties
 ******************************************************************************/
enum sdis_medium_type {
  SDIS_FLUID,
  SDIS_SOLID,
  SDIS_MEDIUM_TYPES_COUNT__
};

/* Functor type used to retrieve the spatio temporal physical properties of a
 * medium. */
typedef double
(*sdis_medium_getter_T)
  (const struct sdis_rwalk_vertex* vert,
   struct sdis_data* data);

/* Functor type used to retrieve the spatio temporal physical properties of an
 * interface. */
typedef double
(*sdis_interface_getter_T)
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data);

/* Define the physical properties of a solid */
struct sdis_solid_shader {
  /* Properties */
  sdis_medium_getter_T calorific_capacity; /* In J.K^-1.kg^-1 */
  sdis_medium_getter_T thermal_conductivity; /* In W.m^-1.K^-1 */
  sdis_medium_getter_T volumic_mass; /* In kg.m^-3 */
  sdis_medium_getter_T delta_solid;

  /* May be NULL if there is no volumic power. One can also return
   * SDIS_VOLUMIC_POWER_NONE to define that there is no volumic power at the
   * submitted position and time */
  sdis_medium_getter_T volumic_power;  /* In W.m^-3 */

  /* Initial/limit condition. A temperature < 0 means that the temperature is
   * unknown for the submitted random walk vertex.
   * This getter is always called at time >= t0 (see below). */
  sdis_medium_getter_T temperature;
  /* The time until the initial condition is maintained for this solid;
   * can neither be negative nor infinity, default is 0. */
  double t0;
};
#define SDIS_SOLID_SHADER_NULL__ {NULL, NULL, NULL, NULL, NULL, NULL, 0}
static const struct sdis_solid_shader SDIS_SOLID_SHADER_NULL =
  SDIS_SOLID_SHADER_NULL__;

/* Define the physical properties of a fluid */
struct sdis_fluid_shader {
  /* Properties */
  sdis_medium_getter_T calorific_capacity; /* In J.K^-1.kg^-1 */
  sdis_medium_getter_T volumic_mass; /* In kg.m^-3 */

  /* Initial/limit condition. A temperature < 0 means that the temperature is
   * unknown for the submitted random walk vertex.
   * This getter is always called at time >= t0 (see below). */
  sdis_medium_getter_T temperature;
  /* The time until the initial condition is maintained for this fluid;
   * can neither be negative nor infinity, default is 0. */
  double t0;
};
#define SDIS_FLUID_SHADER_NULL__ {NULL, NULL, NULL, 0}
static const struct sdis_fluid_shader SDIS_FLUID_SHADER_NULL =
  SDIS_FLUID_SHADER_NULL__;

/* Define the physical properties of one side of an interface. */
struct sdis_interface_side_shader {
  /* Fixed temperature/flux. May be NULL if the temperature/flux is unknown
   * onto the whole interface */
  sdis_interface_getter_T temperature;  /* In Kelvin. < 0 <=> Unknown temp */
  sdis_interface_getter_T flux; /* In W.m^-2. SDIS_FLUX_NONE <=> no flux  */

  /* Control the emissivity of the interface. May be NULL for solid/solid
   * interface or if the emissivity is 0 onto the whole interface. */
  sdis_interface_getter_T emissivity; /* Overall emissivity. */
  sdis_interface_getter_T specular_fraction; /* Specular part in [0,1] */
};
#define SDIS_INTERFACE_SIDE_SHADER_NULL__ { NULL, NULL, NULL, NULL }
static const struct sdis_interface_side_shader SDIS_INTERFACE_SIDE_SHADER_NULL =
  SDIS_INTERFACE_SIDE_SHADER_NULL__;

/* Define the physical properties of an interface between 2 media .*/
struct sdis_interface_shader {
  /* May be NULL for solid/solid or if the convection coefficient is 0 onto
   * the whole interface. */
  sdis_interface_getter_T convection_coef;  /* In W.K^-1.m^-2 */
  /* Under no circumstance can convection_coef() return outside of
   * [0 convection_coef_upper_bound] */
  double convection_coef_upper_bound;

  struct sdis_interface_side_shader front;
  struct sdis_interface_side_shader back;
};
#define SDIS_INTERFACE_SHADER_NULL__ \
  {NULL, 0, SDIS_INTERFACE_SIDE_SHADER_NULL__, SDIS_INTERFACE_SIDE_SHADER_NULL__}
static const struct sdis_interface_shader SDIS_INTERFACE_SHADER_NULL =
  SDIS_INTERFACE_SHADER_NULL__;

/*******************************************************************************
 * Registered heat path data types
 ******************************************************************************/
enum sdis_heat_vertex_type {
  SDIS_HEAT_VERTEX_CONDUCTION,
  SDIS_HEAT_VERTEX_CONVECTION,
  SDIS_HEAT_VERTEX_RADIATIVE
};

enum sdis_heat_path_flag {
  SDIS_HEAT_PATH_SUCCESS = BIT(0),
  SDIS_HEAT_PATH_FAILURE = BIT(1),
  SDIS_HEAT_PATH_ALL = SDIS_HEAT_PATH_SUCCESS | SDIS_HEAT_PATH_FAILURE,
  SDIS_HEAT_PATH_NONE = 0
};

/* Vertex of heat path v*/
struct sdis_heat_vertex {
  double P[3];
  double time;
  double weight;
  enum sdis_heat_vertex_type type;
};
#define SDIS_HEAT_VERTEX_NULL__ {{0,0,0}, 0, 0, SDIS_HEAT_VERTEX_CONDUCTION}
static const struct sdis_heat_vertex SDIS_HEAT_VERTEX_NULL =
  SDIS_HEAT_VERTEX_NULL__;

/* Functor used to process a heat path registered against the estimator */
typedef res_T
(*sdis_process_heat_path_T)
  (const struct sdis_heat_path* path,
   void* context);

/* Functor used to process the vertices of a heat path */
typedef res_T
(*sdis_process_heat_vertex_T)
  (const struct sdis_heat_vertex* vertex,
   void* context);

/*******************************************************************************
 * Green function data types
 ******************************************************************************/
enum sdis_point_type {
  SDIS_FRAGMENT,
  SDIS_VERTEX,
  SDIS_POINT_TYPES_COUNT__,
  SDIS_POINT_NONE = SDIS_POINT_TYPES_COUNT__
};

/* Path used to estimate the green function */
struct sdis_green_path {
  /* Internal data. Should not be accessed */
  void* green__;
  size_t id__;
};
#define SDIS_GREEN_PATH_NULL__ {NULL, 0}
static const struct sdis_green_path SDIS_GREEN_PATH_NULL =
  SDIS_GREEN_PATH_NULL__;

/* Spatio temporal point */
struct sdis_point {
  union {
    struct {
      struct sdis_medium* medium;
      struct sdis_rwalk_vertex vertex;
    } mdmvert; /* Medium and a vertex into it */
    struct {
      struct sdis_interface* intface;
      struct sdis_interface_fragment fragment;
    } itfrag; /* Interface and a fragmetn onto it */
  } data;
  enum sdis_point_type type;
};
#define SDIS_POINT_NULL__ { {{NULL, SDIS_RWALK_VERTEX_NULL__}}, SDIS_POINT_NONE}
static const struct sdis_point SDIS_POINT_NULL = SDIS_POINT_NULL__;

/* Functor used to process the paths registered against the green function */
typedef res_T
(*sdis_process_green_path_T)
  (struct sdis_green_path* path,
   void* context);

/* Functor used to process the power factor registered along a green path for a
 * given medium */
typedef res_T
(*sdis_process_medium_power_term_T)
  (struct sdis_medium* medium,
   const double power_term,
   void* context);

/* Functor used to process the flux factor registered along a green path for a
 * given interface side */
typedef res_T
(*sdis_process_interface_flux_term_T)
  (struct sdis_interface* interf,
   const enum sdis_side side,
   const double flux_term,
   void* context);

/*******************************************************************************
 * Data types of the input simulation parameters
 ******************************************************************************/
struct sdis_solve_probe_args {
  size_t nrealisations; /* #realisations */
  double position[3]; /* Probe position */
  double time_range[2]; /* Observation time */
  double fp_to_meter; /* Scale from floating point units to meters */
  double ambient_radiative_temperature; /* In Kelvin */
  double reference_temperature; /* In Kelvin */
  int register_paths; /* Combination of enum sdis_heat_path_flag */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
};
#define SDIS_SOLVE_PROBE_ARGS_DEFAULT__ {                                      \
  10000, /* #realisations */                                                   \
  {0,0,0}, /* Position  */                                                     \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1.0, /* FP to meter */                                                       \
  -1, /* Ambient radiative temperature */                                      \
  -1, /* Reference temperature */                                              \
  SDIS_HEAT_PATH_NONE, /* Register paths mask */                               \
  NULL /* RNG state */                                                         \
}
static const struct sdis_solve_probe_args SDIS_SOLVE_PROBE_ARGS_DEFAULT =
  SDIS_SOLVE_PROBE_ARGS_DEFAULT__;

/* Arguments of a probe simulation */
struct sdis_solve_probe_boundary_args {
  size_t nrealisations; /* #realisations */
  size_t iprim; /* Identifier of the primitive on which the probe lies */
  double uv[2]; /* Parametric coordinates of the probe onto the primitve */
  double time_range[2]; /* Observation time */
  enum sdis_side side; /* Side of iprim on which the probe lies */
  double fp_to_meter; /* Scale from floating point units to meters */
  double ambient_radiative_temperature; /* In Kelvin */
  double reference_temperature; /* In Kelvin */
  int register_paths; /* Combination of enum sdis_heat_path_flag */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
};
#define SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT__ {                             \
  10000, /* #realisations */                                                   \
  0, /* Primitive identifier */                                                \
  {0,0}, /* UV */                                                              \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  SDIS_SIDE_NULL__,                                                            \
  1, /* FP to meter */                                                         \
  -1, /* Ambient radiative temperature */                                      \
  -1, /* Reference temperature */                                              \
  SDIS_HEAT_PATH_NONE,                                                         \
  NULL /* RNG state */                                                         \
}
static const struct sdis_solve_probe_boundary_args
SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT =
  SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT__;

struct sdis_solve_boundary_args {
  size_t nrealisations; /* #realisations */
  const size_t* primitives; /* List of boundary primitives to handle */
  const enum sdis_side* sides; /* Per primitive side to consider */
  size_t nprimitives; /* #primitives */
  double time_range[2]; /* Observation time */
  double fp_to_meter; /* Scale from floating point units to meters */
  double ambient_radiative_temperature; /* In Kelvin */
  double reference_temperature; /* In Kelvin */
  int register_paths; /* Combination of enum sdis_heat_path_flag */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
};
#define SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT__ {                                   \
  10000, /* #realisations */                                                   \
  NULL, /* List or primitive ids */                                            \
  NULL, /* Per primitive side */                                               \
  0, /* #primitives */                                                         \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* FP to meter */                                                         \
  -1, /* Ambient radiative temperature */                                      \
  -1, /* Reference temperature */                                              \
  SDIS_HEAT_PATH_NONE,                                                         \
  NULL /* RNG state */                                                         \
}
static const struct sdis_solve_boundary_args SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT =
  SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT__;

struct sdis_solve_medium_args {
  size_t nrealisations; /* #realisations */
  struct sdis_medium* medium; /* Medium to solve */
  double time_range[2]; /* Observation time */
  double fp_to_meter; /* Scale from floating point units to meters */
  double ambient_radiative_temperature; /* In Kelvin */
  double reference_temperature; /* In Kelvin */
  int register_paths; /* Combination of enum sdis_heat_path_flag */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
};
#define SDIS_SOLVE_MEDIUM_ARGS_DEFAULT__ {                                     \
  10000, /* #realisations */                                                   \
  NULL, /* Medium */                                                           \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* FP to meter */                                                         \
  -1, /* Ambient radiative temperature */                                      \
  -1, /* Reference temperature */                                              \
  SDIS_HEAT_PATH_NONE,                                                         \
  NULL /* RNG state */                                                         \
}
static const struct sdis_solve_medium_args SDIS_SOLVE_MEDIUM_ARGS_DEFAULT =
  SDIS_SOLVE_MEDIUM_ARGS_DEFAULT__;

struct sdis_solve_probe_boundary_flux_args {
  size_t nrealisations; /* #realisations */
  size_t iprim; /* Identifier of the primitive on which the probe lies */
  double uv[2]; /* Parametric coordinates of the probe onto the primitve */
  double time_range[2]; /* Observation time */
  double fp_to_meter; /* Scale from floating point units to meters */
  double ambient_radiative_temperature; /* In Kelvin */
  double reference_temperature; /* In Kelvin */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
};
#define SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT__ {                        \
  10000, /* #realisations */                                                   \
  0, /* Primitive identifier */                                                \
  {0,0}, /* UV */                                                              \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* FP to meter */                                                         \
  -1, /* Ambient radiative temperature */                                      \
  -1, /* Reference temperature */                                              \
  NULL /* RNG state */                                                         \
}
static const struct sdis_solve_probe_boundary_flux_args
SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT =
  SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT__;

struct sdis_solve_boundary_flux_args {
  size_t nrealisations; /* #realisations */
  const size_t* primitives; /* List of boundary primitives to handle */
  size_t nprimitives; /* #primitives */
  double time_range[2]; /* Observation time */
  double fp_to_meter; /* Scale from floating point units to meters */
  double ambient_radiative_temperature; /* In Kelvin */
  double reference_temperature; /* In Kelvin */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
};
#define SDIS_SOLVE_BOUNDARY_FLUX_ARGS_DEFAULT__ {                              \
  10000, /* #realisations */                                                   \
  NULL, /* List or primitive ids */                                            \
  0, /* #primitives */                                                         \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* FP to meter */                                                         \
  -1, /* Ambient radiative temperature */                                      \
  -1, /* Reference temperature */                                              \
  NULL /* RNG state */                                                         \
}
static const struct sdis_solve_boundary_flux_args
SDIS_SOLVE_BOUNDARY_FLUX_ARGS_DEFAULT =
  SDIS_SOLVE_BOUNDARY_FLUX_ARGS_DEFAULT__;

struct sdis_solve_camera_args {
  struct sdis_camera* cam; /* Point of view */
  double time_range[2]; /* Observation time */
  double fp_to_meter; /* Scale from floating point units to meters */
  double ambient_radiative_temperature; /* In Kelvin */
  double reference_temperature; /* In Kelvin */
  size_t image_resolution[2]; /* Image resolution */
  size_t spp; /* #samples per pixel */
  int register_paths; /* Combination of enum sdis_heat_path_flag */
};
#define SDIS_SOLVE_CAMERA_ARGS_DEFAULT__ {                                     \
  NULL, /* Camera */                                                           \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* FP to meter */                                                         \
  -1, /* Ambient radiative temperature */                                      \
  -1, /* Reference temperature */                                              \
  {512,512}, /* Image resolution */                                            \
  256, /* #realisations per pixel */                                           \
  SDIS_HEAT_PATH_NONE                                                          \
}
static const struct sdis_solve_camera_args SDIS_SOLVE_CAMERA_ARGS_DEFAULT =
  SDIS_SOLVE_CAMERA_ARGS_DEFAULT__;

struct sdis_compute_power_args {
  size_t nrealisations;
  struct sdis_medium* medium; /* Medium to solve */
  double time_range[2]; /* Observation time */
  double fp_to_meter; /* Scale from floating point units to meters */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
};
#define SDIS_COMPUTE_POWER_ARGS_DEFAULT__ {                                    \
  10000, /* #realisations */                                                   \
  NULL, /* Medium */                                                           \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* FP to meter */                                                         \
  NULL /* RNG state */                                                         \
}
static const struct sdis_compute_power_args
SDIS_COMPUTE_POWER_ARGS_DEFAULT = SDIS_COMPUTE_POWER_ARGS_DEFAULT__;

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
   const int verbose, /* Verbosity level */
   struct sdis_device** dev);

SDIS_API res_T
sdis_device_ref_get
  (struct sdis_device* dev);

SDIS_API res_T
sdis_device_ref_put
  (struct sdis_device* dev);

/*******************************************************************************
 * A data stores in the Stardis memory space a set of user defined data. It can
 * be seen as a ref counted memory space allocated by Stardis. It is used to
 * attach user data to the media and to the interfaces.
 ******************************************************************************/
SDIS_API res_T
sdis_data_create
  (struct sdis_device* dev,
   const size_t size, /* Size in bytes of user defined data */
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
 * A camera describes a point of view
 ******************************************************************************/
SDIS_API res_T
sdis_camera_create
  (struct sdis_device* dev,
   struct sdis_camera** cam);

SDIS_API res_T
sdis_camera_ref_get
  (struct sdis_camera* cam);

SDIS_API res_T
sdis_camera_ref_put
  (struct sdis_camera* cam);

/* Width/height projection ratio */
SDIS_API res_T
sdis_camera_set_proj_ratio
  (struct sdis_camera* cam,
   const double proj_ratio);

SDIS_API res_T
sdis_camera_set_fov /* Horizontal field of view */
  (struct sdis_camera* cam,
   const double fov); /* In radian */

SDIS_API res_T
sdis_camera_look_at
  (struct sdis_camera* cam,
   const double position[3],
   const double target[3],
   const double up[3]);

/*******************************************************************************
 * An estimator buffer is 2D array of estimators
******************************************************************************/
SDIS_API res_T
sdis_estimator_buffer_ref_get
  (struct sdis_estimator_buffer* buf);

SDIS_API res_T
sdis_estimator_buffer_ref_put
  (struct sdis_estimator_buffer* buf);

SDIS_API res_T
sdis_estimator_buffer_get_definition
  (const struct sdis_estimator_buffer* buf,
   size_t definition[2]);

SDIS_API res_T
sdis_estimator_buffer_at
  (const struct sdis_estimator_buffer* buf,
   const size_t x,
   const size_t y,
   const struct sdis_estimator** estimator);

SDIS_API res_T
sdis_estimator_buffer_get_realisation_count
  (const struct sdis_estimator_buffer* buf,
   size_t* nrealisations); /* Successful ones */

SDIS_API res_T
sdis_estimator_buffer_get_failure_count
  (const struct sdis_estimator_buffer* buf,
   size_t* nfailures);

SDIS_API res_T
sdis_estimator_buffer_get_temperature
  (const struct sdis_estimator_buffer* buf,
   struct sdis_mc* temperature);

SDIS_API res_T
sdis_estimator_buffer_get_realisation_time
  (const struct sdis_estimator_buffer* buf,
   struct sdis_mc* time);

SDIS_API res_T
sdis_estimator_buffer_get_rng_state
  (const struct sdis_estimator_buffer* buf,
   struct ssp_rng** rng_state);

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
sdis_fluid_get_shader
  (const struct sdis_medium* fluid,
   struct sdis_fluid_shader* shader);

SDIS_API res_T
sdis_solid_create
  (struct sdis_device* dev,
   const struct sdis_solid_shader* shader,
   struct sdis_data* data, /* Data send to the shader. May be NULL */
   struct sdis_medium** solid);

SDIS_API res_T
sdis_solid_get_shader
  (const struct sdis_medium* solid,
   struct sdis_solid_shader* shader);

SDIS_API res_T
sdis_medium_ref_get
  (struct sdis_medium* medium);

SDIS_API res_T
sdis_medium_ref_put
  (struct sdis_medium* medium);

SDIS_API enum sdis_medium_type
sdis_medium_get_type
  (const struct sdis_medium* medium);

SDIS_API struct sdis_data*
sdis_medium_get_data
  (struct sdis_medium* medium);

SDIS_API unsigned
sdis_medium_get_id
  (const struct sdis_medium* medium);

/*******************************************************************************
 * An interface is the boundary between 2 media.
 ******************************************************************************/
SDIS_API res_T
sdis_interface_create
  (struct sdis_device* dev,
   struct sdis_medium* front, /* Medium on the front side of the geometry */
   struct sdis_medium* back, /* Medium on the back side of the geometry */
   const struct sdis_interface_shader* shader,
   struct sdis_data* data, /* Data sent to the shader. May be NULL */
   struct sdis_interface** interf);

SDIS_API res_T
sdis_interface_ref_get
  (struct sdis_interface* interf);

SDIS_API res_T
sdis_interface_ref_put
  (struct sdis_interface* interf);

SDIS_API res_T
sdis_interface_get_shader
  (const struct sdis_interface* interf,
   struct sdis_interface_shader* shader);

SDIS_API struct sdis_data*
sdis_interface_get_data
  (struct sdis_interface* interf);

SDIS_API unsigned
sdis_interface_get_id
  (const struct sdis_interface* interf);

/*******************************************************************************
 * A scene is a collection of primitives. Each primitive is the geometric
 * support of the interface between 2 media.
 ******************************************************************************/
/* Create a 3D scene. The geometry of the scene is defined by an indexed
 * triangular mesh: each triangle is composed of 3 indices where each index
 * references an absolute 3D position. The physical properties of an interface
 * is defined by the interface of the triangle.
 *
 * No duplicate is allowed, either vertex or triangle. No degenerated triangle
 * is allowed.
 *
 * Note that each triangle has 2 sides: a front and a back side. By convention,
 * the front side of a triangle is the side where its vertices are clock wise
 * ordered.  The back side of a triangle is the exact opposite: it is the side
 * where the triangle vertices are counter-clock wise ordered. The front and
 * back media of a triangle interface directly refer to this convention and
 * thus one has to take care of how the triangle vertices are defined to ensure
 * that the front and the back media are correctly defined wrt the geometry. */
SDIS_API res_T
sdis_scene_create
  (struct sdis_device* dev,
   const size_t ntris, /* #triangles */
   void (*indices) /* Retrieve the indices toward the vertices of `itri' */
    (const size_t itri, size_t ids[3], void* ctx),
   void (*interf) /* Get the interface of the triangle `itri' */
    (const size_t itri, struct sdis_interface** bound, void* ctx),
   const size_t nverts, /* #vertices */
   void (*position) /* Retrieve the position of the vertex `ivert' */
    (const size_t ivert, double pos[3], void* ctx),
   void* ctx, /* Client side data sent as input of the previous callbacks */
   struct sdis_scene** scn);

/* Create a 2D scene. The geometry of the 2D scene is defined by an indexed
 * line segments: each segment is composed of 2 indices where each index
 * references an absolute 2D position. The physical properties of an interface
 * is defined by the interface of the segment.
 *
 * No duplicate is allowed, either vertex or segment. No degenerated segment is
 * allowed.
 *
 * Note that each segment has 2 sides: a front and a back side. By convention,
 * the front side of a segment is the side where its vertices are clock wise
 * ordered. The back side of a segment is the exact opposite: it is the side
 * where the segment vertices are counter-clock wise ordered. The front and
 * back media of a segment interface directly refer to this convention and
 * thus one has to take care of how the segment vertices are defined to ensure
 * that the front and the back media are correctly defined wrt the geometry. */
SDIS_API res_T
sdis_scene_2d_create
  (struct sdis_device* dev,
   const size_t nsegs, /* #segments */
   void (*indices) /* Retrieve the indices toward the vertices of `iseg' */
    (const size_t iseg, size_t ids[2], void* ctx),
   void (*interf) /* Get the interface of the segment `iseg' */
    (const size_t iseg, struct sdis_interface** bound, void* ctx),
   const size_t nverts, /* #vertices */
   void (*position) /* Retrieve the position of the vertex `ivert' */
    (const size_t ivert, double pos[2], void* ctx),
   void* ctx, /* Client side data sent as input of the previous callbacks */
   struct sdis_scene** scn);

SDIS_API res_T
sdis_scene_ref_get
  (struct sdis_scene* scn);

SDIS_API res_T
sdis_scene_ref_put
  (struct sdis_scene* scn);

/* Retrieve the Axis Aligned Bounding Box of the scene */
SDIS_API res_T
sdis_scene_get_aabb
  (const struct sdis_scene* scn,
   double lower[3],
   double upper[3]);

/* Search the point onto the scene geometry that is the closest of `pos'. The
 * `radius' parameter controls the maximum search distance around `pos'. The
 * returned closest point is expressed locally to the geometric primitive onto
 * which it lies. If not found, the returned primitive is SDIS_PRIMITIVE_NONE.
 * Note that even though only one point is returned, several position can have
 * the same minimal distance to the queried position. */
SDIS_API res_T
sdis_scene_find_closest_point
  (const struct sdis_scene* scn,
   const double pos[3], /* Query position */
   const double radius, /* Maximum search distance around pos */
   size_t* iprim, /* Primitive index onto which the closest point lies */
   double uv[2]); /* Parametric cordinate onto the primitive */

/* Define the world space position of a point onto the primitive `iprim' whose
 * parametric coordinate is uv. */
SDIS_API res_T
sdis_scene_get_boundary_position
  (const struct sdis_scene* scn,
   const size_t iprim, /* Primitive index */
   const double uv[2], /* Parametric coordinate onto the primitive */
   double pos[3]); /* World space position */

/* roject a world space position onto a primitive wrt its normal and compute
 * the parametric coordinates of the projected point onto the primitive. This
 * function may help to define the probe position onto a boundary as expected
 * by the sdis_solve_probe_boundary function.
 *
 * Note that the projected point can lie outside the submitted primitive. In
 * this case, the parametric coordinates are clamped against the primitive
 * boundaries in order to ensure that the returned parametric coordinates are
 * valid according to the primitive. To ensure this, in 2D, the parametric
 * coordinate is simply clamped to [0, 1]. In 3D, the `uv' coordinates are
 * clamped against the triangle edges. For instance, let the
 * following triangle whose vertices are `a', `b' and `c':
 *            ,     ,
 *             , B ,
 *              , ,
 *               b         E1
 *      E0      / \    ,P
 *             /   \,*'
 *            /     \
 *       ....a-------c......
 *          '         '
 *       A '    E2     '  C
 *        '             '
 * The projected point `P' is orthogonally wrapped to the edge `ab', `bc' or
 * `ca' if it lies in the `E0', `E1' or `E2' region, respectively. If `P' is in
 * the `A', `B' or `C' region, then it is taken back to the `a', `b' or `c'
 * vertex, respectively. */
SDIS_API res_T
sdis_scene_boundary_project_position
  (const struct sdis_scene* scn,
   const size_t iprim,
   const double pos[3],
   double uv[]);

/* Get the 2D scene's enclosures. Only defined for a 2D scene. */
SDIS_API res_T
sdis_scene_get_senc2d_scene
  (struct sdis_scene* scn,
   struct senc2d_scene** senc2d_scn);

/* Get the 3D scene's enclosures. Only defined for a 3D scene. */
SDIS_API res_T
sdis_scene_get_senc3d_scene
  (struct sdis_scene* scn,
   struct senc3d_scene** senc3d_scn);

SDIS_API res_T
sdis_scene_get_dimension
  (const struct sdis_scene* scn,
   enum sdis_scene_dimension* dim);

/* Return the area/volume of occupied by a medium in a 2D/3D scene. Only
 * enclosed media are handled, i.e. media whose border are explicitly defined
 * by a geometry. */
SDIS_API res_T
sdis_scene_get_medium_spread
  (struct sdis_scene* scn,
   const struct sdis_medium* mdm,
   double* spread);

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
sdis_estimator_get_type
  (const struct sdis_estimator* estimator,
   enum sdis_estimator_type* type);

SDIS_API res_T
sdis_estimator_get_realisation_count
  (const struct sdis_estimator* estimator,
   size_t* nrealisations); /* Successful ones */

SDIS_API res_T
sdis_estimator_get_failure_count
  (const struct sdis_estimator* estimator,
   size_t* nfailures);

SDIS_API res_T
sdis_estimator_get_temperature
  (const struct sdis_estimator* estimator,
   struct sdis_mc* temperature);

SDIS_API res_T
sdis_estimator_get_realisation_time
  (const struct sdis_estimator* estimator,
   struct sdis_mc* time);

SDIS_API res_T
sdis_estimator_get_convective_flux
  (const struct sdis_estimator* estimator,
   struct sdis_mc* flux);

SDIS_API res_T
sdis_estimator_get_radiative_flux
  (const struct sdis_estimator* estimator,
   struct sdis_mc* flux);

SDIS_API res_T
sdis_estimator_get_imposed_flux
  (const struct sdis_estimator* estimator,
   struct sdis_mc* flux);

SDIS_API res_T
sdis_estimator_get_total_flux
  (const struct sdis_estimator* estimator,
   struct sdis_mc* flux);

SDIS_API res_T
sdis_estimator_get_power
  (const struct sdis_estimator* estimator,
   struct sdis_mc* power);

SDIS_API res_T
sdis_estimator_get_paths_count
  (const struct sdis_estimator* estimator,
   size_t* npaths);

SDIS_API res_T
sdis_estimator_get_path
  (const struct sdis_estimator* estimator,
   const size_t ipath,
   const struct sdis_heat_path** path);

SDIS_API res_T
sdis_estimator_for_each_path
  (const struct sdis_estimator* estimator,
   sdis_process_heat_path_T func,
   void* context);

/* Retrieve the RNG state at the end of the simulation. */
SDIS_API res_T
sdis_estimator_get_rng_state
  (const struct sdis_estimator* estimator,
   /* The returned value may be NULL as for instance an estimator retrieved
    * from an estimator buffer */
   struct ssp_rng** rng_state);

/*******************************************************************************
 * The green function saves the estimation of the propagator
 ******************************************************************************/
SDIS_API res_T
sdis_green_function_ref_get
  (struct sdis_green_function* green);

SDIS_API res_T
sdis_green_function_ref_put
  (struct sdis_green_function* green);

SDIS_API res_T
sdis_green_function_solve
  (struct sdis_green_function* green,
   struct sdis_estimator** estimator);

SDIS_API res_T
sdis_green_function_write
  (struct sdis_green_function* green,
   FILE* stream);

SDIS_API res_T
sdis_green_function_create_from_stream
  (struct sdis_scene* scn, /* Scene from which the green was evaluated */
   FILE* stream, /* Stream into which the green was serialized */
   struct sdis_green_function** green);

/* Retrieve the number of valid paths used to estimate the green function. It
 * is actually equal to the number of successful realisations. */
SDIS_API res_T
sdis_green_function_get_paths_count
  (const struct sdis_green_function* green,
   size_t* npaths);

/* Retrieve the number of rejected paths during the estimation of the green
 * function due to numerical issues and data inconsistency */
SDIS_API res_T
sdis_green_function_get_invalid_paths_count
  (const struct sdis_green_function* green,
   size_t* nfails);

/* Iterate over all valid green function paths */
SDIS_API res_T
sdis_green_function_for_each_path
  (struct sdis_green_function* green,
   sdis_process_green_path_T func,
   void* context);

/* Retrieve the path's elapsed time */
SDIS_API res_T
sdis_green_path_get_elapsed_time
  (struct sdis_green_path* path_handle, double* elapsed);

/* Retrieve the spatio-temporal end point of a path used to estimate the green
 * function. Note that this point went back in time from the relative
 * observation time 0. Its time is thus negative; its absolute value
 * represents the time spent by the path into the system. */
SDIS_API res_T
sdis_green_path_get_limit_point
  (struct sdis_green_path* path,
   struct sdis_point* pt);

/* Retrieve the number of "power terms" associated to a path. */
SDIS_API res_T
sdis_green_function_get_power_terms_count
  (const struct sdis_green_path* path,
   size_t* nterms);

/* Retrieve the number of "flux terms" associated to a path. */
SDIS_API res_T
sdis_green_function_get_flux_terms_count
  (const struct sdis_green_path* path,
   size_t* nterms);

/* Iterate over all "power terms" associated to the path. Multiply each term
 * by the power of their associated medium, that is assumed to be constant in
 * time and space, gives the medium power registered along the path. */
SDIS_API res_T
sdis_green_path_for_each_power_term
  (struct sdis_green_path* path,
   sdis_process_medium_power_term_T func,
   void* context);

/* Iterate over all "flux terms" associated to the path. Multiply each term by
 * the flux of their associated interface side, that is assumed to be constant
 * in time and space, gives the interface side flux registered along the path. */
SDIS_API res_T
sdis_green_path_for_each_flux_term
  (struct sdis_green_path* path,
   sdis_process_interface_flux_term_T func,
   void* context);

/*******************************************************************************
 * Heat path API
 ******************************************************************************/
SDIS_API res_T
sdis_heat_path_get_vertices_count
  (const struct sdis_heat_path* path,
   size_t* nvertices);

SDIS_API res_T
sdis_heat_path_get_status
  (const struct sdis_heat_path* path,
   enum sdis_heat_path_flag* status);

SDIS_API res_T
sdis_heat_path_get_vertex
  (const struct sdis_heat_path* path,
   const size_t ivertex,
   struct sdis_heat_vertex* vertex);

SDIS_API res_T
sdis_heat_path_for_each_vertex
  (const struct sdis_heat_path* path,
   sdis_process_heat_vertex_T func,
   void* context);

/*******************************************************************************
 * Solvers
 ******************************************************************************/
SDIS_API res_T
sdis_solve_probe
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_args* args,
   struct sdis_estimator** estimator);

SDIS_API res_T
sdis_solve_probe_boundary
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_args* args,
   struct sdis_estimator** estimator);

SDIS_API res_T
sdis_solve_boundary
  (struct sdis_scene* scn,
   const struct sdis_solve_boundary_args* args,
   struct sdis_estimator** estimator);

SDIS_API res_T
sdis_solve_probe_boundary_flux
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_flux_args* args,
   struct sdis_estimator** estimator);

SDIS_API res_T
sdis_solve_boundary_flux
  (struct sdis_scene* scn,
   const struct sdis_solve_boundary_flux_args* args,
   struct sdis_estimator** estimator);

SDIS_API res_T
sdis_solve_camera
  (struct sdis_scene* scn,
   const struct sdis_solve_camera_args* args,
   struct sdis_estimator_buffer** buf);

SDIS_API res_T
sdis_solve_medium
  (struct sdis_scene* scn,
   const struct sdis_solve_medium_args* args,
   struct sdis_estimator** estimator);

/* P = SUM(volumic_power(x)) / Nrealisations * Volume
 * power (in Watt) = time_range[0] == time_range[1]
 *  ? P : P / (time_range[1] - time_range[0]) */
SDIS_API res_T
sdis_compute_power
  (struct sdis_scene* scn,
   const struct sdis_compute_power_args* args,
   struct sdis_estimator** estimator);

/*******************************************************************************
 * Green solvers.
 *
 * Note that only the interfaces/media with flux/volumic power defined during
 * green estimation can update their flux/volumic power value for subsequent
 * sdis_green_function_solve invocations: others interfaces/media are
 * definitely registered against the green function as interfaces/media with no
 * flux/volumic power.
 *
 * Also note that the green solvers assume that the interface fluxes are
 * constant in time and space. The same applies to the volumic power of the
 * solid media.
 *
 * If these assumptions are not ensured by the caller, the behavior of the
 * estimated green function is undefined.
 ******************************************************************************/
SDIS_API res_T
sdis_solve_probe_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_args* args,
   struct sdis_green_function** green);

SDIS_API res_T
sdis_solve_probe_boundary_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_args* args,
   struct sdis_green_function** green);

SDIS_API res_T
sdis_solve_boundary_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_boundary_args* args,
   struct sdis_green_function** green);

SDIS_API res_T
sdis_solve_medium_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_medium_args* args,
   struct sdis_green_function** green);

END_DECLS

#endif /* SDIS_H */
