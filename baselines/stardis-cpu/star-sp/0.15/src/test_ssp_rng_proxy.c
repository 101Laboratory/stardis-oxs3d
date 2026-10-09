/* Copyright (C) 2015-2025 |Méso|Star> (contact@meso-star.com)
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

#include "ssp.h"
#include "test_ssp_utils.h"

#define NRANDS 2000000
#define COUNT 4605307

static void
test_creation(void)
{
  struct ssp_rng_proxy* proxy;
  enum ssp_rng_type type;

  CHK(ssp_rng_proxy_create(NULL, -1, 0, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create(NULL, SSP_RNG_MT19937_64, 0, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create(NULL, -1, 4, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create(NULL, SSP_RNG_MT19937_64, 4, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create(NULL, -1, 0, &proxy) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create(NULL, SSP_RNG_MT19937_64, 0, &proxy) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create(NULL, -1, 4, &proxy) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create(NULL, SSP_RNG_MT19937_64, 4, &proxy) == RES_OK);

  CHK(ssp_rng_proxy_get_type(NULL, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_get_type(proxy, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_get_type(NULL, &type) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_get_type(proxy, &type) == RES_OK);

  CHK(type != SSP_RNG_KISS);
  CHK(type == SSP_RNG_MT19937_64);

  CHK(ssp_rng_proxy_ref_get(NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_ref_get(proxy) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);

  CHK(ssp_rng_proxy_create
    (&mem_default_allocator, SSP_RNG_THREEFRY, 1, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_get_type(proxy, &type) == RES_OK);
  CHK(type == SSP_RNG_THREEFRY);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);

  CHK(ssp_rng_proxy_create(NULL, SSP_RNG_KISS, 4, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);
}

static void
test_creation2(void)
{
  struct ssp_rng_proxy_create2_args args = SSP_RNG_PROXY_CREATE2_ARGS_NULL;
  struct ssp_rng_proxy* proxy;

  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_BAD_ARG);

  args.type = SSP_RNG_MT19937_64;
  args.sequence_offset = 0;
  args.sequence_size = 32;
  args.sequence_pitch = 32;
  args.nbuckets = 1;
  CHK(ssp_rng_proxy_create2(NULL, &args, NULL) == RES_BAD_ARG);

  args.type = SSP_RNG_TYPE_NULL;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_BAD_ARG);
  args.type = SSP_RNG_MT19937_64;
  args.sequence_size= 0;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_BAD_ARG);
  args.sequence_size = 32;
  args.sequence_pitch = 0;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_BAD_ARG);
  args.sequence_pitch = 31;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_BAD_ARG);
  args.sequence_pitch = 32;
  args.nbuckets = 0;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_BAD_ARG);
  args.nbuckets = 33;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_BAD_ARG);
  args.nbuckets = 1;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);

  args.nbuckets = 4;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);

  args.nbuckets = 5;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);

  args.nbuckets = 32;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);

  args.sequence_offset = 1024;
  args.nbuckets = 4;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);

  args.type = SSP_RNG_KISS;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);
}

static void
test_rng_from_proxy(void)
{
  enum ssp_rng_type type;
  struct ssp_rng_proxy* proxy;
  struct ssp_rng* rng[4];
  uint64_t r[4];
  size_t i, j, k;

  CHK(ssp_rng_proxy_create(NULL, SSP_RNG_MT19937_64, 4, &proxy) == RES_OK);

  CHK(ssp_rng_proxy_create_rng(NULL, 0, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create_rng(proxy, 0, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create_rng(NULL, 0, &rng[0]) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create_rng(proxy, 0, &rng[0]) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 0, &rng[1]) == RES_BAD_ARG);
  CHK(ssp_rng_ref_put(rng[0]) == RES_OK);

  CHK(ssp_rng_proxy_create_rng(proxy, 4, &rng[0]) == RES_BAD_ARG);

  FOR_EACH(i, 0, 4) CHK(ssp_rng_proxy_create_rng(proxy, i, &rng[i]) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);

  CHK(ssp_rng_get_type(rng[0], &type) == RES_OK);
  FOR_EACH(i, 1, 4) {
    enum ssp_rng_type type2;
    CHK(ssp_rng_get_type(rng[i], &type2) == RES_OK);
    CHK(type == type2);
  }

  FOR_EACH(i, 0, NRANDS) {
    FOR_EACH(j, 0, 4) {
      r[j] = ssp_rng_get(rng[j]);
      FOR_EACH(k, 0, j) CHK(r[k] != r[j]);
    }
  }

  FOR_EACH(i, 0, 4) CHK(ssp_rng_ref_put(rng[i]) == RES_OK);
}

static void
test_rng_from_proxy2(void)
{
  struct ssp_rng_proxy_create2_args args = SSP_RNG_PROXY_CREATE2_ARGS_NULL;
  struct ssp_rng_proxy* proxy[2];
  struct ssp_rng* rng[4];
  const size_t block_sz = 99;
  const size_t nrands = 1000;
  uint64_t* r[4];
  size_t i, j;

  CHK((r[0] = mem_alloc(sizeof(uint64_t)*nrands)) != NULL);
  CHK((r[1] = mem_alloc(sizeof(uint64_t)*nrands)) != NULL);
  CHK((r[2] = mem_alloc(sizeof(uint64_t)*nrands)) != NULL);
  CHK((r[3] = mem_alloc(sizeof(uint64_t)*nrands)) != NULL);

  args.type = SSP_RNG_MT19937_64;
  args.sequence_size = block_sz;
  args.sequence_pitch = block_sz;
  args.nbuckets = 4;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy[0]) == RES_OK);

  CHK(ssp_rng_proxy_create_rng(proxy[0], 0, &rng[0]) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy[0], 1, &rng[1]) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy[0], 2, &rng[2]) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy[0], 3, &rng[3]) == RES_OK);

  /* Generate random numbers */
  FOR_EACH(i, 0, nrands) {
    r[0][i] = ssp_rng_get(rng[0]);
    r[1][i] = ssp_rng_get(rng[1]);
    r[2][i] = ssp_rng_get(rng[2]);
    r[3][i] = ssp_rng_get(rng[3]);
  }

  /* Check that each RNG generate a unique sequence of random numbers */
  FOR_EACH(i, 0, nrands) {
    FOR_EACH(j, 0, nrands) {
      CHK(r[0][i] != r[1][j]);
      CHK(r[0][i] != r[2][j]);
      CHK(r[0][i] != r[3][j]);
      CHK(r[1][i] != r[2][j]);
      CHK(r[1][i] != r[3][j]);
      CHK(r[2][i] != r[3][j]);
    }
  }

  CHK(ssp_rng_proxy_ref_put(proxy[0]) == RES_OK);
  CHK(ssp_rng_ref_put(rng[0]) == RES_OK);
  CHK(ssp_rng_ref_put(rng[1]) == RES_OK);
  CHK(ssp_rng_ref_put(rng[2]) == RES_OK);
  CHK(ssp_rng_ref_put(rng[3]) == RES_OK);

  args.type = SSP_RNG_MT19937_64;
  args.sequence_size = block_sz;
  args.sequence_pitch = 2*block_sz;
  args.nbuckets = 2;

  args.sequence_offset = 0;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy[0]) == RES_OK);
  args.sequence_offset = block_sz;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy[1]) == RES_OK);

  CHK(ssp_rng_proxy_create_rng(proxy[0], 0, &rng[0]) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy[0], 1, &rng[1]) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy[1], 0, &rng[2]) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy[1], 1, &rng[3]) == RES_OK);

  /* Generate random numbers */
  FOR_EACH(i, 0, nrands) {
    r[0][i] = ssp_rng_get(rng[0]);
    r[1][i] = ssp_rng_get(rng[1]);
    r[2][i] = ssp_rng_get(rng[2]);
    r[3][i] = ssp_rng_get(rng[3]);
  }

  /* Check that each RNG generate a unique sequence of random numbers */
  FOR_EACH(i, 0, nrands) {
    FOR_EACH(j, 0, nrands) {
      CHK(r[0][i] != r[1][j]);
      CHK(r[0][i] != r[2][j]);
      CHK(r[0][i] != r[3][j]);
      CHK(r[1][i] != r[2][j]);
      CHK(r[1][i] != r[3][j]);
      CHK(r[2][i] != r[3][j]);
    }
  }

  CHK(ssp_rng_proxy_ref_put(proxy[0]) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy[1]) == RES_OK);
  CHK(ssp_rng_ref_put(rng[0]) == RES_OK);
  CHK(ssp_rng_ref_put(rng[1]) == RES_OK);
  CHK(ssp_rng_ref_put(rng[2]) == RES_OK);
  CHK(ssp_rng_ref_put(rng[3]) == RES_OK);

  mem_rm(r[0]);
  mem_rm(r[1]);
  mem_rm(r[2]);
  mem_rm(r[3]);
}

static void
test_multi_proxies(void)
{
  struct ssp_rng_proxy* proxy1;
  struct ssp_rng_proxy* proxy2;
  struct ssp_rng* rng1;
  struct ssp_rng* rng2;
  struct ssp_rng* rng3;
  size_t i;

  CHK(ssp_rng_proxy_create(NULL, SSP_RNG_THREEFRY, 1, &proxy1) == RES_OK);
  CHK(ssp_rng_proxy_create(NULL, SSP_RNG_THREEFRY, 1, &proxy2) == RES_OK);

  CHK(ssp_rng_proxy_create_rng(proxy1, 0, &rng1) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy2, 0, &rng2) == RES_OK);
  ssp_rng_create(NULL, SSP_RNG_THREEFRY, &rng3);

  CHK(ssp_rng_proxy_ref_put(proxy1) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy2) == RES_OK);

  ssp_rng_discard(rng1, COUNT);
  ssp_rng_discard(rng3, COUNT+1);
  FOR_EACH(i, 0, COUNT) ssp_rng_get(rng2);

  CHK(ssp_rng_get(rng1) == ssp_rng_get(rng2));
  CHK(ssp_rng_get(rng1) == ssp_rng_get(rng3));

  CHK(ssp_rng_ref_put(rng1) == RES_OK);
  CHK(ssp_rng_ref_put(rng2) == RES_OK);
  CHK(ssp_rng_ref_put(rng3) == RES_OK);
}

static void
test_proxy_from_rng(void)
{
  struct ssp_rng_proxy_create2_args args = SSP_RNG_PROXY_CREATE2_ARGS_NULL;
  struct ssp_rng_proxy* proxy;
  struct ssp_rng* rng[4];
  enum ssp_rng_type type;
  uint64_t r[4];
  size_t i, j, k;

  CHK(ssp_rng_create(NULL, SSP_RNG_THREEFRY, &rng[0]) == RES_OK);
  CHK(ssp_rng_discard(rng[0], COUNT) == RES_OK);

  CHK(ssp_rng_proxy_create_from_rng(NULL, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create_from_rng(NULL, rng[0], 0, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create_from_rng(NULL, NULL, 1, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create_from_rng(NULL, rng[0], 1, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create_from_rng(NULL, NULL, 0, &proxy) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create_from_rng(NULL, rng[0], 0, &proxy) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create_from_rng(NULL, NULL, 1, &proxy) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create_from_rng(NULL, rng[0], 1, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 0, &rng[1]) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 0, &rng[2]) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_create_rng(proxy, 1, &rng[2]) == RES_BAD_ARG);

  CHK(ssp_rng_get(rng[0]) == ssp_rng_get(rng[1]));

  CHK(ssp_rng_ref_put(rng[1]) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);

  CHK(ssp_rng_proxy_create_from_rng(NULL, rng[0], 4, &proxy) == RES_OK);
  CHK(ssp_rng_ref_put(rng[0]) == RES_OK);
  FOR_EACH(i, 0, 4) CHK(ssp_rng_proxy_create_rng(proxy, i, &rng[i]) == RES_OK);
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);

  CHK(ssp_rng_get_type(rng[0], &type) == RES_OK);
  FOR_EACH(i, 1, 4) {
    enum ssp_rng_type type2;
    CHK(ssp_rng_get_type(rng[i], &type2) == RES_OK);
    CHK(type == type2);
  }

  FOR_EACH(i, 0, NRANDS) {
    FOR_EACH(j, 0, 4) {
      r[j] = ssp_rng_get(rng[j]);
      FOR_EACH(k, 0, j) CHK(r[k] != r[j]);
    }
  }

  FOR_EACH(i, 1, 4) CHK(ssp_rng_ref_put(rng[i]) == RES_OK);

  args.rng = rng[0];
  args.sequence_offset = 0;
  args.sequence_size = 4000;
  args.sequence_pitch = 4000;
  args.nbuckets = 4;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_BAD_ARG);
  CHK(ssp_rng_ref_put(rng[0]) == RES_OK);

  CHK(ssp_rng_create(NULL, SSP_RNG_THREEFRY, &rng[0]) == RES_OK);
  CHK(ssp_rng_discard(rng[0], 100) == RES_OK);

  args.rng = rng[0];
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_ref_put(rng[0]) == RES_OK);
  FOR_EACH(i, 0, 4) {
    CHK(ssp_rng_proxy_create_rng(proxy, i, &rng[i]) == RES_OK);
  }

  FOR_EACH(i, 0, NRANDS) {
    FOR_EACH(j, 0, 4) {
      r[j] = ssp_rng_get(rng[j]);
      FOR_EACH(k, 0, j) CHK(r[k] != r[j]);
    }
  }

  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);
  FOR_EACH(i, 0, 4) CHK(ssp_rng_ref_put(rng[i]) == RES_OK);
}

static void
test_read(void)
{
  struct ssp_rng_proxy* proxy;
  struct ssp_rng* rng;
  struct ssp_rng* rng1[4];
  FILE* stream;
  uint64_t r[4];
  size_t i, j, k;

  CHK(ssp_rng_create(NULL, SSP_RNG_MT19937_64, &rng) == RES_OK);
  CHK(ssp_rng_discard(rng, COUNT) == RES_OK);

  /* Create a RNG state */
  stream = tmpfile();
  CHK(stream != NULL);
  CHK(ssp_rng_write(rng, stream) == RES_OK);
  rewind(stream);

  /* Create a proxy from the RNG state */
  CHK(ssp_rng_proxy_create(NULL, SSP_RNG_MT19937_64, 4, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_read(NULL, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_read(proxy, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_read(NULL, stream) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_read(proxy, stream) == RES_OK);

  /* Create the list of RNG managed by the proxy */
  FOR_EACH(i, 0, 4) CHK(ssp_rng_proxy_create_rng(proxy, i, &rng1[i]) == RES_OK);

  /* Check the random number sequences */
  r[0] = ssp_rng_get(rng);
  CHK(r[0] == ssp_rng_get(rng1[0]));
  CHK(r[0] != ssp_rng_get(rng1[1]));
  CHK(r[0] != ssp_rng_get(rng1[2]));
  CHK(r[0] != ssp_rng_get(rng1[3]));
  FOR_EACH(i, 0, NRANDS) {
    FOR_EACH(j, 0, 4) {
      r[j] = ssp_rng_get(rng1[j]);
      FOR_EACH(k, 0, j) CHK(r[k] != r[j]);
    }
  }

  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);
  FOR_EACH(i, 0, 4) CHK(ssp_rng_ref_put(rng1[i]) == RES_OK);

  CHK(fclose(stream) == 0);
}

static void
test_read_with_cached_states(void)
{
  struct ssp_rng_proxy_create2_args args = SSP_RNG_PROXY_CREATE2_ARGS_NULL;
  struct ssp_rng_proxy* proxy;
  struct ssp_rng* rng;
  struct ssp_rng* rng0;
  struct ssp_rng* rng1;
  FILE* stream;
  size_t iseq;
  size_t i;

  CHK(ssp_rng_create(NULL, SSP_RNG_MT19937_64, &rng) == RES_OK);

  /* Create a proxy with a very small sequence size of 2 RNs per bucket in
   * order to maximize the number of cached states. Furthermore, use the
   * Mersenne Twister RNG type since its internal state is the greatest one of
   * the proposed builtin type and is thus the one that will fill quickly the
   * cache stream. */
  args.type = SSP_RNG_MT19937_64;
  args.sequence_size = 10;
  args.sequence_pitch = 10;
  args.nbuckets = 2;
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 0, &rng0) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 1, &rng1) == RES_OK);

  /* Check RNG states */
  FOR_EACH(i, 0, args.sequence_size) {
    if(i < args.sequence_size/2) {
      CHK(ssp_rng_get(rng) == ssp_rng_get(rng0));
    } else {
      CHK(ssp_rng_get(rng) == ssp_rng_get(rng1));
    }
  }

  /* Discard several RNs for the 1st RNG to cache several states for the rng1 */
  CHK(ssp_rng_discard(rng0, 103) == RES_OK);

  /* Generate a RNG state */
  stream = tmpfile();
  CHK(stream != NULL);
  CHK(ssp_rng_discard(rng, 100000) == RES_OK);
  CHK(ssp_rng_write(rng, stream) == RES_OK);
  rewind(stream);

  /* Setup the proxy state and check that the RNGs managed by the proxy now use
   * the new state properly, even though RNG states were previously cached */
  CHK(ssp_rng_proxy_read(proxy, stream) == RES_OK);
  FOR_EACH(iseq, 0, 100) {
    FOR_EACH(i, 0, args.sequence_size) {
      if(i < args.sequence_size/2) {
        CHK(ssp_rng_get(rng) == ssp_rng_get(rng0));
      } else {
        CHK(ssp_rng_get(rng) == ssp_rng_get(rng1));
      }
    }
  }

  /* Discard several RNs for the first RNG only to make under pressure the
   * cache stream of 'rng1'. The cache memory limit is 32 MB maximum and the
   * size of a Mersenne Twister RNG state is greater than 6 KB. Consquently,
   * ~5500 RNG states will exceed the cache stream, i.e. 5500*2 = 11000
   * random generations (since there is 2 RNs per bucket). Above this limit,
   * 'rng1' will not rely anymore on the proxy RNG to manage its state. */
  CHK(ssp_rng_discard(rng0, 20000) == RES_OK);

  /* Generate a new RNG state */
  rewind(stream);
  CHK(ssp_rng_discard(rng, 100000) == RES_OK);
  CHK(ssp_rng_write(rng, stream) == RES_OK);
  rewind(stream);

  /* Now setup a new proxy state and check that the RNGs managed by the proxy
   * correctly use the new state */
  CHK(ssp_rng_proxy_read(proxy, stream) == RES_OK);
  FOR_EACH(iseq, 0, 100) {
    FOR_EACH(i, 0, args.sequence_size) {
      if(i < args.sequence_size/2) {
        CHK(ssp_rng_get(rng) == ssp_rng_get(rng0));
      } else {
        CHK(ssp_rng_get(rng) == ssp_rng_get(rng1));
      }
    }
  }

  /* Discard several RNs for the 1st RNG to cache several states for the rng1 */
  CHK(ssp_rng_discard(rng0, 100) == RES_OK);

  /* Finally verify that the cache was reset on proxy read */
  FOR_EACH(iseq, 0, 100) {
    FOR_EACH(i, 0, args.sequence_size) {
      if(i < args.sequence_size/2) {
        CHK(ssp_rng_discard(rng, 1) == RES_OK);
      } else {
        CHK(ssp_rng_get(rng) == ssp_rng_get(rng1));
      }
    }
  }

  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);
  CHK(ssp_rng_ref_put(rng0) == RES_OK);
  CHK(ssp_rng_ref_put(rng1) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  CHK(fclose(stream) == 0);
}

static void
test_write(void)
{
  struct ssp_rng_proxy* proxy;
  struct ssp_rng* rng0;
  struct ssp_rng* rng1;
  FILE* stream;
  uint64_t r;

  stream = tmpfile();
  CHK(stream != NULL);

  CHK(ssp_rng_proxy_create(NULL, SSP_RNG_RANLUX48, 1, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 0, &rng0) == RES_OK);

  CHK(ssp_rng_proxy_write(NULL, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_write(proxy, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_write(NULL, stream) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_write(proxy, stream) == RES_OK);

  CHK(ssp_rng_create(NULL, SSP_RNG_RANLUX48, &rng1) == RES_OK);
  r = ssp_rng_get(rng0);
  CHK(r == ssp_rng_get(rng1));

  rewind(stream);
  CHK(ssp_rng_read(rng1, stream) == RES_OK);
  CHK(r == ssp_rng_get(rng1));
  CHK(ssp_rng_get(rng0) == ssp_rng_get(rng1));

  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);
  CHK(ssp_rng_ref_put(rng0) == RES_OK);
  CHK(ssp_rng_ref_put(rng1) == RES_OK);

  CHK(fclose(stream) == 0);
}

static void
test_cache(void)
{
  struct ssp_rng_proxy_create2_args args = SSP_RNG_PROXY_CREATE2_ARGS_NULL;
  struct ssp_rng_proxy* proxy;
  struct ssp_rng* rng0;
  struct ssp_rng* rng1;
  struct ssp_rng* rng;
  const size_t nrands = 20000;
  size_t i;

  /* Create a proxy with a very small sequence size of 2 RNs per bucket in
   * order to maximize the number of cached states. Furthermore, use the
   * Mersenne Twister RNG type since its internal state is the greatest one of
   * the proposed builtin type and is thus the one that will fill quickly the
   * cache stream. */
  args.type = SSP_RNG_MT19937_64;
  args.sequence_size = 4;
  args.sequence_pitch = 4;
  args.nbuckets = 2;

  /* Simply test that the RNs generated by the proxy are the same than the
   * ones generated by a regular RNG. Since each RNG invocation are
   * interleaved, the cache pressure is very low, at most 1 RNG state is
   * cached. */
  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 0, &rng0) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 1, &rng1) == RES_OK);
  CHK(ssp_rng_create(NULL, SSP_RNG_MT19937_64, &rng) == RES_OK);
  FOR_EACH(i, 0, nrands*2) {
    if((i / 2) % 2) {
      CHK(ssp_rng_get(rng1) == ssp_rng_get(rng));
    } else {
      CHK(ssp_rng_get(rng0) == ssp_rng_get(rng));
    }
  }
  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);
  CHK(ssp_rng_ref_put(rng0) == RES_OK);
  CHK(ssp_rng_ref_put(rng1) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 0, &rng0) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 1, &rng1) == RES_OK);
  CHK(ssp_rng_create(NULL, SSP_RNG_MT19937_64, &rng) == RES_OK);

  /* Generate several RNs for the first RNG only to make under pressure the
   * cache stream of 'rng1'. The cache memory limit is 32 MB maximum and the
   * size of a Mersenne Twister RNG state is greater than 6 KB. Consquently,
   * ~5500 RNG states will exceed the cache stream, i.e. 5500*2 = 11000
   * random generations (since there is 2 RNs per bucket). Above this limit,
   * 'rng1' will not rely anymore on the proxy RNG to manage its state.
   *
   * For both RNGs, ensure that the generated RNs are the expected ones by
   * comparing them to the ones generated by a regular RNG */
  FOR_EACH(i, 0, nrands) {
    CHK(ssp_rng_get(rng0) == ssp_rng_get(rng));
    if(i % 2) CHK(ssp_rng_discard(rng, 2) == RES_OK);
  }
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  /* Check the cache mechanisme of rng1 */
  CHK(ssp_rng_create(NULL, SSP_RNG_MT19937_64, &rng) == RES_OK);
  CHK(ssp_rng_discard(rng, 2) == RES_OK);
  FOR_EACH(i, 0, nrands) {
    CHK(ssp_rng_get(rng1) == ssp_rng_get(rng));
    if(i % 2) CHK(ssp_rng_discard(rng, 2) == RES_OK);
  }

  CHK(ssp_rng_ref_put(rng) == RES_OK);

  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);
  CHK(ssp_rng_ref_put(rng0) == RES_OK);
  CHK(ssp_rng_ref_put(rng1) == RES_OK);
}


static void
test_sequence(void)
{
  struct ssp_rng_proxy_create2_args args = SSP_RNG_PROXY_CREATE2_ARGS_NULL;
  struct ssp_rng_proxy* proxy;
  struct ssp_rng* rng0;
  struct ssp_rng* rng1;
  struct ssp_rng* rng;
  size_t id;
  size_t iseq;
  size_t i;

  args.type = SSP_RNG_MT19937_64;
  args.sequence_offset = 0;
  args.sequence_size = 16;
  args.sequence_pitch = 16;
  args.nbuckets = 2;

  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 0, &rng0) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 1, &rng1) == RES_OK);
  CHK(ssp_rng_create(NULL, SSP_RNG_MT19937_64, &rng) == RES_OK);

  CHK(ssp_rng_proxy_get_sequence_id(NULL, &id) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_get_sequence_id(proxy, NULL) == RES_BAD_ARG);

  /* Check that directly after its creation, the proxy has a the sequence index
   * of SSP_SEQUENCE_ID_NONE */
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == SSP_SEQUENCE_ID_NONE);

  /* Generate 4 sequences of random numbers and check the progression of the
   * sequence index */
  FOR_EACH(iseq, 0, 4) {
    FOR_EACH(i, 0, args.sequence_size) {
      if(i < args.sequence_size/2) {
        CHK(ssp_rng_get(rng) == ssp_rng_get(rng0));
      } else {
        CHK(ssp_rng_get(rng) == ssp_rng_get(rng1));
      }
      CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
      CHK(id == iseq);
    }
  }

  /* Generate 1 more RN to go to the next sequence. Then, discard the remaining
   * random numbers of the first RNG and check the the sequence is the same
   * until a new RN is discarded */
  CHK(ssp_rng_get(rng) == ssp_rng_get(rng0));
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 4);
  CHK(ssp_rng_discard(rng0, args.sequence_size/2 - 1) == RES_OK);
  CHK(ssp_rng_discard(rng,  args.sequence_size/2 - 1) == RES_OK);
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 4);
  CHK(ssp_rng_discard(rng0, 1) == RES_OK);
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 5);

  /* Check the random sequence of the second RNG */
  FOR_EACH(i, 0, args.sequence_size/2) {
    CHK(ssp_rng_get(rng) == ssp_rng_get(rng1));
  }

  /* Check the sequence of the first RNG */
  CHK(ssp_rng_discard(rng, 1) == RES_OK);
  CHK(ssp_rng_get(rng) == ssp_rng_get(rng0));

  /* Discard the random numbers of the current sequence associated to the
   * second RNG. Check the the sequence is the same until a new random number
   * is discarded */
  CHK(ssp_rng_discard(rng1, args.sequence_size/2) == RES_OK);
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 5);
  CHK(ssp_rng_discard(rng1, 1) == RES_OK);
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 6);

  /* Synchronise the RNGs through sequence 6 */
  CHK(ssp_rng_discard(rng0, args.sequence_size/2-2) == RES_OK);
  CHK(ssp_rng_discard(rng, args.sequence_size-2) == RES_OK);
  FOR_EACH(i, 0, args.sequence_size) {
    if(i < args.sequence_size/2) {
      CHK(ssp_rng_get(rng) == ssp_rng_get(rng0));
    } else if(i == args.sequence_size/2) {
      CHK(ssp_rng_discard(rng, 1) == RES_OK);
    } else {
      CHK(ssp_rng_get(rng) == ssp_rng_get(rng1));
    }
  }
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 6);

  /* Move in sequence 7 */
  CHK(ssp_rng_discard(rng0, 3) == RES_OK);
  CHK(ssp_rng_discard(rng1, 7) == RES_OK);
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 7);

  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);
  CHK(ssp_rng_ref_put(rng1) == RES_OK);
  CHK(ssp_rng_ref_put(rng0) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);
}

static void
test_flush_sequence()
{
  struct ssp_rng_proxy_create2_args args = SSP_RNG_PROXY_CREATE2_ARGS_NULL;
  struct ssp_rng_proxy* proxy;
  struct ssp_rng* rng0;
  struct ssp_rng* rng1;
  struct ssp_rng* rng;
  size_t id;
  size_t i;

  args.type = SSP_RNG_MT19937_64;
  args.sequence_offset = 0;
  args.sequence_size = 10;
  args.sequence_pitch = 10;
  args.nbuckets = 2;

  CHK(ssp_rng_proxy_create2(NULL, &args, &proxy) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 0, &rng0) == RES_OK);
  CHK(ssp_rng_proxy_create_rng(proxy, 1, &rng1) == RES_OK);
  CHK(ssp_rng_create(NULL, SSP_RNG_MT19937_64, &rng) == RES_OK);

  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == SSP_SEQUENCE_ID_NONE);
  CHK(ssp_rng_get(rng) == ssp_rng_get(rng0));

  /* Flush 0 sequence, i.e. do nothing */
  CHK(ssp_rng_proxy_flush_sequences(NULL, 0) == RES_BAD_ARG);
  CHK(ssp_rng_proxy_flush_sequences(proxy, 0) == RES_OK);
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 0);
  CHK(ssp_rng_get(rng) == ssp_rng_get(rng0));

  /* Discard several numbers of the rng1 to fill the cache of rng0. We will
   * thus be able to check the cache clearing when we will flush sequences */
  CHK(ssp_rng_discard(rng1, 314) == RES_OK);
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 62);

  /* Flush the remaining random number of the current sequence */
  CHK(ssp_rng_proxy_flush_sequences(proxy, 1) == RES_OK);

  /* We are still in the sequence 62 but without any available random numbers */
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 62);

  /* Synchronize RNG with the current state of the proxy */
  CHK(ssp_rng_discard
    (rng, args.sequence_size*63 /*#random number to discard */
     - 2/*... minus the 2 already consumed random numbers*/) == RES_OK);

  /* Check RNG states */
  FOR_EACH(i, 0, args.sequence_size) {
    if(i < args.sequence_size/2) {
      CHK(ssp_rng_get(rng) == ssp_rng_get(rng0));
    } else {
      CHK(ssp_rng_get(rng) == ssp_rng_get(rng1));
    }
    CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
    CHK(id == 63);
  }

  /* Fill the cache of the rng1 */
  CHK(ssp_rng_discard(rng0, 1013) == RES_OK);
  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 63 + 203/*ceil(1013/(args.sequence_size*0.5))*/);

  /* Discard several sequences */
  CHK(ssp_rng_proxy_flush_sequences(proxy, 314) == RES_OK);

  /* Synchronize RNG with the current state of the proxy */
  CHK(ssp_rng_discard(rng, args.sequence_size*(203 + 314 - 1)) == RES_OK);

  /* Check RNG states */
  FOR_EACH(i, 0, args.sequence_size) {
    if(i < args.sequence_size/2) {
      CHK(ssp_rng_get(rng) == ssp_rng_get(rng0));
    } else {
      CHK(ssp_rng_get(rng) == ssp_rng_get(rng1));
    }
    CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
    CHK(id == 63 + 203 + 314);
  }

  /* Generate several RNs for the first RNG only to make under pressure the
   * cache stream of 'rng1'. The cache memory limit is 32 MB maximum and the
   * size of a Mersenne Twister RNG state is greater than 6 KB. Consquently,
   * ~5500 RNG states will exceed the cache stream, i.e. 5500*5 = 27500
   * random generations (since there is 5 RNs per bucket). Above this limit,
   * 'rng1' will not rely anymore on the proxy RNG to manage its state. */
  CHK(ssp_rng_discard(rng0, 30000) == RES_OK);

  /* Discard enough random numbers from rng1 to consume all cached RNG states,
   * and thus disable read cache usage */
  CHK(ssp_rng_discard(rng1, 30000) == RES_OK);

  CHK(ssp_rng_proxy_get_sequence_id(proxy, &id) == RES_OK);
  CHK(id == 63+203+314+(30000*2)/args.sequence_size);

  /* Try to flush a sequence on a proxy whose a generator don't use cache */
  CHK(ssp_rng_proxy_flush_sequences(proxy, 1) == RES_OK);

  CHK(ssp_rng_proxy_ref_put(proxy) == RES_OK);
  CHK(ssp_rng_ref_put(rng1) == RES_OK);
  CHK(ssp_rng_ref_put(rng0) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);
}

int
main(int argc, char** argv)
{
  (void)argc, (void)argv;
  test_creation();
  test_creation2();
  test_rng_from_proxy();
  test_rng_from_proxy2();
  test_multi_proxies();
  test_proxy_from_rng();
  test_read();
  test_read_with_cached_states();
  test_write();
  test_cache();
  test_sequence();
  test_flush_sequence();
  CHK(mem_allocated_size() == 0);
  return 0;
}
