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

#include <rsys/cstr.h>
#include<rsys/mem_allocator.h>
#include <rsys/str.h>
#include <rsys/text_reader.h>

#include <errno.h>
#include <string.h>
#include <unistd.h> /* getopt */

#define MAX_DATA 8

/* Input arguments */
struct args {
  const char* output;
  const char* data;
  unsigned long nitems;
  const char* names[MAX_DATA];
  unsigned ndata;
};
static const struct args ARGS_DEFAULT = {0};

/* Command data */
struct cmd {
  const char* names[MAX_DATA];
  const char* data_name;
  FILE* output;
  FILE* data;
  size_t nitems;
};
static const struct cmd CMD_NULL = {0};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE void
usage(FILE* stream)
{
  fprintf(stream,
    "usage: vtk-data [-n name ...] [-o output] -c cell_count [data]\n");
}

static res_T
parse_name(struct args* args, const char* name)
{
  unsigned i;
  ASSERT(name);

  if(args->ndata >= MAX_DATA) {
    fprintf(stderr, "too many names\n");
    return RES_BAD_ARG;
  }

  /* Names cannot contain white spaces */
  if(strcspn(name, " \t") != strlen(name)) {
    fprintf(stderr, "name \"%s\" cannot contain white spaces \n", name);
    return RES_BAD_ARG;
  }

  /* Names must be unique */
  FOR_EACH(i, 0, args->ndata) {
    if(!strcmp(args->names[i], name)) {
      fprintf(stderr, "name \"%s\" already defined\n", name);
      return RES_BAD_ARG;
    }
  }

  args->names[args->ndata++] = name;
  return RES_OK;
}

static res_T
args_init(struct args* args, const int argc, char** argv)
{
  int opt = 0;
  res_T res = RES_OK;

  *args = ARGS_DEFAULT;

  while((opt = getopt(argc, argv, "o:c:n:")) != -1) {
    switch(opt) {
      case 'n': res = parse_name(args, optarg); break;
      case 'o': args->output = optarg; break;
      case 'c':
        res = cstr_to_ulong(optarg, &args->nitems);
        if(res == RES_OK && args->nitems == 0) res = RES_BAD_ARG;
        break;
      default: res = RES_BAD_ARG; goto error;
    }
    if(res != RES_OK) goto error;
  }

  if(optind < argc) args->data = argv[optind];

  if(!args->nitems) {
    fprintf(stderr, "missing item count -- option '-c'\n");
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  usage(stderr);
  goto exit;
}

static void
cmd_release(struct cmd* cmd)
{
  if(cmd->data && cmd->data != stdin) CHK(!fclose(cmd->data));
  if(cmd->output && cmd->output != stdout) CHK(!fclose(cmd->output));
}

static res_T
cmd_init(struct cmd* cmd, const struct args* args)
{
  res_T res = RES_OK;
  ASSERT(cmd && args);

  if(!(cmd->output = args->output ? fopen(args->output, "w") : stdout)) {
    perror(args->output);
    res = RES_IO_ERR;
    goto error;
  }

  if(!(cmd->data = args->data ? fopen(args->data, "r") : stdin)) {
    perror(args->data);
    res = RES_IO_ERR;
    goto error;
  }

  cmd->data_name = args->data ? args->data : "stdin";
  memcpy(cmd->names, args->names, sizeof(char*[MAX_DATA]));
  cmd->nitems = args->nitems;

exit:
  return res;
error:
  cmd_release(cmd);
  goto exit;
}

static res_T
write_data(struct cmd* cmd, const int idata, struct txtrdr* txtrdr)
{
  size_t i = 0;
  int res = RES_OK;

  /* Pre-conditions */
  ASSERT(cmd && idata < MAX_DATA && txtrdr);
  ASSERT(txtrdr_get_line(txtrdr));

  if(cmd->names[idata]) {
    res = fprintf(cmd->output, "SCALARS %s double 1\n", cmd->names[idata]);
  } else {
    /* Make default name unique */
    res = fprintf(cmd->output, "SCALARS data%d double 1\n", idata);
  }
  if(res < 0) {
    perror(NULL);
    res = RES_IO_ERR;
    goto error;
  }

  if(fprintf(cmd->output, "LOOKUP_TABLE default\n") < 0) {
    perror(NULL);
    res = RES_IO_ERR;
    goto error;
  }

  FOR_EACH(i, 0, cmd->nitems) {
    const char* line = NULL;
    double f = 0;

    if(!(line = txtrdr_get_line(txtrdr))) {
      fprintf(stderr, "missing data\n");
      res = RES_BAD_ARG;
      goto error;
    }

    if((sscanf(line, "%lf\n", &f)) != 1) {
      fprintf(stderr, "%s:%lu: unable to read double\n",
        txtrdr_get_name(txtrdr), txtrdr_get_line_num(txtrdr));
      res = RES_IO_ERR;
      goto error;
    }

    if(fprintf(cmd->output, "%lf\n", f) < 0) {
      perror(NULL);
      res = RES_IO_ERR;
      goto error;
    }

    if((res = txtrdr_read_line(txtrdr)) != RES_OK) {
      fprintf(stderr, "%s\n", res_to_cstr(res));
      goto error;
    }
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
cmd_run(struct cmd* cmd)
{
  struct txtrdr* txtrdr = NULL;
  int i = 0;
  res_T res = RES_OK;
  ASSERT(cmd);

  res = txtrdr_stream(NULL, cmd->data, cmd->data_name, '#', &txtrdr);
  if(res != RES_OK) {
    fprintf(stderr, "%s\n", res_to_cstr(res));
    goto error;
  }

  if((res = txtrdr_read_line(txtrdr)) != RES_OK) {
    fprintf(stderr, "%s\n", res_to_cstr(res));
    goto error;
  }

  if(!txtrdr_get_line(txtrdr)) goto exit; /* No data  */

  if(fprintf(cmd->output, "CELL_DATA %zu\n", cmd->nitems) < 0) {
    fprintf(stderr, "%s\n", strerror(errno));
    res = RES_IO_ERR;
    goto error;
  }

  FOR_EACH(i, 0, MAX_DATA) {
    if((res = write_data(cmd, i, txtrdr)) != RES_OK) goto error;
    if(!txtrdr_get_line(txtrdr)) break; /* No more data */
  }

exit:
  if(txtrdr) txtrdr_ref_put(txtrdr);
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Main function
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
