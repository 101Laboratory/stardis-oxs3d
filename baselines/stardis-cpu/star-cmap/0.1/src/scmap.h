/* Copyright (C) 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef SCMAP_H
#define SCMAP_H

#include <rsys/rsys.h>

/* Library symbol management */
#if defined(SCMAP_SHARED_BUILD) /* Build shared library */
  #define SCMAP_API extern EXPORT_SYM
#elif defined(SCMAP_STATIC) /* Use/build static library */
  #define SCMAP_API extern LOCAL_SYM
#else /* Use shared library */
  #define SCMAP_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the scmap function `Func'
 * returns an error. One should use this macro on scmap function calls for
 * which no explicit error checking is performed */
#ifndef NDEBUG
  #define SCMAP(Func) ASSERT(scmap_ ## Func == RES_OK)
#else
  #define SCMAP(Func) scmap_ ## Func
#endif

enum scmap_filter {
  SCMAP_FILTER_NEAREST,
  SCMAP_FILTER_LINEAR,
  SCMAP_FILTERS_COUNT__
};

struct scmap_palette {
  void (*get_color)(const size_t icolor, double color[3], void* context);
  size_t ncolors; /* #colors */
  void* context;
};
#define SCMAP_PALETTE_NULL__ {NULL, 0, NULL}
static const struct scmap_palette SCMAP_PALETTE_NULL = SCMAP_PALETTE_NULL__;

/* Forward declarations */
struct scmap;
struct logger;
struct mem_allocator;

BEGIN_DECLS

/* Builtin palettes */
SCMAP_API const struct scmap_palette scmap_palette_accent;
SCMAP_API const struct scmap_palette scmap_palette_blues;
SCMAP_API const struct scmap_palette scmap_palette_brbg;
SCMAP_API const struct scmap_palette scmap_palette_bugn;
SCMAP_API const struct scmap_palette scmap_palette_bupu;
SCMAP_API const struct scmap_palette scmap_palette_chromajs;
SCMAP_API const struct scmap_palette scmap_palette_dark2;
SCMAP_API const struct scmap_palette scmap_palette_gnbu;
SCMAP_API const struct scmap_palette scmap_palette_gnpu;
SCMAP_API const struct scmap_palette scmap_palette_greens;
SCMAP_API const struct scmap_palette scmap_palette_greys;
SCMAP_API const struct scmap_palette scmap_palette_inferno;
SCMAP_API const struct scmap_palette scmap_palette_jet;
SCMAP_API const struct scmap_palette scmap_palette_magma;
SCMAP_API const struct scmap_palette scmap_palette_moreland;
SCMAP_API const struct scmap_palette scmap_palette_oranges;
SCMAP_API const struct scmap_palette scmap_palette_orrd;
SCMAP_API const struct scmap_palette scmap_palette_paired;
SCMAP_API const struct scmap_palette scmap_palette_parula;
SCMAP_API const struct scmap_palette scmap_palette_pastel1;
SCMAP_API const struct scmap_palette scmap_palette_pastel2;
SCMAP_API const struct scmap_palette scmap_palette_piyg;
SCMAP_API const struct scmap_palette scmap_palette_plasma;
SCMAP_API const struct scmap_palette scmap_palette_prgn;
SCMAP_API const struct scmap_palette scmap_palette_pubu;
SCMAP_API const struct scmap_palette scmap_palette_pubugn;
SCMAP_API const struct scmap_palette scmap_palette_puor;
SCMAP_API const struct scmap_palette scmap_palette_purd;
SCMAP_API const struct scmap_palette scmap_palette_purples;
SCMAP_API const struct scmap_palette scmap_palette_rdbu;
SCMAP_API const struct scmap_palette scmap_palette_rdgy;
SCMAP_API const struct scmap_palette scmap_palette_rdpu;
SCMAP_API const struct scmap_palette scmap_palette_rdylbu;
SCMAP_API const struct scmap_palette scmap_palette_rdylgn;
SCMAP_API const struct scmap_palette scmap_palette_reds;
SCMAP_API const struct scmap_palette scmap_palette_sand;
SCMAP_API const struct scmap_palette scmap_palette_set1;
SCMAP_API const struct scmap_palette scmap_palette_set2;
SCMAP_API const struct scmap_palette scmap_palette_set3;
SCMAP_API const struct scmap_palette scmap_palette_spectral;
SCMAP_API const struct scmap_palette scmap_palette_viridis;
SCMAP_API const struct scmap_palette scmap_palette_whgnbu;
SCMAP_API const struct scmap_palette scmap_palette_whylrd;
SCMAP_API const struct scmap_palette scmap_palette_ylgn;
SCMAP_API const struct scmap_palette scmap_palette_ylgnbu;
SCMAP_API const struct scmap_palette scmap_palette_ylorbr;
SCMAP_API const struct scmap_palette scmap_palette_ylorrd;
SCMAP_API const struct scmap_palette scmap_palette_ylrd;

/*******************************************************************************
 * Star-ColorMap API
 ******************************************************************************/
SCMAP_API res_T
scmap_create
  (struct logger* logger, /* NULL <=> use builtin logger */
   struct mem_allocator* allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   const struct scmap_palette* palette,
   struct scmap** scmap);

SCMAP_API res_T
scmap_ref_get
  (struct scmap* scmap);

SCMAP_API res_T
scmap_ref_put
  (struct scmap* scmap);

SCMAP_API res_T
scmap_fetch_color
  (const struct scmap* scmap,
   const double value,
   const enum scmap_filter filter,
   double color[3]); /* In [0, 1] */

/* Return NULL if the submitted name does not reference a knwon palette */
SCMAP_API const struct scmap_palette*
scmap_get_builtin_palette
  (const char* name); /* e.g. "inferno" */

END_DECLS

#endif /* SCMAP_H */

