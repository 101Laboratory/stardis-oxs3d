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
#include <rsys/str.h>

#include <errno.h>
#include <unistd.h> /* getopt */

enum query_flag {
  QUERY_DCELL = BIT(0),
  QUERY_DNODE = BIT(1),
  QUERY_NCELLS = BIT(2),
  QUERY_NNODES = BIT(3),
  QUERY_PAGESIZE = BIT(4),
  QUERY_ALL = ~0
};

/* Input arguments */
struct args {
  const char* mesh;
  int query_mask; /* Cmbination of query flag */
};
static const struct args ARGS_DEFAULT = {0};

/* Command data */
struct cmd {
  struct args args;
  struct smsh* mesh; /* Tetrahedral mesh */
  uint64_t pagesize;
};
static const struct cmd CMD_NULL = {0};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE void
usage(FILE* stream)
{
  fprintf(stream, "usage: smsh-desc [-cdnpt] mesh\n");
}

static res_T
args_init(struct args* args, const int argc, char** argv)
{
  int opt = 0;
  res_T res = RES_OK;

  *args = ARGS_DEFAULT;

  while((opt = getopt(argc, argv, "cdnpqt")) != -1) {
    switch(opt) {
      case 'c': args->query_mask |= QUERY_NCELLS; break;
      case 'd': args->query_mask |= QUERY_DNODE; break;
      case 'n': args->query_mask |= QUERY_NNODES; break;
      case 'p': args->query_mask |= QUERY_PAGESIZE; break;
      case 't': args->query_mask |= QUERY_DCELL; break;
      default: res = RES_BAD_ARG;
    }
    if(res != RES_OK) goto error;
  }

  if(optind < argc) args->mesh = argv[optind];

  /* By default, query all */
  if(!args->query_mask) args->query_mask = QUERY_ALL;

  if(!args->mesh) {
    fprintf(stderr, "mesh is missing\n");
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  usage(stderr);
  goto exit;
}

static res_T
setup_mesh(struct cmd* cmd, const struct args* args)
{
  struct smsh_create_args create_args = SMSH_CREATE_ARGS_DEFAULT;
  struct smsh_load_args load_args = SMSH_LOAD_ARGS_NULL;
  FILE* fp = NULL;
  res_T res = RES_OK;
  ASSERT(cmd && args);

  if(!(fp = fopen(args->mesh, "r"))) {
    fprintf(stderr, "%s: %s\n", args->mesh, strerror(errno));
    res = RES_IO_ERR;
    goto error;
  }
  if(fread(&cmd->pagesize, sizeof(uint64_t), 1, fp) != 1) {
    fprintf(stderr, "%s: %s\n", args->mesh, strerror(errno));
    res = RES_IO_ERR;
    goto error;
  }

  create_args.verbose = 1;
  if((res = smsh_create(&create_args, &cmd->mesh)) != RES_OK) goto error;

  load_args.path = args->mesh;
  if((res = smsh_load(cmd->mesh, &load_args)) != RES_OK) goto error;

exit:
  if(fp) CHK(fclose(fp) == 0);
  return res;
error:
  if(cmd->mesh) { SMSH(ref_put(cmd->mesh)); cmd->mesh = NULL; }
  goto exit;
}

static void
cmd_release(struct cmd* cmd)
{
  ASSERT(cmd);
  if(cmd->mesh) SMSH(ref_put(cmd->mesh));
}

static res_T
cmd_init(struct cmd* cmd, const struct args* args)
{
  res_T res = RES_OK;
  ASSERT(cmd && args && args->query_mask);

  if((res = setup_mesh(cmd, args)) != RES_OK) goto error;
  cmd->args = *args;

exit:
  return res;
error:
  cmd_release(cmd);
  goto exit;
}

static res_T
cmd_run(struct cmd* cmd)
{
  struct smsh_desc desc = SMSH_DESC_NULL;
  struct str str;
  res_T res = RES_OK;
  ASSERT(cmd);

  str_init(NULL, &str);
  SMSH(get_desc(cmd->mesh, &desc));

  #define APPEND(Fmt, Val) { \
    if((res = str_append_printf(&str, Fmt, (Val))) != RES_OK) goto error; \
  } (void)0
  if(cmd->args.query_mask & QUERY_PAGESIZE) APPEND("%zu ", cmd->pagesize);
  if(cmd->args.query_mask & QUERY_NNODES) APPEND("%zu ", desc.nnodes);
  if(cmd->args.query_mask & QUERY_NCELLS) APPEND("%zu ", desc.ncells);
  if(cmd->args.query_mask & QUERY_DNODE) APPEND("%u ", desc.dnode);
  if(cmd->args.query_mask & QUERY_DCELL) APPEND("%u ", desc.dcell);
  APPEND("%s", cmd->args.mesh);
  #undef APPEND

  printf("%s\n", str_cget(&str));

exit:
  str_release(&str);
  return res;
error:
  fprintf(stderr, "error: %s\n", res_to_cstr(res));
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
  cmd_release(&cmd);
  CHK(mem_allocated_size() == 0);
  return err;
error:
  err = 1;
  goto exit;
}
