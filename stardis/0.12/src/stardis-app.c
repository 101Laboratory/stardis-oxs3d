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

#ifdef STARDIS_ENABLE_MPI
  #define _POSIX_C_SOURCE 200112L
#endif

#include <star/sg3d.h>

#include "stardis-app.h"
#include "stardis-args.h"
#include "stardis-description.h"
#include "stardis-default.h"
#include "stardis-parsing.h"
#include "stardis-compute.h"
#include "stardis-intface.h"
#include "stardis-solid.h"
#include "stardis-fluid.h"
#include "stardis-program.h"

#include <star/senc3d.h>
#include <star/sg3d_sencXd_helper.h>
#include <star/sg3d_sdisXd_helper.h>

#include <rsys/str.h>
#include <rsys/library.h>
#include <rsys/logger.h>
#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/dynamic_array_double.h>

#include <string.h>

#ifdef STARDIS_ENABLE_MPI
  #include <stdio.h>
  #include <mpi.h>
#endif

static const struct dummies DUMMIES_NULL = DUMMIES_NULL__;
static const struct counts COUNTS_NULL = COUNTS_NULL__;

/*******************************************************************************
 * Local Functions
 ******************************************************************************/
static struct sdis_interface*
geometry_get_interface
  (const size_t itri, void* ctx)
{
  struct darray_interface_ptrs* app_interface_data = ctx;
  ASSERT(app_interface_data
    && itri < darray_interface_ptrs_size_get(app_interface_data));
  return darray_interface_ptrs_cdata_get(app_interface_data)[itri];
}

static res_T
check_delta_and_create_solid
  (struct stardis* stardis,
   struct description* description)
{
  res_T res = RES_OK;
  struct senc3d_enclosure* enc = NULL;
  double ratio, delta_range[2] = { DBL_MAX, -DBL_MAX };
  const double acceptance_ratio = 3;
  struct senc3d_enclosure_header header;
  struct solid* solid = description->d.solid;

  ASSERT(stardis && description && description->type == DESC_MAT_SOLID);

  /* Create solid to have ID informed */
  /* Check if delta can fit possible multiple enclosures */
  if(stardis->senc3d_scn) {
    /* Due to previous errors, senc3d_scn can be unavailable */
    unsigned e, ecount = 0;
    const unsigned desc_id = solid->desc_id;
    double solid_volume = 0;

    /* The enclosures where created using description ids */
    ERR(senc3d_scene_get_enclosure_count_by_medium(stardis->senc3d_scn,
      desc_id, &ecount));
    if(ecount == 0) {
      unsigned ccount;
      ERR(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(
        stardis->geometry.sg3d, &ccount));
      CHK(ccount == 0); /* This solid can only be unused if in conflict*/
    } else {
      int external = 0;
      FOR_EACH(e, 0, ecount) {
        ERR(senc3d_scene_get_enclosure_by_medium(stardis->senc3d_scn, desc_id,
          e, &enc));
        ERR(senc3d_enclosure_get_header(enc, &header));
        solid_volume += header.volume;
        if(header.is_infinite) {
          /* External solid, volume is negative and no delta walk expected */
          external = 1;
        } else {
          double d = header.volume / (header.area * 6);
          if(d <= 0) {
            /* Should not */
            res = RES_BAD_OP;
            goto error;
          }
          delta_range[0] = MMIN(delta_range[0], d);
          delta_range[1] = MMAX(delta_range[1], d);
        }
        ERR(senc3d_enclosure_ref_put(enc));
        enc = NULL;
      }
      if(ecount > 1 || !external) {
        ASSERT(0 < delta_range[0] && delta_range[0] <= delta_range[1]);
        ratio = delta_range[1] / delta_range[0];
        if(ratio > acceptance_ratio)
          logger_print(stardis->logger, LOG_WARNING,
            "Solid '%s' is used in %u different enclosures that have different "
            "delta requirements.\n",
            str_cget(&solid->name), ecount);
        /* Delta needs to be substituted with actual value */
        if(solid->delta == DELTA_AUTO) {
          solid->delta = delta_range[0];
          logger_print(stardis->logger, LOG_OUTPUT,
            "Auto delta for solid '%s' set to %g\n",
            str_cget(&solid->name), solid->delta);
        } else {
          int too_small
            = (delta_range[0] > solid->delta * acceptance_ratio);
          int too_big
            = (delta_range[0] * acceptance_ratio < solid->delta);
          /* Check if user delta is OK */
          if(too_small || too_big) {
            logger_print(stardis->logger, LOG_WARNING,
              "User delta for solid '%s' seems too %s: %g; "
              "auto delta would have set it to %g.\n",
              str_cget(&solid->name), (too_big ? "big" : "small"),
              solid->delta, delta_range[0]);
          }
        }
        /* Print power */
        if(solid->vpower != SDIS_VOLUMIC_POWER_NONE && solid->vpower != 0) {
          logger_print(stardis->logger, LOG_OUTPUT,
            "Power of the Solid '%s': %g W\n",
            str_cget(&solid->name), solid_volume * solid->vpower);
        }
      }
    }
  }
  ERR(create_solver_solid(stardis, solid));

end:
  if(enc) SENC3D(enclosure_ref_put(enc));
  return res;
error:
  goto end;
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

#ifdef STARDIS_ENABLE_MPI
/* To be called after logger has been initialized
 * and before stardis is initialized */
res_T
init_mpi
  (int* pargc,
   char** pargv[],
   void (*prt_err_fn)(const char* msg, void* ctx),
   void (*prt_warn_fn)(const char* msg, void* ctx))
{
  res_T res = RES_OK;
  char buf[64];
  int mpi_provided;

  ASSERT(pargc && pargv && prt_err_fn && prt_warn_fn);

  if(MPI_Init_thread(pargc, pargv, MPI_THREAD_MULTIPLE, &mpi_provided)
    != MPI_SUCCESS)
  {
    prt_err_fn("Cannot init MPI\n", NULL);
    res = RES_BAD_ARG;
    goto error;
  }
  else if(mpi_provided != MPI_THREAD_MULTIPLE) {
    const char* lvl;
    switch(mpi_provided) {
      case MPI_THREAD_SINGLE: lvl = "MPI_THREAD_SINGLE"; break;
      case MPI_THREAD_FUNNELED: lvl = "MPI_THREAD_FUNNELED"; break;
      case MPI_THREAD_SERIALIZED: lvl = "MPI_THREAD_SERIALIZED"; break;
      default: FATAL("Unreachable code.\n"); break;
    }
    snprintf(buf, sizeof(buf)-1, "MPI support restricted to %s\n", lvl);
    prt_warn_fn(buf, NULL);
  }

end:
  return res;
error:
  goto end;
}

void
finalize_mpi(void)
{
  int initialized;

  CHK(MPI_Initialized(&initialized) == MPI_SUCCESS);
  if(initialized)
    CHK(MPI_Finalize() == MPI_SUCCESS);
}
#endif

res_T
stardis_init
  (const struct args* args,
   struct logger* logger,
   struct mem_allocator* allocator,
   struct stardis* stardis)
{
  res_T tmp_res, res = RES_OK;
  struct sg3d_sdisXd_scene_create_context create_context;
  struct htable_intface htable_interfaces;
  struct str str, name;
  FILE* f = NULL;
  unsigned i, vcount, tcount, ocount, count;
  int is_for_compute;
  struct sdis_device_create_args dev_args = SDIS_DEVICE_CREATE_ARGS_DEFAULT;

  ASSERT(args && logger && allocator && stardis);

  str_init(allocator, &str);
  str_init(allocator, &name);
  /* Init everything that cannot fail */
  stardis->dummies = DUMMIES_NULL;
  stardis->logger = logger;
  stardis->allocator = allocator;
  htable_intface_init(stardis->allocator, &htable_interfaces);
  darray_descriptions_init(stardis->allocator, &stardis->descriptions);
  d3_set(stardis->probe, args->pos_and_time);
  d2_set(stardis->time_range, args->pos_and_time + 3);
  stardis->dev = NULL;
  stardis->sdis_scn = NULL;
  stardis->senc3d_scn = NULL;
  stardis->mode = args->mode;
  stardis->counts = COUNTS_NULL;
  init_camera(stardis->allocator, &stardis->camera);
  str_init(stardis->allocator, &stardis->solve_name);
  str_init(stardis->allocator, &stardis->dump_model_filename);
  str_init(stardis->allocator, &stardis->paths_filename);
  str_init(stardis->allocator, &stardis->bin_green_filename);
  str_init(stardis->allocator, &stardis->end_paths_filename);
  str_init(stardis->allocator, &stardis->rndgen_state_in_filename);
  str_init(stardis->allocator, &stardis->rndgen_state_out_filename);
  str_init(stardis->allocator, &stardis->chunks_prefix);
  darray_size_t_init(stardis->allocator, &stardis->compute_surface.primitives);
  darray_sides_init(stardis->allocator, &stardis->compute_surface.sides);
  darray_uint_init(stardis->allocator, &stardis->compute_surface.err_triangles);
  darray_probe_boundary_init(stardis->allocator, &stardis->probe_boundary_list);
  stardis->compute_surface.area = 0;
  stardis->samples = args->samples;
  stardis->nthreads = args->nthreads;
  stardis->picard_order = args->picard_order;
  stardis->scale_factor = -1; /* invalid value */
  stardis->radenv = RADIATIVE_ENV_DEFAULT;
  stardis->radenv_def = 0;
  stardis->initial_time = args->initial_time;
  stardis->geometry_initialized = 0;
  d2(stardis->t_range, INF, -INF);
  stardis->dump_paths = SDIS_HEAT_PATH_NONE;
  if(args->dump_paths & DUMP_ERROR)
    stardis->dump_paths |= SDIS_HEAT_PATH_FAILURE;
  if(args->dump_paths & DUMP_SUCCESS)
    stardis->dump_paths |= SDIS_HEAT_PATH_SUCCESS;
  stardis->diff_algo = args->diff_algo;
  stardis->next_medium_id = 0;
  stardis->undefined_medium_behind_boundary_id = SENC3D_UNSPECIFIED_MEDIUM;
  stardis->verbose = args->verbose;
  stardis->extsrc = EXTERN_SOURCE_NULL;
  stardis->disable_intrad = args->disable_intrad;
  darray_media_ptr_init(stardis->allocator, &stardis->media);

  /* If a dump is expected, we won't process any computation */
  is_for_compute =
    (stardis->mode & COMPUTE_MODES) && !(stardis->mode & MODE_DUMP_MODEL);

  dev_args.logger = stardis->logger;
  dev_args.allocator = stardis->allocator;
  dev_args.nthreads_hint = stardis->nthreads;
  dev_args.verbosity = stardis->verbose;

#ifdef STARDIS_ENABLE_MPI
  logger_print(stardis->logger, LOG_OUTPUT, "MPI is enabled.\n");
  /* Open MPI accepts the C/C++ argc and argv arguments to main,
   * but neither modifies, interprets, nor distributes them: use NULL */
  CHK(MPI_Initialized(&stardis->mpi_initialized) == MPI_SUCCESS);
  if(stardis->mpi_initialized)
    CHK(MPI_Comm_rank(MPI_COMM_WORLD, &stardis->mpi_rank) == MPI_SUCCESS);
#else
  logger_print(stardis->logger, LOG_OUTPUT, "MPI is disabled.\n");
  stardis->mpi_initialized = 0;
#endif

  dev_args.use_mpi = stardis->mpi_initialized;

  ERR(sdis_device_create(&dev_args, &stardis->dev));

  ERR(init_geometry(stardis->logger, stardis->allocator, stardis->verbose,
    &stardis->geometry));
  stardis->geometry_initialized = 1;

  if(args->dump_model_filename) {
    ERR(str_set(&stardis->dump_model_filename, args->dump_model_filename));
  }

  if(args->mode & MODE_COMPUTE_IMAGE_IR) {
    ERR(parse_camera(stardis->logger, args->camera, stardis));
  }
  else if(args->mode & MODE_COMPUTE_TEMP_MEAN_IN_MEDIUM) {
    ERR(str_set(&stardis->solve_name, args->medium_name));
  }
  else if((args->mode & MODE_COMPUTE_PROBE_TEMP_ON_SURF)
       || (args->mode & MODE_COMPUTE_PROBE_FLUX_DNSTY_ON_SURF)
       || (args->mode & MODE_COMPUTE_LIST_PROBE_TEMP_ON_SURF)
       || (args->mode & MODE_COMPUTE_LIST_PROBE_FLUX_DNSTY_ON_SURF)) {
    ERR(darray_probe_boundary_copy
      (&stardis->probe_boundary_list, &args->probe_boundary_list));
  }
  else if(args->mode & SURFACE_COMPUTE_MODES) {
    ERR(str_set(&stardis->solve_name, args->solve_filename));
  }
  else if(args->mode & MODE_DUMP_C_CHUNKS) {
    ERR(str_set(&stardis->chunks_prefix, args->chunks_prefix));
  }
  ERR(read_model(&args->model_files, stardis));

  create_context.geometry = stardis->geometry.sg3d;
  create_context.app_interface_getter = geometry_get_interface;
  create_context.app_interface_data = &stardis->geometry.interf_bytrg;
  ERR(sg3d_geometry_get_unique_vertices_count(stardis->geometry.sg3d, &vcount));
  ERR(sg3d_geometry_get_unique_triangles_count(stardis->geometry.sg3d, &tcount));

  logger_print(stardis->logger, LOG_OUTPUT,
    "Read %u unique triangles.\n", tcount);

  ERR(sg3d_geometry_validate_properties(stardis->geometry.sg3d,
     validate_properties, stardis));
  ERR(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(
    stardis->geometry.sg3d, &count));
  if(count) {
    if(!str_is_empty(&stardis->dump_model_filename)) {
      ERR(str_copy(&name, &stardis->dump_model_filename));
      ERR(str_append(&name, "_property_conflits.obj"));
      f = fopen(str_cget(&name), "w");
      if(!f) {
        logger_print(stardis->logger, LOG_ERROR,
          "cannot open file '%s' for writing.\n", str_cget(&name));
        res = RES_IO_ERR;
        goto error;
      }
      ERR(sg3d_geometry_dump_as_obj(stardis->geometry.sg3d, f,
            SG3D_OBJ_DUMP_PROPERTY_CONFLICTS));
      fclose(f); f = NULL;
    }
    logger_print(stardis->logger, (is_for_compute ? LOG_ERROR : LOG_WARNING),
      "Property conflicts found in the model (%u triangles).\n", count);
    if(is_for_compute) {
      res = RES_BAD_ARG;
      goto error;
    }
  }

  /* If computation is on a compute surface, read it */
  if(args->mode & SURFACE_COMPUTE_MODES) {
    unsigned save_count = count;
    ASSERT(!str_is_empty(&stardis->solve_name));
    ERR(read_compute_surface(stardis));
    /* Check compute surface  */
    ERR(sg3d_geometry_validate_properties(stardis->geometry.sg3d,
      validate_properties, stardis));
    ERR(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(
      stardis->geometry.sg3d, &count));
    ASSERT(count >= save_count);
    if(save_count != count) {
      logger_print(stardis->logger, (is_for_compute ? LOG_ERROR : LOG_WARNING),
        "Invalid compute region defined by file '%s'.\n",
        str_cget(&stardis->solve_name));
      logger_print(stardis->logger, (is_for_compute ? LOG_ERROR : LOG_WARNING),
        "The file contains %u triangles not in the model.\n", count - save_count);
      if(is_for_compute) {
        res = RES_BAD_ARG;
        goto error;
      }
    }
    logger_print(stardis->logger, LOG_OUTPUT, "Compute surface area is %g m2\n",
      stardis->compute_surface.area);
  }

  /* Create enclosures */
  tmp_res = init_enclosures(stardis);
  if(tmp_res != RES_OK && is_for_compute) {
    res = tmp_res;
    goto error;
  }
  ERR(senc3d_scene_get_overlapping_triangles_count(stardis->senc3d_scn, &ocount));
  if(ocount) {
    if(!str_is_empty(&stardis->dump_model_filename)) {
      ERR(str_copy(&name, &stardis->dump_model_filename));
      ERR(str_append(&name, "_overlapping_triangles.obj"));
      f = fopen(str_cget(&name), "w");
      if(!f) {
        logger_print(stardis->logger, LOG_ERROR,
          "cannot open file '%s' for writing.\n", str_cget(&name));
        res = RES_IO_ERR;
        goto error;
      }
      /* Dump vertices */
      for(i = 0; i < vcount; i++) {
        double coord[3];
        ERR(senc3d_scene_get_vertex(stardis->senc3d_scn, i, coord));
        fprintf(f, "v %.16g %.16g %.16g\n", SPLIT3(coord));
      }
      /* Dump triangles */
      for(i = 0; i < ocount; i++) {
        unsigned id, trg[3];
        ERR(senc3d_scene_get_overlapping_triangle(stardis->senc3d_scn, i, &id));
        ERR(senc3d_scene_get_triangle(stardis->senc3d_scn, id, trg));
        fprintf(f, "f %u %u %u\n",
            1 + trg[0], 1 + trg[1], 1 + trg[2]); /* OBJ indexing starts at 1 */
      }
      fclose(f); f = NULL;
    }
    logger_print(stardis->logger, (is_for_compute ? LOG_ERROR : LOG_WARNING),
      "Scene contains %u overlapping triangles.\n",
      ocount);
    if(is_for_compute) {
      res = RES_BAD_ARG;
      goto error;
    }
  }

  /* Create solids, finalize program data, and log model information */
  for(i = 0; i < darray_descriptions_size_get(&stardis->descriptions); i++) {
    struct description* desc =
      darray_descriptions_data_get(&stardis->descriptions) + i;
    if(desc->type == DESC_MAT_SOLID) {
      tmp_res = check_delta_and_create_solid(stardis, desc);
    } else if(desc->type == DESC_PROGRAM && desc->d.program->finalize) {
      enum stardis_return_status rs;
      rs = desc->d.program->finalize(desc->d.program->prog_data);
      switch(rs) {
        case STARDIS_SUCCESS: tmp_res = RES_OK; break;
        case STARDIS_FAILURE: tmp_res = RES_BAD_ARG; break;
        default:
          FATAL("error:" STR(__FILE__) ":" STR(__LINE__)": Invalid type.\n");
      }
    }
    if(tmp_res != RES_OK && is_for_compute) {
      res = tmp_res;
      goto error;
    }
    ERR(str_print_description(&str, i, desc));
    logger_print(stardis->logger, LOG_OUTPUT, "%s\n", str_cget(&str));
  }

  if(is_for_compute) {
    for(i = 0; i < tcount; ++i) {
      ERR(create_intface(stardis, i, &htable_interfaces));
    }
    if(args->paths_filename) {
      ERR(str_set(&stardis->paths_filename, args->paths_filename));
    }
    if(args->bin_green_filename) {
      ERR(str_set(&stardis->bin_green_filename, args->bin_green_filename));
    }
    if(args->end_paths_filename) {
      ERR(str_set(&stardis->end_paths_filename, args->end_paths_filename));
    }
    if(args->rndgen_state_in_filename) {
      ERR(str_set(&stardis->rndgen_state_in_filename,
        args->rndgen_state_in_filename));
    }
    if(args->rndgen_state_out_filename) {
      ERR(str_set(&stardis->rndgen_state_out_filename,
        args->rndgen_state_out_filename));
    }
  }

  /* If computation is on a volume, check medium is known */
  if(args->mode & MODE_COMPUTE_TEMP_MEAN_IN_MEDIUM) {
    if(!find_medium_by_name(stardis, str_cget(&stardis->solve_name), NULL)) {
      logger_print(stardis->logger, (is_for_compute ? LOG_ERROR : LOG_WARNING),
        "Cannot find medium '%s'\n", str_cget(&stardis->solve_name));
      res = RES_BAD_ARG;
      goto error;
    }
  }

  if(is_for_compute) {
    struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
    ASSERT(darray_interface_ptrs_size_get(&stardis->geometry.interf_bytrg)
      == tcount);

    res = radiative_env_create_solver_radiative_env(&stardis->radenv, stardis);
    if(res != RES_OK) goto error;

    scn_args.get_indices = sg3d_sdisXd_geometry_get_indices;
    scn_args.get_interface = sg3d_sdisXd_geometry_get_interface;
    scn_args.get_position = sg3d_sdisXd_geometry_get_position;
    scn_args.nprimitives = tcount;
    scn_args.nvertices = vcount;
    scn_args.fp_to_meter = stardis->scale_factor;
    scn_args.t_range[0] = stardis->t_range[0];
    scn_args.t_range[1] = stardis->t_range[1];
    scn_args.source = stardis->extsrc.sdis_src;
    scn_args.radenv = stardis->radenv.sdis_radenv;
    scn_args.context = &create_context;

    /* Setting Tmax to 0 is a way of setting the radiative coefficient to 0, and
     * thus setting the probability of evolving in a radiative random walk to
     * zero. */
    if(stardis->disable_intrad) {
      scn_args.t_range[0] = 0;
      scn_args.t_range[1] = 0;
    }

    res = sdis_scene_create(stardis->dev, &scn_args, &stardis->sdis_scn);
    if(res != RES_OK) {
      logger_print(stardis->logger, LOG_ERROR,
        "Cannot create the stardis solver scene.\n");
      goto error;
    }
  }

exit:
  if(f) fclose(f);
  str_release(&str);
  str_release(&name);
  htable_intface_release(&htable_interfaces);
  return res;
error:
  stardis_release(stardis);
  goto exit;
}

void
stardis_release
  (struct stardis* stardis)
{
  size_t i;

  ASSERT(stardis);

  if(stardis->dev) SDIS(device_ref_put(stardis->dev));
  if(stardis->sdis_scn) SDIS(scene_ref_put(stardis->sdis_scn));
  if(stardis->senc3d_scn) SENC3D(scene_ref_put(stardis->senc3d_scn));
  str_release(&stardis->solve_name);
  str_release(&stardis->dump_model_filename);
  str_release(&stardis->paths_filename);
  str_release(&stardis->bin_green_filename);
  str_release(&stardis->end_paths_filename);
  str_release(&stardis->chunks_prefix);

  extern_source_release(&stardis->extsrc);
  radiative_env_release(&stardis->radenv);

  /* release non-PROGRAM descritions first */
  FOR_EACH(i, 0, darray_descriptions_size_get(&stardis->descriptions)) {
    struct description* d = darray_descriptions_data_get(&stardis->descriptions) +i;
    if(d->type == DESC_PROGRAM) continue;
    release_description(d, stardis->allocator);
  }
  /* release PROGRAM descritions */
  FOR_EACH(i, 0, darray_descriptions_size_get(&stardis->descriptions)) {
    struct description* d = darray_descriptions_data_get(&stardis->descriptions) +i;
    if(d->type != DESC_PROGRAM) continue;
    release_description(d, stardis->allocator);
  }
  darray_descriptions_release(&stardis->descriptions);
  if(stardis->geometry_initialized)
    release_geometry(&stardis->geometry);
  darray_size_t_release(&stardis->compute_surface.primitives);
  darray_sides_release(&stardis->compute_surface.sides);
  darray_uint_release(&stardis->compute_surface.err_triangles);
  FOR_EACH(i, 0, darray_media_ptr_size_get(&stardis->media)) {
    if(darray_media_ptr_data_get(&stardis->media)[i])
      SDIS(medium_ref_put(darray_media_ptr_data_get(&stardis->media)[i]));
  }
  darray_media_ptr_release(&stardis->media);
  release_camera(&stardis->camera);
  if(stardis->dummies.stardis_fluid) {
    release_fluid(stardis->dummies.stardis_fluid, stardis->allocator);
  }
  if(stardis->dummies.stardis_solid) {
    release_solid(stardis->dummies.stardis_solid, stardis->allocator);
  }
  darray_probe_boundary_release(&stardis->probe_boundary_list);
}

unsigned
allocate_stardis_medium_id
  (struct stardis* stardis)
{
  ASSERT(stardis);
  return stardis->next_medium_id++;
}

res_T
init_enclosures
  (struct stardis* stardis)
{
  res_T res = RES_OK;
  unsigned tsz, vsz;
  struct senc3d_device* senc_dev = NULL;

  ERR(sg3d_geometry_get_unique_triangles_count(stardis->geometry.sg3d, &tsz));

  ERR(sg3d_geometry_get_unique_vertices_count(stardis->geometry.sg3d, &vsz));
  ERR(senc3d_device_create(stardis->logger, stardis->allocator,
    stardis->nthreads, stardis->verbose, &senc_dev));
  stardis->undefined_medium_behind_boundary_id
    = allocate_stardis_medium_id(stardis);
  ERR(senc3d_scene_create(senc_dev,
    SENC3D_CONVENTION_NORMAL_BACK | SENC3D_CONVENTION_NORMAL_OUTSIDE,
    tsz, sg3d_sencXd_geometry_get_indices, sg3d_sencXd_geometry_get_media,
    vsz, sg3d_sencXd_geometry_get_position, stardis->geometry.sg3d,
    &stardis->senc3d_scn));
exit:
  if(senc_dev) senc3d_device_ref_put(senc_dev);
  return res;
error:
  goto exit;
}

#define COUNT_SIDE(Rank) {\
  if(properties[(Rank)] == SG3D_UNSPECIFIED_PROPERTY) undef_count++;\
  else {\
    ASSERT(properties[(Rank)] < darray_descriptions_size_get(&stardis->descriptions));\
    ASSERT(DESC_IS_MEDIUM(descs+properties[(Rank)]));\
    if(DESC_IS_SOLID(descs+properties[(Rank)])) { solid_count++; }\
    else { ASSERT(DESC_IS_FLUID(descs+properties[(Rank)])); fluid_count++; }\
  }\
}

res_T
validate_properties
  (const unsigned itri,
   const unsigned properties[SG3D_PROP_TYPES_COUNT__],
   void* context,
   int* properties_conflict_status)
{
  res_T res = RES_OK;
  struct stardis* stardis = context;
  unsigned undef_count, solid_count, fluid_count, intface_count;
  const struct description* descs;
  const struct description* intface = NULL;

  (void)itri;
  ASSERT(stardis && properties_conflict_status);
  descs = darray_descriptions_cdata_get(&stardis->descriptions);
  *properties_conflict_status = NO_PROPERTY_CONFLICT;
  undef_count = solid_count = fluid_count = intface_count = 0;

  COUNT_SIDE(SG3D_FRONT);
  COUNT_SIDE(SG3D_BACK);
  if(properties[SG3D_INTFACE] == SG3D_UNSPECIFIED_PROPERTY)
    undef_count++;
  else intface_count++;

  ASSERT(solid_count <= 2 && fluid_count <= 2 && intface_count <= 1);
  ASSERT(undef_count + solid_count + fluid_count + intface_count == 3);
  if(intface_count) {
    ASSERT(properties[SG3D_INTFACE]
      < darray_descriptions_size_get(&stardis->descriptions));
    intface = descs + properties[SG3D_INTFACE];
    switch (intface->type) {
    case DESC_BOUND_H_FOR_FLUID:
    case DESC_BOUND_H_FOR_FLUID_PROG:
      if(!(solid_count == 0 && fluid_count == 1)) {
        if(solid_count + fluid_count == 2)
          *properties_conflict_status = BOUND_H_FOR_FLUID_BETWEEN_2_DEFS;
        else if(solid_count + fluid_count == 0)
          *properties_conflict_status = BOUND_H_FOR_FLUID_BETWEEN_2_UNDEFS;
        else if(solid_count == 1)
          *properties_conflict_status = BOUND_H_FOR_FLUID_ENCLOSING_SOLID;
        else FATAL("error:" STR(__FILE__) ":" STR(__LINE__)"\n");
        goto end;
      }
      break;
    case DESC_BOUND_H_FOR_SOLID:
    case DESC_BOUND_H_FOR_SOLID_PROG:
      if(!(solid_count == 1 && fluid_count == 0)) {
        if(solid_count + fluid_count == 2)
          *properties_conflict_status = BOUND_H_FOR_SOLID_BETWEEN_2_DEFS;
        else if(solid_count + fluid_count == 0)
          *properties_conflict_status = BOUND_H_FOR_SOLID_BETWEEN_2_UNDEFS;
        else if(fluid_count == 1)
          *properties_conflict_status = BOUND_H_FOR_SOLID_ENCLOSING_FLUID;
        else FATAL("error:" STR(__FILE__) ":" STR(__LINE__)"\n");
        goto end;
      }
      break;
    case DESC_BOUND_HF_FOR_SOLID:
    case DESC_BOUND_HF_FOR_SOLID_PROG:
      if(!(solid_count == 1 && fluid_count == 0)) {
        if(solid_count + fluid_count == 2)
          *properties_conflict_status = BOUND_HF_FOR_SOLID_BETWEEN_2_DEFS;
        else if(solid_count + fluid_count == 0)
          *properties_conflict_status = BOUND_HF_FOR_SOLID_BETWEEN_2_UNDEFS;
        else if(fluid_count == 1)
          *properties_conflict_status = BOUND_HF_FOR_SOLID_ENCLOSING_FLUID;
        else FATAL("error:" STR(__FILE__) ":" STR(__LINE__)"\n");
        goto end;
      }
      break;
    case DESC_BOUND_T_FOR_SOLID:
    case DESC_BOUND_T_FOR_SOLID_PROG:
      if(!(solid_count == 1 && fluid_count == 0)) {
        if(solid_count + fluid_count == 2)
          *properties_conflict_status = BOUND_T_FOR_SOLID_BETWEEN_2_DEFS;
        else if(solid_count + fluid_count == 0)
          *properties_conflict_status = BOUND_T_FOR_SOLID_BETWEEN_2_UNDEFS;
        else if(fluid_count == 1)
          *properties_conflict_status = BOUND_T_FOR_SOLID_ENCLOSING_FLUID;
        else FATAL("error:" STR(__FILE__) ":" STR(__LINE__)"\n");
        goto end;
      }
      break;
    case DESC_BOUND_F_FOR_SOLID:
    case DESC_BOUND_F_FOR_SOLID_PROG:
      if(!(solid_count == 1 && fluid_count == 0)) {
        if(solid_count + fluid_count == 2)
          *properties_conflict_status = BOUND_F_FOR_SOLID_BETWEEN_2_DEFS;
        else if(solid_count + fluid_count == 0)
          *properties_conflict_status = BOUND_F_FOR_SOLID_BETWEEN_2_UNDEFS;
        else if(fluid_count == 1)
          *properties_conflict_status = BOUND_F_FOR_SOLID_ENCLOSING_FLUID;
        else FATAL("error:" STR(__FILE__) ":" STR(__LINE__)"\n");
        goto end;
      }
      break;
    case DESC_SOLID_FLUID_CONNECT:
    case DESC_SOLID_FLUID_CONNECT_PROG:
      if(solid_count != 1 || fluid_count != 1) {
        if(solid_count == 2)
          *properties_conflict_status = SFCONNECT_BETWEEN_2_SOLIDS;
        else if(fluid_count == 2)
          *properties_conflict_status = SFCONNECT_BETWEEN_2_FLUIDS;
        else if(solid_count + fluid_count == 1)
          *properties_conflict_status = SFCONNECT_USED_AS_BOUNDARY;
        else if(solid_count + fluid_count == 0)
          *properties_conflict_status = SFCONNECT_BETWEEN_2_UNDEFS;
        else FATAL("error:" STR(__FILE__) ":" STR(__LINE__)"\n");
        goto end;
      }
      break;
    case DESC_SOLID_SOLID_CONNECT:
    case DESC_SOLID_SOLID_CONNECT_PROG:
      if(solid_count != 2) {
        /*if(soli_count == 1 && fluid_count == 1)*/
          /**properties_conflict_status = SSCONNECT_BETWEEN_SOLID_AND_FLUID;*/
        goto end;
      }
      break;
    default:
      FATAL("error:" STR(__FILE__) ":" STR(__LINE__)": Invalid type.\n");
    }
  } else {
    /* No interface defined */
    ASSERT(intface_count == 0 && undef_count >= 1);
    if(undef_count == 3) {
      *properties_conflict_status = TRG_WITH_NO_PROPERTY;
      goto end;
    }
    if(fluid_count == 2) {
      *properties_conflict_status = NO_CONNECTION_BETWEEN_2_FLUIDS;
      goto end;
    }
    if(undef_count == 2) {
      ASSERT(fluid_count + solid_count == 1);
      if(fluid_count)
        *properties_conflict_status = NO_BOUND_BETWEEN_FLUID_AND_UNDEF;
      else *properties_conflict_status = NO_BOUND_BETWEEN_SOLID_AND_UNDEF;
      goto end;
    }
    if(undef_count == 1 && solid_count == 1 && fluid_count == 1) {
      *properties_conflict_status = NO_CONNECTION_BETWEEN_SOLID_AND_FLUID;
      goto end;
    }
    /* Undef interface between solids is OK */
    CHK(solid_count == 2);
  }

end:
  return res;
}

#undef COUNT_SIDE

res_T
init_geometry
  (struct logger* logger,
   struct mem_allocator* allocator,
   const int verbose,
   struct geometry* geom)
{
  res_T res = RES_OK;
  struct sg3d_device* sg3d_dev = NULL;

  ASSERT(allocator && geom);

  geom->sg3d = NULL;
  darray_interface_ptrs_init(allocator, &geom->interfaces);
  darray_interface_ptrs_init(allocator, &geom->interf_bytrg);
  ERR(sg3d_device_create(logger, allocator, verbose, &sg3d_dev));
  ERR(sg3d_geometry_create(sg3d_dev, &geom->sg3d));

exit:
  if(sg3d_dev) SG3D(device_ref_put(sg3d_dev));
  return res;
error:
  release_geometry(geom);
  goto exit;
}

void
release_geometry
  (struct geometry* geom)
{
  size_t i;
  struct sdis_interface
    ** intf = darray_interface_ptrs_data_get(&geom->interfaces);
  if(geom->sg3d) SG3D(geometry_ref_put(geom->sg3d));
  for(i = 0; i < darray_interface_ptrs_size_get(&geom->interfaces); ++i)
    SDIS(interface_ref_put(intf[i]));
  darray_interface_ptrs_release(&geom->interfaces);
  darray_interface_ptrs_release(&geom->interf_bytrg);
}

void
init_camera
  (struct mem_allocator* alloc, struct camera* cam)
{
  ASSERT(alloc && cam);
  d3(cam->pos, STARDIS_DEFAULT_RENDERING_POS);
  d3(cam->tgt, STARDIS_DEFAULT_RENDERING_TGT);
  d3(cam->up, STARDIS_DEFAULT_RENDERING_UP);
  cam->fmt = STARDIS_DEFAULT_RENDERING_OUTPUT_FILE_FMT;
  cam->fov = STARDIS_DEFAULT_RENDERING_FOV;
  cam->spp = STARDIS_DEFAULT_RENDERING_SPP;
  cam->img_width = STARDIS_DEFAULT_RENDERING_IMG_WIDTH;
  cam->img_height = STARDIS_DEFAULT_RENDERING_IMG_HEIGHT;
  d2(cam->time_range, STARDIS_DEFAULT_RENDERING_TIME);
  cam->auto_look_at = 1;
  str_init(alloc, &cam->file_name);
}

void
release_camera
  (struct camera* cam)
{
  ASSERT(cam);
  str_release(&cam->file_name);
}

void
log_err_fn
  (const char* msg, void* ctx)
{
#ifdef STARDIS_ENABLE_MPI
  int initialized, rank = 0;
#endif

  ASSERT(msg);
  (void)ctx;

#ifdef STARDIS_ENABLE_MPI
  CHK(MPI_Initialized(&initialized) == MPI_SUCCESS);
  if(initialized) CHK(MPI_Comm_rank(MPI_COMM_WORLD, &rank) == MPI_SUCCESS);
  /* Only master prints */
  if(rank != 0) return;
#endif

#ifdef OS_WINDOWS
  fprintf(stderr, "error: %s", msg);
#else
  fprintf(stderr, "\x1b[31merror:\x1b[0m %s", msg);
#endif
}

void
log_warn_fn
  (const char* msg, void* ctx)
{
#ifdef STARDIS_ENABLE_MPI
  int initialized, rank = 0;
#endif

  ASSERT(msg);
  (void)ctx;

#ifdef STARDIS_ENABLE_MPI
  CHK(MPI_Initialized(&initialized) == MPI_SUCCESS);
  if(initialized) CHK(MPI_Comm_rank(MPI_COMM_WORLD, &rank) == MPI_SUCCESS);
  /* Only master prints */
  if(rank != 0) return;
#endif

#ifdef OS_WINDOWS
  fprintf(stderr, "warning: %s", msg);
#else
  fprintf(stderr, "\x1b[33mwarning:\x1b[0m %s", msg);
#endif
}

void
log_prt_fn
  (const char* msg, void* ctx)
{
#ifdef STARDIS_ENABLE_MPI
  int initialized, rank = 0;
#endif

  ASSERT(msg);
  (void)ctx;

#ifdef STARDIS_ENABLE_MPI
  CHK(MPI_Initialized(&initialized) == MPI_SUCCESS);
  if(initialized) CHK(MPI_Comm_rank(MPI_COMM_WORLD, &rank) == MPI_SUCCESS);
  /* only master prints */
  if(rank != 0) return;
#endif

#ifdef OS_WINDOWS
  fprintf(stderr, "message: %s", msg);
#else
  fprintf(stderr, "\x1b[32moutput:\x1b[0m %s", msg);
#endif
}

