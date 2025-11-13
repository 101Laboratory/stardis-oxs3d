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

#ifndef SDIS_BRDF_H
#define SDIS_BRDF_H

#include <rsys/rsys.h>

enum brdf_component {
  BRDF_SPECULAR,
  BRDF_DIFFUSE,
  BRDF_NONE
};

struct brdf_sample {
  double dir[3];
  double pdf;
  enum brdf_component cpnt;
};
#define BRDF_SAMPLE_NULL__ {{0}, 0, BRDF_NONE}
static const struct brdf_sample BRDF_SAMPLE_NULL = BRDF_SAMPLE_NULL__;

struct brdf {
  double emissivity;
  double specular_fraction;
};
#define BRDF_NULL__ {0, 0}
static const struct brdf BRDF_NULL = BRDF_NULL__;

/* forward declarations */
struct sdis_device;
struct sdis_interface;
struct sdis_interface_fragment;
struct ssp_rng;

struct brdf_setup_args {
   struct sdis_interface* interf;
   struct sdis_interface_fragment* frag;
   unsigned source_id;
};
#define BRDF_SETUP_ARGS_NULL__ {NULL, NULL, SDIS_INTERN_SOURCE_ID}
static const struct brdf_setup_args BRDF_SETUP_ARGS_NULL =
  BRDF_SETUP_ARGS_NULL__;

extern LOCAL_SYM res_T
brdf_setup
  (struct sdis_device* dev,
   const struct brdf_setup_args* args,
   struct brdf* brdf);

extern LOCAL_SYM void
brdf_sample
  (const struct brdf* brdf,
   struct ssp_rng* rng,
   const double wi[3], /* Incident direction. Point away from the surface */
   const double N[3], /* Surface normal */
   struct brdf_sample* sample);

#endif /* SDIS_BRDF_H */
