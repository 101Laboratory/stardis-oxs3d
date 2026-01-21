/* Copyright (C) 2018-2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef SDIS_INTFACE_H
#define SDIS_INTFACE_H

#include <rsys/hash_table.h>

#include <limits.h>

struct stardis;
struct stardis_interface_fragment;

/*******************************************************************************
 * Interface data
 ******************************************************************************/
struct intface {
  /* programmed interfaces */
  double (*get_temp)(const struct stardis_interface_fragment*, void*);
  double (*get_flux)(const struct stardis_interface_fragment*, void*);
  double (*get_hc)(const struct stardis_interface_fragment*, void*);
  double (*get_ref_temp)(const struct stardis_interface_fragment*, void*);
  double (*get_tcr)(const struct stardis_interface_fragment*, void*);
  double (*get_emissivity)
    (const struct stardis_interface_fragment*, const unsigned src_id, void*);
  double (*get_alpha)
    (const struct stardis_interface_fragment*, const unsigned src_id, void*);

  void* prog_data;
  /* fluid - solid */
  double hc;
  double ref_temperature;
  double emissivity;
  double alpha;
  /* solid - solid */
  double tcr;
  /* Imposed compute temperature & flux */
  double imposed_temperature;
  double imposed_flux;
  /* IDs */
  unsigned front_medium_id, back_medium_id;
  unsigned desc_id; /* The description this interfaces comes from */
};

/* Declare the hash table that map an interface to its descriptor */
struct int_descs {
  unsigned front, back, intface;
};
#define INT_DESCS_NULL__ { UINT_MAX, UINT_MAX, UINT_MAX }
static const struct int_descs INT_DESCS_NULL = INT_DESCS_NULL__;

static INLINE char
eq_desc(const struct int_descs* a, const struct int_descs* b)
{
  return (char)(a->front == b->front && a->back == b->back
    && a->intface == b->intface);
}

#define HTABLE_NAME intface
#define HTABLE_DATA struct sdis_interface*
#define HTABLE_KEY struct int_descs
#define HTABLE_KEY_FUNCTOR_EQ eq_desc
#include <rsys/hash_table.h>

res_T
create_intface
  (struct stardis* stardis,
   unsigned tr_idx,
   struct htable_intface* htable_interfaces);

#endif
