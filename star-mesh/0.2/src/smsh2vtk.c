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

#define _POSIX_C_SOURCE 200112L /* getopt support */

#include "smsh.h"

#include <rsys/cstr.h>
#include<rsys/mem_allocator.h>
#include <rsys/text_reader.h>

#include <errno.h>
#include <string.h> /* strerror */
#include <unistd.h> /* getopt */

/* Input arguments */
struct args {
  const char* mesh; /* Tetrahedral mesh */
  const char* output; /* Output file */
};
static const struct args ARGS_DEFAULT = {0};

/* Command data */
struct cmd {
  struct smsh* mesh; /* Tetrahedral mesh */
  FILE* output; /* Ouput file */
};
static const struct cmd CMD_NULL = {0};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE void
usage(FILE* stream)
{
  fprintf(stream,
    "usage: smsh2vtk [-o output] mesh\n");
}

static void
args_release(struct args* args)
{
  ASSERT(args);
  *args = ARGS_DEFAULT;
}

static res_T
args_init(struct args* args, const int argc, char** argv)
{
  int opt = 0;
  res_T res = RES_OK;

  *args = ARGS_DEFAULT;

  while((opt = getopt(argc, argv, "o:")) != -1) {
    switch(opt) {
      case 'o': args->output = optarg; break;
      default: res = RES_BAD_ARG;
    }
    if(res != RES_OK) goto error;
  }

  if(optind < argc) args->mesh = argv[optind];

  if(!args->mesh) {
    fprintf(stderr, "mesh is missing\n");
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  usage(stderr);
  args_release(args);
  goto exit;
}

static res_T
check_mem_leaks(void)
{
  char buffer[128] = {0};
  size_t sz = 0;

  if((sz = mem_allocated_size()) == 0)
    return RES_OK; /* No memory leak */

  size_to_cstr(sz, SIZE_ALL, NULL, buffer, sizeof(buffer));
  fprintf(stderr, "memory leaks: %s\n", buffer);
  return RES_MEM_ERR;
}

static res_T
write_vtk_header(struct cmd* cmd)
{
  res_T res = RES_OK;
  ASSERT(cmd);

  #define FPRINTF(Msg) \
    { if(fprintf(cmd->output, Msg) < 0) goto error; } (void)0
  FPRINTF("# vtk DataFile Version 2.0\n");
  FPRINTF("Volumic mesh\n");
  FPRINTF("ASCII\n");
  #undef FPRINTF

exit:
  return res;
error:
  fprintf(stderr, "header write error -- %s\n", strerror(errno));
  res = RES_IO_ERR;
  goto exit;
}

static INLINE int
write_nodes(FILE* stream, const struct smsh_desc* desc)
{
  int err = 0;
  size_t i = 0;
  ASSERT(stream && desc && desc->dnode == 3);

  #define FPRINTF(...) \
    { if(fprintf(stream, __VA_ARGS__) < 0) goto error; } (void)0
  /* Vertices */
  FPRINTF("POINTS %zu double\n", desc->nnodes);
  FOR_EACH(i, 0, desc->nnodes) FPRINTF("%f %f %f\n", SPLIT3(desc->nodes+i*3));
  #undef FPRINTF

exit:
  return err;
error:
  err = errno;
  goto exit;
}

static res_T
write_tetrahedra(FILE* stream, const struct smsh_desc* desc)
{
  size_t i = 0;
  int err = 0;

  ASSERT(stream && desc && desc->dnode == 3 && desc->dcell == 4);

  #define FPRINTF(...) { \
    if(fprintf(stream, __VA_ARGS__) < 0) { err = errno; goto error; } \
  } (void)0

  FPRINTF("DATASET UNSTRUCTURED_GRID\n");

  /* Vertices */
  if((err = write_nodes(stream, desc))) goto error;

  /* Cells */
  FPRINTF("CELLS %zu %zu\n", desc->ncells, desc->ncells*(4+1));
  FOR_EACH(i, 0, desc->ncells) {
    FPRINTF("4 %zu %zu %zu %zu\n", SPLIT4(desc->cells+i*4));
  }

  /* Cell types (VTK_TETRA == 10) */
  FPRINTF("CELL_TYPES %zu\n", desc->ncells);
  FOR_EACH(i, 0, desc->ncells) FPRINTF("10\n");

  #undef FPRINTF

exit:
  return err;
error:
  goto exit;
}

static res_T
write_triangles(FILE* stream, const struct smsh_desc* desc)
{
  size_t i = 0;
  int err = 0;

  ASSERT(stream && desc && desc->dnode == 3 && desc->dcell == 3);

  #define FPRINTF(...) { \
    if(fprintf(stream, __VA_ARGS__) < 0) { err = errno; goto error; } \
  } (void)0

  FPRINTF("DATASET POLYDATA\n");

  /* Vertices */
  if((err == write_nodes(stream, desc))) goto error;

  /* Triangles */
  FPRINTF("POLYGONS %zu %zu\n", desc->ncells, desc->ncells*(3+1));
  FOR_EACH(i, 0, desc->ncells) {
    FPRINTF("3 %zu %zu %zu\n", SPLIT3(desc->cells+i*3));
  }

  #undef FPRINTF

exit:
  return err;
error:
  goto exit;
}

static int
write_mesh(struct cmd* cmd)
{
  struct smsh_desc desc = SMSH_DESC_NULL;
  int err = 0;
  res_T res = RES_OK;
  ASSERT(cmd);

  SMSH(get_desc(cmd->mesh, &desc));

  switch(desc.dcell) {
    case 3: err = write_triangles(cmd->output, &desc); break;
    case 4: err = write_tetrahedra(cmd->output, &desc); break;
    default: FATAL("Unreachable code\n");
  }
  if(err != 0) goto error;

exit:
  return res;
error:
  fprintf(stderr, "mesh write error -- %s\n", strerror(err));
  res = RES_IO_ERR;
  goto exit;
}

static res_T
setup_mesh(struct cmd* cmd, const struct args* args)
{
  struct smsh_create_args create_args = SMSH_CREATE_ARGS_DEFAULT;
  struct smsh_load_args load_args = SMSH_LOAD_ARGS_NULL;
  struct smsh_desc desc = SMSH_DESC_NULL;
  res_T res = RES_OK;
  ASSERT(cmd && args);

  create_args.verbose = 1;
  if((res = smsh_create(&create_args, &cmd->mesh)) != RES_OK) goto error;

  load_args.path = args->mesh;
  if((res = smsh_load(cmd->mesh, &load_args)) != RES_OK) goto error;

  if((res = smsh_get_desc(cmd->mesh, &desc)) != RES_OK) goto error;

  if(desc.dnode != 3 || (desc.dcell != 3 && desc.dcell != 4)) {
    fprintf(stderr,
      "expecting a tetrahedral mesh or a triangle mesh "
      "-- dcell = %u, dnode = %u\n",
      desc.dcell, desc.dnode);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  if(cmd->mesh) { SMSH(ref_put(cmd->mesh)); cmd->mesh = NULL; }
  goto exit;
}

static res_T
setup_output(struct cmd* cmd, const struct args* args)
{
  res_T res = RES_OK;
  ASSERT(cmd && args);

  if(!args->output) {
    cmd->output = stdout;
  } else if((cmd->output = fopen(args->output, "w")) == NULL) {
    fprintf(stderr, "error opening output file '%s' -- %s\n",
      args->output, strerror(errno));
    res = RES_IO_ERR;
    goto error;
  }

exit:
  return res;
error:
  if(cmd->output) { CHK(fclose(cmd->output) == 0); cmd->output = NULL; }
  goto exit;
}

static void
cmd_release(struct cmd* cmd)
{
  ASSERT(cmd);
  if(cmd->mesh) SMSH(ref_put(cmd->mesh));
  if(cmd->output && cmd->output != stdout) CHK(fclose(cmd->output) == 0);
}

static res_T
cmd_init(struct cmd* cmd, const struct args* args)
{
  res_T res = RES_OK;
  ASSERT(cmd && args);

  if((res = setup_mesh(cmd, args)) != RES_OK) goto error;
  if((res = setup_output(cmd, args)) != RES_OK) goto error;

exit:
  return res;
error:
  cmd_release(cmd);
  goto exit;
}

static res_T
cmd_run(struct cmd* cmd)
{
  res_T res = RES_OK;
  ASSERT(cmd);

  if((res = write_vtk_header(cmd)) != RES_OK) goto error;
  if((res = write_mesh(cmd)) != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * The program
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct args args = ARGS_DEFAULT;
  struct cmd cmd = CMD_NULL;
  int err = 0;
  res_T res = RES_OK;

  if((res = args_init(&args, argc, argv)) != RES_OK) goto error;
  if((res = cmd_init(&cmd, &args)) != RES_OK) goto error;
  if((res = cmd_run(&cmd)) != RES_OK) goto error;

exit:
  args_release(&args);
  cmd_release(&cmd);
  if((res = check_mem_leaks()) != RES_OK && !err) err = 1;
  return err;
error:
  err = 1;
  goto exit;
}
