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

#ifndef SDIS_H
#define SDIS_H

#include <star/s2d.h>
#include <star/s3d.h>
#include <star/ssp.h>

#include <rsys/hash.h>
#include <rsys/rsys.h>

#include <float.h> /* DBL_MAX */
#include <limits.h> /* UINT_MAX */

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

#define SDIS_VOLUMIC_POWER_NONE DBL_MAX /* <=> No volumic power */
#define SDIS_FLUX_NONE DBL_MAX /* <=> No flux */
#define SDIS_PRIMITIVE_NONE SIZE_MAX /* Invalid primitive */

/* Syntactic sugar used to define whether a temperature is known or not */
#define SDIS_TEMPERATURE_NONE NaN /* Unknown temperature */
#define SDIS_TEMPERATURE_IS_KNOWN(Temp) (!IS_NaN(Temp))
#define SDIS_TEMPERATURE_IS_UNKNOWN(Temp) (IS_NaN(Temp))

/* Identifier of the internal source of radiation */
#define SDIS_INTERN_SOURCE_ID UINT_MAX

/* Forward declaration of external opaque data types */
struct logger;
struct mem_allocator;
struct senc2d_scene;
struct senc3d_scene;

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
struct sdis_radiative_env; /* Radiative environment */
struct sdis_scene;
struct sdis_source;

/* Forward declaration of non ref counted types */
struct sdis_green_path;
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

enum sdis_diffusion_algorithm {
  SDIS_DIFFUSION_DELTA_SPHERE,
  SDIS_DIFFUSION_WOS, /* Walk on Sphere */
  SDIS_DIFFUSION_ALGORITHMS_COUNT__,
  SDIS_DIFFUSION_NONE = SDIS_DIFFUSION_ALGORITHMS_COUNT__
};

/* Random walk vertex, i.e. a spatio-temporal position at a given step of the
 * random walk. */
struct sdis_rwalk_vertex {
  double P[3]; /* World space position */
  double time; /* "Time" of the vertex */
};
#define SDIS_RWALK_VERTEX_NULL__ {{0}, -1}
static const struct sdis_rwalk_vertex SDIS_RWALK_VERTEX_NULL =
  SDIS_RWALK_VERTEX_NULL__;

/* Spatio-temporal position onto an interface. As a random walk vertex, it
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

/* Ray traced in radiative environment */
struct sdis_radiative_ray {
  double dir[3]; /* Direction */
  double time; /* Time */
};
#define SDIS_RADIATIVE_RAY_NULL__ {{0,0,0}, DBL_MAX}
static const struct sdis_radiative_ray SDIS_RADIATIVE_RAY_NULL=
  SDIS_RADIATIVE_RAY_NULL__;

/* Input arguments of the sdis_device_create function */
struct sdis_device_create_args {
  struct logger* logger; /* NULL <=> default logger */
  struct mem_allocator* allocator; /* NULL <=> default allocator */
  unsigned nthreads_hint; /* Hint on the number of threads to use */
  int verbosity; /* Verbosity level */
  int no_escape_sequence; /* Rm escape sequences from log messages */

  /* Use the Message Passing Interface to distribute work between processes.
   * This option is taken into account only if Stardis-Solver is compiled with
   * MPI support */
  int use_mpi;
};
#define SDIS_DEVICE_CREATE_ARGS_DEFAULT__ {                                    \
  NULL, NULL, SDIS_NTHREADS_DEFAULT, 1, 0, 0                                   \
}
static const struct sdis_device_create_args SDIS_DEVICE_CREATE_ARGS_DEFAULT =
  SDIS_DEVICE_CREATE_ARGS_DEFAULT__;

/* Informations on the Stardis-Solver library */
struct sdis_info {
  int mpi_enabled; /* Define if Stardis-Solver was built with MPI support */
};
#define SDIS_INFO_NULL__ {0}
static const struct sdis_info SDIS_INFO_NULL = SDIS_INFO_NULL__;

/* Type of functor used to retrieve the source's position relative to time */
typedef void
(*sdis_get_position_T)
  (const double time, /* [s] */
   double pos[3],
   struct sdis_data* data);

/* Type of functor used to retrieve the source's power relative to time */
typedef double
(*sdis_get_power_T)
  (const double time, /* [s] */
   struct sdis_data* data);

/* Type of functor used to retrieve the diffuse part of the external radiance */
typedef double /* [W/perpendicular m^2/sr] */
(*sdis_get_diffuse_radiance_T)
  (const double time, /* [s] */
   const double dir[3],
   struct sdis_data* data);

/* Parameters of an external spherical source */
struct sdis_spherical_source_shader {
  sdis_get_position_T position; /* [m/fp_to_meter] */
  sdis_get_power_T power; /* Total power [W] */

  /* Describes the diffuse part of the source's radiance, i.e. the radiance
   * emitted by the source and scattered at least once in the environment. This
   * parameter is actually used to approximate a semi-transparent medium. Its
   * value can be NULL, meaning that the source has not been scattered by the
   * environment, or, to put it another way, that the source is in a vacuum. */
  sdis_get_diffuse_radiance_T diffuse_radiance; /* [W/m^2/sr] */

  double radius; /* [m] */
};
#define SDIS_SPHERICAL_SOURCE_SHADER_NULL__ {NULL, NULL, NULL, 0}
static const struct sdis_spherical_source_shader
SDIS_SPHERICAL_SOURCE_SHADER_NULL = SDIS_SPHERICAL_SOURCE_SHADER_NULL__;

struct sdis_scene_find_closest_point_args {
  double position[3]; /* Query position */
  double radius; /* Maxium search distance around pos */

  /* User defined filter function */
  s2d_hit_filter_function_T filter_2d;
  s3d_hit_filter_function_T filter_3d;
  void* filter_data; /* Filter function data */
};
#define SDIS_SCENE_FIND_CLOSEST_POINT_ARGS_NULL__ {{0,0,0}, 0, NULL, NULL, NULL}
static const struct sdis_scene_find_closest_point_args
SDIS_SCENE_FIND_CLOSEST_POINT_ARGS_NULL = SDIS_SCENE_FIND_CLOSEST_POINT_ARGS_NULL__;

/* A sampled path */
struct sdis_path {
  struct sdis_rwalk_vertex vtx; /* Current position and time */

  /* Surface intersected by the path. When defined, the path is on a border */
  struct s2d_primitive prim_2d;
  struct s3d_primitive prim_3d;

  double weight; /* Monte Carlo weight update along the path */

  /* Define whether the path has reached a boundary condition in time/space */
  int at_limit;
};
#define SDIS_PATH_NULL__ {                                                     \
  SDIS_RWALK_VERTEX_NULL__,                                                    \
  S2D_PRIMITIVE_NULL__,                                                        \
  S3D_PRIMITIVE_NULL__,                                                        \
  0, /* MC weight */                                                           \
  0 /* At limit */                                                             \
}
static const struct sdis_path SDIS_PATH_NULL = SDIS_PATH_NULL__;

/* Type of functor used by the user to write the way in which the path is
 * sampled. So it's no longer Stardis that samples the path, but the user
 * through his own function. */
typedef res_T
(*sdis_sample_path_T)
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   struct sdis_path* path,
   struct sdis_data* data);

/* Key to a geometric primitive, i.e its unique identifier. Its member variables
 * must be treated as private variables, i.e. the caller must not access them
 * directly but use the primkey API functions instead (see below) */
struct sdis_primkey {
  /* List of primitive nodes sorted in ascending order */
  double nodes[9];

  /* Overall number of coordinates (4 in 2D, 9 in 3D) */
  unsigned ncoords;
};
#define SDIS_PRIMKEY_NULL__ {{0,0,0,0,0,0,0,0,0},0}
static const struct sdis_primkey SDIS_PRIMKEY_NULL = SDIS_PRIMKEY_NULL__;

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
  (const struct sdis_rwalk_vertex* vert, /* Medium position */
   struct sdis_data* data); /* User data */

/* Functor type used to retrieve the spatio temporal physical properties of an
 * interface. */
typedef double
(*sdis_interface_getter_T)
  (const struct sdis_interface_fragment* frag, /* Interface position */
   struct sdis_data* data); /* User data */

/* Type of functor for obtaining the spatio temporal physical properties of an
 * interface, as a function of the radiation source */
typedef double
(*sdis_radiative_interface_getter_T)
  (const struct sdis_interface_fragment* frag, /* Interface position */
   const unsigned source_id, /* Identifier of the radiation source */
   struct sdis_data* data); /* User data */

/* Type of functor for obtaining radiative environment properties */
typedef double
(*sdis_radiative_ray_getter_T)
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data);

/* Define the physical properties of a solid */
struct sdis_solid_shader {
  /* Properties */
  sdis_medium_getter_T calorific_capacity; /* In J.K^-1.kg^-1 */
  sdis_medium_getter_T thermal_conductivity; /* In W.m^-1.K^-1 */
  sdis_medium_getter_T volumic_mass; /* In kg.m^-3 */
  sdis_medium_getter_T delta;

  /* May be NULL if there is no volumic power. One can also return
   * SDIS_VOLUMIC_POWER_NONE to define that there is no volumic power at the
   * submitted position and time */
  sdis_medium_getter_T volumic_power;  /* In W.m^-3 */

  /* Initial/limit condition. A temperature set to SDIS_TEMPERATURE_NONE
   * means that the temperature is unknown for the submitted random walk vertex.
   * This getter is always called at time >= t0 (see below). */
  sdis_medium_getter_T temperature;

  /* Function to be used to sample the path through the solid. If not defined,
   * let stardis sample a conductive path */
  sdis_sample_path_T sample_path;

  /* The time until the initial condition is maintained for this solid.
   * Can be negative or set to +/- infinity to simulate a system that is always
   * in the initial state or never reaches it, respectively. */
  double t0;
};
#define SDIS_SOLID_SHADER_NULL__ {NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0}
static const struct sdis_solid_shader SDIS_SOLID_SHADER_NULL =
  SDIS_SOLID_SHADER_NULL__;

/* Define the physical properties of a fluid */
struct sdis_fluid_shader {
  /* Properties */
  sdis_medium_getter_T calorific_capacity; /* In J.K^-1.kg^-1 */
  sdis_medium_getter_T volumic_mass; /* In kg.m^-3 */

  /* Initial/limit condition. A temperature set to SDIS_TEMPERATURE_NONE
   * means that the temperature is unknown for the submitted random walk vertex.
   * This getter is always called at time >= t0 (see below). */
  sdis_medium_getter_T temperature;

  /* The time until the initial condition is maintained for this fluid.
   * Can be negative or set to +/- infinity to simulate a system that is always
   * in the initial state or never reaches it, respectively. */
  double t0;
};
#define SDIS_FLUID_SHADER_NULL__ {NULL, NULL, NULL, 0}
static const struct sdis_fluid_shader SDIS_FLUID_SHADER_NULL =
  SDIS_FLUID_SHADER_NULL__;

/* Define the physical properties of one side of an interface. */
struct sdis_interface_side_shader {
  /* Fixed temperature/flux. May be NULL if the temperature/flux is unknown
   * onto the whole interface */
  sdis_interface_getter_T temperature; /* [K]. SDIS_TEMPERATURE_NONE = Unknown */
  sdis_interface_getter_T flux; /* Toward solid. [W.m^-2]. SDIS_FLUX_NONE = no flux */

  /* Control the emissivity of the interface. May be NULL for solid/solid
   * interface or if the emissivity is 0 onto the whole interface. */
  sdis_radiative_interface_getter_T emissivity; /* Overall emissivity */
  sdis_radiative_interface_getter_T specular_fraction; /* Specular part in [0,1] */

  /* Reference temperature used in Picard 1 */
  sdis_interface_getter_T reference_temperature;

  /* Define whether external sources interact with the interface, i.e. whether
   * external fluxes should be processed or not */
  int handle_external_flux;
};
#define SDIS_INTERFACE_SIDE_SHADER_NULL__ { NULL, NULL, NULL, NULL, NULL, 1 }
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

  /* May be NULL for solid/fluid or if the thermal contact resistance is 0 onto
   * the whole interface. */
  sdis_interface_getter_T thermal_contact_resistance;  /* In K.m^2.W^-1 */

  struct sdis_interface_side_shader front;
  struct sdis_interface_side_shader back;
};
#define SDIS_INTERFACE_SHADER_NULL__ \
  {NULL, 0, NULL, SDIS_INTERFACE_SIDE_SHADER_NULL__, \
   SDIS_INTERFACE_SIDE_SHADER_NULL__}
static const struct sdis_interface_shader SDIS_INTERFACE_SHADER_NULL =
  SDIS_INTERFACE_SHADER_NULL__;

/* Parameters of a radiative environment */
struct sdis_radiative_env_shader {
  sdis_radiative_ray_getter_T temperature; /* [K] */
  sdis_radiative_ray_getter_T reference_temperature; /* [K] */
};
#define SDIS_RADIATIVE_ENV_SHADER_NULL__ {NULL, NULL}
static const struct sdis_radiative_env_shader SDIS_RADIATIVE_ENV_SHADER_NULL =
  SDIS_RADIATIVE_ENV_SHADER_NULL__;

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

/* Vertex of heat path */
struct sdis_heat_vertex {
  double P[3];
  double time;
  double weight;
  enum sdis_heat_vertex_type type;
  int branch_id;
};
#define SDIS_HEAT_VERTEX_NULL__ {{0,0,0}, 0, 0, SDIS_HEAT_VERTEX_CONDUCTION, 0}
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
enum sdis_green_path_end_type {
  SDIS_GREEN_PATH_END_AT_INTERFACE,
  SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV,
  SDIS_GREEN_PATH_END_IN_VOLUME,
  SDIS_GREEN_PATH_END_TYPES_COUNT__,
  SDIS_GREEN_PATH_END_ERROR = SDIS_GREEN_PATH_END_TYPES_COUNT__
};

/* Spatio temporal point */
struct sdis_green_path_end {
  union {
    /* Path end in volume */
    struct {
      struct sdis_medium* medium;
      struct sdis_rwalk_vertex vertex;
    } mdmvert;
    /* Path end at interface */
    struct {
      struct sdis_interface* intface;
      struct sdis_interface_fragment fragment;
    } itfrag;
    /* Path end in radiative environement */
    struct {
      struct sdis_radiative_env* radenv;
      struct sdis_radiative_ray ray;
    } radenvray;
  } data;
  enum sdis_green_path_end_type type;
};
#define SDIS_GREEN_PATH_END_NULL__ {                                           \
  {{NULL, SDIS_RWALK_VERTEX_NULL__}},                                          \
  SDIS_GREEN_PATH_END_ERROR                                                    \
}
static const struct sdis_green_path_end SDIS_GREEN_PATH_END_NULL =
  SDIS_GREEN_PATH_END_NULL__;

struct sdis_green_external_flux_terms {
  /* Term relative to source power [K/W] */
  double term_wrt_power;

  /* Term relative to diffuse source radiance [K/W/m^2/sr] */
  double term_wrt_diffuse_radiance;

  double time; /* [s] */
  double dir[3]; /* Direction on which term_wrt_diffuse_radiance depends */
};
#define SDIS_GREEN_EXTERNAL_FLUX_TERMS_NULL__ {0,0,0,{0,0,0}}
static const struct sdis_green_external_flux_terms
SDIS_GREEN_EXTERNAL_FLUX_TERMS_NULL = SDIS_GREEN_EXTERNAL_FLUX_TERMS_NULL__;

/* Function profile used to process the paths stored in the green function */
typedef res_T
(*sdis_process_green_path_T)
  (struct sdis_green_path* path,
   void* context);

/* Function profile used to process power factors registered along a green path
 * for a given medium */
typedef res_T
(*sdis_process_medium_power_term_T)
  (struct sdis_medium* medium,
   const double power_term, /* [K/W] */
   void* context);

/* Function profile used to process flux factors recorded along a green path for
 * a given interface side */
typedef res_T
(*sdis_process_interface_flux_term_T)
  (struct sdis_interface* interf,
   const enum sdis_side side,
   const double flux_term, /* [K/W/m^2] */
   void* context);

/* Function profile used to process external flux factors recorded along a green
 * path */
typedef res_T
(*sdis_process_external_flux_terms_T)
  (struct sdis_source* source,
   const struct sdis_green_external_flux_terms* terms,
   void* context);

/*******************************************************************************
 * Data types of scene creation
 ******************************************************************************/
/* Functor used to retrieve the indices toward the vertices of a geometric
 * primitive. Geometric primitive means for segment in 2D and triangle in 3D */
typedef void
(*sdis_get_primitive_indices_T)
  (const size_t iprim, /* Index of the primitive */
   size_t ids[], /* Output list of primitive indices */
   void* ctx); /* User defined data */

/* Retrieve the interface, i.e. the physical properties,  associated to a given
 * geometric primitive */
typedef void
(*sdis_get_primitive_interface_T)
  (const size_t iprim, /* Index of the primitive */
   struct sdis_interface** interf,
   void* ctx);

/* Retrieve the coordinates of a vertex */
typedef void
(*sdis_get_vertex_position_T)
  (const size_t ivert, /* Index of the vertex */
   double pos[], /* Output list of vertex coordinates */
   void* ctx);

struct sdis_scene_create_args {
  /* Functors to retrieve the geometric description */
  sdis_get_primitive_indices_T get_indices;
  sdis_get_primitive_interface_T get_interface;
  sdis_get_vertex_position_T get_position;

  /* Pointer toward client side sent as the last argument of the callbacks */
  void* context;

  size_t nprimitives; /* #primitives, i.e. #segments or #triangles */
  size_t nvertices; /* #vertices */
  double fp_to_meter; /* Scale factor used to convert a float in meter */

  /* Min/max temperature used to linearise the radiative temperature */
  double t_range[2];

  /* External source. Can be NULL <=> no external flux will be calculated on
   * scene interfaces */
  struct sdis_source* source;

  /* Radiative environment. Can be NULL <=> sampled radiative trajectories
   * cannot (in fact must not) reach the surrounding environment */
  struct sdis_radiative_env* radenv;
};

#define SDIS_SCENE_CREATE_ARGS_DEFAULT__ {                                     \
  NULL, /* Get indices */                                                      \
  NULL, /* Get interfaces */                                                   \
  NULL, /* Get position */                                                     \
  NULL, /* Context */                                                          \
  0, /* #primitives */                                                         \
  0, /* #vertices */                                                           \
  1.0, /* #Floating point to meter scale factor */                             \
  {SDIS_TEMPERATURE_NONE, SDIS_TEMPERATURE_NONE}, /* Temperature range */      \
  NULL, /* source */                                                           \
  NULL /* Radiative environement */                                            \
}
static const struct sdis_scene_create_args SDIS_SCENE_CREATE_ARGS_DEFAULT =
  SDIS_SCENE_CREATE_ARGS_DEFAULT__;

/*******************************************************************************
 * Data types of the input simulation parameters
 ******************************************************************************/
struct sdis_solve_probe_args {
  size_t nrealisations; /* #realisations */
  double position[3]; /* Probe position */
  double time_range[2]; /* Observation time */

  /* Set the Picard recursion order to estimate the radiative temperature. An
   * order of one means that the radiative temperature is linearized, while
   * higher orders allow the estimation of the T4 radiative transfer. */
  size_t picard_order;

  int register_paths; /* Combination of enum sdis_heat_path_flag */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
  enum ssp_rng_type rng_type; /* RNG type to use if `rng_state' is NULL */

  enum sdis_diffusion_algorithm diff_algo; /* Diffusion algorithm to be used */

  /* Signature of the estimated green function. The signature is ignored in an
   * ordinary probe estimation. The signature of the green function can be
   * queried to verify that it is the expected one with respect to the caller's
   * constraints that the green function cannot otherwise ensure */
  hash256_T signature;
};
#define SDIS_SOLVE_PROBE_ARGS_DEFAULT__ {                                      \
  10000, /* #realisations */                                                   \
  {0,0,0}, /* Position  */                                                     \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* Picard order */                                                        \
  SDIS_HEAT_PATH_NONE, /* Register paths mask */                               \
  NULL, /* RNG state */                                                        \
  SSP_RNG_THREEFRY, /* RNG type */                                             \
  SDIS_DIFFUSION_DELTA_SPHERE, /* Diffusion algorithm */                       \
  {0} /* Signature */                                                          \
}
static const struct sdis_solve_probe_args SDIS_SOLVE_PROBE_ARGS_DEFAULT =
  SDIS_SOLVE_PROBE_ARGS_DEFAULT__;

struct sdis_solve_probe_list_args {
  struct sdis_solve_probe_args* probes; /* List of probes to compute */
  size_t nprobes; /* Total number of probes */

  /* State/type of the RNG to use for the list of probes to calculate.
   * The state/type defines per probe is ignored */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
  enum ssp_rng_type rng_type; /* RNG type to use if `rng_state' is NULL */
};
#define SDIS_SOLVE_PROBE_LIST_ARGS_DEFAULT__ {                                 \
  NULL, /* List of probes */                                                   \
  0, /* #probes */                                                             \
  NULL, /* RNG state */                                                        \
  SSP_RNG_THREEFRY /* RNG type */                                              \
}
static const struct sdis_solve_probe_list_args
SDIS_SOLVE_PROBE_LIST_ARGS_DEFAULT = SDIS_SOLVE_PROBE_LIST_ARGS_DEFAULT__;

/* Arguments of a probe simulation */
struct sdis_solve_probe_boundary_args {
  size_t nrealisations; /* #realisations */
  size_t iprim; /* Identifier of the primitive on which the probe lies */
  double uv[2]; /* Parametric coordinates of the probe onto the primitve */
  double time_range[2]; /* Observation time */

  /* Set the Picard recursion order to estimate the radiative temperature. An
   * order of one means that the radiative temperature is linearized, while
   * higher orders allow the estimation of the T4 radiative transfer. */
  size_t picard_order;

  enum sdis_side side; /* Side of iprim on which the probe lies */
  int register_paths; /* Combination of enum sdis_heat_path_flag */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
  enum ssp_rng_type rng_type; /* RNG type to use if `rng_state' is NULL */

  enum sdis_diffusion_algorithm diff_algo; /* Diffusion algorithm to be used */

  /* Signature of the estimated green function. The signature is ignored in an
   * ordinary probe estimation. The signature of the green function can be
   * queried to verify that it is the expected one with respect to the caller's
   * constraints that the green function cannot otherwise ensure */
  hash256_T signature;
};
#define SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT__ {                             \
  10000, /* #realisations */                                                   \
  0, /* Primitive identifier */                                                \
  {0,0}, /* UV */                                                              \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* Picard order */                                                        \
  SDIS_SIDE_NULL__,                                                            \
  SDIS_HEAT_PATH_NONE,                                                         \
  NULL, /* RNG state */                                                        \
  SSP_RNG_THREEFRY, /* RNG type */                                             \
  SDIS_DIFFUSION_DELTA_SPHERE, /* Diffusion algorithm */                       \
  {0} /* Signature */                                                          \
}
static const struct sdis_solve_probe_boundary_args
SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT =
  SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT__;

/* Input arguments of the solve function that distributes the calculations of
 * several boundary probes rather than the realizations of a probe */
struct sdis_solve_probe_boundary_list_args {
  struct sdis_solve_probe_boundary_args* probes; /* List of probes to compute */
  size_t nprobes; /* Total number of probes */

  /* State/type of the RNG to use for the list of probes to calculate.
   * The state/type defines per probe is ignored */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
  enum ssp_rng_type rng_type; /* RNG type to use if `rng_state' is NULL */
};
#define SDIS_SOLVE_PROBE_BOUNDARY_LIST_ARGS_DEFAULT__ {                        \
  NULL, /* List of probes */                                                   \
  0, /* #probes */                                                             \
  NULL, /* RNG state */                                                        \
  SSP_RNG_THREEFRY /* RNG type */                                              \
}
static const struct sdis_solve_probe_boundary_list_args
SDIS_SOLVE_PROBE_BOUNDARY_LIST_ARGS_DEFAULT =
  SDIS_SOLVE_PROBE_BOUNDARY_LIST_ARGS_DEFAULT__;

struct sdis_solve_boundary_args {
  size_t nrealisations; /* #realisations */
  const size_t* primitives; /* List of boundary primitives to handle */
  const enum sdis_side* sides; /* Per primitive side to consider */
  size_t nprimitives; /* #primitives */
  double time_range[2]; /* Observation time */

  /* Set the Picard recursion order to estimate the radiative temperature. An
   * order of one means that the radiative temperature is linearized, while
   * higher orders allow the estimation of the T4 radiative transfer. */
  size_t picard_order;

  int register_paths; /* Combination of enum sdis_heat_path_flag */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
  enum ssp_rng_type rng_type; /* RNG type to use if `rng_state' is NULL */

  enum sdis_diffusion_algorithm diff_algo; /* Diffusion algorithm to be used */

  /* Signature of the estimated green function. The signature is ignored in an
   * ordinary probe estimation. The signature of the green function can be
   * queried to verify that it is the expected one with respect to the caller's
   * constraints that the green function cannot otherwise ensure */
  hash256_T signature;
};
#define SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT__ {                                   \
  10000, /* #realisations */                                                   \
  NULL, /* List or primitive ids */                                            \
  NULL, /* Per primitive side */                                               \
  0, /* #primitives */                                                         \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* Picard order */                                                        \
  SDIS_HEAT_PATH_NONE,                                                         \
  NULL, /* RNG state */                                                        \
  SSP_RNG_THREEFRY, /* RNG type */                                             \
  SDIS_DIFFUSION_DELTA_SPHERE, /* Diffusion algorithm */                       \
  {0} /* Signature */                                                          \
}
static const struct sdis_solve_boundary_args SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT =
  SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT__;

struct sdis_solve_medium_args {
  size_t nrealisations; /* #realisations */
  struct sdis_medium* medium; /* Medium to solve */
  double time_range[2]; /* Observation time */

  /* Set the Picard recursion order to estimate the radiative temperature. An
   * order of one means that the radiative temperature is linearized, while
   * higher orders allow the estimation of the T4 radiative transfer. */
  size_t picard_order;

  int register_paths; /* Combination of enum sdis_heat_path_flag */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
  enum ssp_rng_type rng_type; /* RNG type to use if `rng_state' is NULL */

  enum sdis_diffusion_algorithm diff_algo; /* Diffusion algorithm to be used */

  /* Signature of the estimated green function. The signature is ignored in an
   * ordinary probe estimation. The signature of the green function can be
   * queried to verify that it is the expected one with respect to the caller's
   * constraints that the green function cannot otherwise ensure */
  hash256_T signature;
};
#define SDIS_SOLVE_MEDIUM_ARGS_DEFAULT__ {                                     \
  10000, /* #realisations */                                                   \
  NULL, /* Medium */                                                           \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* Picard order */                                                        \
  SDIS_HEAT_PATH_NONE,                                                         \
  NULL, /* RNG state */                                                        \
  SSP_RNG_THREEFRY, /* RNG type */                                             \
  SDIS_DIFFUSION_DELTA_SPHERE, /* Diffusion algorithm */                       \
  {0} /* Signature */                                                          \
}
static const struct sdis_solve_medium_args SDIS_SOLVE_MEDIUM_ARGS_DEFAULT =
  SDIS_SOLVE_MEDIUM_ARGS_DEFAULT__;

struct sdis_solve_probe_boundary_flux_args {
  size_t nrealisations; /* #realisations */
  size_t iprim; /* Identifier of the primitive on which the probe lies */
  double uv[2]; /* Parametric coordinates of the probe onto the primitive */
  double time_range[2]; /* Observation time */

  /* Set the Picard recursion order to estimate the radiative temperature. An
   * order of one means that the radiative temperature is linearized, while
   * higher orders allow the estimation of the T4 radiative transfer. */
  size_t picard_order;

  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
  enum ssp_rng_type rng_type; /* RNG type to use if `rng_state' is NULL */

  enum sdis_diffusion_algorithm diff_algo; /* Diffusion algorithm to be used */
};
#define SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT__ {                        \
  10000, /* #realisations */                                                   \
  0, /* Primitive identifier */                                                \
  {0,0}, /* UV */                                                              \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* Picard order */                                                        \
  NULL, /* RNG state */                                                        \
  SSP_RNG_THREEFRY, /* RNG type */                                             \
  SDIS_DIFFUSION_DELTA_SPHERE /* Diffusion algorithm */                        \
}
static const struct sdis_solve_probe_boundary_flux_args
SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT =
  SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT__;

struct sdis_solve_boundary_flux_args {
  size_t nrealisations; /* #realisations */
  const size_t* primitives; /* List of boundary primitives to handle */
  size_t nprimitives; /* #primitives */
  double time_range[2]; /* Observation time */

  /* Set the Picard recursion order to estimate the radiative temperature. An
   * order of one means that the radiative temperature is linearized, while
   * higher orders allow the estimation of the T4 radiative transfer. */
  size_t picard_order;

  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
  enum ssp_rng_type rng_type; /* RNG type to use if `rng_state' is NULL */

  enum sdis_diffusion_algorithm diff_algo; /* Diffusion algorithm to be used */
};
#define SDIS_SOLVE_BOUNDARY_FLUX_ARGS_DEFAULT__ {                              \
  10000, /* #realisations */                                                   \
  NULL, /* List or primitive ids */                                            \
  0, /* #primitives */                                                         \
  {DBL_MAX, DBL_MAX}, /* Time range */                                         \
  1, /* Picard order */                                                        \
  NULL, /* RNG state */                                                        \
  SSP_RNG_THREEFRY, /* RNG type */                                             \
  SDIS_DIFFUSION_DELTA_SPHERE /* Diffusion algorithm */                        \
}
static const struct sdis_solve_boundary_flux_args
SDIS_SOLVE_BOUNDARY_FLUX_ARGS_DEFAULT =
  SDIS_SOLVE_BOUNDARY_FLUX_ARGS_DEFAULT__;

struct sdis_solve_camera_args {
  struct sdis_camera* cam; /* Point of view */
  double time_range[2]; /* Observation time */

  /* Set the Picard recursion order to estimate the radiative temperature. An
   * order of one means that the radiative temperature is linearized, while
   * higher orders allow the estimation of the T4 radiative transfer. */
  size_t picard_order;

  size_t image_definition[2]; /* Image definition */
  size_t spp; /* #samples per pixel */
  int register_paths; /* Combination of enum sdis_heat_path_flag */

  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
  enum ssp_rng_type rng_type; /* RNG type to use */

  enum sdis_diffusion_algorithm diff_algo; /* Diffusion algorithm to be used */
};
#define SDIS_SOLVE_CAMERA_ARGS_DEFAULT__ {                                     \
  NULL, /* Camera */                                                           \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  1, /* Picard order */                                                        \
  {512,512}, /* Image resolution */                                            \
  256, /* #realisations per pixel */                                           \
  SDIS_HEAT_PATH_NONE,                                                         \
  NULL, /* RNG state */                                                        \
  SSP_RNG_THREEFRY, /* RNG type */                                             \
  SDIS_DIFFUSION_DELTA_SPHERE /* Diffusion algorithm */                        \
}
static const struct sdis_solve_camera_args SDIS_SOLVE_CAMERA_ARGS_DEFAULT =
  SDIS_SOLVE_CAMERA_ARGS_DEFAULT__;

struct sdis_compute_power_args {
  size_t nrealisations;
  struct sdis_medium* medium; /* Medium to solve */
  double time_range[2]; /* Observation time */
  struct ssp_rng* rng_state; /* Initial RNG state. May be NULL */
  enum ssp_rng_type rng_type; /* RNG type to use if `rng_state' is NULL */
};
#define SDIS_COMPUTE_POWER_ARGS_DEFAULT__ {                                    \
  10000, /* #realisations */                                                   \
  NULL, /* Medium */                                                           \
  {DBL_MAX,DBL_MAX}, /* Time range */                                          \
  NULL, /* RNG state */                                                        \
  SSP_RNG_THREEFRY /* RNG type */                                              \
}
static const struct sdis_compute_power_args
SDIS_COMPUTE_POWER_ARGS_DEFAULT = SDIS_COMPUTE_POWER_ARGS_DEFAULT__;

struct sdis_green_function_create_from_stream_args {
  struct sdis_scene* scene; /* Scene from which the green was evaluated */
  FILE* stream; /* Stream from which the green function is deserialized */

  /* This signature is compared to the one serialized with the green function.
   * An error is returned if they differ */
  hash256_T signature;
};
#define SDIS_GREEN_FUNCTION_CREATE_FROM_STREAM_ARGS_DEFAULT__ {                \
  NULL, /* Scene */                                                            \
  NULL, /* Stream */                                                           \
  {0} /* Signature */                                                          \
}
static const struct sdis_green_function_create_from_stream_args
SDIS_GREEN_FUNCTION_CREATE_FROM_STREAM_ARGS_DEFAULT =
  SDIS_GREEN_FUNCTION_CREATE_FROM_STREAM_ARGS_DEFAULT__;

BEGIN_DECLS

/*******************************************************************************
 * Stardis Device. It is an handle toward the Stardis library. It manages the
 * Stardis resources.
 ******************************************************************************/
SDIS_API res_T
sdis_device_create
  (const struct sdis_device_create_args* args,
   struct sdis_device** dev);

SDIS_API res_T
sdis_device_ref_get
  (struct sdis_device* dev);

SDIS_API res_T
sdis_device_ref_put
  (struct sdis_device* dev);

SDIS_API res_T
sdis_device_is_mpi_used
  (struct sdis_device* dev,
   int* is_mpi_used);

SDIS_API res_T
sdis_device_get_mpi_rank
  (struct sdis_device* dev,
   int* rank);

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
 * API of the radiative environment. Describes the system when the sampled
 * radiative paths reach infinity.
 ******************************************************************************/
SDIS_API res_T
sdis_radiative_env_create
  (struct sdis_device* dev,
   const struct sdis_radiative_env_shader* shader,
   struct sdis_data* data, /* Data sent to the shader. May be NULL */
   struct sdis_radiative_env** radenv);

SDIS_API res_T
sdis_radiative_env_ref_get
  (struct sdis_radiative_env* radenv);

SDIS_API res_T
sdis_radiative_env_ref_put
  (struct sdis_radiative_env* radenv);

SDIS_API res_T
sdis_radiative_env_get_shader
  (struct sdis_radiative_env* radenv,
   struct sdis_radiative_env_shader* shader);

SDIS_API struct sdis_data*
sdis_radiative_env_get_data
  (struct sdis_radiative_env* radenv);

/*******************************************************************************
 * External source API. When a scene has external sources, an external flux
 * (in both its direct and diffuse parts) is imposed on the interfaces.
 ******************************************************************************/
SDIS_API res_T
sdis_spherical_source_create
  (struct sdis_device* dev,
   const struct sdis_spherical_source_shader* shader,
   struct sdis_data* data, /* Data sent to the shader. May be NULL */
   struct sdis_source** source);

SDIS_API res_T
sdis_spherical_source_get_shader
  (const struct sdis_source* source,
   struct sdis_spherical_source_shader* shader);

SDIS_API res_T
sdis_source_ref_get
  (struct sdis_source* source);

SDIS_API res_T
sdis_source_ref_put
  (struct sdis_source* source);

SDIS_API struct sdis_data*
sdis_source_get_data
  (struct sdis_source* source);

SDIS_API unsigned
sdis_source_get_id
  (const struct sdis_source* source);

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
 * ordered. The back side of a triangle is the exact opposite: it is the side
 * where the triangle vertices are counter-clock wise ordered. The front and
 * back media of a triangle interface directly refer to this convention and
 * thus one has to take care of how the triangle vertices are defined to ensure
 * that the front and the back media are correctly defined wrt the geometry. */
SDIS_API res_T
sdis_scene_create
  (struct sdis_device* dev,
   const struct sdis_scene_create_args* args,
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
   const struct sdis_scene_create_args* args,
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
   double lower[],
   double upper[]);

/* Get scene's fp_to_meter */
SDIS_API res_T
sdis_scene_get_fp_to_meter
  (const struct sdis_scene* scn,
   double* fp_to_meter);

/* Set scene's fp_to_meter */
SDIS_API res_T
sdis_scene_set_fp_to_meter
  (struct sdis_scene* scn,
   const double fp_to_meter);

/* Get scene's minimum/maximum temperature */
SDIS_API res_T
sdis_scene_get_temperature_range
  (const struct sdis_scene* scn,
   double t_range[2]);

/* Set scene's minimum/maximum temperature. Must be correctly defined if there
 * is any radiative transfer in the scene */
SDIS_API res_T
sdis_scene_set_temperature_range
  (struct sdis_scene* scn,
   const double t_range[2]);

/* Search the point onto the scene geometry that is the closest of `pos'. The
 * `radius' parameter controls the maximum search distance around `pos'. The
 * returned closest point is expressed locally to the geometric primitive onto
 * which it lies. If not found, the returned primitive is SDIS_PRIMITIVE_NONE.
 * Note that even though only one point is returned, several position can have
 * the same minimal distance to the queried position. */
SDIS_API res_T
sdis_scene_find_closest_point
  (const struct sdis_scene* scn,
   const struct sdis_scene_find_closest_point_args* args,
   size_t* iprim, /* Primitive index onto which the closest point lies */
   double uv[]); /* Parametric cordinate onto the primitive */

/* Define the world space position of a point onto the primitive `iprim' whose
 * parametric coordinate is uv. */
SDIS_API res_T
sdis_scene_get_boundary_position
  (const struct sdis_scene* scn,
   const size_t iprim, /* Primitive index */
   const double uv[], /* Parametric coordinate onto the primitive */
   double pos[]); /* World space position */

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
   const double pos[],
   double uv[]);

/* Get Star-Enclosure-2D scene. Defined on 2D scene only */
SDIS_API res_T
sdis_scene_get_senc2d_scene
  (struct sdis_scene* scn,
   struct senc2d_scene** senc2d_scn);

/* Get Star-Enclosure-3D scene. Defined on 3D scene only */
SDIS_API res_T
sdis_scene_get_senc3d_scene
  (struct sdis_scene* scn,
   struct senc3d_scene** senc3d_scn);

/* Get Star-2D scene view. Defined on 2D scene only */
SDIS_API res_T
sdis_scene_get_s2d_scene_view
  (struct sdis_scene* scn,
   struct s2d_scene_view** s2d_view);

/* Get Star-3D scene view. Defined on 3D scene only */
SDIS_API res_T
sdis_scene_get_s3d_scene_view
  (struct sdis_scene* scn,
   struct s3d_scene_view** s3d_view);

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

SDIS_API res_T
sdis_scene_get_device
  (struct sdis_scene* scn,
   struct sdis_device** device);

SDIS_API res_T
sdis_scene_get_source
  (struct sdis_scene* scn,
   struct sdis_source** src); /* The returned pointer can be NULL <=> no source */

SDIS_API res_T
sdis_scene_get_radiative_env
  (struct sdis_scene* scn,
   /* The returned pointer can be NULL, i.e. there is no radiative environement*/
   struct sdis_radiative_env** radenv);

/* Get the internal Star-2D primitive corresponding to the primitive key */
SDIS_API res_T
sdis_scene_get_s2d_primitive
  (struct sdis_scene* scn,
   const struct sdis_primkey* key,
   struct s2d_primitive* primitive);

/* Get the internal Star-3D primitive corresponding to the primitive key */
SDIS_API res_T
sdis_scene_get_s3d_primitive
  (struct sdis_scene* scn,
   const struct sdis_primkey* key,
   struct s3d_primitive* primitive);

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
   struct sdis_mc* flux); /* In W/m² */

SDIS_API res_T
sdis_estimator_get_radiative_flux
  (const struct sdis_estimator* estimator,
   struct sdis_mc* flux); /* In W/m² */

SDIS_API res_T
sdis_estimator_get_imposed_flux
  (const struct sdis_estimator* estimator,
   struct sdis_mc* flux); /* In W/m² */

SDIS_API res_T
sdis_estimator_get_total_flux
  (const struct sdis_estimator* estimator,
   struct sdis_mc* flux); /* In W/m² */

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
  (struct sdis_green_function_create_from_stream_args* args,
   struct sdis_green_function** green);

/* Retrieve the scene used to compute the green function */
SDIS_API res_T
sdis_green_function_get_scene
  (const struct sdis_green_function* green,
   struct sdis_scene** scn);

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

SDIS_API res_T
sdis_green_function_get_signature
  (const struct sdis_green_function* green,
   hash256_T signature);

/* Iterate over all valid green function paths */
SDIS_API res_T
sdis_green_function_for_each_path
  (struct sdis_green_function* green,
   sdis_process_green_path_T func,
   void* context);

/* Retrieve the path's elapsed time */
SDIS_API res_T
sdis_green_path_get_elapsed_time
  (struct sdis_green_path* path_handle,
   double* elapsed);

/* Retrieve the spatio-temporal limit of a path used to estimate the green
 * function */
SDIS_API res_T
sdis_green_path_get_end
  (struct sdis_green_path* path,
   struct sdis_green_path_end* end);

/* Retrieve the green function the path belongs to */
SDIS_API res_T
sdis_green_path_get_green_function
  (struct sdis_green_path* path_handle,
   struct sdis_green_function** green);

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

SDIS_API res_T
sdis_green_function_get_external_flux_terms_count
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

/* Iterate over all external flux terms associated to the path */
SDIS_API res_T
sdis_green_path_for_each_external_flux_terms
  (struct sdis_green_path* path,
   sdis_process_external_flux_terms_T func,
   void* context);

/*******************************************************************************
 * Heat path API
 ******************************************************************************/
SDIS_API res_T
sdis_heat_path_get_status
  (const struct sdis_heat_path* path,
   enum sdis_heat_path_flag* status);

SDIS_API res_T
sdis_heat_path_get_line_strips_count
  (const struct sdis_heat_path* path,
   size_t* nstrips);

SDIS_API res_T
sdis_heat_path_line_strip_get_vertices_count
  (const struct sdis_heat_path* path,
   const size_t istrip,
   size_t* nvertices);

SDIS_API res_T
sdis_heat_path_line_strip_get_vertex
  (const struct sdis_heat_path* path,
   const size_t istrip,
   const size_t ivert,
   struct sdis_heat_vertex* vertex);

SDIS_API res_T
sdis_heat_path_line_strip_for_each_vertex
  (const struct sdis_heat_path* path,
   const size_t istrip,
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

/* Calculate the flux density in W/m² _entering_ the solid through the given
 * boundary position, i.e. the flux density is positive or negative if the
 * solid gains or loses energy, respectively. */
SDIS_API res_T
sdis_solve_probe_boundary_flux
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_flux_args* args,
   struct sdis_estimator** estimator);

/* Calculate the average flux density in W/m² _entering_ the solid through the
 * given boundary surfaces, i.e. the flux density is positive or negative if
 * the solid gains or loses energy, respectively. */
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

/* power (in Watt)  = SUM(volumic_power(x)) / Nrealisations * Volume */
SDIS_API res_T
sdis_compute_power
  (struct sdis_scene* scn,
   const struct sdis_compute_power_args* args,
   struct sdis_estimator** estimator);

/*******************************************************************************
 * Solvers of a list of probes
 *
 * Unlike their single-probe counterpart, this function parallelizes the list of
 * probes, rather than calculating a single probe. Calling these functions is
 * therefore more advantageous in terms of load distribution when the number of
 * probes to be evaluated is large compared to the cost of calculating a single
 * probe.
 ******************************************************************************/
SDIS_API res_T
sdis_solve_probe_list
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_list_args* args,
   struct sdis_estimator_buffer** buf);

SDIS_API res_T
sdis_solve_probe_boundary_list
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_list_args* args,
   struct sdis_estimator_buffer** buf);

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
 * solid media and the power of external sources.
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

/*******************************************************************************
 * Retrieve infos from the Stardis-Solver library
 ******************************************************************************/
SDIS_API res_T
sdis_get_info
  (struct sdis_info* info);

/*******************************************************************************
 * Primitive identifier
 ******************************************************************************/
SDIS_API void
sdis_primkey_setup
  (struct sdis_primkey* key,
   const double node0[3],
   const double node1[3],
   const double node2[3]);

SDIS_API void
sdis_primkey_2d_setup
  (struct sdis_primkey* key,
   const double node0[2],
   const double node1[2]);

SDIS_API size_t
sdis_primkey_hash
  (const struct sdis_primkey* key);

SDIS_API char
sdis_primkey_eq
  (const struct sdis_primkey* key0,
   const struct sdis_primkey* key1);

END_DECLS

#endif /* SDIS_H */
