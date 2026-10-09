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

#include "dynamic_array_char.h"
#include "hash.h"
#include "hash_table.h"
#include "str.h"
#include "test_utils.h"
#include <string.h>

#define HTABLE_KEY int
#define HTABLE_DATA float
#define HTABLE_NAME int_float
#include "hash_table.h"

static void
test_htbl_int_float(void)
{
  struct mem_allocator allocator_proxy;
  struct htable_int_float htbl;
  int i;
  const int n = 678;

  htable_int_float_init(NULL, &htbl);
  htable_int_float_release(&htbl);

  mem_init_proxy_allocator(&allocator_proxy, &mem_default_allocator);

  htable_int_float_init(&allocator_proxy, &htbl);
  CHK(htable_int_float_reserve(&htbl, 30) == RES_OK);

  FOR_EACH(i, 0, n) {
    float f = (float)i;
    CHK(htable_int_float_set(&htbl, &i, &f) == RES_OK);
  }

  FOR_EACH(i, 0, n) {
    float* p = htable_int_float_find(&htbl, &i);
    struct htable_int_float_iterator it;
    struct htable_int_float_iterator end;
    htable_int_float_find_iterator(&htbl, &i, &it);
    htable_int_float_end(&htbl, &end);
    CHK(p != NULL);
    CHK(*p == (float) i);
    CHK(!htable_int_float_iterator_eq(&it, &end));
    CHK(*htable_int_float_iterator_key_get(&it) == i);
    CHK(*htable_int_float_iterator_data_get(&it) == (float)i);
  }
  CHK(htable_int_float_size_get(&htbl) == (size_t)n);
  FOR_EACH(i, 0, n / 2) {
    CHK(htable_int_float_erase(&htbl, &i) == 1);
  }
  CHK(htable_int_float_size_get(&htbl) == (size_t)(n/2));
  FOR_EACH(i, 0, n/2) {
    float* p = htable_int_float_find(&htbl, &i);
    struct htable_int_float_iterator it;
    struct htable_int_float_iterator end;
    htable_int_float_find_iterator(&htbl, &i, &it);
    htable_int_float_end(&htbl, &end);
    CHK(p == NULL);
    CHK(htable_int_float_iterator_eq(&it, &end));
  }
  FOR_EACH(i, n/2, n) {
    float* p = htable_int_float_find(&htbl, &i);
    struct htable_int_float_iterator it;
    struct htable_int_float_iterator end;
    htable_int_float_find_iterator(&htbl, &i, &it);
    htable_int_float_end(&htbl, &end);
    CHK(p != NULL);
    CHK(*p == (float) i);
    CHK(!htable_int_float_iterator_eq(&it, &end));
    CHK(*htable_int_float_iterator_key_get(&it) == i);
    CHK(*htable_int_float_iterator_data_get(&it) == (float)i);
  }
  htable_int_float_release(&htbl);

  check_memory_allocator(&allocator_proxy);
  mem_shutdown_proxy_allocator(&allocator_proxy);
}

static INLINE char
eq_str(const struct str* a, const struct str* b)
{
  return !strcmp(str_cget(a), str_cget(b));
}

static INLINE size_t
hash_str(const struct str* a)
{
  return hash_fnv32(str_cget(a), str_len(a));
}

#define HTABLE_KEY struct str
#define HTABLE_DATA int
#define HTABLE_NAME str_int
#define HTABLE_KEY_FUNCTOR_INIT str_init
#define HTABLE_KEY_FUNCTOR_RELEASE str_release
#define HTABLE_KEY_FUNCTOR_COPY str_copy
#define HTABLE_KEY_FUNCTOR_COPY_AND_RELEASE str_copy_and_release
#define HTABLE_KEY_FUNCTOR_EQ eq_str
#define HTABLE_KEY_FUNCTOR_HASH hash_str
#include "hash_table.h"

static void
test_htbl_str_int(void)
{
  struct mem_allocator allocator_proxy;
  struct htable_str_int htbl;
  struct htable_str_int_iterator it0;
  struct htable_str_int_iterator it1;
  struct htable_str_int_iterator it2;
  const char* str[] = {
    "analyse", "analysis", "analyst", "analytic", "analytical", "analytically",
    "analyze", "approach", "approachable", "area", "assess", "assessable",
    "assessment", "assume", "assumed", "assuming", "assumption",
    "authoritative", "authoritatively", "authority", "availability",
    "available", "beneficial", "beneficiary", "benefit", "blinker", "concept",
    "conception", "conceptual", "conceptualize", "conceptually", "consist",
    "consistency", "consistent", "consistently", "constituency", "constituent",
    "constitute", "constitution", "constitutional", "constitutionally",
    "constitutive", "context", "contextual", "contextualization",
    "contextualize", "contextually", "contract", "contractor", "create",
    "creation", "creative", "creatively", "creativity", "creator", "data",
    "definable", "define", "definition", "derivation", "derivative", "derive",
    "disestablish", "disestablishment", "dissimilar", "dissimilarity",
    "distribute", "distribution", "distributional", "distributive",
    "distributor", "economic", "economical", "economically", "economics",
    "economist", "economy", "environment", "environmental", "environmentalism",
    "environmentalist", "environmentally", "establish", "established",
    "establishment", "estimate", "estimation", "evidence", "evident",
    "evidential", "evidently", "export", "exporter", "factor", "finance",
    "financial", "financially", "financier", "financing", "formula"
  };
  char str_found[sizeof(str) / sizeof(const char*)];
  const int str_size = (int)(sizeof(str) / sizeof(const char*));
  int array[32];
  const int array_size = (int)(sizeof(array) / sizeof(int));
  struct str tmp;
  int i = 0;
  struct str* key = NULL;
  int* data = NULL;
  STATIC_ASSERT
    (sizeof(str)/sizeof(const char*) >= sizeof(array)/sizeof(int),
      Unexpected_array_size);

  mem_init_proxy_allocator(&allocator_proxy, &mem_default_allocator);

  FOR_EACH(i, 0, (int)array_size) {
    /* Compute a str id that is not already registered into array */
    int j = rand() % str_size;
    for(;;) {
      int k;
      FOR_EACH(k, 0, i) {
        if(array[k] == j) break;
      }
      if(k >= i) break;
      j = j + 1 % str_size;
    }
    array[i] = j;
  }

  str_init(&allocator_proxy, &tmp);

  htable_str_int_init(&allocator_proxy, &htbl);
  CHK(htable_str_int_is_empty(&htbl) == 1);
  str_set(&tmp, "empty");
  CHK(htable_str_int_find(&htbl, &tmp) == NULL);
  CHK(htable_str_int_size_get(&htbl) == 0);
  htable_str_int_find_iterator(&htbl, &tmp, &it0);
  htable_str_int_end(&htbl, &it1);
  CHK(htable_str_int_iterator_eq(&it0, &it1));
  htable_str_int_iterator_next(&it0);
  CHK(htable_str_int_iterator_eq(&it0, &it1));
  i = 0;
  str_set(&tmp, str[i]);
  CHK(htable_str_int_set(&htbl, &tmp, &i) == RES_OK);
  CHK(htable_str_int_is_empty(&htbl) == 0);
  CHK(htable_str_int_size_get(&htbl) == 1);

  FOR_EACH(i, 1, str_size) {
    str_set(&tmp, str[i]);
    CHK(htable_str_int_set(&htbl, &tmp, &i) == RES_OK);
  }
  CHK(htable_str_int_size_get(&htbl) == (size_t)str_size);

  str_set(&tmp, str[str_size/2]);
  htable_str_int_find_iterator(&htbl, &tmp, &it0);
  htable_str_int_end(&htbl, &it1);
  CHK(!htable_str_int_iterator_eq(&it0, &it1));
  while(!htable_str_int_iterator_eq(&it0, &it1)) {
    key = htable_str_int_iterator_key_get(&it0);
    data = htable_str_int_iterator_data_get(&it0);
    CHK(*htable_str_int_find(&htbl, key) == *data);
    htable_str_int_iterator_next(&it0);
  }

  str_set(&tmp, "Terra icognita");
  CHK(htable_str_int_find(&htbl, &tmp) == NULL);
  FOR_EACH(i, 0, 64) {
    int j = rand() % str_size;
    str_set(&tmp, str[j]);
    data = htable_str_int_find(&htbl, &tmp);
    CHK(data != NULL);
    CHK(*data == (int)j);
    htable_str_int_find_iterator(&htbl, &tmp, &it0);
    htable_str_int_end(&htbl, &it1);
    CHK(!htable_str_int_iterator_eq(&it0, &it1));
    CHK(!str_cmp(htable_str_int_iterator_key_get(&it0), &tmp));
    CHK(*htable_str_int_iterator_data_get(&it0) == (int)j);
  }

  str_set(&tmp, "Terra icognita");
  CHK(htable_str_int_erase(&htbl, &tmp) == 0);
  FOR_EACH(i, 0, array_size) {
    str_set(&tmp, str[array[i]]);
    CHK(htable_str_int_erase(&htbl, &tmp) == 1);
  }
  FOR_EACH(i, 0, array_size) {
    str_set(&tmp, str[array[i]]);
    CHK(htable_str_int_find(&htbl, &tmp) == 0);
    htable_str_int_find_iterator(&htbl, &tmp, &it0);
    htable_str_int_end(&htbl, &it1);
    CHK(htable_str_int_iterator_eq(&it0, &it1));
  }
  CHK(htable_str_int_size_get(&htbl) == (size_t)(str_size - array_size));
  FOR_EACH(i, 0, str_size) {
    int j = 0;
    FOR_EACH(j, 0, array_size) {
      if(array[j] == i)
        break;
    }
    if(j >= array_size) {
      str_set(&tmp, str[i]);
      data = htable_str_int_find(&htbl, &tmp);
      CHK(data != NULL);
      CHK(*data == (int)i);
      htable_str_int_find_iterator(&htbl, &tmp, &it0);
      htable_str_int_end(&htbl, &it1);
      CHK(!htable_str_int_iterator_eq(&it0, &it1));
      CHK(!str_cmp(htable_str_int_iterator_key_get(&it0), &tmp));
      CHK(*htable_str_int_iterator_data_get(&it0) == (int)i);
    }
  }
  FOR_EACH(i, 0, array_size) {
    str_set(&tmp, str[array[i]]);
    CHK(htable_str_int_set(&htbl, &tmp, array + i) == RES_OK);
  }
  CHK(htable_str_int_size_get(&htbl) == (size_t)str_size);
  CHK(htable_str_int_is_empty(&htbl) == 0);

  htable_str_int_begin(&htbl, &it0);
  htable_str_int_end(&htbl, &it1);
  CHK(htable_str_int_iterator_eq(&it0, &it0) == 1);
  CHK(htable_str_int_iterator_eq(&it0, &it1) == 0);
  memset(str_found, 0, sizeof(str_found));
  while(!htable_str_int_iterator_eq(&it0, &it1) ) {
    key = htable_str_int_iterator_key_get(&it0);
    data = htable_str_int_iterator_data_get(&it0);
    CHK(*htable_str_int_find(&htbl, key) == *data);

    htable_str_int_find_iterator(&htbl, key, &it2);
    CHK(htable_str_int_iterator_eq(&it0, &it2));
    CHK(!str_cmp(htable_str_int_iterator_key_get(&it2), key));
    CHK(*htable_str_int_iterator_data_get(&it2) == *data);

    CHK(str_found[*data] == 0);
    str_found[*data] = 1;
    htable_str_int_iterator_next(&it0);
  }

  FOR_EACH(i, 0, str_size) {
    CHK(str_found[i] == 1);
  }

  htable_str_int_purge(&htbl);
  htable_str_int_begin(&htbl, &it0);
  htable_str_int_end(&htbl, &it1);
  CHK(htable_str_int_iterator_eq(&it0, &it1) == 1);

  CHK(htable_str_int_is_empty(&htbl) == 1);
  CHK(htable_str_int_size_get(&htbl) == 0);
  htable_str_int_release(&htbl);

  htable_str_int_init(&allocator_proxy, &htbl);
  htable_str_int_reserve(&htbl, 3);
  str_set(&tmp, "Zero"), i = 0;
  CHK(htable_str_int_set(&htbl, &tmp, &i) == RES_OK);
  str_set(&tmp, "One"), i = 1;
  CHK(htable_str_int_set(&htbl, &tmp, &i) == RES_OK);
  str_set(&tmp, "Two"), i = 2;
  CHK(htable_str_int_set(&htbl, &tmp, &i) == RES_OK);
  str_set(&tmp, "Three"), i = 3;
  CHK(htable_str_int_set(&htbl, &tmp, &i) == RES_OK);
  str_set(&tmp, "Four"), i = 4;
  CHK(htable_str_int_set(&htbl, &tmp, &i) == RES_OK);
  CHK(htable_str_int_size_get(&htbl) == 5);
  str_set(&tmp, "Zero"), i = 'a';
  CHK(htable_str_int_set(&htbl, &tmp, &i) == RES_OK);
  CHK(htable_str_int_size_get(&htbl) == 5);

  data = htable_str_int_find(&htbl, &tmp);
  htable_str_int_find_iterator(&htbl, &tmp, &it0);
  htable_str_int_end(&htbl, &it1);
  CHK(data != NULL);
  CHK(*data == 'a');
  CHK(!htable_str_int_iterator_eq(&it0, &it1));
  CHK(!str_cmp(htable_str_int_iterator_key_get(&it0), &tmp));
  CHK(*htable_str_int_iterator_data_get(&it0) == *data);

  htable_str_int_clear(&htbl);
  htable_str_int_begin(&htbl, &it0);
  htable_str_int_end(&htbl, &it1);
  CHK(htable_str_int_iterator_eq(&it0, &it1) == 1);
  CHK(htable_str_int_is_empty(&htbl) == 1);
  CHK(htable_str_int_size_get(&htbl) == 0);

  htable_str_int_release(&htbl);

  str_release(&tmp);

  check_memory_allocator(&allocator_proxy);
  mem_shutdown_proxy_allocator(&allocator_proxy);
}

#define HTABLE_KEY struct str
#define HTABLE_DATA struct darray_char
#define HTABLE_NAME str_darray
#define HTABLE_KEY_FUNCTOR_INIT str_init
#define HTABLE_KEY_FUNCTOR_RELEASE str_release
#define HTABLE_KEY_FUNCTOR_COPY str_copy
#define HTABLE_KEY_FUNCTOR_COPY_AND_RELEASE str_copy_and_release
#define HTABLE_KEY_FUNCTOR_EQ eq_str
#define HTABLE_KEY_FUNCTOR_HASH hash_str
#define HTABLE_DATA_FUNCTOR_INIT darray_char_init
#define HTABLE_DATA_FUNCTOR_RELEASE darray_char_release
#define HTABLE_DATA_FUNCTOR_COPY darray_char_copy
#define HTABLE_DATA_FUNCTOR_COPY_AND_RELEASE darray_char_copy_and_release
#include "hash_table.h"

static void
test_htbl_str_darray(void)
{
  struct mem_allocator allocator_proxy;
  struct htable_str_darray htbl, htbl2, htbl3;
  struct htable_str_darray_iterator it0;
  struct htable_str_darray_iterator it1;
  struct darray_char darray;
  struct darray_char* data = NULL;
  struct str* key = NULL;
  struct str tmp;
  const char* str[] = {
"Rcvfbqr 1, XARR-QRRC VA GUR QRNQ:\n\
---------------------------------",

"BAPR LBH ORNG GUR OVT ONQNFFRF NAQ PYRNA BHG GUR ZBBA ONFR LBH'ER FHCCBFRQ GB\n\
JVA, NERA'G LBH? NERA'G LBH? JURER'F LBHE SNG ERJNEQ NAQ GVPXRG UBZR? JUNG\n\
GUR URYY VF GUVF? VG'F ABG FHCCBFRQ GB RAQ GUVF JNL!",

"VG FGVAXF YVXR EBGGRA ZRNG, OHG YBBXF YVXR GUR YBFG QRVZBF ONFR. YBBXF YVXR\n\
LBH'ER FGHPX BA GUR FUBERF BS URYY. GUR BAYL JNL BHG VF GUEBHTU.",

"GB PBAGVAHR GUR QBBZ RKCREVRAPR, CYNL GUR FUBERF BS URYY NAQ VGF NZNMVAT\n\
FRDHRY, VASREAB!",

"Rcvfbqr 2, GUR FUBERF BS URYY:\n\
------------------------------",

"LBH'IR QBAR VG! GUR UVQRBHF PLORE- QRZBA YBEQ GUNG EHYRQ GUR YBFG QRVZBF ZBBA\n\
ONFR UNF ORRA FYNVA NAQ LBH NER GEVHZCUNAG! OHG ... JURER NER LBH? LBH\n\
PYNZORE GB GUR RQTR BS GUR ZBBA NAQ YBBX QBJA GB FRR GUR NJSHY GEHGU.",

"QRVZBF SYBNGF NOBIR URYY VGFRYS!  LBH'IR ARIRE URNEQ BS NALBAR RFPNCVAT SEBZ\n\
URYY, OHG LBH'YY ZNXR GUR ONFGNEQF FBEEL GURL RIRE URNEQ BS LBH! DHVPXYL, LBH\n\
ENCCRY QBJA GB GUR FHESNPR BS URYY.",

"ABJ, VG'F BA GB GUR SVANY PUNCGRE BS QBBZ! -- VASREAB.",

"Rcvfbqr 3, VASREAB:\n\
-------------------",

"GUR YBNGUFBZR FCVQREQRZBA GUNG ZNFGREZVAQRQ GUR VAINFVBA BS GUR ZBBA ONFRF\n\
NAQ PNHFRQ FB ZHPU QRNGU UNF UNQ VGF NFF XVPXRQ SBE NYY GVZR.",

"N UVQQRA QBBEJNL BCRAF NAQ LBH RAGRE.  LBH'IR CEBIRA GBB GBHTU SBE URYY GB\n\
PBAGNVA, NAQ ABJ URYY NG YNFG CYNLF SNVE -- SBE LBH RZRETR SEBZ GUR QBBE GB\n\
FRR GUR TERRA SVRYQF BS RNEGU!  UBZR NG YNFG.",

"LBH JBAQRE JUNG'F ORRA UNCCRAVAT BA RNEGU JUVYR LBH JRER ONGGYVAT RIVY\n\
HAYRNFURQ. VG'F TBBQ GUNG AB URYY- FCNJA PBHYQ UNIR PBZR GUEBHTU GUNG QBBE\n\
JVGU LBH ...",

"Rcvfbqr 4, GUL SYRFU PBAFHZRQ:\n\
------------------------------",

"GUR FCVQRE ZNFGREZVAQ ZHFG UNIR FRAG SBEGU VGF YRTVBAF BS URYYFCNJA ORSBER\n\
LBHE SVANY PBASEBAGNGVBA JVGU GUNG GREEVOYR ORNFG SEBZ URYY. OHG LBH FGRCCRQ\n\
SBEJNEQ NAQ OEBHTUG SBEGU RGREANY QNZANGVBA NAQ FHSSREVAT HCBA GUR UBEQR NF N\n\
GEHR UREB JBHYQ VA GUR SNPR BS FBZRGUVAT FB RIVY.",

"ORFVQRF, FBZRBAR JNF TBAAN CNL SBE JUNG UNCCRARQ GB QNVFL, LBHE CRG ENOOVG.",

"OHG ABJ, LBH FRR FCERNQ ORSBER LBH ZBER CBGRAGVNY CNVA NAQ TVOOVGHQR NF N\n\
ANGVBA BS QRZBAF EHA NZBX VA BHE PVGVRF.",

"ARKG FGBC, URYY BA RNEGU!"
  };
  size_t nstrs = sizeof(str)/sizeof(const char*);
  size_t i = 0, j = 0, k = 0;
  size_t buf[16];
  const size_t nerase = 3;

  mem_init_proxy_allocator(&allocator_proxy, &mem_default_allocator);
  htable_str_darray_init(&allocator_proxy, &htbl);
  htable_str_darray_init(&allocator_proxy, &htbl2);
  htable_str_darray_init(&allocator_proxy, &htbl3);
  darray_char_init(&allocator_proxy, &darray);
  str_init(&allocator_proxy, &tmp);

  FOR_EACH(i, 0, nstrs) {
    darray_char_clear(&darray);
    FOR_EACH(j, 0, strlen(str[i]) + 1) {
      CHK(darray_char_push_back(&darray, str[i] + j) == RES_OK);
    }
    str_set(&tmp, str[i]);
    CHK(htable_str_darray_set(&htbl, &tmp, &darray) == RES_OK);
  }

  CHK(htable_str_darray_size_get(&htbl) == nstrs);
  CHK(htable_str_darray_size_get(&htbl2) == 0);
  CHK(htable_str_darray_size_get(&htbl3) == 0);

  CHK(htable_str_darray_copy(&htbl3, &htbl) == RES_OK);
  CHK(htable_str_darray_copy(&htbl, &htbl2) == RES_OK);

  CHK(htable_str_darray_size_get(&htbl) == 0);
  CHK(htable_str_darray_size_get(&htbl2) == 0);
  CHK(htable_str_darray_size_get(&htbl3) == nstrs);

  CHK(htable_str_darray_copy(&htbl, &htbl3) == RES_OK);
  CHK(htable_str_darray_copy_and_clear(&htbl2, &htbl3) == RES_OK);

  CHK(htable_str_darray_size_get(&htbl) == nstrs);
  CHK(htable_str_darray_size_get(&htbl2) == nstrs);
  CHK(htable_str_darray_size_get(&htbl3) == 0);

  FOR_EACH(i, 0, nstrs) {
    darray_char_clear(&darray);
    FOR_EACH(j, 0, strlen(str[i]) + 1) {
      CHK(darray_char_push_back(&darray, str[i] + j) == RES_OK);
    }
    str_set(&tmp, str[i]);
    CHK(htable_str_darray_set(&htbl3, &tmp, &darray) == RES_OK);
  }

  CHK(htable_str_darray_size_get(&htbl3) == nstrs);

  FOR_EACH(i, 0, nstrs) {
    str_set(&tmp, str[i]);

    data = htable_str_darray_find(&htbl, &tmp);
    htable_str_darray_find_iterator(&htbl, &tmp, &it0);
    htable_str_darray_end(&htbl, &it1);
    CHK(data != NULL);
    CHK(strcmp(darray_char_cdata_get(data), str[i]) == 0);
    CHK(!htable_str_darray_iterator_eq(&it0, &it1));
    key = htable_str_darray_iterator_key_get(&it0);
    data = htable_str_darray_iterator_data_get(&it0);
    CHK(!str_cmp(key, &tmp));
    CHK(strcmp(darray_char_cdata_get(data), str[i]) == 0);

    data = htable_str_darray_find(&htbl2, &tmp);
    htable_str_darray_find_iterator(&htbl2, &tmp, &it0);
    htable_str_darray_end(&htbl2, &it1);
    CHK(data != NULL);
    CHK(strcmp(darray_char_cdata_get(data), str[i]) == 0);
    CHK(!htable_str_darray_iterator_eq(&it0, &it1));
    key = htable_str_darray_iterator_key_get(&it0);
    data = htable_str_darray_iterator_data_get(&it0);
    CHK(!str_cmp(key, &tmp));
    CHK(strcmp(darray_char_cdata_get(data), str[i]) == 0);

    data = htable_str_darray_find(&htbl3, &tmp);
    htable_str_darray_find_iterator(&htbl3, &tmp, &it0);
    htable_str_darray_end(&htbl3, &it1);
    CHK(data != NULL);
    CHK(strcmp(darray_char_cdata_get(data), str[i]) == 0);
    CHK(!htable_str_darray_iterator_eq(&it0, &it1));
    key = htable_str_darray_iterator_key_get(&it0);
    data = htable_str_darray_iterator_data_get(&it0);
    CHK(!str_cmp(key, &tmp));
    CHK(strcmp(darray_char_cdata_get(data), str[i]) == 0);
  }

  FOR_EACH(j, 0, nerase) {
    for(;;) {
      i = (size_t)rand() % nstrs;
      FOR_EACH(k, 0, j)
        if(buf[k] == i) break;
      if(k >= j) break;
    }

    buf[j] = i;

    str_set(&tmp, str[i]);
    data = htable_str_darray_find(&htbl, &tmp);
    htable_str_darray_find_iterator(&htbl, &tmp, &it0);
    htable_str_darray_end(&htbl, &it1);
    CHK(data != NULL);
    CHK(strcmp(darray_char_cdata_get(data), str_cget(&tmp)) == 0);
    key = htable_str_darray_iterator_key_get(&it0);
    data = htable_str_darray_find(&htbl, &tmp);
    CHK(!str_cmp(key, &tmp));
    CHK(strcmp(darray_char_cdata_get(data), str_cget(&tmp)) == 0);
    CHK(htable_str_darray_erase(&htbl, &tmp) == 1);
    CHK(htable_str_darray_erase(&htbl, &tmp) == 0);
  }
  CHK(htable_str_darray_size_get(&htbl) == nstrs - nerase);

  FOR_EACH(j, 0, nerase) {
    str_set(&tmp, str[buf[j]]);

    data = htable_str_darray_find(&htbl, &tmp);
    htable_str_darray_find_iterator(&htbl, &tmp, &it0);
    htable_str_darray_end(&htbl, &it1);
    CHK(data == NULL);
    CHK(htable_str_darray_iterator_eq(&it0, &it1));

    data = htable_str_darray_find(&htbl2, &tmp);
    htable_str_darray_find_iterator(&htbl2, &tmp, &it0);
    htable_str_darray_end(&htbl2, &it1);
    CHK(data != NULL);
    CHK(strcmp(darray_char_cdata_get(data), str[buf[j]]) == 0);
    CHK(!htable_str_darray_iterator_eq(&it0, &it1));
    key = htable_str_darray_iterator_key_get(&it0);
    data = htable_str_darray_iterator_data_get(&it0);
    CHK(!str_cmp(key, &tmp));
    CHK(strcmp(darray_char_cdata_get(data), str[buf[j]]) == 0);

    data = htable_str_darray_find(&htbl3, &tmp);
    htable_str_darray_find_iterator(&htbl3, &tmp, &it0);
    htable_str_darray_end(&htbl3, &it1);
    CHK(data != NULL);
    CHK(strcmp(darray_char_cdata_get(data), str[buf[j]]) == 0);
    CHK(!htable_str_darray_iterator_eq(&it0, &it1));
    key = htable_str_darray_iterator_key_get(&it0);
    data = htable_str_darray_iterator_data_get(&it0);
    CHK(!str_cmp(key, &tmp));
    CHK(strcmp(darray_char_cdata_get(data), str[buf[j]]) == 0);
  }


  htable_str_darray_begin(&htbl, &it0);
  htable_str_darray_end(&htbl, &it1);
  while(!htable_str_darray_iterator_eq(&it0, &it1)) {
    key = htable_str_darray_iterator_key_get(&it0);
    data = htable_str_darray_iterator_data_get(&it0);
    CHK(strcmp(str_cget(key), darray_char_cdata_get(data)) == 0);
    htable_str_darray_iterator_next(&it0);
  }

  CHK(htable_str_darray_reserve(&htbl, 2891) == RES_OK);

  FOR_EACH(j, 0, 5) {
    for(;;) {
      i = (size_t)rand() % nstrs;
      FOR_EACH(k, 0, nerase)
        if(buf[k] == i) break;
      if(k >= nerase) break;
    }
    str_set(&tmp, str[i]);
    data = htable_str_darray_find(&htbl, &tmp);
    CHK(data != NULL);
    CHK(strcmp(darray_char_cdata_get(data), str_cget(&tmp)) == 0);
  }

  htable_str_darray_copy_and_release(&htbl, &htbl3);
  FOR_EACH(i, 0, nstrs) {
    str_set(&tmp, str[i]);

    data = htable_str_darray_find(&htbl, &tmp);
    CHK(data != NULL);
    CHK(strcmp(darray_char_cdata_get(data), str[i]) == 0);

    data = htable_str_darray_find(&htbl2, &tmp);
    CHK(data != NULL);
    CHK(strcmp(darray_char_cdata_get(data), str[i]) == 0);
  }

  str_release(&tmp);
  darray_char_release(&darray);
  htable_str_darray_release(&htbl);
  htable_str_darray_release(&htbl2);
  check_memory_allocator(&allocator_proxy);
  mem_shutdown_proxy_allocator(&allocator_proxy);
}

int
main(int argc, char** argv)
{
  (void)argc, (void)argv;
  test_htbl_int_float();
  test_htbl_str_int();
  test_htbl_str_darray();
  return 0;
}
