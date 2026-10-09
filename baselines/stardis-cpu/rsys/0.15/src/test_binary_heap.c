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

#include "binary_heap.h"
#include "str.h"
#include "test_utils.h"

#define BHEAP_NAME u64
#define BHEAP_DATA uint64_t
#include "binary_heap.h"

static void
test_primitive_type(void)
{
  struct mem_allocator allocator_proxy;
  struct bheap_u64 heap;
  uint64_t i;

  mem_init_proxy_allocator(&allocator_proxy, &mem_default_allocator);

  bheap_u64_init(&allocator_proxy, &heap);
  CHK(bheap_u64_is_empty(&heap) == 1);
  bheap_u64_clear(&heap);
  CHK(bheap_u64_is_empty(&heap) == 1);

  CHK(bheap_u64_is_empty(&heap) == 1);
  CHK(bheap_u64_top(&heap, &i) == 0);
  CHK(bheap_u64_pop(&heap, &i) == 0);
  i = 48; CHK(bheap_u64_insert(&heap, &i) == RES_OK);
  i = 51; CHK(bheap_u64_insert(&heap, &i) == RES_OK);
  i = 1;  CHK(bheap_u64_insert(&heap, &i) == RES_OK);
  i = 7;  CHK(bheap_u64_insert(&heap, &i) == RES_OK);
  i = 78; CHK(bheap_u64_insert(&heap, &i) == RES_OK);
  i = 4;  CHK(bheap_u64_insert(&heap, &i) == RES_OK);
  i = 61; CHK(bheap_u64_insert(&heap, &i) == RES_OK);
  i = 72; CHK(bheap_u64_insert(&heap, &i) == RES_OK);
  CHK(bheap_u64_is_empty(&heap) == 0);

  CHK(bheap_u64_top(&heap, &i) != 0);
  CHK(i == 1);
  CHK(bheap_u64_top(&heap, &i) != 0);
  CHK(i == 1);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  CHK(i == 1);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  CHK(i == 4);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  CHK(i == 7);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  CHK(i == 48);

  i = 5;  CHK(bheap_u64_insert(&heap, &i) == RES_OK);
  i = 50; CHK(bheap_u64_insert(&heap, &i) == RES_OK);
  i = 51; CHK(bheap_u64_insert(&heap, &i) == RES_OK);
  CHK(bheap_u64_top(&heap, &i) != 0);
  CHK(i == 5);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  CHK(i == 5);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  CHK(i == 50);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  CHK(i == 51);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  CHK(i == 51);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  CHK(i == 61);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  CHK(i == 72);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  CHK(i == 78);
  CHK(bheap_u64_pop(&heap, &i) == 0);

  CHK(bheap_u64_is_empty(&heap) == 1);

  FOR_EACH(i, 0, 17689) {
    const uint64_t j = (uint64_t)rand();
    CHK(bheap_u64_insert(&heap, &j) == RES_OK);
  }
  CHK(bheap_u64_is_empty(&heap) == 0);
  CHK(bheap_u64_pop(&heap, &i) != 0);
  while(bheap_u64_is_empty(&heap)) {
    uint64_t j = 0;
    CHK(bheap_u64_pop(&heap, &j) != 0);
    CHK(j >= i);
    i = j;
  }

  bheap_u64_release(&heap);
  check_memory_allocator(&allocator_proxy);
  mem_shutdown_proxy_allocator(&allocator_proxy);
}

#define BHEAP_NAME str
#define BHEAP_DATA struct str
#define BHEAP_FUNCTOR_INIT str_init
#define BHEAP_FUNCTOR_COPY str_copy
#define BHEAP_FUNCTOR_RELEASE str_release
#define BHEAP_FUNCTOR_COPY_AND_RELEASE str_copy_and_release
#define BHEAP_FUNCTOR_COMPARE str_cmp
#include "binary_heap.h"

static void
test_struct(void)
{
  struct mem_allocator allocator_proxy;
  struct bheap_str heap;
  struct str str;
  size_t i;
  const char* strs[] = {
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
  const size_t nstrs = sizeof(strs)/sizeof(const char*);

  mem_init_proxy_allocator(&allocator_proxy, &mem_default_allocator);
  str_init(&allocator_proxy, &str);

  bheap_str_init(&allocator_proxy, &heap);
  FOR_EACH(i, 0, nstrs) {
    str_set(&str, strs[i]);
    bheap_str_insert(&heap, &str);
  }

  CHK(bheap_str_pop(&heap, &str) != 0);
  while(bheap_str_is_empty(&heap)) {
    struct str str2;
    str_init(&allocator_proxy, &str2);
    CHK(bheap_str_pop(&heap, &str2) != 0);
    CHK(strcmp(str_cget(&str), str_cget(&str2)) == -1);
    str_set(&str, str_cget(&str2));
    str_release(&str2);
  }

  str_release(&str);
  bheap_str_release(&heap);

  check_memory_allocator(&allocator_proxy);
  mem_shutdown_proxy_allocator(&allocator_proxy);
}

int
main(int argc, char** argv)
{
  (void)argc, (void)argv;
  test_primitive_type();
  test_struct();
  CHK(mem_allocated_size() == 0);
  return 0;
}
