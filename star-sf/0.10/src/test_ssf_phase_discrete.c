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

#include <star/ssp.h>

/* Pre-normalized discrete phase function */
static const struct ssf_discrete_item g_items[] = {
  {0.0000000000000000, 68.603348420512177},
  {1.7453292519943295e-2, 50.735150232030207},
  {3.4906585039886591e-2, 24.908999683188814},
  {5.2359877559829883e-2, 11.936459532538100},
  {6.9813170079773182e-2, 6.5845291580807999},
  {8.7266462599716474e-2, 4.1115681738409000},
  {0.10471975511965977, 2.7971255874985190},
  {0.12217304763960307, 2.0200365258652173},
  {0.13962634015954636, 1.5241311066143122},
  {0.15707963267948966, 1.1891144970876193},
  {0.17453292519943295, 0.95259719392666198},
  {0.26179938779914941, 0.40729722471207858},
  {0.34906585039886590, 0.22439173045102234},
  {0.52359877559829882, 9.8911118269091880e-2},
  {0.69813170079773179, 5.7013070175769544e-2},
  {0.87266462599716477, 3.8428151500227763e-2},
  {1.0471975511965976, 2.8819207088581208e-2},
  {1.2217304763960306, 2.3435147759515759e-2},
  {1.3962634015954636, 2.0323680045268303e-2},
  {1.5707963267948966, 1.8554414090107985e-2},
  {1.7453292519943295, 1.7631650380735577e-2},
  {1.9198621771937625, 1.7273221501888446e-2},
  {2.0943951023931953, 1.7265595355529996e-2},
  {2.2689280275926285, 1.7471501307208134e-2},
  {2.4434609527920612, 1.7768921015187674e-2},
  {2.6179938779914944, 1.8089219162242556e-2},
  {2.7925268031909272, 1.8356134284788293e-2},
  {2.8797932657906435, 1.8455274187448138e-2},
  {2.9670597283903604, 1.8531535651032636e-2},
  {2.9845130209103035, 1.8546787943749535e-2},
  {3.0019663134302466, 1.8554414090107985e-2},
  {3.0194196059501901, 1.8562040236466435e-2},
  {3.0368728984701332, 1.8569666382824885e-2},
  {3.0543261909900763, 1.8584918675541785e-2},
  {3.0717794835100198, 1.8638301700050933e-2},
  {3.0892327760299634, 1.9156879652425504e-2},
  {3.1066860685499069, 2.1879413902392029e-2},
  {3.1241393610698500, 2.8323507575281980e-2},
  {3.1415926535897931, 3.2632280267806027e-2}
};
static const size_t g_nitems = sizeof(g_items) / sizeof(*g_items);

struct context {
  const struct ssf_discrete_item* items;
  size_t nitems;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
get_item(const size_t id, struct ssf_discrete_item* item, void* context)
{
  const struct context* ctx = context;
  CHK(id < ctx->nitems);
  *item = ctx->items[id];
}

static void
test_setup(struct ssf_phase* discrete)
{
  struct ssf_discrete_setup_args args = SSF_DISCRETE_SETUP_ARGS_NULL;
  struct context ctx;
  struct ssf_phase* dummy = NULL;
  ASSERT(discrete);

  ctx.items = g_items;
  ctx.nitems = g_nitems;

  args.get_item = get_item;
  args.context = &ctx;
  args.nitems = g_nitems;

  CHK(ssf_phase_discrete_setup(NULL, &args) == RES_BAD_ARG);
  CHK(ssf_phase_discrete_setup(discrete, NULL) == RES_BAD_ARG);
  CHK(ssf_phase_discrete_setup(discrete, &args) == RES_OK);

  /* Invalid phase function type */
  CHK(ssf_phase_create(&mem_default_allocator, &phase_dummy, &dummy) == RES_OK);
  CHK(ssf_phase_discrete_setup(dummy, &args) == RES_BAD_ARG);
  CHK(ssf_phase_ref_put(dummy) == RES_OK);

  /* Invalid last #items */
  ctx.nitems = args.nitems = 1;
  CHK(ssf_phase_discrete_setup(discrete, &args) == RES_BAD_ARG);

  /* Last angle is not PI */
  ctx.nitems = args.nitems = g_nitems - 1;
  CHK(ssf_phase_discrete_setup(discrete, &args) == RES_BAD_ARG);

  /* First angle is not 0 */
  ctx.items = g_items + 1;
  CHK(ssf_phase_discrete_setup(discrete, &args) == RES_BAD_ARG);
}

static void
test_eval(struct ssf_phase* discrete, struct ssp_rng* rng)
{
  struct ssf_discrete_setup_args args = SSF_DISCRETE_SETUP_ARGS_NULL;
  struct context ctx;
  double wo[3];
  double wi[3];
  size_t i;

  ctx.items = g_items;
  ctx.nitems = g_nitems;
  args.get_item = get_item;
  args.context = &ctx;
  args.nitems = g_nitems;
  CHK(ssf_phase_discrete_setup(discrete, &args) == RES_OK);

  d3(wo, 0, 0,-1);
  d3(wi, 0, 0, 1);
  CHK(ssf_phase_eval(discrete, wo, wi) == g_items[0].value);
  d3(wo, 0, 0, 1);
  CHK(ssf_phase_eval(discrete, wo, wi) == g_items[g_nitems-1].value);

  FOR_EACH(i, 0, 10) {
    double cos_theta;
    double val;
    double ref;
    size_t iitem;

    ssp_ran_sphere_uniform(rng, wo, NULL);
    ssp_ran_sphere_uniform(rng, wi, NULL);
    val = ssf_phase_eval(discrete, wo, wi);

    cos_theta = d3_dot(d3_minus(wo, wo), wi);
    if(cos_theta == 0) {
      ref = g_items[0].value;
    } else {
      const double theta = acos(cos_theta);
      double u;
      /* Look for the phase function discrete items to consider regarding the
       * sampled wo and wi directions */
      FOR_EACH(iitem, 0, g_nitems) {
        if(g_items[iitem].theta >= theta) break;
      }
      ASSERT(iitem < g_nitems && iitem > 0);

      /* Compute the parameter of the linear interpolation */
      u  = cos_theta - cos(g_items[iitem-1].theta);
      u /= cos(g_items[iitem].theta) - cos(g_items[iitem-1].theta);

      ref = g_items[iitem-1].value + u*(g_items[iitem].value - g_items[iitem-1].value);
    }
    CHK(eq_eps(val, ref, 1.e-6));
  }
}

static void
test_sample(struct ssf_phase* discrete, struct ssp_rng* rng)
{
  struct ssf_discrete_setup_args args = SSF_DISCRETE_SETUP_ARGS_NULL;
  struct context ctx;
  double ref = 0;
  size_t iitem;
  size_t i;

  ctx.items = g_items;
  ctx.nitems = g_nitems;
  args.get_item = get_item;
  args.context = &ctx;
  args.nitems = g_nitems;
  CHK(ssf_phase_discrete_setup(discrete, &args) == RES_OK);

  FOR_EACH(iitem, 1, g_nitems) {
    const double mu0 = cos(g_items[iitem-1].theta);
    const double mu1 = cos(g_items[iitem-0].theta);
    const double phi0 = g_items[iitem-1].value;
    const double phi1 = g_items[iitem-0].value;
    const double delta_mu = mu0 - mu1;
    ref += (mu0*phi0 + mu1*phi1)/2 * delta_mu;
  }
  ref *= 2*PI;

  FOR_EACH(i, 0, 10) {
    const size_t N = 10000;
    double wo[3];
    double wi[3];
    double sum = 0;
    double sum2 = 0;
    double E = 0;
    double V = 0;
    double SE = 0;
    size_t ireal;
    ssp_ran_sphere_uniform(rng, wo, NULL);

    FOR_EACH(ireal, 0, N) {
      double w[3];
      double mu;
      double pdf;
      ssf_phase_sample(discrete, rng, wo, wi, &pdf);

      mu = d3_dot(d3_minus(w, wo), wi);
      CHK(pdf == ssf_phase_eval(discrete, wo, wi));

      sum += mu;
      sum2 += mu*mu;
    }
    E = sum / (double)N;
    V = sum2 / (double)N - E*E;
    SE = sqrt(V/(double)N);
    CHK(eq_eps(E, ref, 3*SE));
  }
}

/*******************************************************************************
 * Main function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct ssp_rng* rng = NULL;
  struct ssf_phase* discrete = NULL;
  (void)argc, (void)argv;

  CHK(ssf_phase_create
    (&mem_default_allocator, &ssf_phase_discrete, &discrete) == RES_OK);
  CHK(ssp_rng_create(&mem_default_allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);

  test_setup(discrete);
  test_eval(discrete, rng);
  test_sample(discrete, rng);

  CHK(ssf_phase_ref_put(discrete) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);
  return 0;
}
