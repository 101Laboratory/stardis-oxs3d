/* Copyright (C) 2013-2023, 2025 Vincent Forest (vaplv@free.fr)
 *
 * The RSys library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The RSys library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the RSys library. If not, see <http://www.gnu.org/licenses/>. */

#include "dynamic_array.h"
#include "test_utils.h"

static const char* strs[] = {
  "Rcvfbqr", "1,", "XARR-QRRC", "VA", "GUR", "QRNQ:\n",
  "---------------------------------", "BAPR", "LBH", "ORNG", "GUR", "OVT",
  "ONQNFFRF", "NAQ", "PYRNA", "BHG", "GUR", "ZBBA", "ONFR", "LBH'ER",
  "FHCCBFRQ", "GB\n", "JVA", "NERA'G", "LBH?", "NERA'G", "LBH?", "JURER'F",
  "LBHE", "SNG", "ERJNEQ", "NAQ", "GVPXRG", "UBZR?", "JUNG\n", "GUR", "URYY",
  "VF", "GUVF?", "VG'F", "ABG", "FHCCBFRQ", "GB", "RAQ", "GUVF", "JNL!", "VG",
  "FGVAXF", "YVXR", "EBGGRA", "ZRNG,", "OHG", "YBBXF", "YVXR", "GUR", "YBFG",
  "QRVZBF", "ONFR.", "YBBXF", "YVXR\n", "LBH'ER", "FGHPX", "BA", "GUR",
  "FUBERF", "BS", "URYY.", "GUR", "BAYL", "JNL", "BHG", "VF", "GUEBHTU.", "GB",
  "PBAGVAHR", "GUR", "QBBZ", "RKCREVRAPR,", "CYNL", "GUR", "FUBERF", "BS",
  "URYY", "NAQ", "VGF", "NZNMVAT\n", "FRDHRY,", "VASREAB!",

  "Rcvfbqr 2, GUR FUBERF BS URYY:\n\
  ------------------------------\n\
  \n\
  LBH'IR QBAR VG! GUR UVQRBHF PLORE- QRZBA YBEQ GUNG EHYRQ GUR YBFG QRVZBF ZBBA\n\
  ONFR UNF ORRA FYNVA NAQ LBH NER GEVHZCUNAG! OHG ... JURER NER LBH? LBH\n\
  PYNZORE GB GUR RQTR BS GUR ZBBA NAQ YBBX QBJA GB FRR GUR NJSHY GEHGU.\n\
  \n",

  "QRVZBF SYBNGF NOBIR URYY VGFRYS!  LBH'IR ARIRE URNEQ BS NALBAR RFPNCVAT SEBZ\n\
  URYY, OHG LBH'YY ZNXR GUR ONFGNEQF FBEEL GURL RIRE URNEQ BS LBH! DHVPXYL, LBH\n\
  ENCCRY QBJA GB GUR FHESNPR BS URYY.\n\
  \n\
  ABJ, VG'F BA GB GUR SVANY PUNCGRE BS QBBZ! -- VASREAB."
};

#define DARRAY_NAME str
#define DARRAY_DATA const char*
#include "dynamic_array.h"

static void
test_cstr(struct mem_allocator* allocator)
{
  struct darray_str darray;
  const size_t nstrs = sizeof(strs)/sizeof(const char*);
  size_t i = 0;

  darray_str_init(NULL, &darray);
  CHK(darray_str_size_get(&darray) == 0);
  darray_str_clear(&darray);
  CHK(darray_str_size_get(&darray) == 0);

  FOR_EACH(i, 0, 4) {
    darray_str_push_back(&darray, strs + i);
  }
  CHK(darray_str_size_get(&darray) == 4);
  darray_str_clear(&darray);
  CHK(darray_str_size_get(&darray) == 0);
  darray_str_release(&darray);

  darray_str_init(allocator, &darray);
  FOR_EACH(i, 0, nstrs) {
    darray_str_push_back(&darray, strs + i);
  }
  FOR_EACH(i, 0, nstrs) {
    CHK(DARRAY_BUF(&darray) == darray_str_cdata_get(&darray));
    CHK(DARRAY_BUF(&darray) == darray_str_data_get(&darray));
    CHK(DARRAY_BUF((struct darray_str*)(&darray)) == DARRAY_BUF(&darray));
    CHK(strcmp(darray_str_cdata_get(&darray)[i], strs[i]) == 0);
    CHK(strcmp(DARRAY_BUF(&darray)[i], strs[i]) == 0);
  }
  darray_str_purge(&darray);
  CHK(darray_str_size_get(&darray) == 0);
  darray_str_release(&darray);

  darray_str_init(allocator, &darray);
  CHK(darray_str_size_get(&darray) == 0);
  darray_str_resize(&darray, 8);
  CHK(darray_str_size_get(&darray) == 8);
  darray_str_resize(&darray, 0);
  CHK(darray_str_size_get(&darray) == 0);
  darray_str_resize(&darray, 33);
  CHK(darray_str_size_get(&darray) == 33);
  darray_str_release(&darray);
}

#include "str.h"
#define DARRAY_NAME string
#define DARRAY_DATA struct str
#define DARRAY_FUNCTOR_INIT str_init
#define DARRAY_FUNCTOR_COPY str_copy
#define DARRAY_FUNCTOR_RELEASE str_release
#define DARRAY_FUNCTOR_COPY_AND_RELEASE str_copy_and_release
#include "dynamic_array.h"

static void
test_string
  (struct mem_allocator* allocator0,
   struct mem_allocator* allocator1)
{
  struct darray_string darray, darray2;
  const size_t nstrs = sizeof(strs)/sizeof(const char*);
  size_t i = 0;

  darray_string_init(allocator0, &darray);
  darray_string_init(allocator1, &darray2);
  CHK(darray_string_size_get(&darray) == 0);
  CHK(darray_string_size_get(&darray2) == 0);

  darray_string_resize(&darray, nstrs);
  FOR_EACH(i, 0, nstrs) {
    str_set(darray_string_data_get(&darray) + i, strs[i]);
  }
  CHK(darray_string_size_get(&darray) == nstrs);
  FOR_EACH(i, 0, nstrs) {
    const struct str* str = darray_string_cdata_get(&darray) + i;
    CHK(strcmp(str_cget(str), strs[i]) == 0);
  }

  darray_string_copy(&darray, &darray2);
  CHK(darray_string_size_get(&darray) == 0);
  FOR_EACH(i, 0, nstrs) {
    struct str str;
    str_init(allocator1, &str);
    str_set(&str, strs[i]);
    darray_string_push_back(&darray, &str);
    str_release(&str);
  }

  CHK(darray_string_size_get(&darray) == nstrs);
  FOR_EACH(i, 0, nstrs) {
    const struct str* str = darray_string_cdata_get(&darray) + i;
    CHK(strcmp(str_cget(str), strs[i]) == 0);
  }

  darray_string_copy(&darray2, &darray);
  CHK(darray_string_size_get(&darray2) == nstrs);
  FOR_EACH(i, 0, nstrs) {
    const struct str* str = darray_string_cdata_get(&darray) + i;
    const struct str* str2 = darray_string_cdata_get(&darray2) + i;
    CHK(strcmp(str_cget(str), strs[i]) == 0);
    CHK(strcmp(str_cget(str2), strs[i]) == 0);
  }
  darray_string_clear(&darray2);
  i = MEM_ALLOCATED_SIZE(allocator1);
  darray_string_purge(&darray2);
  CHK(MEM_ALLOCATED_SIZE(allocator1) < i);
  CHK(darray_string_size_get(&darray2) == 0);

  darray_string_copy_and_clear(&darray2, &darray);
  CHK(darray_string_size_get(&darray2) == nstrs);
  CHK(darray_string_size_get(&darray) == 0);
  FOR_EACH(i, 0, nstrs) {
    const struct str* str = darray_string_cdata_get(&darray2) + i;
    CHK(strcmp(str_cget(str), strs[i]) == 0);
  }

  darray_string_copy_and_release(&darray, &darray2);
  FOR_EACH(i, 0, nstrs) {
    const struct str* str = darray_string_cdata_get(&darray) + i;
    CHK(strcmp(str_cget(str), strs[i]) == 0);
  }

  darray_string_release(&darray);
}

#define DARRAY_NAME int
#define DARRAY_DATA int
#include "dynamic_array.h"

static void
test_swap_int
  (struct mem_allocator* allocator0,
   struct mem_allocator* allocator1)
{
  struct darray_int a;
  struct darray_int b;
  int i;

  #define PUSH_BACK(V, E) {                                                    \
    const int i__ = (E);                                                       \
    CHK(darray_int_push_back(&(V), &i__) == RES_OK);                           \
  } (void)0

  darray_int_init(allocator0, &a);
  darray_int_init(allocator1, &b);

  PUSH_BACK(a, 1);
  PUSH_BACK(a, 2);
  PUSH_BACK(a, 3);
  PUSH_BACK(b,-1);
  PUSH_BACK(b,-2);

  CHK(darray_int_swap(&a, &b) == RES_OK);
  CHK(darray_int_size_get(&a) == 2);
  CHK(darray_int_size_get(&b) == 3);

  CHK(darray_int_cdata_get(&a)[0] == -1);
  CHK(darray_int_cdata_get(&a)[1] == -2);
  CHK(darray_int_cdata_get(&b)[0] == 1);
  CHK(darray_int_cdata_get(&b)[1] == 2);
  CHK(darray_int_cdata_get(&b)[2] == 3);

  darray_int_clear(&a);
  darray_int_clear(&b);

  FOR_EACH(i, 0, 128) PUSH_BACK(a, i);
  FOR_EACH(i, 0, 112) PUSH_BACK(b,-i);

  CHK(darray_int_swap(&a, &b) == RES_OK);
  CHK(darray_int_size_get(&a) == 112);
  CHK(darray_int_size_get(&b) == 128);
  FOR_EACH(i, 0, 112) CHK(darray_int_cdata_get(&a)[i] == -i);
  FOR_EACH(i, 0, 128) CHK(darray_int_cdata_get(&b)[i] == i);

  darray_int_release(&b);
  darray_int_init(allocator1, &b);
  PUSH_BACK(b, -1);
  PUSH_BACK(b, -314);

  CHK(darray_int_swap(&a, &b) == RES_OK);
  CHK(darray_int_size_get(&a) == 2);
  CHK(darray_int_size_get(&b) == 112);
  FOR_EACH(i, 0, 112) CHK(darray_int_cdata_get(&b)[i] == -i);
  CHK(darray_int_cdata_get(&a)[0] == -1);
  CHK(darray_int_cdata_get(&a)[1] == -314);

  darray_int_release(&a);
  darray_int_release(&b);

  #undef PUSH_BACK
}

static void
test_swap_string
  (struct mem_allocator* allocator0,
   struct mem_allocator* allocator1)
{
  struct darray_string a;
  struct darray_string b;
  const size_t nstrs = sizeof(strs)/sizeof(const char*);
  size_t i;

  #define PUSH_BACK(V, CStr) {                                                 \
    struct str str__;                                                          \
    str_init(allocator1, &str__);                                              \
    CHK(str_set(&str__, CStr) == RES_OK);                                      \
    CHK(darray_string_push_back(&(V), &str__) == RES_OK);                      \
    str_release(&str__);                                                       \
  } (void)0

  darray_string_init(allocator0, &a);
  darray_string_init(allocator1, &b);

  CHK(nstrs >= 3);
  PUSH_BACK(a, strs[0]);
  PUSH_BACK(a, strs[1]);
  PUSH_BACK(a, strs[2]);
  PUSH_BACK(b, strs[3]);
  PUSH_BACK(b, strs[4]);

  CHK(darray_string_swap(&a, &b) == RES_OK);
  CHK(darray_string_size_get(&a) == 2);
  CHK(darray_string_size_get(&b) == 3);

  CHK(strcmp(str_cget(darray_string_cdata_get(&a)+0), strs[3]) == 0);
  CHK(strcmp(str_cget(darray_string_cdata_get(&a)+1), strs[4]) == 0);
  CHK(strcmp(str_cget(darray_string_cdata_get(&b)+0), strs[0]) == 0);
  CHK(strcmp(str_cget(darray_string_cdata_get(&b)+1), strs[1]) == 0);
  CHK(strcmp(str_cget(darray_string_cdata_get(&b)+2), strs[2]) == 0);

  darray_string_clear(&a);
  darray_string_clear(&b);

  FOR_EACH(i, 0, nstrs/2) PUSH_BACK(a, strs[i]);
  FOR_EACH(i, nstrs/2, nstrs) PUSH_BACK(b, strs[i]);

  CHK(darray_string_swap(&a, &b) == RES_OK);
  CHK(darray_string_size_get(&a) == nstrs/2);
  CHK(darray_string_size_get(&b) == nstrs - nstrs/2);

  FOR_EACH(i, 0, nstrs/2) {
    CHK(strcmp(str_cget(darray_string_cdata_get(&b)+i), strs[i]) == 0);
  }
  FOR_EACH(i, 0, nstrs - nstrs/2) {
    CHK(strcmp(str_cget(darray_string_cdata_get(&a)+i), strs[i+nstrs/2]) == 0);
  }

  darray_string_release(&b);
  darray_string_init(allocator1, &b);
  PUSH_BACK(b, strs[0]);
  PUSH_BACK(b, strs[1]);

  CHK(darray_string_swap(&a, &b) == RES_OK);
  CHK(darray_string_size_get(&a) == 2);
  CHK(darray_string_size_get(&b) == nstrs - nstrs/2);
  FOR_EACH(i, 0, nstrs - nstrs/2) {
    CHK(strcmp(str_cget(darray_string_cdata_get(&b)+i), strs[i+nstrs/2]) == 0);
  }

  CHK(strcmp(str_cget(darray_string_cdata_get(&a)+0), strs[0]) == 0);
  CHK(strcmp(str_cget(darray_string_cdata_get(&a)+1), strs[1]) == 0);

  darray_string_release(&a);
  darray_string_release(&b);

  #undef PUSH_BACK
}

#define DARRAY_NAME byte
#define DARRAY_DATA char
#include "dynamic_array.h"

#define DARRAY_NAME byte64
#define DARRAY_DATA char
#define DARRAY_ALIGNMENT 64
#include "dynamic_array.h"

#define DARRAY_NAME byte1K
#define DARRAY_DATA char
#define DARRAY_ALIGNMENT 1024
#include "dynamic_array.h"

static void
test_alignment(struct mem_allocator* allocator)
{
  struct darray_byte bytes;
  struct darray_byte64 bytes64;
  struct darray_byte1K bytes1K;

  CHK(allocator != NULL);

  darray_byte_init(allocator, &bytes);
  CHK(darray_byte_resize(&bytes, 2) == RES_OK);
  CHK(IS_ALIGNED(darray_byte_cdata_get(&bytes), ALIGNOF(char)) == 1);
  CHK(darray_byte_resize(&bytes, 314159) == RES_OK);
  CHK(IS_ALIGNED(darray_byte_cdata_get(&bytes), ALIGNOF(char)) == 1);
  darray_byte_release(&bytes);

  darray_byte64_init(allocator, &bytes64);
  CHK(darray_byte64_resize(&bytes64, 2) == RES_OK);
  CHK(IS_ALIGNED(darray_byte64_cdata_get(&bytes64), 64) == 1);
  CHK(darray_byte64_resize(&bytes64, 314159) == RES_OK);
  CHK(IS_ALIGNED(darray_byte64_cdata_get(&bytes64), 64) == 1);
  darray_byte64_release(&bytes64);

  darray_byte1K_init(allocator, &bytes1K);
  CHK(darray_byte1K_resize(&bytes1K, 2) == RES_OK);
  CHK(IS_ALIGNED(darray_byte1K_cdata_get(&bytes1K), 1024) == 1);
  CHK(darray_byte1K_resize(&bytes1K, 314159) == RES_OK);
  CHK(IS_ALIGNED(darray_byte1K_cdata_get(&bytes1K), 1024) == 1);
  darray_byte1K_release(&bytes1K);
}

static void
test_allocation_policy(struct mem_allocator* allocator)
{
  struct darray_int integers;
  struct darray_int integers2;
  const int* mem;
  int i;

  darray_int_init(allocator, &integers);
  darray_int_init(allocator, &integers2);
  CHK(darray_int_capacity(&integers) == 0);

  CHK(darray_int_reserve(&integers, 33) == RES_OK);
  CHK(darray_int_capacity(&integers) == 33);
  CHK(darray_int_size_get(&integers) == 0);
  mem = darray_int_cdata_get(&integers);

  CHK(darray_int_resize(&integers, 14) == RES_OK);
  CHK(darray_int_capacity(&integers) == 33);
  CHK(darray_int_size_get(&integers) == 14);
  CHK(darray_int_cdata_get(&integers) == mem);

  darray_int_clear(&integers);
  CHK(darray_int_capacity(&integers) == 33);
  CHK(darray_int_size_get(&integers) == 0);
  CHK(darray_int_cdata_get(&integers) == mem);

  CHK(darray_int_resize(&integers, 35) == RES_OK);
  CHK(darray_int_capacity(&integers) == 35);
  CHK(darray_int_size_get(&integers) == 35);
  CHK(darray_int_cdata_get(&integers) != mem);

  darray_int_purge(&integers);
  CHK(darray_int_capacity(&integers) == 0);
  CHK(darray_int_size_get(&integers) == 0);

  i = 0;
  CHK(darray_int_push_back(&integers, &i) == RES_OK);
  CHK(darray_int_capacity(&integers) == 1);
  CHK(darray_int_size_get(&integers) == 1);
  mem = darray_int_cdata_get(&integers);

  CHK(darray_int_push_back(&integers, &i) == RES_OK);
  CHK(darray_int_capacity(&integers) == 2);
  CHK(darray_int_size_get(&integers) == 2);
  CHK(darray_int_cdata_get(&integers) != mem);
  mem = darray_int_cdata_get(&integers);

  CHK(darray_int_push_back(&integers, &i) == RES_OK);
  CHK(darray_int_capacity(&integers) == 4);
  CHK(darray_int_size_get(&integers) == 3);
  CHK(darray_int_cdata_get(&integers) != mem);
  mem = darray_int_cdata_get(&integers);

  CHK(darray_int_push_back(&integers, &i) == RES_OK);
  CHK(darray_int_capacity(&integers) == 4);
  CHK(darray_int_size_get(&integers) == 4);
  CHK(darray_int_cdata_get(&integers) == mem);

  CHK(darray_int_push_back(&integers, &i) == RES_OK);
  CHK(darray_int_capacity(&integers) == 8);
  CHK(darray_int_size_get(&integers) == 5);
  CHK(darray_int_cdata_get(&integers) != mem);
  mem = darray_int_cdata_get(&integers);

  darray_int_purge(&integers);
  CHK(darray_int_capacity(&integers) == 0);
  CHK(darray_int_size_get(&integers) == 0);
  CHK(darray_int_cdata_get(&integers) == NULL);

  FOR_EACH(i, 0, 33) {
    CHK(darray_int_push_back(&integers, &i) == RES_OK);
  }
  CHK(darray_int_capacity(&integers) == 64);
  CHK(darray_int_size_get(&integers) == 33);

  darray_int_purge(&integers);
  CHK(darray_int_capacity(&integers) == 0);
  CHK(darray_int_size_get(&integers) == 0);
  CHK(darray_int_cdata_get(&integers) == NULL);

  CHK(darray_int_resize(&integers, 10) == RES_OK);
  CHK(darray_int_capacity(&integers) == 10);
  CHK(darray_int_size_get(&integers) == 10);

  CHK(darray_int_push_back(&integers, &i) == RES_OK);
  CHK(darray_int_capacity(&integers) == 20);
  CHK(darray_int_size_get(&integers) == 11);

  CHK(darray_int_push_back(&integers, &i) == RES_OK);
  CHK(darray_int_capacity(&integers) == 20);
  CHK(darray_int_size_get(&integers) == 12);

  CHK(darray_int_resize(&integers, 17) == RES_OK);
  CHK(darray_int_capacity(&integers) == 20);
  CHK(darray_int_size_get(&integers) == 17);

  CHK(darray_int_resize(&integers, 24) == RES_OK);
  CHK(darray_int_capacity(&integers) == 34);
  CHK(darray_int_size_get(&integers) == 24);

  CHK(darray_int_resize(&integers, 41) == RES_OK);
  CHK(darray_int_capacity(&integers) == 48);
  CHK(darray_int_size_get(&integers) == 41);

  CHK(darray_int_reserve(&integers, 30) == RES_OK);
  CHK(darray_int_capacity(&integers) == 48);
  CHK(darray_int_size_get(&integers) == 41);

  CHK(darray_int_reserve(&integers, 49) == RES_OK);
  CHK(darray_int_capacity(&integers) == 49);
  CHK(darray_int_size_get(&integers) == 41);

  CHK(darray_int_resize(&integers2, 42) == RES_OK);
  CHK(darray_int_copy(&integers, &integers2) == RES_OK);
  CHK(darray_int_capacity(&integers) == 49);
  CHK(darray_int_size_get(&integers) == 42);

  CHK(darray_int_copy(&integers2, &integers) == RES_OK);
  CHK(darray_int_capacity(&integers2) == 42);
  CHK(darray_int_size_get(&integers2) == 42);

  CHK(darray_int_reserve(&integers2, 70) == RES_OK);
  CHK(darray_int_copy(&integers, &integers2) == RES_OK);
  CHK(darray_int_capacity(&integers) == 49);
  CHK(darray_int_size_get(&integers) == 42);

  darray_int_release(&integers);
  darray_int_release(&integers2);
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator_proxy;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator_proxy, &mem_default_allocator);

  test_cstr(&allocator_proxy);

  test_string(&allocator_proxy, &allocator_proxy);
  test_string(&allocator_proxy, &mem_default_allocator);

  test_swap_int(&mem_default_allocator, &allocator_proxy);
  test_swap_int(&allocator_proxy, &allocator_proxy);

  test_swap_string(&mem_default_allocator, &allocator_proxy);
  test_swap_string(&allocator_proxy, &allocator_proxy);

  test_alignment(&mem_default_allocator);
  test_allocation_policy(&mem_default_allocator);

  check_memory_allocator(&allocator_proxy);
  mem_shutdown_proxy_allocator(&allocator_proxy);
  CHK(mem_allocated_size() == 0);
  return 0;
}
