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

#include <rsys/clock_time.h>

#include <string.h>

#define NRAND 1024

static void /* Really basic test */
test_rng(const enum ssp_rng_type type)
{
  FILE* stream;
  enum ssp_rng_type type2;
  struct ssp_rng* rng = NULL;
  struct ssp_rng* rng1 = NULL;
  struct ssp_rng* rng2 = NULL;
  struct mem_allocator allocator;
  struct time t0, t1;
  uint64_t datai0[NRAND];
  uint64_t datai1[NRAND];
  double datad[NRAND];
  float dataf[NRAND];
  size_t len = 0;
  char buf[512];
  char* cstr = NULL;
  int i, j;
  res_T r;
  const char can_set = (type != SSP_RNG_RANDOM_DEVICE);
  const char can_rw = (type != SSP_RNG_RANDOM_DEVICE);
  const char can_have_entropy = (type == SSP_RNG_RANDOM_DEVICE);
  const char can_discard = (type != SSP_RNG_RANDOM_DEVICE);

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(ssp_rng_create(NULL, -1, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_create(&allocator, -1, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_create(NULL, type, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_create(&allocator, type, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_create(NULL, -1, &rng) == RES_BAD_ARG);
  CHK(ssp_rng_create(&allocator, -1, &rng) == RES_BAD_ARG);
  r = ssp_rng_create(NULL, type, &rng);
  if(type == SSP_RNG_AES) {
    switch(r) {
      case RES_BAD_ARG:
        fprintf(stderr, "AES not supported\n");
        break;
      case RES_OK: /* Do nohting */ break;
      case RES_BAD_OP:
        fprintf(stderr, "AES-NI instructions not available.\n");
        break;
      default: FATAL("Unreachable code\n"); break;
    }
  }
  CHK(r == RES_OK);

  CHK(ssp_rng_get_type(NULL, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_get_type(rng, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_get_type(NULL, &type2) == RES_BAD_ARG);
  CHK(ssp_rng_get_type(rng, &type2) == RES_OK);
  CHK(type == type2);

  CHK(ssp_rng_ref_get(NULL) == RES_BAD_ARG);
  CHK(ssp_rng_ref_get(rng) == RES_OK);
  CHK(ssp_rng_ref_put(NULL) == RES_BAD_ARG);
  CHK(ssp_rng_ref_put(rng) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  CHK(ssp_rng_create(&allocator, type, &rng) == RES_OK);
  CHK(ssp_rng_set(NULL, 0) == RES_BAD_ARG);
  CHK(ssp_rng_set(rng, 0) == (can_set ? RES_OK : RES_BAD_OP));

  CHK(ssp_rng_create(NULL, type, &rng1) == RES_OK);
  CHK(ssp_rng_create(NULL, type, &rng2) == RES_OK);
  CHK(ssp_rng_discard(rng1, 10) == (can_discard ? RES_OK : RES_BAD_OP));
  if(type != SSP_RNG_RANDOM_DEVICE) {
    FOR_EACH(i, 0, 10) ssp_rng_get(rng2);
    CHK(ssp_rng_get(rng1) == ssp_rng_get(rng2));
  }

  FOR_EACH(i, 0, NRAND) {
    datai0[i] = ssp_rng_get(rng);
    CHK(datai0[i] >= ssp_rng_min(rng));
    CHK(datai0[i] <= ssp_rng_max(rng));
    FOR_EACH(j, 0, i) {
      CHK(datai0[i] != datai0[j]);
    }
  }

  CHK(ssp_rng_set(rng, 0xDECAFBAD) == (can_set ? RES_OK : RES_BAD_OP));
  FOR_EACH(i, 0, NRAND) {
    datai1[i] = ssp_rng_get(rng);
    CHK(datai1[i] != datai0[i]);
    CHK(datai1[i] >= ssp_rng_min(rng));
    CHK(datai1[i] <= ssp_rng_max(rng));
    FOR_EACH(j, 0, i) {
      CHK(datai1[i] != datai1[j]);
    }
  }

  FOR_EACH(i, 0, NRAND) {
    datai0[i] = ssp_rng_uniform_uint64(rng, 1, 79);
    CHK(datai0[i] >= 1);
    CHK(datai0[i] <= 79);
  }
  FOR_EACH(i, 0, NRAND) {
    FOR_EACH(j, 0, NRAND) if(datai0[i] != datai0[j]) break;
    CHK(j != NRAND);
  }

  FOR_EACH(i, 0, NRAND) {
    datad[i] = ssp_rng_uniform_double(rng, -5.0, 11.3);
    CHK(datad[i] >= -5.0);
    CHK(datad[i] <= 11.3);
  }
  FOR_EACH(i, 0, NRAND) {
    FOR_EACH(j, 0, NRAND) if(datad[i] != datad[j]) break;
    CHK(j != NRAND);
  }

  FOR_EACH(i, 0, NRAND) {
    dataf[i] = ssp_rng_uniform_float(rng, -1.0, 12.5);
    CHK(dataf[i] >= -1.0);
    CHK(dataf[i] <= 12.5);
  }
  FOR_EACH(i, 0, NRAND) {
    FOR_EACH(j, 0, NRAND) if(dataf[i] != dataf[j]) break;
    CHK(j != NRAND);
  }

  FOR_EACH(i, 0, NRAND) {
    datad[i] = ssp_rng_canonical(rng);
    CHK(datad[i] >= 0.0);
    CHK(datad[i] < 1.0 );
    FOR_EACH(j, 0, i) {
      CHK(datad[i] != datad[j]);
    }
  }

  FOR_EACH(i, 0, NRAND) {
    dataf[i] = ssp_rng_canonical_float(rng);
    CHK(dataf[i] >= 0.0);
    CHK(dataf[i] < 1.0);
    FOR_EACH(j, 0, i) {
      CHK(dataf[i] != dataf[j]);
    }
  }

  time_current(&t0);
  FOR_EACH(i, 0, 1000000) {
    ssp_rng_get(rng);
  }
  time_current(&t1);
  time_sub(&t0, &t1, &t0);
  time_dump(&t0, TIME_SEC|TIME_MSEC|TIME_USEC, NULL, buf, sizeof(buf));
  printf("1,000,000 random numbers in %s\n", buf);

  if(can_have_entropy) {
    printf("Entropy for this implementation and system: %f\n",
      ssp_rng_entropy(rng));
  }

  stream = tmpfile();
  CHK(stream != NULL);
  CHK(ssp_rng_write(NULL, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_write(rng, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_write(NULL, stream) == RES_BAD_ARG);
  CHK(ssp_rng_write(rng, stream) == (can_rw ? RES_OK : RES_BAD_OP));

  CHK(ssp_rng_write_cstr(NULL, NULL, 0, &len) == RES_BAD_ARG);
  CHK(ssp_rng_write_cstr(rng, NULL, 0, NULL) == (can_rw ? RES_OK : RES_BAD_OP));
  CHK(ssp_rng_write_cstr(rng, NULL, 0, &len) == (can_rw ? RES_OK : RES_BAD_OP));
  cstr = mem_calloc(len+1, 1);
  CHK(cstr != NULL);
  CHK(ssp_rng_write_cstr(rng, cstr, len+1, NULL) == (can_rw ? RES_OK : RES_BAD_OP));

  /* Reserialize the RNG state */
  CHK(ssp_rng_write(rng, stream) == (can_rw ? RES_OK : RES_BAD_OP));

  FOR_EACH(i, 0, NRAND)
    datai0[i] = ssp_rng_get(rng);

  fflush(stream);

  CHK(ssp_rng_read(NULL, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_read(rng, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_read(NULL, stream) == RES_BAD_ARG);
  CHK(ssp_rng_read(rng, stream) == (can_rw ? RES_IO_ERR : RES_BAD_OP));
  rewind(stream);

  /* Read the first state of the stream */
  CHK(ssp_rng_read(rng, stream) == (can_rw ? RES_OK : RES_BAD_OP));
  if(can_rw) {
    FOR_EACH(i, 0, NRAND) {
      uint64_t rn = ssp_rng_get(rng);
      CHK(rn == datai0[i]);
    }
  }

  /* Read the second state of the stream */
  CHK(ssp_rng_read(rng, stream) == (can_rw ? RES_OK : RES_BAD_OP));
  if(can_rw) {
    FOR_EACH(i, 0, NRAND) {
      uint64_t rn = ssp_rng_get(rng);
      CHK(rn == datai0[i]);
    }
  }

  /* Read the RNG state from an in memory buffer */
  CHK(ssp_rng_read_cstr(NULL, cstr) == RES_BAD_ARG);
  CHK(ssp_rng_read_cstr(rng, NULL) == RES_BAD_ARG);
  CHK(ssp_rng_read_cstr(rng, cstr) == (can_rw ? RES_OK : RES_BAD_OP));
  if(can_rw) {
    FOR_EACH(i, 0, NRAND) {
      uint64_t rn = ssp_rng_get(rng);
      CHK(rn == datai0[i]);
    }
  }

  mem_rm(cstr);
  fclose(stream);

  CHK(ssp_rng_ref_put(rng) == RES_OK);
  ssp_rng_ref_put(rng1);
  ssp_rng_ref_put(rng2);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
}

int
main(int argc, char** argv)
{
  if (argc <= 1) {
    fprintf(stderr,
      "Usage: %s <kiss|mt19937_64|ranlux48|random_device|threefry|aes>\n",
      argv[0]);
    exit(0);
  }
  if(!strcmp(argv[1], "kiss")) {
    test_rng(SSP_RNG_KISS);
  } else if(!strcmp(argv[1], "mt19937_64")) {
    test_rng(SSP_RNG_MT19937_64);
  } else if(!strcmp(argv[1], "ranlux48")) {
    test_rng(SSP_RNG_RANLUX48);
  } else if(!strcmp(argv[1], "random_device")) {
    test_rng(SSP_RNG_RANDOM_DEVICE);
  } else if(!strcmp(argv[1], "threefry")) {
    test_rng(SSP_RNG_THREEFRY);
  } else if(!strcmp(argv[1], "aes")) {
    test_rng(SSP_RNG_AES);
  } else {
    fprintf(stderr, "Unknown RNG `%s'\n", argv[1]);
    ASSERT(0);
  }

  CHK(mem_allocated_size() == 0);
  return 0;
}

