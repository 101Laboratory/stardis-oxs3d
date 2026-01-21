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

#define _POSIX_C_SOURCE 200809L /* getopt support */

#include "s4vs_args.h"

#include <rsys/cstr.h>
#include <unistd.h> /* getopt */

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
usage(FILE* stream, char* name)
{
  ASSERT(stream && name);
  fprintf(stream,
    "usage: %s [-n realisations_count] [-s scattering_coef] [file]\n",
    name);
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
s4vs_args_init(struct s4vs_args* args, int argc, char** argv)
{
  int opt = 0;
  res_T res = RES_OK;
  ASSERT(args && argc && argv);

  *args = S4VS_ARGS_DEFAULT;

  while((opt = getopt(argc, argv, "n:s:")) != -1) {
    switch(opt) {
      case 'n':
        res = cstr_to_ulong(optarg, &args->nrealisations);
        if(res == RES_OK && args->nrealisations == 0) res = RES_BAD_ARG;
        break;
      case 's':
        res = cstr_to_double(optarg, &args->ks);
        if(res == RES_OK && args->ks < 0) res = RES_BAD_ARG;
        break;
      default: res = RES_BAD_ARG; break;
    }
    if(res != RES_OK) goto error;
  }
  if(optind < argc) args->filename = argv[optind];

exit:
  return res;
error:
  usage(stderr, argv[0]);
  goto exit;
}

void
s4vs_args_release(struct s4vs_args* args)
{
  /* Nothing to do */
  (void)args; /* Avoid "unused argument" warning */
}
