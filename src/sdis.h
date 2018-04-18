/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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

#define SDIS_VOLUMIC_POWER_NONE DBL_MAX /* <=> No volumic power */
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
struct sdis_interface;
struct sdis_medium;
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
  enum sdis_side_flag side;
};
#define SDIS_INTERFACE_FRAGMENT_NULL__ {{0}, {0}, {0}, -1, SDIS_SIDE_NULL__}
static const struct sdis_interface_fragment SDIS_INTERFACE_FRAGMENT_NULL =
  SDIS_INTERFACE_FRAGMENT_NULL__;

/* Monte-Carlo accumulator */
struct sdis_accum {
  double sum_weights; /* Sum of Monte-Carlo weights */
  double sum_weights_sqr; /* Sum of Monte-Carlo square weights */
  size_t nweights; /* #accumulated weights */
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
  sdis_medium_getter_T delta_boundary;

  /* May be NULL if there is no volumic power. One can also return
   * SDIS_VOLUMIC_POWER_NONE to define that there is no volumic power at the
   * submitted position and time */
  sdis_medium_getter_T volumic_power;  /* In W.m^-3 */

  /* Initial/limit condition. A temperature < 0 means that the temperature is
   * unknown for the submitted random walk vertex. */
  sdis_medium_getter_T temperature;
};
#define SDIS_SOLID_SHADER_NULL__ {NULL, NULL, NULL, NULL, NULL, NULL, NULL}
static const struct sdis_solid_shader SDIS_SOLID_SHADER_NULL =
  SDIS_SOLID_SHADER_NULL__;

/* Define the physical properties of a fluid */
struct sdis_fluid_shader {
  /* Properties */
  sdis_medium_getter_T calorific_capacity; /* In J.K^-1.kg^-1 */
  sdis_medium_getter_T volumic_mass; /* In kg.m^-3 */

  /* Initial/limit condition. A temperature < 0 means that the temperature is
   * unknown for the submitted position and time. */
  sdis_medium_getter_T temperature;
};
#define SDIS_FLUID_SHADER_NULL__ {NULL, NULL, NULL}
static const struct sdis_fluid_shader SDIS_FLUID_SHADER_NULL =
  SDIS_FLUID_SHADER_NULL__;

/* Define the physical properties of one side of an interface. */
struct sdis_interface_side_shader {
  /* Fixed temperature/flux. May be NULL if the temperature/flux is unknown
   * onto the whole interface */
  sdis_interface_getter_T temperature;  /* In Kelvin. < 0 <=> Unknown temp */
  sdis_interface_getter_T flux; /* In W.m^-2. SDIS_FLUX_NONE <=> no flux  */

  /* Control the emissivity of the interface. May be NULL for solid/sold
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

  struct sdis_interface_side_shader front;
  struct sdis_interface_side_shader back;
};
#define SDIS_INTERFACE_SHADER_NULL__ \
  {NULL, SDIS_INTERFACE_SIDE_SHADER_NULL__, SDIS_INTERFACE_SIDE_SHADER_NULL__}
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

/* Helper function that matches the `sdis_write_accums_T' functor type. On can
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

/*******************************************************************************
 * A scene is a collection of primitives. Each primitive is the geometric
 * support of the interface between 2 mediums.
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
 *             /   \,*^
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
sdis_solve_probe
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const double position[3], /* Probe position */
   const double time, /* Observation time */
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
   const double time, /* Observation time */
   const enum sdis_side_flag side, /* Side of iprim on which the probe lies */
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

END_DECLS

#endif /* SDIS_H */

