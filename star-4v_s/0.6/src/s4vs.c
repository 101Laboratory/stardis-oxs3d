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

#include "s4vs_args.h"
#include "s4vs_realization.h"

#include <star/s3d.h>
#include <star/s3daw.h>
#include <star/smc.h>

#include <rsys/cstr.h>
#include <rsys/float3.h>
#include<rsys/mem_allocator.h>
#include <rsys/clock_time.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
compute_4v_s
  (struct s3d_scene* scene,
   const size_t max_realisations,
   const double ks)
{
  /* Star-Monte Carlo */
  struct smc_device_create_args args = SMC_DEVICE_CREATE_ARGS_DEFAULT;
  struct smc_estimator_status estimator_status = SMC_ESTIMATOR_STATUS_NULL;
  struct smc_integrator integrator = SMC_INTEGRATOR_NULL;
  struct smc_device* smc = NULL;
  struct smc_estimator* estimator = NULL;

  /* Miscellaneous */
  char dump[64];
  struct s4vs_context ctx;
  struct s3d_scene_view* view = NULL;
  struct time t0, t1;
  float S, V, reference;
  res_T res = RES_OK;
  ASSERT(scene && max_realisations > 0 && ks >= 0);

  S3D(scene_view_create(scene, S3D_SAMPLE|S3D_TRACE, &view));

  /* Compute the expected result using a mesh-based method */
  S3D(scene_view_compute_area(view, &S));
  S3D(scene_view_compute_volume(view, &V));
  if(eq_epsf(S, 0, 1.e-6f) || S < 0) {
    fprintf(stderr, "No surface to sample. Is the scene empty?\n");
    res = RES_BAD_ARG;
    goto error;
  }
  if(eq_epsf(V, 0, 1.e-6f) || V < 0) {
    fprintf(stderr,
      "Invalid volume \"%.2f\". The scene might not match the prerequisites:\n"
      "it must be closed and its normals must point *into* the volume.\n", V);
    res = RES_BAD_ARG;
    goto error;
  }
  reference = 4*V/S;

  /* Initialize context for MC computation */
  ctx.view = view;
  ctx.ks = ks;
  ctx.g = PI/4.0;

  /* Setup Star-MC */
  args.verbose = 1;
  SMC(device_create(&args, &smc));
  integrator.integrand = &s4vs_realization; /* Realization function */
  integrator.type = &smc_double; /* Type of the Monte Carlo weight */
  integrator.max_realisations = max_realisations; /* Realization count */
  integrator.max_failures = max_realisations / 1000;

  /* Solve */
  time_current(&t0);
  SMC(solve(smc, &integrator, &ctx, &estimator));
  time_sub(&t0, time_current(&t1), &t0);
  time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));
  printf("Computation time: %s\n", dump);

  /* Print the simulation results */
  SMC(estimator_get_status(estimator, &estimator_status));

  if(estimator_status.NF > integrator.max_failures) {
    fprintf(stderr,
      "Too many failures (%lu). The scene might not match the prerequisites:\n"
      "it must be closed and its normals must point *into* the volume.\n",
      (unsigned long)estimator_status.NF);
    goto error;
  }
  printf("4V/S = %g ~ %g +/- %g\n#failures = %lu/%lu\n",
   reference,
   SMC_DOUBLE(estimator_status.E),
   SMC_DOUBLE(estimator_status.SE),
   (unsigned long)estimator_status.NF,
   (unsigned long)max_realisations);

exit:
  /* Clean-up data */
  if(view) S3D(scene_view_ref_put(view));
  if(smc) SMC(device_ref_put(smc));
  if(estimator) SMC(estimator_ref_put(estimator));
  return res;

error:
  goto exit;
}

/* Create a S3D scene from an obj in a scene */
static res_T
import_obj(const char* filename, struct s3d_scene** out_scene)
{
  /* Star-3D */
  struct s3d_device* s3d = NULL;
  struct s3d_scene* scene = NULL;

  /* Star-3DAW */
  struct s3daw* s3daw = NULL;
  size_t ishape, nshapes;

  /* Miscellaneous */
  char dump[64];
  struct time t0, t1;
  res_T res = RES_OK;

  ASSERT(out_scene); /* Pre-condition */

  S3D(device_create(NULL, NULL, 0/*verbose?*/, &s3d));
  S3DAW(create(NULL, NULL, NULL, NULL, s3d, 1/*verbose?*/, &s3daw));

  time_current(&t0);
  if(filename) {
    res = s3daw_load(s3daw, filename);
  } else {
    res = s3daw_load_stream(s3daw, stdin);
  }
  if(res != RES_OK) {
    fprintf(stderr, "Error loading obj -- %s\n", res_to_cstr(res));
    goto error;
  }
  time_current(&t1);
  time_sub(&t0, &t1, &t0);
  time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));
  printf("Obj loaded in %s\n", dump);

  S3DAW(get_shapes_count(s3daw, &nshapes));
  S3D(scene_create(s3d, &scene));

  FOR_EACH(ishape, 0, nshapes) {
    struct s3d_shape* shape = NULL;
    S3DAW(get_shape(s3daw, ishape, &shape));
    S3D(mesh_set_hit_filter_function(shape, s4vs_discard_self_hit, NULL));
    S3D(scene_attach_shape(scene, shape));
  }

exit:
  /* Release memory */
  if(s3daw) S3DAW(ref_put(s3daw));
  if(s3d) S3D(device_ref_put(s3d));
  *out_scene = scene;
  return res;
error:
  if(scene) {
    S3D(scene_ref_put(scene));
    scene = NULL;
  }
  goto exit;
}

/*******************************************************************************
 * The program
 ******************************************************************************/
int
main(int argc, char* argv[])
{
  struct s4vs_args args = S4VS_ARGS_DEFAULT;
  struct s3d_scene* scn = NULL;
  res_T res = RES_OK;
  int err = 0;

  res = s4vs_args_init(&args, argc, argv);
  if(res != RES_OK) goto error;
  res = import_obj(args.filename, &scn);
  if(res != RES_OK) goto error;
  res = compute_4v_s(scn, args.nrealisations, args.ks);
  if(res != RES_OK) goto error;

exit:
  if(scn) S3D(scene_ref_put(scn));
  if(mem_allocated_size() != 0) {
    fprintf(stderr, "Memory leaks: %lu Bytes\n",
      (unsigned long)mem_allocated_size());
    err = 1;
  }
  return err;
error:
  err = 1;
  goto exit;
}
