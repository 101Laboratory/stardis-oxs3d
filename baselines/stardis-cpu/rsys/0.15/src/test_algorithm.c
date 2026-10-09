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

#include "algorithm.h"

struct data {
  char dummy[16];
  double dbl;
};

static int
cmp_int(const void* a, const void* b)
{
  CHK(a != NULL);
  CHK(b != NULL);
  return *(const int*)a - *(const int*)b;
}

static int
cmp_dbl(const void* a, const void* b)
{
  CHK(a != NULL);
  CHK(b != NULL);
  return *(const double*)a < *(const double*) b
    ? -1 : (*(const double*)a > *(const double*)b ? 1 : 0);
}

static int
cmp_key_data(const void* key, const void* data)
{
  const double h = *((const double*)key);
  const struct data* d = (const struct data*)data;
  return h < d->dbl ? -1 : (h > d->dbl ? 1 : 0);
}

int
main(int argc, char** argv)
{
  const int array_int[] = { 0, 2, 4, 4, 4, 6, 10, 11, 13, 15, 17 };
  const size_t nints = sizeof(array_int)/sizeof(int);
  const double array_dbl[] = {
    -1.123, 2.3, 2.3, 2.3, 4, 5.0, 5.2, 6, 9.1, 19.123
  };
  const size_t ndbls = sizeof(array_dbl)/sizeof(double);
  const struct data data[] = {
    {{0},-1.123},
    {{0},2.3},
    {{0},2.3},
    {{0},2.3},
    {{0},4.0},
    {{0},5.0},
    {{0},5.2},
    {{0},6.0},
    {{0},9.1},
    {{0},19.123}
  };
  const size_t ndata = sizeof(data)/sizeof(struct data);

  int* pi;
  int i;
  double* pd;
  struct data* pdata;
  double d;
  (void)argc, (void)argv;

  #define SEARCH search_lower_bound

  i = 3, pi = SEARCH(&i, array_int, nints, sizeof(int), cmp_int);
  CHK(*pi == 4);
  CHK(pi[3] == 6);
  i = 4, pi = SEARCH(&i, array_int, nints, sizeof(int), cmp_int);
  CHK(*pi == 4);
  CHK(pi[3] == 6);
  i = 5, pi = SEARCH(&i, array_int, nints, sizeof(int), cmp_int);
  CHK(*pi == 6);
  i = 13, pi = SEARCH(&i, array_int, nints, sizeof(int), cmp_int);
  CHK(*pi == 13);
  i = -1, pi = SEARCH(&i, array_int, nints, sizeof(int), cmp_int);
  CHK(*pi == 0);
  i = 19, pi = SEARCH(&i, array_int, nints, sizeof(int), cmp_int);
  CHK(pi == NULL);

  d = 2.1, pd = SEARCH(&d, array_dbl, ndbls, sizeof(double), cmp_dbl);
  CHK(*pd == 2.3);
  d = 2.3, pd = SEARCH(&d, array_dbl, ndbls, sizeof(double), cmp_dbl);
  CHK(*pd == 2.3);
  CHK(pd[3] == 4);
  d = -1.0, pd = SEARCH(&d, array_dbl, ndbls, sizeof(double), cmp_dbl);
  CHK(*pd == 2.3);
  d = 6.001, pd = SEARCH(&d, array_dbl, ndbls, sizeof(double), cmp_dbl);
  CHK(*pd == 9.1);
  d = 19.0, pd = SEARCH(&d, array_dbl, ndbls, sizeof(double), cmp_dbl);
  CHK(*pd == 19.123);
  d = 20.0, pd = SEARCH(&d, array_dbl, ndbls, sizeof(double), cmp_dbl);
  CHK(pd == NULL);

  d = 2.1, pdata = SEARCH(&d, data, ndata, sizeof(struct data), cmp_key_data);
  CHK(pdata->dbl == 2.3);
  d = 2.3, pdata = SEARCH(&d, data, ndata, sizeof(struct data), cmp_key_data);
  CHK(pdata->dbl == 2.3);
  CHK(pdata[3].dbl == 4);
  d = -1.0, pdata = SEARCH(&d, data, ndata, sizeof(struct data), cmp_key_data);
  CHK(pdata->dbl == 2.3);
  d = 6.001, pdata = SEARCH(&d, data, ndata, sizeof(struct data), cmp_key_data);
  CHK(pdata->dbl == 9.1);
  d = 19.0, pdata = SEARCH(&d, data, ndata, sizeof(struct data), cmp_key_data);
  CHK(pdata->dbl == 19.123);
  d = 20.0, pdata = SEARCH(&d, data, ndata, sizeof(struct data), cmp_key_data);
  CHK(pdata == NULL);

  i = -1, pi = SEARCH(&i, array_int, 1, sizeof(int), cmp_int);
  CHK(*pi ==  0);

  return 0;
}
