/* Copyright (C) 2015, 2016, 2019, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#define _POSIX_C_SOURCE 200112L /* getopt support */

#include "sstl.h"

#include <rsys/mem_allocator.h>
#include <unistd.h> /* getopt */

struct args {
  /* List of input meshes.
   * Empty list means that a mesh is read from standard */
  char* const* meshes;
  unsigned nmeshes;

  enum sstl_type type; /* Type of input meshes */
  int verbose; /* Verbosity level */
  int quit;
};
static const struct args ARGS_DEFAULT = {NULL, 0, SSTL_NONE__, 0, 0};

struct cmd {
  struct args args;
  struct sstl* sstl;
};
static const struct cmd CMD_NULL = {0};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE void
usage(FILE* stream)
{
  fprintf(stream, "usage: sstl [-abhv] [file ...]\n");
}

static res_T
args_init(struct args* args, int argc, char** argv)
{
  int opt = 0;
  res_T res = RES_OK;

  *args = ARGS_DEFAULT;

  while((opt = getopt(argc, argv, "abhv")) != -1) {
    switch(opt) {
      case 'a': args->type = SSTL_ASCII; break;
      case 'b': args->type = SSTL_BINARY; break;
      case 'h':
        usage(stdout);
        args->quit = 1;
        goto exit;
      case 'v': args->verbose += (args->verbose < 3); break;
      default: res = RES_BAD_ARG; break;
    }
    if(res != RES_OK) goto error;
  }

  /* Setup the list of meshes */
  args->meshes = argv + optind;
  args->nmeshes = (unsigned)(argc - optind);

  if(!args->nmeshes && args->type == SSTL_NONE__) {
    fprintf(stderr,
        "StL type must be defined for reading on stdin "
        "-- options '-a' or -b'\n");
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  usage(stderr);
  goto exit;
}

static INLINE void
cmd_release(struct cmd* cmd)
{
  if(cmd->sstl) SSTL(ref_put(cmd->sstl));
}

static INLINE res_T
cmd_init(struct cmd* cmd, const struct args* args)
{
  res_T res = RES_OK;
  ASSERT(cmd && args);

  cmd->args = *args;

  res = sstl_create(NULL, NULL, args->verbose, &cmd->sstl);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  cmd_release(cmd);
  goto exit;
}

static INLINE const char*
type_to_cstr(const enum sstl_type type)
{
  const char* cstr = NULL;
  switch(type) {
    case SSTL_ASCII: cstr = "ascii"; break;
    case SSTL_BINARY: cstr = "binary"; break;
    default: FATAL("Unreachable code"); break;
  }
  return cstr;
}

static INLINE void
print_info(const struct cmd* cmd)
{
  struct sstl_desc desc = SSTL_DESC_NULL;
  ASSERT(cmd);

  SSTL(get_desc(cmd->sstl, &desc));
  printf("%s\t%s\t%lu\t%lu\t%s\n",
    type_to_cstr(desc.type),
    desc.solid_name ? desc.solid_name : "null",
    (unsigned long)desc.triangles_count,
    (unsigned long)desc.vertices_count,
    desc.filename);
}

static INLINE res_T
cmd_run(const struct cmd* cmd)
{
  res_T res = RES_OK;
  ASSERT(cmd);

  /* Read from the standard input */
  if(!cmd->args.nmeshes) {
    switch(cmd->args.type) {
      case SSTL_ASCII:
        res = sstl_load_stream_ascii(cmd->sstl, stdin, "stdin (ASCII)");
        break;
      case SSTL_BINARY:
        res = sstl_load_stream_binary(cmd->sstl, stdin, "stdin (binary)");
        break;
      default: FATAL("Unreachable code"); break;
    }
    if(res != RES_OK) goto error;
    print_info(cmd);

  /* Load files */
  } else {
    unsigned i;
    FOR_EACH(i, 0, cmd->args.nmeshes) {
      switch(cmd->args.type) {
        case SSTL_ASCII:
          res = sstl_load_ascii(cmd->sstl, cmd->args.meshes[i]);
          break;
        case SSTL_BINARY:
          res = sstl_load_binary(cmd->sstl, cmd->args.meshes[i]);
          break;
        case SSTL_NONE__:
          res = sstl_load(cmd->sstl, cmd->args.meshes[i]);
          break;
        default: FATAL("Unreachable code"); break;
      }
      if(res != RES_OK) goto error;
      print_info(cmd);
    }
  }

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
  if(args.quit) goto exit;

  if((res = cmd_init(&cmd, &args)) != RES_OK) goto error;
  if((res = cmd_run(&cmd)) != RES_OK) goto error;

exit:
  cmd_release(&cmd);
  CHK(mem_allocated_size() == 0);
  return err;
error:
  err = 1;
  goto exit;
}
