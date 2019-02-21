/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

/* Forward declaration of external opaque data types */
struct logger;
struct mem_allocator;

/* Forward declaration of the Stardis opaque data types. These data types are
 * ref counted. Once created with the appropriated `sdis_<TYPE>_create'
 * function, the caller implicitly owns the created data, i.e. its reference
 * counter is set to 1. The sdis_<TYPE>_ref_<get|put> functions get or release
 * a reference on the data, i.e. they increment or decrement the reference
 * counter, respectively. When this counter reaches 0, the object is silently
 * destroyed and cannot be used anymore. */
struct sdis_accum_buffer;
struct sdis_camera;
struct sdis_data;
struct sdis_device;
struct sdis_estimator;
struct sdis_green_function;
struct sdis_interface;
struct sdis_medium;
struct sdis_scene;

enum sdis_side {
  SDIS_FRONT,
  SDIS_BACK,
  SDIS_SIDE_NULL__
};

enum sdis_medium_type {
  SDIS_FLUID,
  SDIS_SOLID,
  SDIS_MEDIUM_TYPES_COUNT__
};

enum sdis_estimator_type {
  SDIS_ESTIMATOR_TEMPERATURE,
  SDIS_ESTIMATOR_FLUX,
  SDIS_ESTIMATOR_TYPES_COUNT__
};

enum sdis_scene_dimension {
  SDIS_SCENE_2D,
  SDIS_SCENE_3D
};

enum sdis_point_type {
  SDIS_FRAGMENT,
  SDIS_VERTEX,
  SDIS_POINT_TYPES_COUNT__,
  SDIS_POINT_NONE = SDIS_POINT_TYPES_COUNT__
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

/* Monte-Carlo accumulator */
struct sdis_accum {
  double sum_weights; /* Sum of Monte-Carlo weights */
  double sum_weights_sqr; /* Sum of Monte-Carlo square weights */
  size_t nweights; /* #accumulated weights */
  size_t nfailures; /* #failures */
};

/* Monte-Carlo estimation */
struct sdis_mc {
  double E; /* Expected value */
  double V; /* Variance */
  double SE; /* Standard error */
};
#define SDIS_MC_NULL__ {0, 0, 0}
static const struct sdis_mc SDIS_MC_NULL = SDIS_MC_NULL__;

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

struct sdis_accum_buffer_layout {
  size_t width;
  size_t height;
};
#define SDIS_ACCUM_BUFFER_LAYOUT_NULL__ {0, 0}
static const struct sdis_accum_buffer_layout SDIS_ACCUM_BUFFER_LAYOUT_NULL =
  SDIS_ACCUM_BUFFER_LAYOUT_NULL__;

/* Functor use to write accumulations performed by sdis_solve_camera */
typedef res_T
(*sdis_write_accums_T)
  (void* context, /* User data */
   const size_t origin[2], /* Coordinates of the 1st accumulation in image plane */
   const size_t naccums[2], /* #accumulations in X and Y */
   const struct sdis_accum* accums); /* List of row ordered accumulations */

struct sdis_green_path {
  /* Internal data. Should not be accessed */
  void* green__;
  size_t id__;
};
#define SDIS_GREEN_PATH_NULL__ {NULL, 0}
static const struct sdis_green_path SDIS_GREEN_PATH_NULL =
  SDIS_GREEN_PATH_NULL__;

struct sdis_point {
  union {
    struct {
      struct sdis_medium* medium;
      struct sdis_rwalk_vertex vertex;
    } mdmvert;
    struct {
      struct sdis_interface* interface;
      struct sdis_interface_fragment fragment;
    } itfrag;
  } data;
  enum sdis_point_type type;
};
#define SDIS_POINT_NULL__ { {{NULL, SDIS_RWALK_VERTEX_NULL__}}, SDIS_POINT_NONE}
static const struct sdis_point SDIS_POINT_NULL = SDIS_POINT_NULL__;

/* Functor used to process the paths registered agains the green function */
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
 * A buffer of accumulations is a 2D array whose each cell stores an
 * Monte-Carlo accumulation, i.e. a sum of MC weights, the sum of their square
 * and the overall number of summed weights (see struct sdis_accum)
 ******************************************************************************/
SDIS_API res_T
sdis_accum_buffer_create
  (struct sdis_device* dev,
   const size_t width,  /* #cells in X */
   const size_t height, /* #cells in Y */
   struct sdis_accum_buffer** buf);

SDIS_API res_T
sdis_accum_buffer_ref_get
  (struct sdis_accum_buffer* buf);

SDIS_API res_T
sdis_accum_buffer_ref_put
  (struct sdis_accum_buffer* buf);

SDIS_API res_T
sdis_accum_buffer_get_layout
  (const struct sdis_accum_buffer* buf,
   struct sdis_accum_buffer_layout* layout);

/* Get a read only pointer toward the memory space of the accum buffer */
SDIS_API res_T
sdis_accum_buffer_map
  (const struct sdis_accum_buffer* buf,
   const struct sdis_accum** accums);

SDIS_API res_T
sdis_accum_buffer_unmap
  (const struct sdis_accum_buffer* buf);

/* Helper function that matches the `sdis_write_accums_T' functor type. One can
 * send this function directly to the sdis_solve_camera function, to fill the
 * accum buffer with the estimation of the radiative temperature that reaches
 * each pixel of an image whose definition matches the definition of the accum
 * buffer. */
SDIS_API res_T
sdis_accum_buffer_write
  (void* buf, /* User data */
   const size_t origin[2], /* Coordinates of the 1st accum in image plane */
   const size_t naccum[2], /* #accum in X and Y */
   const struct sdis_accum* accums); /* List of row ordered accum */

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
  (const struct sdis_interface* interface,
   struct sdis_interface_shader* shader);

SDIS_API struct sdis_data*
sdis_interface_get_data
  (struct sdis_interface* interface);

/*******************************************************************************
 * A scene is a collection of primitives. Each primitive is the geometric
 * support of the interface between 2 media.
 ******************************************************************************/
/* Create a 3D scene. The geometry of the scene is defined by an indexed
 * triangular mesh: each triangle is composed of 3 indices where each index
 * references an absolute 3D position. The physical properties of an interface
 * is defined by the interface of the triangle.
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
    (const size_t itri, size_t ids[3], void*),
   void (*interf) /* Get the interface of the triangle `itri' */
    (const size_t itri, struct sdis_interface** bound, void*),
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
    (const size_t iseg, size_t ids[2], void*),
   void (*interf) /* Get the interface of the segment `iseg' */
    (const size_t iseg, struct sdis_interface** bound, void*),
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

/* Define the world space position of a point onto the primitive `iprim' whose
 * parametric coordinate is uv. */
SDIS_API res_T
sdis_scene_get_boundary_position
  (const struct sdis_scene* scn,
   const size_t iprim, /* Primitive index */
   const double uv[2], /* Parametric coordinate onto the primitive */
   double pos[3]); /* World space position */

/* Project a world space position onto a primitive wrt its normal and compute
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

SDIS_API res_T
sdis_scene_get_dimension
  (const struct sdis_scene* scn,
   enum sdis_scene_dimension* dim);

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
sdis_estimator_get_convective_flux
  (const struct sdis_estimator* estimator,
   struct sdis_mc* flux);

SDIS_API res_T
sdis_estimator_get_radiative_flux
  (const struct sdis_estimator* estimator,
   struct sdis_mc* flux);

SDIS_API res_T
sdis_estimator_get_total_flux
  (const struct sdis_estimator* estimator,
   struct sdis_mc* flux);

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
   const double time_range[2], /* Observation time */
   struct sdis_estimator** estimator);

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

/* Retrieve the spatio-temporal end point of a path used to estimate the green
 * function. Note that this point went back in time from the relative
 * observation time 0. Its time is thus negative ;  its absolute value
 * represents the time spent by the path into the system. */
SDIS_API res_T
sdis_green_path_get_limit_point
  (struct sdis_green_path* path,
   struct sdis_point* pt);

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
 * Solvers
 ******************************************************************************/
SDIS_API res_T
sdis_solve_probe
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const double position[3], /* Probe position */
   const double time_range[2], /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double ambient_radiative_temperature, /* In Kelvin */
   const double reference_temperature, /* In Kelvin */
   struct sdis_estimator** estimator);

SDIS_API res_T
sdis_solve_probe_boundary
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t iprim, /* Identifier of the primitive on which the probe lies */
   const double uv[2], /* Parametric coordinates of the probe onto the primitve */
   const double time_range[2], /* Observation time */
   const enum sdis_side side, /* Side of iprim on which the probe lies */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double ambient_radiative_temperature, /* In Kelvin */
   const double reference_temperature, /* In Kelvin */
   struct sdis_estimator** estimator);

SDIS_API res_T
sdis_solve_boundary
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t primitives[], /* List of boundary primitives to handle */
   const enum sdis_side sides[], /* Per primitive side to consider */
   const size_t nprimitives, /* #primitives */
   const double time_range[2], /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double ambient_radiative_temperature, /* In Kelvin */
   const double reference_temperature, /* In Kelvin */
   struct sdis_estimator** estimator);

SDIS_API res_T
sdis_solve_probe_boundary_flux
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t iprim, /* Identifier of the primitive on which the probe lies */
   const double uv[2], /* Parametric coordinates of the probe onto the primitve */
   const double time_range[2], /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double ambient_radiative_temperature, /* In Kelvin */
   const double reference_temperature, /* In Kelvin */
   struct sdis_estimator** estimator);

SDIS_API res_T
sdis_solve_boundary_flux
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t primitives[], /* List of boundary primitives to handle */
   const size_t nprimitives, /* #primitives */
   const double time_range[2], /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double ambient_radiative_temperature, /* In Kelvin */
   const double reference_temperature, /* In Kelvin */
   struct sdis_estimator** estimator);

SDIS_API res_T
sdis_solve_camera
  (struct sdis_scene* scn,
   const struct sdis_camera* cam, /* Point of view */
   const double time, /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double ambient_radiative_temperature, /* In Kelvin */
   const double reference_temperature, /* In Kelvin */
   const size_t width, /* Image definition in in X */
   const size_t height, /* Image definition in Y */
   const size_t spp, /* #samples per pixel */
   sdis_write_accums_T writer,
   void* writer_data);

/*******************************************************************************
 * Green solvers.
 *
 * The caller should ensure that green solvers are invoked on scenes whose data
 * do not depend on time. Indeed, on green estimation, the time parameter along
 * the random walks registers the relative time spent in the system rather than
 * an absolute time. As a consequence, the media/interfaces parameters cannot
 * vary in time with respect to an absolute time value.
 *
 * In addition, the green solvers assumes that the interface fluxes are
 * constants in time and space. In the same way the volumic power of the solid
 * media must be constant in time and space too. Furthermore, note that only
 * the interfaces/media that had a flux/volumic power during green estimation
 * can update their flux/volumic power value for subsequent
 * sdis_green_function_solve invocations : other interfaces/media are
 * definitely registered against the green function as interfaces/media with no
 * flux/volumic power.
 *
 * If the aforementioned assumptions are not ensured by the caller, the
 * behavior of the estimated green function is undefined.
 ******************************************************************************/
SDIS_API res_T
sdis_solve_probe_green_function
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const double position[3], /* Probe position */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double ambient_radiative_temperature, /* In Kelvin */
   const double reference_temperature, /* In Kelvin */
   struct sdis_green_function** green);

END_DECLS

#endif /* SDIS_H */

