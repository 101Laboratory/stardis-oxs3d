/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redismshbute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is dismshbuted in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#ifndef SMSH_H
#define SMSH_H

#include <rsys/hash.h>
#include <rsys/rsys.h>

/* Library symbol management */
#if defined(SMSH_SHARED_BUILD) /* Build shared library */
  #define SMSH_API extern EXPORT_SYM
#elif defined(SMSH_STATIC) /* Use/build static library */
  #define SMSH_API extern LOCAL_SYM
#else /* Use shared library */
  #define SMSH_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the smsh function `Func'
 * returns an error. One should use this macro on smsh function calls for
 * which no explicit error checking is performed */
#ifndef NDEBUG
  #define SMSH(Func) ASSERT(smsh_ ## Func == RES_OK)
#else
  #define SMSH(Func) smsh_ ## Func
#endif

/* Forward declaration of external data types */
struct logger;
struct mem_allocator;

struct smsh_create_args {
  struct logger* logger; /* May be NULL <=> default logger */
  struct mem_allocator* allocator; /* NULL <=> use default allocator */
  int verbose; /* Verbosity level */
};
#define SMSH_CREATE_ARGS_DEFAULT__ {NULL, NULL, 0}
static const struct smsh_create_args SMSH_CREATE_ARGS_DEFAULT =
  SMSH_CREATE_ARGS_DEFAULT__;

struct smsh_load_args {
  const char* path;
  int memory_mapping; /* Use memory mapping instead of normal loading */
};
#define SMSH_LOAD_ARGS_NULL__ {NULL, 0}
static const struct smsh_load_args SMSH_LOAD_ARGS_NULL =
  SMSH_LOAD_ARGS_NULL__;

struct smsh_load_stream_args {
  FILE* stream;
  const char* name; /* Name of the stream */
  /* Use memory mapping instead of normal loading. Note that memory mapping
   * cannot be used on some stream like stdin */
  int memory_mapping;
};
#define SMSH_LOAD_STREAM_ARGS_NULL__ {NULL, "stream", 0}
static const struct smsh_load_stream_args SMSH_LOAD_STREAM_ARGS_NULL =
  SMSH_LOAD_STREAM_ARGS_NULL__;

struct smsh_desc {
  const double* nodes; /* List of double[dnode] */
  const uint64_t* cells; /* List of uint64_t[dcell] */
  size_t nnodes;
  size_t ncells;
  unsigned dnode; /* Dimension of a node */
  unsigned dcell; /* Dimension of a cell */
};
#define SMSH_DESC_NULL__ {NULL, NULL, 0, 0, 0, 0}
static const struct smsh_desc SMSH_DESC_NULL = SMSH_DESC_NULL__;

/* Forward declaration of opaque data types */
struct smsh;

BEGIN_DECLS

/*******************************************************************************
 * Star-Mesh API
 ******************************************************************************/
SMSH_API res_T
smsh_create
  (const struct smsh_create_args* args,
   struct smsh** smsh);

SMSH_API res_T
smsh_ref_get
  (struct smsh* smsh);

SMSH_API res_T
smsh_ref_put
  (struct smsh* smsh);

SMSH_API res_T
smsh_load
  (struct smsh* smsh,
   const struct smsh_load_args* args);

SMSH_API res_T
smsh_load_stream
  (struct smsh* smsh,
   const struct smsh_load_stream_args* args);

SMSH_API res_T
smsh_get_desc
  (const struct smsh* smsh,
   struct smsh_desc* desc);

SMSH_API res_T
smsh_desc_compute_hash
  (const struct smsh_desc* desc,
   hash256_T hash);

static INLINE const double*
smsh_desc_get_node
  (const struct smsh_desc* desc,
   const size_t inode)
{
  ASSERT(desc && inode < desc->nnodes);
  return desc->nodes + inode*desc->dnode;
}

static INLINE const uint64_t*
smsh_desc_get_cell
  (const struct smsh_desc* desc,
   const size_t icell)
{
  const uint64_t* cell;
  ASSERT(desc && icell < desc->ncells);
  cell = desc->cells + icell*desc->dcell;
#ifndef NDEBUG
  {unsigned i; FOR_EACH(i, 0, desc->dcell) ASSERT(cell[i] < desc->nnodes);}
#endif
  return cell;
}

END_DECLS

#endif /* SMSH_H */
