/* Copyright (C) 2016-2018, 2021-2025 |Méso|Star> (contact@meso-star.com)
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

#include "ssf.h"
#include "test_ssf_utils.h"

#include <string.h>

int
main(int argc, char** argv)
{
  static const size_t NSAMPS = 10000;

  struct mem_allocator allocator;
  struct ssf_info info = SSF_INFO_NULL;
  struct ssf_phase_rdgfa_setup_args args = SSF_PHASE_RDGFA_SETUP_ARGS_DEFAULT;
  struct ssf_phase_rdgfa_desc desc = SSF_PHASE_RDGFA_DESC_NULL;
  struct ssf_phase_rdgfa_interval interval = SSF_PHASE_RDGFA_INTERVAL_NULL;
  struct ssf_phase* phase;
  struct ssf_phase* dummy;
  struct ssp_rng* rng;
  double cumulative_prev;
  double wo[3];
  int err = 0;
  size_t i;
  (void)argc, (void)argv;

  if(argc <= 1) {
    fprintf(stderr, "Usage: %s <simd_none|simd_128|simd_256>\n", argv[0]);
    goto error;
  }

  if(!strcmp(argv[1], "simd_none")) {
    args.simd = SSF_SIMD_NONE;
  } else if(!strcmp(argv[1], "simd_128")) {
    args.simd = SSF_SIMD_128;
  } else if(!strcmp(argv[1], "simd_256")) {
    args.simd = SSF_SIMD_256;
  } else {
    fprintf(stderr, "Invalid argument '%s'.\n", argv[1]);
    goto error;
  }

  CHK(ssf_get_info(NULL) == RES_BAD_ARG);
  CHK(ssf_get_info(&info) == RES_OK);
  if(args.simd == SSF_SIMD_128) {
    CHK(info.simd_128 != 0);
  }
  if(args.simd == SSF_SIMD_256) {
    CHK(info.simd_256 != 0);
  }

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);

  CHK(ssf_phase_create(&allocator, &ssf_phase_rdgfa, &phase) == RES_OK);
  CHK(ssf_phase_create(&allocator, &phase_dummy, &dummy) == RES_OK);

  args.wavelength = 532;
  args.fractal_dimension = 1.80;
  args.gyration_radius =  111.6372805638;
  args.nintervals = 1000;
  CHK(ssf_phase_rdgfa_setup(NULL, &args) == RES_BAD_ARG);
  CHK(ssf_phase_rdgfa_setup(phase, NULL) == RES_BAD_ARG);
  CHK(ssf_phase_rdgfa_setup(dummy, &args) == RES_BAD_ARG);
  CHK(ssf_phase_rdgfa_setup(phase, &args) == RES_OK);

  CHK(ssf_phase_rdgfa_get_desc(NULL, &desc) == RES_BAD_ARG);
  CHK(ssf_phase_rdgfa_get_desc(phase, NULL) == RES_BAD_ARG);
  CHK(ssf_phase_rdgfa_get_desc(dummy, &desc) == RES_BAD_ARG);
  CHK(ssf_phase_rdgfa_get_desc(phase, &desc) == RES_OK);

  CHK(desc.wavelength == args.wavelength);
  CHK(desc.fractal_dimension == args.fractal_dimension);
  CHK(desc.gyration_radius == args.gyration_radius);
  CHK(desc.nintervals = args.nintervals);

  CHK(ssf_phase_rdgfa_get_interval(NULL, 0, &interval) == RES_BAD_ARG);
  CHK(ssf_phase_rdgfa_get_interval(phase, desc.nintervals+1, &interval)
    == RES_BAD_ARG);
  CHK(ssf_phase_rdgfa_get_interval(phase, 0, NULL) == RES_BAD_ARG);

  cumulative_prev = 0;
  FOR_EACH(i, 0, desc.nintervals) {
    double range[2];

    range[0] = PI/(double)desc.nintervals * (double)(i+0);
    range[1] = PI/(double)desc.nintervals * (double)(i+1);

    CHK(ssf_phase_rdgfa_get_interval(phase, i, &interval) == RES_OK);
    CHK(eq_eps(interval.range[0], range[0], fabs(range[0]*1.e-6)));
    CHK(eq_eps(interval.range[1], range[1], fabs(range[1]*1.e-6)));
    CHK(interval.cumulative > cumulative_prev);
    CHK(interval.cumulative <= 1.0);

    cumulative_prev = interval.cumulative;
  }

  ssp_ran_sphere_uniform(rng, wo, NULL);
  FOR_EACH(i, 0, NSAMPS) {
    double wi[3];
    double pdf;
    ssf_phase_sample(phase, rng, wo, wi, &pdf);
    CHK(eq_eps(pdf, ssf_phase_eval(phase, wo, wi), fabs(pdf*1.e-6)));
    CHK(d3_is_normalized(wi));

#if 0
    fprintf(stderr, "v %g %g %g\n", wi[0]*pdf, wi[1]*pdf, wi[2]*pdf);
    fprintf(stderr, "p %lu\n", (unsigned long)(i+1));
#endif
  }

  CHK(ssf_phase_ref_put(phase) == RES_OK);
  CHK(ssf_phase_ref_put(dummy) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
exit:
  return err;
error:
  err = -1;
  goto exit;
}

