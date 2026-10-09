/* Copyright (C) 2015-2018, 2021, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef S4V_S_REALIZATION_H
#define S4V_S_REALIZATION_H

#include <rsys/rsys.h>

/* forward definition */
struct s3d_hit;
struct s3d_scene_view;
struct ssp_rng;

struct s4vs_context {
  struct s3d_scene_view* view;
  double ks;
  double g;
};

/* Hit filter function used to handle auto intersection */
extern int
s4vs_discard_self_hit
  (const struct s3d_hit* hit,
   const float ray_org[3],
   const float ray_dir[3],
   const float ray_range[2],
   void* ray_data,
   void* filter_data);

/*******************************************************************************
 * MC realization function
 ******************************************************************************/
extern res_T
s4vs_realization
  (void* length,
   struct ssp_rng* rng,
   const unsigned ithread,
   const uint64_t irealisation,
   void* context); /* User defined data */

#endif /* REALIZATION_H */
