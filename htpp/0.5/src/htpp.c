/* Copyright (C) 2018-2020, 2023 |Méso|Star> (contact@meso-star.com)
 * Copyright (C) 2018, 2019 Centre National de la Recherche Scientifique
 * Copyright (C) 2018, 2019 Université Paul Sabatier
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

#define _POSIX_C_SOURCE 200112L /* getopt/close support */

#include "htpp_version.h"

#include <rsys/cstr.h>
#include <rsys/double33.h>
#include <rsys/mem_allocator.h>
#include <rsys/rsys.h>
#include <rsys/text_reader.h>

#include <star/scmap.h>

#include <errno.h>
#include <fcntl.h> /* open */
#include <omp.h>
#include <string.h>
#include <sys/stat.h> /* S_IRUSR & S_IWUSR */
//#include <unistd.h> /* getopt & close functions */
#include <getopt.h>

#ifdef _WIN32
#define strtok_r strtok_s
#endif

enum pixcpnt {
  PIXCPNT_X,
  PIXCPNT_R = PIXCPNT_X,
  PIXCPNT_X_STDERR,
  PIXCPNT_Y,
  PIXCPNT_G = PIXCPNT_Y,
  PIXCPNT_Y_STDERR,
  PIXCPNT_Z,
  PIXCPNT_B = PIXCPNT_Z,
  PIXCPNT_Z_STDERR,
  PIXCPNT_TIME,
  PIXCPNT_TIME_STDERR,
  PIXCPNTS_COUNT__
};

enum pp_type {
  PP_IMAGE,
  PP_MAP
};

struct args {
  const char* input;
  const char* output;

  enum pp_type pp_type;

  struct image_opt {
    double exposure; /* In [0, inf) */
    double white; /* In ]0, inf) */
  } image;

  struct map_opt {
    const struct scmap_palette* palette;
    unsigned pixcpnt; /* In [0, PIXCPNTS_COUNT__[ */
    double range[2];
    int gnuplot;
  } map;

  int verbose;
  int force_overwrite;
  int nthreads;
  int quit;
};
#define ARGS_DEFAULT__ {                                                       \
  NULL,                     /* Input */                                        \
  NULL,                     /* Output */                                       \
  PP_IMAGE,                 /* Post process type */                            \
  {                                                                            \
    1.0,                    /* Image exposure */                               \
    -1.0,                   /* Image white scale */                            \
  }, {                                                                         \
    &scmap_palette_inferno, /* Map palette */                                  \
    0,                      /* Map channel */                                  \
    {DBL_MAX,-DBL_MAX},     /* Range */                                        \
    0                       /* Gnuplot */                                      \
  },                                                                           \
  0,                        /* Verbosity level */                              \
  0,                        /* Force overwrite? */                             \
  INT_MAX,                  /* #threads */                                     \
  0                         /* Quit? */                                        \
}

struct img {
  char* pixels; /* row majored pixels */
  size_t width;
  size_t height;
  size_t pitch; /* #bytes of a row */

  /* Ranges of the loaded value */
  double ranges[PIXCPNTS_COUNT__][2];
};
#define IMG_NULL__ { NULL, 0, 0, 0, {{ 0, 0}} }
static const struct img IMG_NULL = IMG_NULL__;

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
usage(void)
{
  printf(
"usage: htpp [-fhVv] [-i image_option[:image_option ...]]\n"
"            [-m map_option[:map_option ...]] [-o output]\n"
"            [-t threads_count] [input]\n");
}

static res_T
parse_multiple_options
  (struct args* args,
   const char* str,
   res_T (*parse_option)(struct args* args, const char* str))
{
  char buf[512];
  char* tk;
  char* ctx;
  res_T res = RES_OK;
  ASSERT(args && str);

  if(strlen(str) >= sizeof(buf) - 1/*NULL char*/) {
    fprintf(stderr, "Could not duplicate the option string `%s'.\n", str);
    res = RES_MEM_ERR;
    goto error;
  }
  strncpy(buf, str, sizeof(buf));

  tk = strtok_r(buf, ":", &ctx);
  do {
    res = parse_option(args, tk);
    if(res != RES_OK) goto error;
    tk = strtok_r(NULL, ":", &ctx);
  } while(tk);

exit:
  return res;
error:
  goto exit;
}

static res_T
parse_img_option(struct args* args, const char* str)
{
  char buf[128];
  char* key;
  char* val;
  char* tk_ctx;
  const struct args args_default = ARGS_DEFAULT__;
  res_T res = RES_OK;

  if(strlen(str) >= sizeof(buf) - 1/*NULL char*/) {
    fprintf(stderr,
      "Could not duplicate the image options string `%s'.\n", str);
    res = RES_MEM_ERR;
    goto error;
  }
  strncpy(buf, str, sizeof(buf));

  key = strtok_r(buf, "=", &tk_ctx);
  val = strtok_r(NULL, "", &tk_ctx);

  if(!strcmp(key, "default")) {
    if(val) {
      fprintf(stderr, "Unexpected value to the image option `%s'.\n", key);
      res = RES_BAD_ARG;
      goto error;
    }
    args->image = args_default.image;

  } else {

    if(!val) {
      fprintf(stderr, "Missing value to the image option `%s'.\n", key);
      res = RES_BAD_ARG;
      goto error;
    }

    if(!strcmp(key, "exposure")) {
      res = cstr_to_double(val, &args->image.exposure);
      if(res != RES_OK) goto error;
      if(args->image.exposure < 0) {
        fprintf(stderr, "Invalid image exposure %g.\n", args->image.exposure);
        res = RES_BAD_ARG;
        goto error;
      }
    } else if(!strcmp(key, "white")) {
      res = cstr_to_double(val, &args->image.white);
      if(res != RES_OK) goto error;
      if(args->image.exposure < 0) {
        fprintf(stderr, "Invalid image white scale %g.\n", args->image.white);
        res = RES_BAD_ARG;
        goto error;
      }
    } else {
      fprintf(stderr, "Invalid image option `%s'.\n", key);
      res = RES_BAD_ARG;
      goto error;
    }
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
parse_map_option(struct args* args, const char* str)
{
  char buf[128];
  char* key;
  char* val;
  char* tk_ctx;
  const struct args args_default = ARGS_DEFAULT__;
  res_T res = RES_OK;

  if(strlen(str) >= sizeof(buf) - 1/*NULL char*/) {
    fprintf(stderr,
      "Could not duplicate the map options string `%s'.\n", str);
    res = RES_MEM_ERR;
    goto error;
  }
  strncpy(buf, str, sizeof(buf));

  key = strtok_r(buf, "=", &tk_ctx);
  val = strtok_r(NULL, "", &tk_ctx);

  if(!strcmp(key, "default")) {
    if(val) {
      fprintf(stderr, "Unexpected value to the map option `%s'.\n", key);
      res = RES_BAD_ARG;
      goto error;
    }
    args->map = args_default.map;
  } else if(!strcmp(key, "gnuplot")) {
    args->map.gnuplot = 1;
  } else {
    if(!val) {
      fprintf(stderr, "Missing value to the map option `%s'.\n", key);
      res = RES_BAD_ARG;
      goto error;
    }

    if(!strcmp(key, "pixcpnt")) {
      res = cstr_to_uint(val, &args->map.pixcpnt);
      if(res != RES_OK) goto error;
      if(args->map.pixcpnt >= PIXCPNTS_COUNT__) {
        fprintf(stderr, "Invalid pixel component `%u'.\n", args->map.pixcpnt);
        res = RES_BAD_ARG;
        goto error;
      }
    } else if(!strcmp(key, "palette")) {
      args->map.palette = scmap_get_builtin_palette(val);
      if(!args->map.palette) {
        fprintf(stderr, "Invalid palette `%s'.\n", val);
        res = RES_BAD_ARG;
        goto error;
      }
    } else if(!strcmp(key, "range")) {
      size_t len;
      res = cstr_to_list_double(val, ',', args->map.range, &len, 2);
      if(res != RES_OK) goto error;
    } else {
      fprintf(stderr, "Invalid map option `%s'.\n", key);
      res = RES_BAD_ARG;
      goto error;
    }
  }

exit:
  return res;
error:
  goto exit;
}

static void
args_release(struct args* args)
{
  const struct args args_default = ARGS_DEFAULT__;
  ASSERT(args);
  *args = args_default;
}

static res_T
args_init(struct args* args, const int argc, char** argv)
{
  int opt;
  res_T res = RES_OK;
  ASSERT(args && argc && argv);

  /* Begin the optstring by ':' to make silent getopt */
  while((opt = getopt(argc, argv, "fhi:m:o:t:vV")) != -1) {
    switch(opt) {
      case 'f': args->force_overwrite = 1; break;
      case 'h':
        usage();
        args_release(args);
        args->quit = 1;
        goto exit;
      case 'i':
        args->pp_type = PP_IMAGE;
        res = parse_multiple_options(args, optarg, parse_img_option);
        break;
      case 'm':
        args->pp_type = PP_MAP;
        res = parse_multiple_options(args, optarg, parse_map_option);
        break;
      case 'o': args->output = optarg; break;
      case 't':
        res = cstr_to_int(optarg, &args->nthreads);
        if(res == RES_OK && args->nthreads <= 0) res = RES_BAD_ARG;
        break;
      case 'v': args->verbose = 1; break;
      case 'V':
        printf("High-Tune: Post-Process %d.%d.%d\n",
          HTPP_VERSION_MAJOR,
          HTPP_VERSION_MINOR,
          HTPP_VERSION_PATCH);
        args->quit = 1;
        goto exit;
      default: res = RES_BAD_ARG; break;
    }
    if(res != RES_OK) {
      if(optarg) {
        fprintf(stderr, "%s: invalid option argument '%s' -- '%c'\n",
          argv[0], optarg, opt);
      }
      goto error;
    }
  }

  if(optind < argc) {
    args->input = argv[optind];
  }

  args->nthreads = MMIN(omp_get_num_procs(), args->nthreads);

exit:
  optind = 1;
  return res;
error:
  usage();
  args_release(args);
  goto exit;
}

static int
cmp_dbl(const void* a, const void* b)
{
  const double* dbl0 = a;
  const double* dbl1 = b;
  if(*dbl0 < *dbl1) return-1;
  if(*dbl0 > *dbl1) return 1;
  return 0;
}

static res_T
open_output_stream(const char* path, const int force_overwrite, FILE** stream)
{
  int fd = -1;
  FILE* fp = NULL;
  res_T res = RES_OK;
  ASSERT(path && stream);

  if(force_overwrite) {
    fp = fopen(path, "w");
    if(!fp) {
      fprintf(stderr, "Could not open the output file '%s' -- %s.\n",
        path, strerror(errno));
      goto error;
    }
  } else {
#ifdef _WIN32
      fd = open(path, O_CREAT | O_WRONLY | O_EXCL | O_TRUNC);
#else
    fd = open(path, O_CREAT|O_WRONLY|O_EXCL|O_TRUNC, S_IRUSR|S_IWUSR);
#endif
    if(fd >= 0) {
      fp = fdopen(fd, "w");
      if(fp == NULL) {
        fprintf(stderr, "Could not open the output file '%s' -- %s.\n",
          path, strerror(errno));
        goto error;
      }
    } else if(errno == EEXIST) {
      fprintf(stderr,
        "The output file '%s' already exists. Use -f to overwrite it.\n", path);
      goto error;
    } else {
      fprintf(stderr,
        "Unexpected error while opening the output file `'%s' -- %s.\n",
         path, strerror(errno));
      goto error;
    }
  }

exit:
  *stream = fp;
  return res;
error:
  res = RES_IO_ERR;
  if(fp) {
    CHK(fclose(fp) == 0);
    fp = NULL;
  } else if(fd >= 0) {
    CHK(close(fd) == 0);
  }
  goto exit;
}

static res_T
open_input_stream(const char* path, FILE** stream)
{
  FILE* fp = NULL;
  res_T res = RES_OK;
  ASSERT(path && stream);

  fp = fopen(path, "r");
  if(!fp) {
    fprintf(stderr, "Could not open the input file '%s' -- %s.\n",
      path, strerror(errno));
    res = RES_IO_ERR;
    goto error;
  }

exit:
  *stream = fp;
  return res;
error:
  if(fp) fclose(fp);
  goto exit;
}

/* http://filmicworlds.com/blog/filmic-tonemapping-operators/ */
static double*
filmic_tone_mapping
  (double pixel[PIXCPNTS_COUNT__],
   const double exposure,
   const double Ymax)
{
  const double A = 0.15;
  const double B = 0.50;
  const double C = 0.10;
  const double D = 0.20;
  const double E = 0.02;
  const double F = 0.30;
  const double W = Ymax;
  #define TONE_MAP(X) ((((X)*(A*(X)+C*B)+D*E)/((X)*(A*(X)+B)+D*F))-E/F)
  const double white_scale = TONE_MAP(W*exposure);
  ASSERT(pixel);

  pixel[PIXCPNT_X] = TONE_MAP(pixel[PIXCPNT_X]*exposure) / white_scale;
  pixel[PIXCPNT_Y] = TONE_MAP(pixel[PIXCPNT_Y]*exposure) / white_scale;
  pixel[PIXCPNT_Z] = TONE_MAP(pixel[PIXCPNT_Z]*exposure) / white_scale;
  #undef TONE_MAP
  return pixel;
}

static double*
XYZ_to_sRGB(double pixel[PIXCPNTS_COUNT__])
{
  #define D65_x 0.31271
  #define D65_y 0.32902
  const double D65[3] = {D65_x / D65_y, 1.0, (1 - D65_x - D65_y) / D65_y};
  const double mat[9] = { /* XYZ to sRGB matrix */
     3.2404542, -0.9692660,  0.0556434,
    -1.5371385,  1.8760108, -0.2040259,
    -0.4985314,  0.0415560,  1.0572252
  };
  double XYZ[3], sRGB[3], XYZ_D65[3];
  ASSERT(pixel);

  XYZ[0] = pixel[PIXCPNT_X];
  XYZ[1] = pixel[PIXCPNT_Y];
  XYZ[2] = pixel[PIXCPNT_Z];

  d3_mul(XYZ_D65, XYZ, D65);
  d33_muld3(sRGB, mat, XYZ_D65);
  sRGB[0] = MMAX(sRGB[0], 0);
  sRGB[1] = MMAX(sRGB[1], 0);
  sRGB[2] = MMAX(sRGB[2], 0);

  pixel[PIXCPNT_R] = sRGB[0];
  pixel[PIXCPNT_G] = sRGB[1];
  pixel[PIXCPNT_B] = sRGB[2];
  return pixel;
}

static double
sRGB_gamma_correct(double value)
{
  if(value <= 0.0031308) {
    return value * 12.92;
  } else {
    return 1.055 * pow(value, 1.0/2.4) - 0.055;
  }
}

static void
img_release(struct img* img)
{
  ASSERT(img);
  if(img->pixels) mem_rm(img->pixels);
}

static res_T
img_load
  (struct img* img,
   FILE* stream,
   const char* stream_name)
{
  struct txtrdr* txtrdr = NULL;
  size_t icpnt;
  size_t x, y;
  unsigned resolution[2];
  res_T res = RES_OK;
  ASSERT(img && stream);

  *img = IMG_NULL;

  res = txtrdr_stream(NULL, stream, stream_name, '#', &txtrdr);
  if(res != RES_OK) {
    fprintf(stderr, "%s: could not setup the text reader -- %s.\n",
      txtrdr_get_name(txtrdr), res_to_cstr(res));
    goto error;
  }

  res = txtrdr_read_line(txtrdr);
  if(res != RES_OK) {
    fprintf(stderr, "%s: could not read the image resolution -- %s.\n",
      txtrdr_get_name(txtrdr), res_to_cstr(res));
    goto error;
  }

  if(!txtrdr_get_line(txtrdr)) {
    fprintf(stderr, "%s: missing image definition.\n", txtrdr_get_name(txtrdr));
    res = RES_BAD_ARG;
    goto error;
  }

  res = cstr_to_list_uint(txtrdr_get_cline(txtrdr), ' ', resolution, NULL, 2);
  if(res != RES_OK) {
    fprintf(stderr, "%s: invalid image resolution `%s' -- %s\n",
      txtrdr_get_name(txtrdr), txtrdr_get_cline(txtrdr), res_to_cstr(res)) ;
    goto error;
  }

  img->width = (size_t)resolution[0];
  img->height = (size_t)resolution[1];
  img->pitch = ALIGN_SIZE(sizeof(double[PIXCPNTS_COUNT__])*img->width, 16u);
  img->pixels = mem_calloc(img->height, img->pitch);
  if(!img->pixels) {
    fprintf(stderr, "Could not allocate the image pixels.\n");
    res = RES_MEM_ERR;
    goto error;
  }

  /* Reset the range of each pixel components */
  FOR_EACH(icpnt, 0, PIXCPNTS_COUNT__) {
    img->ranges[icpnt][0] = DBL_MAX;
    img->ranges[icpnt][1] = -DBL_MAX;
  }

  /* Read pixel components */
  FOR_EACH(y, 0, img->height) {
    double* row = (double*)(img->pixels + y*img->pitch);
    FOR_EACH(x, 0, img->width) {
      double* pixel = row + x*PIXCPNTS_COUNT__;
      size_t ncpnts;

      res = txtrdr_read_line(txtrdr);
      if(res != RES_OK) {
        fprintf(stderr,
          "%s: could not read the components of the pixel (%lu, %lu) -- %s.\n",
          txtrdr_get_name(txtrdr), (unsigned long)x, (unsigned long)y,
          res_to_cstr(res));
        goto error;
      }

      res = cstr_to_list_double(txtrdr_get_cline(txtrdr), ' ', pixel, &ncpnts, 8);
      if(res != RES_OK || ncpnts < 6) {
        fprintf(stderr, "%s: invalid components for the (%lu, %lu) pixel.\n",
          txtrdr_get_name(txtrdr), (unsigned long)x, (unsigned long)y);
        res = RES_BAD_ARG;
        goto error;
      }

      FOR_EACH(icpnt, 0, ncpnts) {
        img->ranges[icpnt][0] = MMIN(img->ranges[icpnt][0], pixel[icpnt]);
        img->ranges[icpnt][1] = MMAX(img->ranges[icpnt][1], pixel[icpnt]);
      }
    }
  }
exit:
  if(txtrdr) txtrdr_ref_put(txtrdr);
  return res;
error:
  img_release(img);
  goto exit;
}

static res_T
img_write_ppm(const struct img* img, FILE* stream, const char* stream_name)
{
  size_t x, y;
  int i;
  res_T res = RES_OK;
  ASSERT(img && stream && stream_name);

  /* Write LDR image */
  i = fprintf(stream, "P3 %lu %lu\n255\n",
    (unsigned long)img->width, (unsigned long)img->height);
  if(i < 0) {
    fprintf(stderr, "%s: could not write the PPM header.\n", stream_name);
    res = RES_IO_ERR;
    goto error;
  }
  FOR_EACH(y, 0, img->height) {
    const double* row = (double*)(img->pixels + y*img->pitch);
    FOR_EACH(x, 0, img->width) {
      const double* pixel = row + x*PIXCPNTS_COUNT__;
      i = fprintf(stream, "%i %i %i\n",
        (uint8_t)(CLAMP(pixel[PIXCPNT_X], 0.0, 1.0) * 255.0 + 0.5/*Round*/),
        (uint8_t)(CLAMP(pixel[PIXCPNT_Y], 0.0, 1.0) * 255.0 + 0.5/*Round*/),
        (uint8_t)(CLAMP(pixel[PIXCPNT_Z], 0.0, 1.0) * 255.0 + 0.5/*Round*/));
      if(i < 0) {
        fprintf(stderr, "%s: could not write the (%lu, %lu) pixel.\n",
          stream_name, (unsigned long)x, (unsigned long)y);
        res = RES_IO_ERR;
        goto error;
      }
    }
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
img_write_gnuplot
  (const struct img* img,
   const struct args* args,
   FILE* stream,
   const char* stream_name)
{
  double cbox_width = 0.8;
  double cbox_height = 0.08;
  double cbox_tmargin = 0.02;
  size_t icol;
  size_t x, y;
  res_T res = RES_OK;
  ASSERT(img && args && stream && stream_name);

  #define CHKWR(FPrintf) {                                                     \
    const int i = FPrintf;                                                     \
    if(i < 0) {                                                                \
      fprintf(stderr, "%s: could not write the gnuplot map.\n", stream_name);  \
      res = RES_IO_ERR;                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0
  CHKWR(fprintf(stream, "unset xtics\n"));
  CHKWR(fprintf(stream, "unset ytics\n"));
  CHKWR(fprintf(stream, "unset key\n"));
  CHKWR(fprintf(stream, "unset colorbox\n"));
  CHKWR(fprintf(stream, "unset origin\n"));
  CHKWR(fprintf(stream, "unset border\n"));
  CHKWR(fprintf(stream, "unset title\n"));
  CHKWR(fprintf(stream, "unset margin\n"));
  CHKWR(fprintf(stream, "set margins 0,0,0,0\n"));
  CHKWR(fprintf(stream, "set xrange[0:%lu]\n", (unsigned long)img->width-1));
  CHKWR(fprintf(stream, "set yrange[%lu:0]\n", (unsigned long)img->height-1));
  if(args->map.range[0] < args->map.range[1]) {
    CHKWR(fprintf(stream, "set cbrange[%g:%g]\n",
      args->map.range[0], args->map.range[1]));
  }
  CHKWR(fprintf(stream, "set terminal png size %lu,%lu*(1+%g+%g)\n",
    (long unsigned)img->width, (unsigned long)img->height,
    cbox_height, cbox_tmargin));
  CHKWR(fprintf(stream, "set origin 0, %g\n", (cbox_height+cbox_tmargin)*0.5));
  CHKWR(fprintf(stream, "set size ratio %g\n",
    (double)img->height/(double)img->width));
  CHKWR(fprintf(stream, "set colorbox horiz user origin %g,%g size %g,%g\n",
    (1.0-cbox_width)*0.5, cbox_height*0.5, cbox_width, cbox_height*0.5));

  CHKWR(fprintf(stream, "set palette defined (\\\n"));
  FOR_EACH(icol, 0, args->map.palette->ncolors) {
    double col[3];
    args->map.palette->get_color(icol, col, args->map.palette->context);
    CHKWR(fprintf(stream, "  %lu %g %g %g",
      (unsigned long)icol, col[0], col[1], col[2]));
    if(icol < args->map.palette->ncolors-1) {
      CHKWR(fprintf(stream, ",\\\n"));
    } else {
      CHKWR(fprintf(stream, ")\n"));
    }
  }

  CHKWR(fprintf(stream, "$map2 << EOD\n"));
  FOR_EACH(y, 0, img->height) {
    FOR_EACH(x, 0, img->width) {
      double* row = (double*)(img->pixels + img->pitch*y);
      double* pixel = row + x*PIXCPNTS_COUNT__;
      CHKWR(fprintf(stream, "%lu %lu %g\n",
        (unsigned long)y, (unsigned long)x, pixel[args->map.pixcpnt]));
    }
    if(y != img->height-1) {
      CHKWR(fprintf(stream, "\n"));
    }
  }
  CHKWR(fprintf(stream, "EOD\n"));
  CHKWR(fprintf(stream, "plot '$map2' using 2:1:3 with image pixels\n"));
  #undef CHKWR

exit:
  return res;
error:
  goto exit;
}

static double
compute_XYZ_normalization_factor(const struct img* img)
{
  double* Y = NULL;
  double Ymax;
  size_t i, x, y;
  ASSERT(img);

  CHK((Y = mem_alloc(sizeof(*Y)*img->width*img->height))!= NULL);

  /* Copy the pixel luminance in the Y array */
  i = 0;
  FOR_EACH(y, 0, img->height) {
    const double* row = (double*)(img->pixels + y*img->pitch);
    FOR_EACH(x, 0, img->width) {
      const double* pixel = row + x*PIXCPNTS_COUNT__;
      Y[i] = pixel[PIXCPNT_Y];
      i++;
    }
  }

  /* Sort the Pixel luminance in ascending order */
  qsort(Y, img->width*img->height, sizeof(*Y), cmp_dbl);

  /* Arbitrarly use the luminance at 99.5% of the overal pixel at the maxium
   * luminance of the image */
  i = (size_t)((double)(img->width*img->height)*0.995);
  Ymax = Y[i];

  mem_rm(Y);
  return Ymax;
}

static uint8_t
rgb_to_c256(const uint8_t rgb[3])
{
  uint8_t c256 = 0;
  ASSERT(rgb);

  if(rgb[0] == rgb[1] && rgb[1] == rgb[2]) {
    int c = rgb[0];
    if(c < 4) {
      c256 = 16; /* Grey 0 */
    } else if(c >= 243) {
      c256 = 231; /* Grey 100 */
    } else {
      c256 = (uint8_t)(232 + (c-8+5) / 10);
    }
  } else {
    int r, g, b;
    r = rgb[0] < 48 ? 0 : (rgb[0] < 95 ? 1 : 1 + (rgb[0] - 95 + 20) / 40);
    g = rgb[1] < 48 ? 0 : (rgb[1] < 95 ? 1 : 1 + (rgb[1] - 95 + 20) / 40);
    b = rgb[2] < 48 ? 0 : (rgb[2] < 95 ? 1 : 1 + (rgb[2] - 95 + 20) / 40);
    c256 = (uint8_t)(36*r + 6*g + b + 16);
  }
  return c256;
}

static void
print_color_map(const struct scmap* scmap, const double range[2])
{
  const double ransz = range[1] - range[0];
  const int map_length = 65;
  const int map_quarter = map_length / 4;
  const int label_length = map_length / 4;
  int i;
  ASSERT(range && range[0] <= range[1]);

  FOR_EACH(i, 0, map_length) {
    const double u = (double)i / (double)(map_length-1);
    double color[3] = {0,0,0};
    uint8_t rgb[3];
    uint8_t c256;
    SCMAP(fetch_color(scmap, u, SCMAP_FILTER_LINEAR, color));

    rgb[0] = (uint8_t)(CLAMP(color[0], 0, 1) * 255. + 0.5/*round*/);
    rgb[1] = (uint8_t)(CLAMP(color[1], 0, 1) * 255. + 0.5/*round*/);
    rgb[2] = (uint8_t)(CLAMP(color[2], 0, 1) * 255. + 0.5/*round*/);
    c256 = rgb_to_c256(rgb);
    if(i == 0 * map_quarter
    || i == 1 * map_quarter
    || i == 2 * map_quarter
    || i == 3 * map_quarter
    || i == 4 * map_quarter) {
      fprintf(stderr, "\x1b[0m|");
    } else {
      fprintf(stderr, "\x1b[48;5;%dm ", c256);
    }
  }
  fprintf(stderr, "\n");
  fprintf(stderr, "%-*.5g", label_length, range[0]);
  fprintf(stderr, "%-*.5g", label_length, 0.25 * ransz + range[0]);
  fprintf(stderr, "%-*.5g", label_length, 0.50 * ransz + range[0]);
  fprintf(stderr, "%-*.5g", label_length, 0.75 * ransz + range[0]);
  fprintf(stderr, "%-*.5g", label_length, range[1]);
  fprintf(stderr, "\n");
}

static res_T
pp_map(struct img* img, const struct args* args)
{
  struct scmap* scmap = NULL;
  int64_t i;
  double range[2];
  double ransz;
  res_T res = RES_OK;
  ASSERT(img && args && args->pp_type == PP_MAP);

  res = scmap_create(NULL, NULL, 1, args->map.palette, &scmap);
  if(res != RES_OK) {
    fprintf(stderr, "Could not create the color map -- %s.\n",
      res_to_cstr(res));
    goto error;
  }

  if(args->map.range[0] < args->map.range[1]) {
    /* The range is fixed */
    range[0] = args->map.range[0];
    range[1] = args->map.range[1];
  } else {
    /* The range is defined from the loaded data  */
    range[0] = img->ranges[args->map.pixcpnt][0];
    range[1] = img->ranges[args->map.pixcpnt][1];
  }
  ransz = range[1] - range[0];

  omp_set_num_threads(args->nthreads);
  #pragma omp parallel for
  for(i=0; i < (int64_t)(img->width*img->height); ++i) {
    const size_t y = (size_t)i / img->width;
    const size_t x = (size_t)i % img->width;
    double* row = (double*)(img->pixels + img->pitch*y);
    double* pixel = row + x*PIXCPNTS_COUNT__;
    if(ransz == 0) {
      pixel[PIXCPNT_R] = 0;
      pixel[PIXCPNT_G] = 0;
      pixel[PIXCPNT_B] = 0;
    } else {
      const double val = CLAMP((pixel[args->map.pixcpnt] - range[0])/ransz, 0, 1);
      double color[3] = {0,0,0};

      SCMAP(fetch_color(scmap, val, SCMAP_FILTER_LINEAR, color));
      pixel[PIXCPNT_R] = color[0];
      pixel[PIXCPNT_G] = color[1];
      pixel[PIXCPNT_B] = color[2];
    }
  }

  if(args->verbose) {
    print_color_map(scmap, range);
  }

exit:
  if(scmap) SCMAP(ref_put(scmap));
  return res;
error:
  goto exit;
}

static res_T
pp_image(struct img* img, const struct args* args)
{
  int64_t i;
  double Ymax;
  res_T res = RES_OK;
  ASSERT(img && args && args->pp_type == PP_IMAGE);

  if(args->image.white> 0) {
    Ymax = args->image.white;
  } else {
    Ymax = compute_XYZ_normalization_factor(img);
    if(args->verbose) {
      fprintf(stderr, "White scale = %g\n", Ymax);
    }
  }

  omp_set_num_threads(args->nthreads);
  #pragma omp parallel for
  for(i=0; i < (int64_t)(img->width*img->height); ++i) {
    const size_t y = (size_t)i / img->width;
    const size_t x = (size_t)i % img->width;
    double* row = (double*)(img->pixels + img->pitch*y);
    double* pixel = row + x*PIXCPNTS_COUNT__;

    filmic_tone_mapping(pixel, args->image.exposure, Ymax);
    XYZ_to_sRGB(pixel);
    pixel[PIXCPNT_R] = sRGB_gamma_correct(pixel[PIXCPNT_R]);
    pixel[PIXCPNT_G] = sRGB_gamma_correct(pixel[PIXCPNT_G]);
    pixel[PIXCPNT_B] = sRGB_gamma_correct(pixel[PIXCPNT_B]);
  }

  return res;
}

/*******************************************************************************
 * Program
 ******************************************************************************/
int
main(int argc, char** argv)
{
  FILE* stream_out = stdout;
  FILE* stream_in = stdin;
  const char* stream_out_name = "stdout";
  const char* stream_in_name = "stdin";
  struct img img = IMG_NULL;
  struct args args = ARGS_DEFAULT__;
  int img_is_loaded = 0;
  int err = 0;
  res_T res = RES_OK;

  res = args_init(&args, argc, argv);
  if(res != RES_OK) goto error;
  if(args.quit) goto exit;

  if(args.output) {
    res = open_output_stream(args.output, args.force_overwrite, &stream_out);
    if(res != RES_OK) goto error;
    stream_out_name = args.output;
  }
  if(args.input) {
    res = open_input_stream(args.input, &stream_in);
    if(res != RES_OK) goto error;
    stream_in_name = args.input;
  } else if(args.verbose) {
    fprintf(stderr, "Read image from standard input.\n");
  }

  res = img_load(&img, stream_in, stream_in_name);
  if(res != RES_OK) goto error;
  img_is_loaded = 1;

  switch(args.pp_type) {
    case PP_IMAGE:
      res = pp_image(&img, &args);
      if(res != RES_OK) goto error;
      res = img_write_ppm(&img, stream_out, stream_out_name);
      if(res != RES_OK) goto error;
      break;
    case PP_MAP:
      if(args.map.gnuplot) {
        img_write_gnuplot(&img, &args, stream_out, stream_out_name);
        if(res != RES_OK) goto error;
      } else {
        res = pp_map(&img, &args);
        if(res != RES_OK) goto error;
        res = img_write_ppm(&img, stream_out, stream_out_name);
        if(res != RES_OK) goto error;
      }
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

exit:
  if(stream_out && stream_out != stdout) fclose(stream_out);
  if(stream_in && stream_in != stdin) fclose(stream_in);
  if(img_is_loaded) img_release(&img);
  args_release(&args);
  if(mem_allocated_size() != 0) {
    fprintf(stderr, "Memory leaks: %lu Bytes.\n",
      (unsigned long)mem_allocated_size());
  }
  return err;
error:
  err = -1;
  goto exit;
}

