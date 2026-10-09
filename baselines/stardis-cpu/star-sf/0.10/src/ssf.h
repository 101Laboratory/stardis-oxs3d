/* Copyright (C) 2016-2018, 2021-2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef SSF_H
#define SSF_H

#include <rsys/rsys.h>
#include <float.h>

/* Library symbol management */
#if defined(SSF_SHARED_BUILD) /* Build shared library */
  #define SSF_API extern EXPORT_SYM
#elif defined(SSF_STATIC_BUILD) /* Use/build static library */
  #define SSF_API extern LOCAL_SYM
#else /* Use shared library */
  #define SSF_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the ssf function `Func'
 * returns an error. */
#ifndef NDEBUG
  #define SSF(Func) ASSERT(ssf_##Func == RES_OK)
#else
  #define SSF(Func) ssf_##Func
#endif

/* Max value of the exponent parameter in the Blinn microfacet distribution */
#define SSF_BLINN_DISTRIBUTION_MAX_EXPONENT 10000.0

/* Forward declaration */
struct mem_allocator;
struct ssp_rng;

/* Opaque data types */
struct ssf_bsdf; /* Bidirectional Scattering Distribution Function */
struct ssf_fresnel; /* Equation of the Fresnel term */
struct ssf_microfacet_distribution;
struct ssf_phase; /* Phase function */

enum ssf_bxdf_flag {
  SSF_REFLECTION = BIT(0),
  SSF_TRANSMISSION = BIT(1),
  SSF_SPECULAR = BIT(2),
  SSF_DIFFUSE = BIT(3),
  SSF_GLOSSY = BIT(4)
};

enum ssf_simd {
  SSF_SIMD_NONE,
  SSF_SIMD_128,
  SSF_SIMD_256
};

/* Generic BSDF type descriptor. Note that by convention the outgoing direction
 * `wo' and the incoming direction `wi' point outward the surface. Furthermore,
 * `wo' and the normal N must point on the same side of the surface. As a
 * consequence the reflected or refracted direction `wi' must point on the same
 * side or on the opposite side of `N', respectively. */
struct ssf_bsdf_type {
  res_T (*init)(struct mem_allocator* allocator, void* bsdf); /* Can be NULL */
  void (*release)(void* bsdf); /* Can be NULL */

  /* Sample a direction `wi' wrt `wo' whose pdf is BSDF(wo, wi) |wi.n|. Return
   * the value of BSDF(wo, wi) */
  double
  (*sample)
    (void* bsdf,
     struct ssp_rng* rng, /* Random number generator */
     const double wo[3], /* Normalized outgoing direction */
     const double N[3], /* Normalized surface normal */
     double wi[3], /* Sampled normalized incoming direction */
     int* type, /* Sampled component. Combination of ssf_bxdf_flag. Can be NULL */
     double* pdf); /* PDF to sample wi wrt wo. Can be NULL */

  /* Evaluate the BxDF wrt `wo' and `wi' */
  double
  (*eval)
    (void* bsdf,
     const double wo[3], /* Normalized outgoing direction */
     const double N[3], /* Normalized surface normal */
     const double wi[3]);/* Normalized incoming direction */

  /* Probability density function */
  double
  (*pdf)
    (void* bsdf,
     const double wo[3], /* Normalized outgoing direction */
     const double N[3], /* Normalized surface normal */
     const double wi[3]);/* Normalized incoming direction */

  size_t sizeof_bsdf; /* In Bytes */
  size_t alignof_bsdf; /* In Bytes. Must be a power of 2 */
};
#define SSF_BSDF_TYPE_NULL__ {NULL, NULL, NULL, NULL, NULL, 0, 1}
static const struct ssf_bsdf_type SSF_BXDF_TYPE_NULL = SSF_BSDF_TYPE_NULL__;

/* Generic Fresnel term descriptor */
struct ssf_fresnel_type {
  res_T (*init)(struct mem_allocator* allocator, void* fresnel); /* Can be NULL */
  void (*release)(void* bxdf); /* Can be NULL */

  double
  (*eval)
    (void* fresnel,
     const double cos_wi_N); /* Cosine between facet normal and incoming dir */

  size_t sizeof_fresnel; /* In Bytes */
  size_t alignof_fresnel; /* In Bytes. Must be a power of 2 */
};
#define SSF_FRESNEL_TYPE_NULL__ {NULL, NULL, NULL, 0, 1}
static const struct ssf_fresnel_type SSF_FRESNEL_TYPE_NULL =
  SSF_FRESNEL_TYPE_NULL__;

/* Generic descriptor of a microfacet distribution */
struct ssf_microfacet_distribution_type {
  res_T (*init)(struct mem_allocator* allocator, void* distrib); /* Can be NULL */
  void (*release)(void* distrib); /* Can be NULL */

  void
  (*sample)
    (void* distrib,
     struct ssp_rng* rng, /* Random number generator */
     const double N[3], /* Normalized Z-direction of the distribution */
     double wh[3], /* Sampled normalized half vector */
     double* pdf); /* PDF to sample wh. Can be NULL */

  double
  (*eval)
    (void* distrib,
     const double N[3], /* Normalized Z-direction of the distribution */
     const double wh[3]); /* Normalized half vector */

  double
  (*pdf)
    (void* distrib,
     const double N[3], /* Normalized Z-direction of the distribution */
     const double wh[3]); /* Normalized half vector */

  size_t sizeof_distribution; /* In Bytes */
  size_t alignof_distribution; /* In Bytes. Must be a Power of 2 */
};
#define SSF_MICROFACET_DISTRIBUTION_TYPE_NULL__ \
  {NULL, NULL, NULL, NULL, NULL, 0, 1}
static const struct ssf_microfacet_distribution_type
SSF_MICROFACET_DISTRIBUTION_TYPE_NULL = SSF_MICROFACET_DISTRIBUTION_TYPE_NULL__;

/* Generic phase function type descriptor. Note that by convention the outgoing
 * direction `wo' and the incoming direction `wi' point *OUTWARD* the scattering
 * point. The scattering angle is thus acos(-wo.wi) */
struct ssf_phase_type {
  res_T (*init)(struct mem_allocator* allocator, void* phase); /*Can be NULL*/
  void (*release)(void* phase); /* Can be NULL */

  /* Sample a direction `wi' wrt `wo'. */
  void
  (*sample)
    (void* phase,
     struct ssp_rng* rng, /* Random number generator */
     const double wo[3], /* Normalized outgoing direction */
     double wi[3], /* Sampled normalized incoming direction */
     double* pdf); /* PDF to sample wi wrt wo. Can be NULL */

  /* Evaluate the phase function wrt `wo' and `wi' */
  double
  (*eval)
    (void* phase,
     const double wo[3], /* Normalized outgoing direction */
     const double wi[3]); /* Normalized incoming direction */

  /* Probability density function */
  double
  (*pdf)
    (void* phase,
     const double wo[3], /* Normalized outgoing direction */
     const double wi[3]); /* Normalized incoming direction */

  size_t sizeof_phase; /* In Bytes */
  size_t alignof_phase; /* In Bytes. Must be a power of 2 */
};
#define SSF_PHASE_TYPE_NULL__ {NULL,NULL,NULL,NULL,NULL,0,1}
static const struct ssf_phase_type SSF_PHASE_TYPE_NULL = SSF_PHASE_TYPE_NULL__;

struct ssf_info {
  /* Define the supported SIMD instruction sets */
  char simd_128;
  char simd_256;
};
#define SSF_INFO_NULL__ {0,0}
static const struct ssf_info SSF_INFO_NULL = SSF_INFO_NULL__;

/* RDGFA phase function input arguments */
struct ssf_phase_rdgfa_setup_args {
  double wavelength; /* In nm */
  double fractal_dimension;  /* No unit */
  double gyration_radius; /* In nm */

  /* Number of #intervals to use to discretize the angular domain */
  size_t nintervals;

  enum ssf_simd simd; /* SIMD instruction sets to use */
};
#define SSF_PHASE_RDGFA_SETUP_ARGS_DEFAULT__ {0,0,0,1000,SSF_SIMD_NONE}
static const struct ssf_phase_rdgfa_setup_args
SSF_PHASE_RDGFA_SETUP_ARGS_DEFAULT = SSF_PHASE_RDGFA_SETUP_ARGS_DEFAULT__;

struct ssf_phase_rdgfa_desc {
  double wavelength; /* In nm */
  double fractal_dimension;  /* No unit */
  double gyration_radius; /* In nm */

  double normalization_factor; /* Normalization factor of the cumulative */
  size_t nintervals; /* #intervals used to discretized the cumulative */
};
#define SSF_PHASE_RDGFA_DESC_NULL__ {0,0,0,0,0}
static const struct ssf_phase_rdgfa_desc SSF_PHASE_RDGFA_DESC_NULL =
  SSF_PHASE_RDGFA_DESC_NULL__;

struct ssf_phase_rdgfa_interval {
  double range[2]; /* Angular range of the interval. In rad */
  double cumulative; /* Value of the cumulative of the interval */
};
#define SSF_PHASE_RDGFA_INTERVAL_NULL__ {{DBL_MAX,-DBL_MAX}, 0}
static const struct ssf_phase_rdgfa_interval SSF_PHASE_RDGFA_INTERVAL_NULL =
  SSF_PHASE_RDGFA_INTERVAL_NULL__;

struct ssf_discrete_item {
  double theta; /* In radian */
  double value;
};
#define SSF_DISCRETE_ITEM_NULL__ {0,0}
static const struct ssf_discrete_item SSF_DISCRETE_ITEM_NULL =
  SSF_DISCRETE_ITEM_NULL__;

/* Discrete phase function input arguments */
struct ssf_discrete_setup_args {
  void (*get_item) /* Returns a given discrete item */
    (const size_t iitem,
     struct ssf_discrete_item* item,
     void* context);
  void* context; /* User defined data sent to `get_item' functor */
  size_t nitems; /* #discrete items */
};
#define SSF_DISCRETE_SETUP_ARGS_NULL__ {NULL,NULL,0}
static const struct ssf_discrete_setup_args
SSF_DISCRETE_SETUP_ARGS_NULL = SSF_DISCRETE_SETUP_ARGS_NULL__;

BEGIN_DECLS

/*******************************************************************************
 * Built-in BSDFs
 ******************************************************************************/
/* Dirac distribution whose incoming direction `wi' is the reflection of the
 * supplied direction `wo' with respect to the surface normal `N'. The
 * directional reflectance is controlled by the Fresnel term `Fr'.
 *    fr(wo, wi) = Fr(|wi.N|) * delta(wo - Reflect(wi, N)) / |wi.N|
 * Since it is a dirac distribution, the returned value of the `eval' and `pdf'
 * function is always 0 while the pdf returned by `sample' function is
 * infinity. */
SSF_API const struct ssf_bsdf_type ssf_specular_reflection;

/* Reflects the same intensity in all direction independently of the incoming
 * direction; fr(wo, wr) = R/PI */
SSF_API const struct ssf_bsdf_type ssf_lambertian_reflection;

/* Glossy reflections with respect to a microfacet distribution. It is based on
 * the Torrance Sparrow BRDF to provide a general Microfacet-based BRDF model
 *    fr(wo, wi) = Fr(|wi.wh|) * G(wo, wi) * D(wh) / (4 * |wo.N| * |wi.N|)
 * with `wh' the half vector between `wi' and `wo', `Fr(|wi.wh|)' the Fresnel
 * term that controls the directional reflectance, `G(wo, wi)' the geometric
 * attenuation term that describes the masking and the shadowing of the
 * microfacets and `D(wh)' the distribution area of the microfacets whose
 * normal is `wh'. Note that common BRDFs based on this model ignore multiple
 * scattering of lights into the microfacet structure and remove them with the
 * 'G' term. As a consequence, this model does not satisfy the energy
 * conservation property */
SSF_API const struct ssf_bsdf_type ssf_microfacet_reflection;

/* Glossy reflections with respect to a microfacet distribution. In contrast to
 * the ssp_microfacet_reflection model, this BRDF ensures, by design, the
 * energy conservation property without requiring the normalization of the
 * BRDF. As a counterpart, it only provides the sample function. The others
 * functions return invalid value. However, it is well suited for Monte Carlo
 * integrations that only need to sample a direction and to define its
 * associated directional reflectance */
SSF_API const struct ssf_bsdf_type ssf_microfacet2_reflection;

/* This BSDF consists in a model of refraction effects within a thin slab of a
 * dielectric material, combine with a dirac distribution of the reflection at
 * the interfaces as provided by ssf_specular_reflection. Global reflection (R),
 * absorption (A) and transmission (T) parameters are computed from the
 * infinite series of contributions that come from multiple refraction effects
 * within the slab.
 *
 * Assuming a perfect dielectric material, the Fresnel term provided by
 * ssf_fresnel_dielectric_dielectric is used to compute "rho", i.e. the
 * reflected part during a single refraction event. But since real materials
 * are not ideal dielectrics, the slab is also supposed to absorb incoming
 * radiation. The absorption coefficient "alpha" of the material is provided
 * by the user. */
SSF_API const struct ssf_bsdf_type ssf_thin_specular_dielectric;

/* Dirac distribution whose incoming direction `wi' is either refracted or
 * reflecter wrt N and a dielectric/dielectric fresnel term `Fr'.
 *    f_reflected(wo, wi) = Fr(|wi.N|) * delta(wo - Reflect(wi, N)) / |wi.N|
 *    f_refracted(wo, wi) = (1-Fr(|wi.N|)) * delta(wo - Refract(wi, N)) / |wi.N|
 * Since it is a dirac distribution, the returned value of the `eval' and `pdf'
 * function is always 0 while the pdf returned by `sample' function is
 * infinity. */
SSF_API const struct ssf_bsdf_type ssf_specular_dielectric_dielectric_interface;

/*******************************************************************************
 * Built-in Fresnel terms
 ******************************************************************************/
/* Fresnel term is a constant; Fr = k with k in [0, 1]. */
SSF_API const struct ssf_fresnel_type ssf_fresnel_constant;

/* Fresnel term for perfect reflection; Fr = 1 */
SSF_API const struct ssf_fresnel_type ssf_fresnel_no_op;

/* Fresnel term between 2 dielectric mediums.
 *    Fr = 1/2 * (Rs^2 + Rp^2)
 * with `Rs' and `Rp' the reflectance for the light polarized with its electric
 * field perpendicular or parallel to the plane of incidence, respectively.
 *    Rs = (n1*|wi.N| - n2*|wt.N|) / (n1*|wi.N| + n2*|wt.N)
 *    Rp = (n2*|wi.N| - n1*|wt.N|) / (n2*|wi.N| + n1*|wt.N)
 * with `n1' and `n2' the indices of refraction of the incident and transmitted
 * media, and `wi' and `wt' the incident and transmitted direction. */
SSF_API const struct ssf_fresnel_type ssf_fresnel_dielectric_dielectric;

/* Fresnel term between a dielectric and a conductor.
 *    Fr = 1/2 * (Rs^2 + Rp^2)
 * with `Rs' and `Rp' the reflectance for the light polarized with its electric
 * field perpendicular or parallel to the plane of incidence, respectively.
 *    Rs = (n1*|wi.N| - (n2 - i*k2)*|wt.N|) / (n1*|wi.N| + (n2 - i*k2)*|wt.N|)
 *    Rp = ((n2 - i*k2)*|wt.N| - n1*|wi.N|) / ((n2 - i*k2)*|wt.N| + n1*|wi.N|)
 * with `n1' the index of refraction of the incident media, `n2' and `k2' the
 * real and imaginary part of the index of refraction of the transmitted media,
 * and `wi' and `wt' the incident and transmitted direction */
SSF_API const struct ssf_fresnel_type ssf_fresnel_dielectric_conductor;

/*******************************************************************************
 * Built-in microfacet distributions
 ******************************************************************************/
/* Beckmann microfacet distribution.
 *    D(wh) = exp(-tan^2(a)/m^2) / (PI*m^2*cos^4(a))
 * with `a' = arccos(wh.N) and `m' the rougness in ]0, 1] of the surface */
SSF_API const struct ssf_microfacet_distribution_type ssf_beckmann_distribution;

/* Blinn microfacet distribution.
 *    D(wh) = (e + 2) / (2*PI) * (wh.N)^e
 * with `e' an exponent in [0, SSF_BLINN_DISTRIBUTION_MAX_EXPONENT] */
SSF_API const struct ssf_microfacet_distribution_type ssf_blinn_distribution;

/* Pillbox microfacet distribution.
 *            | 0; if |wh.N| >= m
 *    D(wh) = | 1 / (PI * (1 - cos^2(m))); if |wh.N| < m
 * with `m' the roughness parameter in ]0, 1] */
SSF_API const struct ssf_microfacet_distribution_type ssf_pillbox_distribution;

/*******************************************************************************
 * Built-in phase functions.
 ******************************************************************************/
/* Henyey & Greenstein phase function normalized to 1; p(wo, wi) == pdf(wo, wi).
 *    p(wo, wi) = 1/(4*PI)* (1-g^2) / (1+g^2-2*g*(-wo.wi))^3/2 */
SSF_API const struct ssf_phase_type ssf_phase_hg;

/* Rayleigh phase function normalized to 1; p(wo, wi) == pdf(wo, wi).
 *    p(wo, wi) = 3/(16*PI) * (1+(-wo.wi)^2) */
SSF_API const struct ssf_phase_type ssf_phase_rayleigh;

/* RDGFA phase function normalized to 1:
 *    p(wo, wi) = 3/(16*PI) * f(theta)/g * (1+cos(theta)^2);
 *    theta = acos(wo.wi)
 *
 * "Effects of multiple scattering on radiative properties of soot fractal
 * aggregates" J. Yon et al., Journal of Quantitative Spectroscopy and
 * Radiative Transfer Vol 133, pp. 374-381, 2014 */
SSF_API const struct ssf_phase_type ssf_phase_rdgfa;

/* Discrete phase function normalized to 1 */
SSF_API const struct ssf_phase_type ssf_phase_discrete;

/*******************************************************************************
 * SSF API
 ******************************************************************************/
SSF_API res_T
ssf_get_info
  (struct ssf_info* info);

/*******************************************************************************
 * BSDF API - Bidirectional Scattering Distribution Function. Describes the way
 * the light is scattered by a surface. Note that by convention the outgoing
 * direction `wo' and the incoming direction `wi' point outward the surface.
 * Furthermore, `wo' and the normal `N' must point on the same side of the
 * surface. As a consequence the reflected or refracted direction `wi' must
 * point on the same side or on the opposite side of `N', respectively.
 ******************************************************************************/
SSF_API res_T
ssf_bsdf_create
  (struct mem_allocator* allocator, /* May be NULL <=> Use default allocator */
   const struct ssf_bsdf_type* type,
   struct ssf_bsdf** bsdf);

SSF_API res_T
ssf_bsdf_ref_get
  (struct ssf_bsdf* bsdf);

SSF_API res_T
ssf_bsdf_ref_put
  (struct ssf_bsdf* bsdf);

/* Sample a direction `wi' wrt `wo' whose pdf is BSDF(wo, wi) |wi.n|. Return
 * the directional reflectance, i.e. pdf to be reflected in *any* direction
 * wrt to the incident direction `wi' */
SSF_API double
ssf_bsdf_sample
  (struct ssf_bsdf* bsdf,
   struct ssp_rng* rng, /* Random number generator */
   const double wo[3], /* Normalized outgoing direction */
   const double N[3], /* Normalized surface normal */
   double wi[3], /* Sampled normalized incoming direction */
   int* type, /* Type of the sampled component. Combination of ssf_bxdf_flag */
   double* pdf); /* PDF to sample wi wrt wo */

SSF_API double
ssf_bsdf_eval
  (struct ssf_bsdf* bsdf,
   const double wo[3], /* Normalized outgoing direction */
   const double N[3], /* Normalized surface normal */
   const double wi[3]); /* Normalized incoming direction */

SSF_API double /* Probability density function*/
ssf_bsdf_pdf
  (struct ssf_bsdf* bsdf,
   const double wo[3], /* Normalized outgoing direction */
   const double N[3], /* Normalized surface normal */
   const double wi[3]); /* Normalized incoming direction */

SSF_API res_T
ssf_bsdf_get_data
  (struct ssf_bsdf* bsdf,
   void** data);

SSF_API res_T
ssf_specular_reflection_setup
  (struct ssf_bsdf* bsdf,
   struct ssf_fresnel* fresnel);

SSF_API res_T
ssf_lambertian_reflection_setup
  (struct ssf_bsdf* bsdf,
   const double reflectivity);

SSF_API res_T
ssf_microfacet_reflection_setup
  (struct ssf_bsdf* bsdf,
   struct ssf_fresnel* fresnel,
   struct ssf_microfacet_distribution* distrib);

SSF_API res_T
ssf_thin_specular_dielectric_setup
  (struct ssf_bsdf* bsdf,
   const double absorption, /* In [0, 1] */
   const double eta_i, /* Refraction id of the medium the ray travels in */
   const double eta_t, /* Refraction id of the thin dielectric slab */
   const double thickness); /* Thickness of the slab */

SSF_API res_T
ssf_specular_dielectric_dielectric_interface_setup
  (struct ssf_bsdf* bsdf,
   const double eta_i,
   const double eta_t);

/*******************************************************************************
 * Phase function API - Describes the way the light is scattered in a medium.
 * Note that by convention the outgoing direction `wo' and the incoming
 * direction `wi' point outward the scattering position.
 ******************************************************************************/
SSF_API res_T
ssf_phase_create
  (struct mem_allocator* allocator, /* May be NULL <=> Use default allocator */
   const struct ssf_phase_type* type,
   struct ssf_phase** phase);

SSF_API res_T
ssf_phase_ref_get
  (struct ssf_phase* phase);

SSF_API res_T
ssf_phase_ref_put
  (struct ssf_phase* phase);

SSF_API void
ssf_phase_sample
  (struct ssf_phase* phase,
   struct ssp_rng* rng, /* Random number generator */
   const double wo[3], /* Normalized outgoing direction */
   double wi[3], /* Sampled normalized incoming direction */
   double* pdf); /* PDF to sample wi wrt wo */

SSF_API double
ssf_phase_eval
  (struct ssf_phase* phase,
   const double wo[3], /* Normalized outgoing direction */
   const double wi[3]); /* Normalized incoming direction */

SSF_API double
ssf_phase_pdf
  (struct ssf_phase* phase,
   const double wo[3], /* Normalized outgoing direction */
   const double wi[3]); /* Normalized incoming direction */

SSF_API res_T
ssf_phase_get_data
  (struct ssf_phase* bsdf,
   void** data);

SSF_API res_T
ssf_phase_hg_setup
  (struct ssf_phase* phase,
   const double g); /* Asymmetric parameter in [-1, 1] */

SSF_API res_T
ssf_phase_discrete_setup
  (struct ssf_phase* phase,
   const struct ssf_discrete_setup_args* args);

/*******************************************************************************
 * RDGFA phase function
 ******************************************************************************/
SSF_API res_T
ssf_phase_rdgfa_setup
  (struct ssf_phase* phase,
   const struct ssf_phase_rdgfa_setup_args* args);

SSF_API res_T
ssf_phase_rdgfa_get_desc
  (const struct ssf_phase* phase,
   struct ssf_phase_rdgfa_desc* desc);

SSF_API res_T
ssf_phase_rdgfa_get_interval
  (const struct ssf_phase* phase,
   const size_t interval_id, /* In [0, desc.nintervals[ */
   struct ssf_phase_rdgfa_interval* interval);

/*******************************************************************************
 * Fresnel API - Define the equation of the fresnel term
 ******************************************************************************/
SSF_API res_T
ssf_fresnel_create
  (struct mem_allocator* allocator,
   const struct ssf_fresnel_type* type,
   struct ssf_fresnel** fresnel);

SSF_API res_T
ssf_fresnel_ref_get
  (struct ssf_fresnel* fresnel);

SSF_API res_T
ssf_fresnel_ref_put
  (struct ssf_fresnel* fresnel);

SSF_API double
ssf_fresnel_eval
  (struct ssf_fresnel* fresnel,
   const double cos_theta); /* Cos between facet normal and incoming dir */

/* Retrieve the internal data of the Fresnel term. Usefull for user defined
 * Fresnel terms on which the caller has to retrieve their data to setup the
 * parameters of their Fresnel equation. */
SSF_API res_T
ssf_fresnel_get_data
  (struct ssf_fresnel* fresnel,
   void** data);

SSF_API res_T
ssf_fresnel_dielectric_dielectric_setup
  (struct ssf_fresnel* fresnel,
   /* Refraction id of the medium the incoming ray travels in */
   const double eta_i,
   /* Refraction id of the medium the outgoing transmission ray travels in */
   const double eta_t);

SSF_API res_T
ssf_fresnel_dielectric_conductor_setup
  (struct ssf_fresnel* fresnel,
   const double eta_i, /* Refraction id of the dielectric medium */
   const double eta_t, /* Real part of the refraction id of the conductor */
   const double k_t); /* Imaginary part of the refraction id of the conductor */

SSF_API res_T
ssf_fresnel_constant_setup
  (struct ssf_fresnel* fresnel,
   const double constant); /* in [0, 1] */

/*******************************************************************************
 * Microfacet Distribution API - Note that by convention the outgoing direction
 * `wo' and the half vector `wh' point outward the surface.
 ******************************************************************************/
SSF_API res_T
ssf_microfacet_distribution_create
  (struct mem_allocator* allocator,
   const struct ssf_microfacet_distribution_type* type,
   struct ssf_microfacet_distribution** distrib);

SSF_API res_T
ssf_microfacet_distribution_ref_get
  (struct ssf_microfacet_distribution* distrib);

SSF_API res_T
ssf_microfacet_distribution_ref_put
  (struct ssf_microfacet_distribution* distrib);

SSF_API double
ssf_microfacet_distribution_eval
  (struct ssf_microfacet_distribution* distrib,
   const double N[3], /* Normalized Z-direction of the distribution */
   const double wh[3]); /* Normalized half vector */

SSF_API void
ssf_microfacet_distribution_sample
  (struct ssf_microfacet_distribution* distrib,
   struct ssp_rng* rng, /* Random number generator */
   const double N[3], /* Normalized Z-direction of the distribution */
   double wh[3], /* Normalized half vector */
   double* pdf); /* PDF of the sampled half vector */

SSF_API double
ssf_microfacet_distribution_pdf
  (struct ssf_microfacet_distribution* distrib,
   const double N[3], /* Normalized Z-direction of the distribution */
   const double wh[3]); /* Normalized half vector */

/* Retrieve the internal data of the microfacet distribution. Usefull for user
 * defined distributions on which the caller has to retrieve their data to
 * setup their parameters. */
SSF_API res_T
ssf_microfacet_distribution_get_data
  (struct ssf_microfacet_distribution* distrib,
   void** data);

SSF_API res_T
ssf_beckmann_distribution_setup
  (struct ssf_microfacet_distribution* distrib,
   const double roughness); /* In ]0, 1] */

SSF_API res_T
ssf_blinn_distribution_setup
  (struct ssf_microfacet_distribution* distrib,
   const double exponent); /* in [0, SSF_BLINN_DISTRIBUTION_MAX_EXPONENT] */

SSF_API res_T
ssf_pillbox_distribution_setup
  (struct ssf_microfacet_distribution* distrib,
   const double roughness); /* In ]0, 1] */

END_DECLS

#endif /* SSF_H */
