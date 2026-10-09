/* Copyright (C) 2018-2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef SBB_H
#define SBB_H

#include <rsys/rsys.h>

/* Library symbol management */
#if defined(SBB_SHARED_BUILD) /* Build shared library */
  #define SBB_API extern EXPORT_SYM
#elif defined(SBB_STATIC) /* Use/build static library */
  #define SBB_API extern LOCAL_SYM
#else /* Use shared library */
  #define SBB_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the sbb function
 * `Func' returns an error. One should use this macro on smc function
 * calls for which no explicit error checking is performed */
#ifndef NDEBUG
  #define SBB(Func) ASSERT(sbb_ ## Func == RES_OK)
#else
  #define SBB(Func) sbb_ ## Func
#endif

/* Forward declarations of opaque types */
struct sbb_ran_planck;

/* Forward declarations of external types */
struct logger;
struct mem_allocator;

struct sbb_ran_planck_create_args {
  struct mem_allocator* allocator; /* NULL <=> use default allocator */
  struct logger* logger; /* NULL <=> user default logger */
  double range[2]; /* Spectral range [m] */
  double ref_temperature; /* Reference temperature [K] */

  /* Indication of the number of bands used to discretize the CDF. These bands
   * are only used to speed up sampling and do not introduce any approximation:
   * once a band is sampled, the wavelength in the band is sampled continuously.
   * To disable this feature, set nbands to 0 */
  size_t nbands;
  int verbose; /* Verbosity level */
};
#define SBB_RAN_PLANCK_CREATE_ARGS_DEFAULT__ {\
  NULL, /* Allocator */\
  NULL, /* Logger */\
  {9.6e-6, 11.5e-6}, /* Spectral range [m] */\
  300, /* Reference temperature [K] */\
  1900, /* #bands used to speed up sampling, actually 1 band per nanometers */\
  0 /* Verbosity */\
}
static const struct sbb_ran_planck_create_args
SBB_RAN_PLANCK_CREATE_ARGS_DEFAULT = SBB_RAN_PLANCK_CREATE_ARGS_DEFAULT__;

BEGIN_DECLS

/*******************************************************************************
 * Blackbody API
 ******************************************************************************/
SBB_API double
sbb_blackbody_fraction
  (const double lambda0, /* [m] */
   const double lambda1, /* [m] */
   const double temperature); /* [K] */

/* Return the Planck value at a given wavelength */
SBB_API double /* [W/m^2/sr/m] */
sbb_planck_monochromatic
  (const double lambda, /* [m] */
   const double temperature); /* [K] */

/* Return the average Planck value over the [lambda_min, lambda_max] interval */
SBB_API double /* [W/m^2/sr/m] */
sbb_planck_interval
  (const double lambda_min, /* [m] */
   const double lambda_max, /* [m] */
   const double temperature); /* [K] */

/* Invoke planck_monochromatic or planck_interval whether the submitted
 * interval is null or not, respectively */
static INLINE double /* [W/m^2/sr/m] */
sbb_planck
  (const double lambda_min, /* [m] */
   const double lambda_max, /* [m] */
   const double temperature) /* [K] */
{
  ASSERT(lambda_min >= 0 && lambda_max >= 0);
  ASSERT(lambda_min <= lambda_max);
  ASSERT(temperature >0);
  if(lambda_min == lambda_max) {
    return sbb_planck_monochromatic(lambda_min, temperature);
  } else {
    return sbb_planck_interval(lambda_min, lambda_max, temperature);
  }
}

SBB_API res_T
sbb_brightness_temperature
  (const double lambda_min, /* [m] */
   const double lambda_max, /* [m] */
   const double radiance, /* Averaged over the spectral range [W/m^2/sr/m] */
   double* temperature); /* [K] */

SBB_API res_T
sbb_radiance_temperature
  (const double lambda_min, /* [m] */
   const double lambda_max, /* [m] */
   const double radiance, /* [W/m^2/sr] */
   double* temperature); /* [K] */

/*******************************************************************************
 * Planck distribution
 ******************************************************************************/
SBB_API res_T
sbb_ran_planck_create
  (struct sbb_ran_planck_create_args* args,
   struct sbb_ran_planck** planck);

SBB_API res_T
sbb_ran_planck_ref_get
  (struct sbb_ran_planck* planck);

SBB_API res_T
sbb_ran_planck_ref_put
  (struct sbb_ran_planck* planck);

SBB_API double /* Wavelength [m] */
sbb_ran_planck_sample
  (struct sbb_ran_planck* planck,
   const double r0, /* Random number in [0, 1[ */
   const double r1, /* Random number in [0, 1[ */
   double* pdf); /* [m^-1]. May be NULL */

END_DECLS

#endif /* SBB_H */
