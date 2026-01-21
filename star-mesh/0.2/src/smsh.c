/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#define _POSIX_C_SOURCE 200809L /* mmap support */
#define _DEFAULT_SOURCE 1 /* MAP_POPULATE support */
#define _BSD_SOURCE 1 /* MAP_POPULATE for glibc < 2.19 */

#include "smsh.h"
#include "smsh_c.h"
#include "smsh_log.h"

#include <rsys/cstr.h>
#include<rsys/mem_allocator.h>

#include <errno.h>
#include <unistd.h>
#include <sys/mman.h> /* mmap */
#include <sys/stat.h> /* fstat */

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
is_stdin(FILE* stream)
{
  struct stat stream_buf;
  struct stat stdin_buf;
  ASSERT(stream);

  CHK(fstat(fileno(stream), &stream_buf) == 0);
  CHK(fstat(STDIN_FILENO, &stdin_buf) == 0);
  return stream_buf.st_dev == stdin_buf.st_dev;
}

static INLINE res_T
check_smsh_create_args(const struct smsh_create_args* args)
{
  /* Nothing to check. Only return RES_BAD_ARG if args is NULL */
  return args ? RES_OK : RES_BAD_ARG;
}

static INLINE res_T
check_smsh_load_args(const struct smsh_load_args* args)
{
  if(!args || !args->path) return RES_BAD_ARG;
  return RES_OK;
}

static INLINE res_T
check_smsh_load_stream_args
  (struct smsh* smsh,
   const struct smsh_load_stream_args* args)
{
  if(!args || !args->stream || !args->name) return RES_BAD_ARG;
  if(args->memory_mapping && is_stdin(args->stream)) {
    log_err(smsh,
      "%s: unable to use memory mapping on data loaded from stdin\n",
      args->name);
    return RES_BAD_ARG;
  }
  return RES_OK;
}

static void
reset_smsh(struct smsh* smsh)
{
  ASSERT(smsh);
  smsh->nnodes = 0;
  smsh->ncells = 0;
  smsh->pagesize = 0;
  if(smsh->nodes) {
    if(!smsh->map_len_nodes) {
      MEM_RM(smsh->allocator, smsh->nodes);
    } else if(smsh->nodes != MAP_FAILED) {
      munmap(smsh->nodes, smsh->map_len_nodes);
    }
  }
  if(smsh->cells) {
    if(!smsh->map_len_cells) {
      MEM_RM(smsh->allocator, smsh->cells);
    } else if(smsh->nodes != MAP_FAILED) {
      munmap(smsh->cells, smsh->map_len_cells);
    }
  }
  smsh->nodes = NULL;
  smsh->cells = NULL;
  smsh->map_len_nodes = 0;
  smsh->map_len_cells = 0;
}

static res_T
map_data
  (struct smsh* smsh,
   const char* name,
   const int fd, /* File descriptor */
   const size_t filesz, /* Overall filesize */
   const char* data_name,
   const off_t offset, /* Offset of the data into file */
   const size_t map_len,
   void** out_map) /* Lenght of the data to map */
{
  void* map = NULL;
  res_T res = RES_OK;
  ASSERT(smsh && name && filesz && data_name && map_len && out_map);
  ASSERT(IS_ALIGNED((size_t)offset, (size_t)smsh->pagesize));

  if((size_t)offset + map_len > filesz) {
    log_err(smsh, "%s: the %s to load exceed the file size.\n",
      name, data_name);
    res = RES_IO_ERR;
    goto error;
  }

  map = mmap(NULL, map_len, PROT_READ, MAP_PRIVATE|MAP_POPULATE, fd, offset);
  if(map == MAP_FAILED) {
    log_err(smsh, "%s: could not map the %s -- %s.\n",
      name, data_name, strerror(errno));
    res = RES_IO_ERR;
    goto error;
  }

exit:
  *out_map = map;
  return res;
error:
  if(map == MAP_FAILED) map = NULL;
  goto exit;
}

static res_T
map_file(struct smsh* smsh, FILE* stream, const char* name)
{
  off_t pos_offset;
  off_t ids_offset;
  size_t filesz;
  res_T res = RES_OK;
  ASSERT(smsh && stream && name);

  /* Compute the length in bytes of the data to map */
  smsh->map_len_nodes = smsh->nnodes * sizeof(double) * smsh->dnode;
  smsh->map_len_cells = smsh->ncells * sizeof(uint64_t) * smsh->dcell;
  smsh->map_len_nodes = ALIGN_SIZE(smsh->map_len_nodes, (size_t)smsh->pagesize);
  smsh->map_len_cells = ALIGN_SIZE(smsh->map_len_cells, (size_t)smsh->pagesize);

  /* Find the offsets of the positions/indices data into the stream */
  pos_offset = (off_t)ALIGN_SIZE((uint64_t)ftell(stream), smsh->pagesize);
  ids_offset = (off_t)((size_t)pos_offset + smsh->map_len_nodes);

  /* Retrieve the overall filesize */
  fseek(stream, 0, SEEK_END);
  filesz = (size_t)ftell(stream);

  /* Map the nodes */
  res = map_data(smsh, name, fileno(stream), filesz, "nodes",
    pos_offset, smsh->map_len_nodes, (void**)&smsh->nodes);
  if(res != RES_OK) goto error;

  /* Map the cells */
  res = map_data(smsh, name, fileno(stream), filesz, "cells",
    ids_offset, smsh->map_len_cells, (void**)&smsh->cells);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

static res_T
read_padding(FILE* stream, const size_t padding)
{
  char chunk[1024];
  size_t remaining_nbytes = padding;

  while(remaining_nbytes) {
    const size_t nbytes = MMIN(sizeof(chunk), remaining_nbytes);
    if(fread(chunk, 1, nbytes, stream) != nbytes) return RES_IO_ERR;
    remaining_nbytes -= nbytes;
  }
  return RES_OK;
}

static res_T
load_file(struct smsh* smsh, FILE* stream, const char* name)
{
  size_t ncoords;
  size_t nindices;
  size_t padding_bytes;
  size_t sizeof_nodes;
  size_t sizeof_cells;
  size_t sizeof_header;
  res_T res = RES_OK;
  ASSERT(smsh && stream && name);

  ncoords = smsh->nnodes * smsh->dnode;
  nindices = smsh->ncells * smsh->dcell;
  sizeof_nodes = ncoords * sizeof(*smsh->nodes);
  sizeof_cells = nindices * sizeof(*smsh->cells);
  sizeof_header =
    sizeof(smsh->pagesize)
  + sizeof(smsh->nnodes)
  + sizeof(smsh->ncells)
  + sizeof(smsh->dnode)
  + sizeof(smsh->dcell);

  /* Allocate the memory space where the loaded data will be stored */
  smsh->nodes = MEM_CALLOC(smsh->allocator, ncoords, sizeof(*smsh->nodes));
  if(!smsh->nodes) { res = RES_MEM_ERR; goto error; }
  smsh->cells = MEM_CALLOC(smsh->allocator, nindices, sizeof(*smsh->cells));
  if(!smsh->cells) { res = RES_MEM_ERR; goto error; }

  padding_bytes = ALIGN_SIZE(sizeof_header, smsh->pagesize) - sizeof_header;
  if((res = read_padding(stream, padding_bytes)) != RES_OK) goto error;

  /* Load the nodes */
  if(fread(smsh->nodes, sizeof(*smsh->nodes), ncoords, stream) != ncoords) {
    res = RES_IO_ERR;
    goto error;
  }

  padding_bytes = ALIGN_SIZE(sizeof_nodes, smsh->pagesize) - sizeof_nodes;
  if((res = read_padding(stream, padding_bytes)) != RES_OK) goto error;

  /* Load the cells */
  if(fread(smsh->cells, sizeof(*smsh->cells), nindices, stream) != nindices) {
    res = RES_IO_ERR;
    goto error;
  }

  padding_bytes = ALIGN_SIZE(sizeof_cells, smsh->pagesize) - sizeof_cells;
  if((res = read_padding(stream, padding_bytes)) != RES_OK) goto error;

exit:
  return res;
error:
  log_err(smsh, "%s: error while loading data -- %s.\n",
    name, res_to_cstr(res));
  goto exit;
}

static res_T
load_stream(struct smsh* smsh, const struct smsh_load_stream_args* args)
{
  res_T res = RES_OK;
  ASSERT(smsh && check_smsh_load_stream_args(smsh, args) == RES_OK);

  reset_smsh(smsh);

  /* Read file header */
  if(fread(&smsh->pagesize, sizeof(&smsh->pagesize), 1, args->stream) != 1) {
    if(ferror(args->stream)) {
      log_err(smsh, "%s: could not read the pagesize.\n", args->name);
    }
    res = RES_IO_ERR;
    goto error;
  }

  #define READ(Var, N, Name) {                                                 \
    if(fread((Var), sizeof(*(Var)), (N), args->stream) != (N)) {               \
      log_err(smsh, "%s: could not read the %s.\n", args->name, (Name));       \
      res = RES_IO_ERR;                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0
  READ(&smsh->nnodes, 1, "number of nodes");
  READ(&smsh->ncells, 1, "number of cells");
  READ(&smsh->dnode, 1, "node dimension");
  READ(&smsh->dcell, 1, "cell dimension");
  #undef READ

  if(!IS_ALIGNED(smsh->pagesize, smsh->pagesize_os)) {
    log_err(smsh,
      "%s: invalid page size %li. The page size attribute must be aligned on "
      "the page size of the operating system (%lu).\n",
      args->name, smsh->pagesize, (unsigned long)smsh->pagesize_os);
    res = RES_BAD_ARG;
    goto error;
  }

  if(args->memory_mapping) {
    res = map_file(smsh, args->stream, args->name);
    if(res != RES_OK) goto error;
  } else {
    res = load_file(smsh, args->stream, args->name);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  reset_smsh(smsh);
  goto exit;
}

static void
release_smsh(ref_T* ref)
{
  struct smsh* smsh;
  ASSERT(ref);
  smsh = CONTAINER_OF(ref, struct smsh, ref);
  reset_smsh(smsh);
  if(smsh->logger == &smsh->logger__) logger_release(&smsh->logger__);
  MEM_RM(smsh->allocator, smsh);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
smsh_create
  (const struct smsh_create_args* args,
   struct smsh** out_smsh)
{
  struct smsh* smsh = NULL;
  struct mem_allocator* allocator = NULL;
  res_T res = RES_OK;

  if(!out_smsh) { res = RES_BAD_ARG; goto error; }
  res = check_smsh_create_args(args);
  if(res != RES_OK) goto error;

  allocator = args->allocator ? args->allocator : &mem_default_allocator;
  smsh = MEM_CALLOC(allocator, 1, sizeof(*smsh));
  if(!smsh) {
    if(args->verbose) {
      #define ERR_STR "Could not allocate the Star-Mesh device.\n"
      if(args->logger) {
        logger_print(args->logger, LOG_ERROR, ERR_STR);
      } else {
        fprintf(stderr, MSG_ERROR_PREFIX ERR_STR);
      }
      #undef ERR_STR
    }
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&smsh->ref);
  smsh->allocator = allocator;
  smsh->verbose = args->verbose;
  smsh->pagesize_os = (size_t)sysconf(_SC_PAGESIZE);
  if(args->logger) {
    smsh->logger = args->logger;
  } else {
    setup_log_default(smsh);
  }

exit:
  if(out_smsh) *out_smsh = smsh;
  return res;
error:
  if(smsh) {
    SMSH(ref_put(smsh));
    smsh = NULL;
  }
  goto exit;
}

res_T
smsh_ref_get(struct smsh* smsh)
{
  if(!smsh) return RES_BAD_ARG;
  ref_get(&smsh->ref);
  return RES_OK;
}

res_T
smsh_ref_put(struct smsh* smsh)
{
  if(!smsh) return RES_BAD_ARG;
  ref_put(&smsh->ref, release_smsh);
  return RES_OK;
}


res_T
smsh_load(struct smsh* smsh, const struct smsh_load_args* args)
{
  struct smsh_load_stream_args stream_args = SMSH_LOAD_STREAM_ARGS_NULL;
  FILE* file = NULL;
  res_T res = RES_OK;

  if(!smsh) { res = RES_BAD_ARG; goto error; }
  res = check_smsh_load_args(args);
  if(res != RES_OK) goto error;

  file = fopen(args->path, "r");
  if(!file) {
    log_err(smsh, "%s: error opening file `%s'.\n", FUNC_NAME, args->path);
    res = RES_IO_ERR;
    goto error;
  }

  stream_args.stream = file;
  stream_args.name = args->path;
  stream_args.memory_mapping = args->memory_mapping;
  res = load_stream(smsh, &stream_args);
  if(res != RES_OK) goto error;

exit:
  if(file) fclose(file);
  return res;
error:
  goto exit;
}

res_T
smsh_load_stream(struct smsh* smsh, const struct smsh_load_stream_args* args)
{
  res_T res = RES_OK;
  if(!smsh) return RES_BAD_ARG;
  res = check_smsh_load_stream_args(smsh, args);
  if(res != RES_OK) return res;
  return load_stream(smsh, args);
}

res_T
smsh_get_desc(const struct smsh* smsh, struct smsh_desc* desc)
{
  if(!smsh || !desc) return RES_BAD_ARG;
  desc->nodes = smsh->nodes;
  desc->cells = smsh->cells;
  desc->nnodes = smsh->nnodes;
  desc->ncells = smsh->ncells;
  desc->dnode = smsh->dnode;
  desc->dcell = smsh->dcell;
  return RES_OK;
}

res_T
smsh_desc_compute_hash(const struct smsh_desc* desc, hash256_T hash)
{
  struct sha256_ctx ctx;

  if(!desc || !hash) return RES_BAD_ARG;

  #define HASH(Var, Nb) \
    sha256_ctx_update(&ctx, (const char*)(Var), sizeof(*Var)*(Nb));

  sha256_ctx_init(&ctx);
  HASH(desc->nodes, desc->nnodes*desc->dnode);
  HASH(desc->cells, desc->ncells*desc->dcell);
  HASH(&desc->nnodes, 1);
  HASH(&desc->ncells, 1);
  HASH(&desc->dnode, 1);
  HASH(&desc->dcell, 1);
  sha256_ctx_finalize(&ctx, hash);

  #undef HASH

  return RES_OK;
}
