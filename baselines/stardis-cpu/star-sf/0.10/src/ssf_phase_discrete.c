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

#define _POSIX_C_SOURCE 200112L /* nextafter */

#include "ssf.h"
#include "ssf_phase_c.h"

#include <star/ssp.h>

#include <rsys/algorithm.h>
#include <rsys/double3.h>
#include <rsys/dynamic_array_double.h>

#include <rsys_math.h> /* nextafter */

struct discrete_item {
  double cos_theta;
  double value;
};

/* Define the dynamic array of discrete items */
#define DARRAY_NAME discrete_item
#define DARRAY_DATA struct discrete_item
#include <rsys/dynamic_array.h>

struct discrete {
  struct darray_discrete_item items;
  struct darray_double cumulative;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE res_T
check_ssf_discrete_setup_args
  (const struct ssf_discrete_setup_args* args)
{
  struct ssf_discrete_item item;
  if(!args || args->nitems < 2 || !args->get_item)
    return RES_BAD_ARG;

  /* Invalid 1st angle */
  args->get_item(0, &item, args->context);
  if(item.theta != 0)
    return RES_BAD_ARG;

  /* Invalid last angle */
  args->get_item(args->nitems-1, &item, args->context);
  if(!eq_eps(item.theta, PI, 1.e-4))
    return RES_BAD_ARG;

  return RES_OK;
}

static res_T
setup_discrete_items
  (struct discrete* discrete,
   const struct ssf_discrete_setup_args* args)
{
  struct discrete_item* items = NULL;
  size_t i = 0;
  res_T res = RES_OK;
  ASSERT(discrete && args);

  res = darray_discrete_item_resize(&discrete->items, args->nitems);
  if(res != RES_OK) goto error;
  items = darray_discrete_item_data_get(&discrete->items);

  FOR_EACH(i, 0, args->nitems) {
    struct ssf_discrete_item item;
    args->get_item(i, &item, args->context);
    items[i].cos_theta = cos(item.theta);
    items[i].value = item.value;

    /* Angles must be sorted in ascending order */
    if(i > 0 && items[i].cos_theta > items[i-1].cos_theta) {
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
compute_cumulative(struct discrete* discrete)
{
  struct discrete_item* items = NULL;
  double* cumul = NULL;
  double alpha = 0;
  size_t n;
  size_t i;
  res_T res = RES_OK;
  ASSERT(discrete);

  n = darray_discrete_item_size_get(&discrete->items);
  ASSERT(n >= 2);

  res = darray_double_resize(&discrete->cumulative, n);
  if(res != RES_OK) goto error;

  items = darray_discrete_item_data_get(&discrete->items);
  cumul = darray_double_data_get(&discrete->cumulative);

  alpha = 0;
  cumul[0] = alpha;
  FOR_EACH(i, 1, n) {
    alpha += PI
    * (items[i-1].value + items[i].value)
    * (items[i-1].cos_theta - items[i].cos_theta);

    cumul[i] = alpha;
  }

  /* Ensure that the phase function is normalized */
  if(!eq_eps(alpha, 1, 1.e-6)) {
    const double rcp_alpha = 1.0 / alpha;
    FOR_EACH(i, 0, n) {
      items[i].value *= rcp_alpha;
      cumul[i] *= rcp_alpha;
    }
  }

exit:
  return res;
error:
  goto exit;
}

static INLINE int
cmp_discrete_item(const void* a, const void* b)
{
  const struct discrete_item* item;
  double key;
  ASSERT(a && b);

  key = *((double*)a);
  item = b;

  /* The array is sorted in ascending order of the angles while it stores the
   * cosine of the angles. In the following, we reverse the test to find the
   * first entry whose _angle_ is not less than the angle whose cosine is the
   * key sought. This ensures that the array is sorted in ascending order as
   * expected by the search_lower_bound routine */
  if(key < item->cos_theta) {
    return +1;
  } else if(key > item->cos_theta) {
    return -1;
  } else {
    return 0;
  }
}

static INLINE int
cmp_cumul(const void* a, const void* b)
{
  double key;
  double cumul;
  ASSERT(a && b);

  key = *((double*)a);
  cumul = *((double*)b);

  if(key < cumul) {
    return -1;
  } else if(key > cumul) {
    return +1;
  } else {
    return 0;
  }
}

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static res_T
discrete_init(struct mem_allocator* allocator, void* phase)
{
  struct discrete* discrete = phase;
  ASSERT(phase);
  darray_discrete_item_init(allocator, &discrete->items);
  darray_double_init(allocator, &discrete->cumulative);
  return RES_OK;
}

static void
discrete_release(void* phase)
{
  struct discrete* discrete = phase;
  ASSERT(phase);
  darray_discrete_item_release(&discrete->items);
  darray_double_release(&discrete->cumulative);
}

static double
discrete_eval(void* phase, const double wo[3], const double wi[3])
{
  const struct discrete* discrete = phase;
  const struct discrete_item* items = NULL;
  double v[3];
  double cos_theta = 0;
  double value = 0;
  size_t nitems = 0;
  ASSERT(phase && wo && wi);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(wi));

  d3_minus(v, wo);
  cos_theta = d3_dot(v, wi);

  items = darray_discrete_item_cdata_get(&discrete->items);
  nitems = darray_discrete_item_size_get(&discrete->items);
  ASSERT(nitems >= 2);

  if(eq_eps(cos_theta, 1, 1.e-6)) {
    value = items[0].value;
  } else if(eq_eps(cos_theta, 0, 1.e-6)) {
    value = items[nitems-1].value;
  } else {
    const struct discrete_item* found_item = NULL;
    size_t iitem = 0;
    double u = 0;

    /* Search for the discrete item whose angle is greater or equal to the
     * angle between wo and wi */
    found_item = search_lower_bound
      (&cos_theta, items, nitems, sizeof(*items), cmp_discrete_item);
    iitem = (size_t)(found_item - items);
    ASSERT(found_item && iitem > 0 && iitem < nitems);
    ASSERT(cos_theta < items[iitem-1].cos_theta);
    ASSERT(cos_theta >=  items[iitem].cos_theta);

    /* Linearly interpolate the phase function value */
    u =
      (cos_theta - items[iitem].cos_theta)
    / (items[iitem-1].cos_theta - items[iitem].cos_theta);
    value = items[iitem].value + u*(items[iitem-1].value - items[iitem].value);
  }

  return value;
}

static void
discrete_sample
  (void* phase,
   struct ssp_rng* rng,
   const double wo[3],
   double wi[3],
   double *pdf)
{
  const struct discrete* discrete = phase;
  const struct discrete_item* items = NULL;
  const double* cumul = NULL;
  double frame[9];
  double w[3];
  double cos_theta = 0;
  double sin_theta = 0;
  double phi = 0;
  size_t ncumul = 0;
  size_t i = 0;
  double r = 0;
  ASSERT(phase && rng && wo && wi);
  ASSERT(d3_is_normalized(wo));

  items = darray_discrete_item_cdata_get(&discrete->items);
  cumul = darray_double_cdata_get(&discrete->cumulative);
  ncumul = darray_double_size_get(&discrete->cumulative);
  ASSERT(ncumul == darray_discrete_item_size_get(&discrete->items));

  /* Sample r in [0, 1] */
  r = ssp_rng_uniform_double(rng, 0, nextafter(1, 2));
  if(r == 0) {
    cos_theta = 1;
    sin_theta = 0;
  } else if(r ==1) {
    cos_theta =-1;
    sin_theta = 0;
  } else {
    double* found_cumul = NULL;
    double u;

    /* Search for the first entry in the cumulative that is greater than or equal
     * to the sampled number in [0, 1] */
    found_cumul = search_lower_bound(&r, cumul, ncumul, sizeof(*cumul), cmp_cumul);

    i = (size_t)(found_cumul - cumul);
    ASSERT(found_cumul && i > 0 && i < ncumul);

    /* Linearly interpolate the sampled cos_theta */
    u = (r - cumul[i-1]) / (cumul[i] - cumul[i-1]);
    ASSERT(r >= cumul[i-1] && r < cumul[i]);
    cos_theta = items[i-1].cos_theta + u*(items[i].cos_theta - items[i-1].cos_theta);

    /* Compute the sinus of theta from its cosine */
    sin_theta = sqrt(1 - cos_theta*cos_theta);
  }

  /* Uniformly sample phi in [0, 2PI[ */
  phi = ssp_rng_uniform_double(rng, 0, 2*PI);

  /* Calculate the Cartesian coordinates of the direction in the local
   * coordinate system of the phase function */
  wi[0] = cos(phi) * sin_theta;
  wi[1] = sin(phi) * sin_theta;
  wi[2] = cos_theta;

  /* Calculate the transformation matrix from the local coordinate system of
   * the phase function to the absolute coordinate system. Note that by
   * convention in Star-SF the directions point outward from the scattering
   * position. That's why we reverse 'wo' to point inside the scattering
   * position */
  d33_basis(frame, d3_minus(w, wo));
  d33_muld3(wi, frame, wi);
  ASSERT(eq_eps(d3_dot(wi, w), cos_theta, fabs(cos_theta*1.e-6)));

  if(pdf) *pdf = discrete_eval(phase, wo, wi);
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_phase_type ssf_phase_discrete = {
  discrete_init,
  discrete_release,
  discrete_sample,
  discrete_eval,
  discrete_eval,
  sizeof(struct discrete),
  ALIGNOF(struct discrete)
};

res_T
ssf_phase_discrete_setup
  (struct ssf_phase* phase,
   const struct ssf_discrete_setup_args* args)
{
  struct discrete* discrete = NULL;
  res_T res = RES_OK;

  if(!phase || !PHASE_TYPE_EQ(&phase->type, &ssf_phase_discrete)) {
    res = RES_BAD_ARG;
    goto error;
  }
  res = check_ssf_discrete_setup_args(args);
  if(res != RES_OK) goto error;

  discrete = phase->data;

  res = setup_discrete_items(discrete, args);
  if(res != RES_OK) goto error;
  res = compute_cumulative(discrete);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  if(discrete) {
    darray_discrete_item_purge(&discrete->items);
    darray_double_purge(&discrete->cumulative);
  }
  goto exit;
}
