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

#define _POSIX_C_SOURCE 200112L /* strtok_r support */

#include "cstr.h"
#include "rsys_math.h"

#include <string.h>

static void
test_double(void)
{
  double d;
  CHK(cstr_to_double(NULL, &d) ==  RES_BAD_ARG);
  CHK(cstr_to_double("a", &d) ==  RES_BAD_ARG);
  CHK(cstr_to_double(STR(PI), &d) ==  RES_OK);
  CHK(d ==  PI);
  CHK(cstr_to_double("  1.e-3", &d) ==  RES_OK);
  CHK(d ==  1.e-3);
  CHK(cstr_to_double("+1.E+3 ", &d) ==  RES_OK);
  CHK(d ==  1.e3);
  CHK(cstr_to_double("INF", &d) ==  RES_OK);
  CHK(d ==  INF);
  CHK(cstr_to_double("INFINITY", &d) ==  RES_OK);
  CHK(d ==  INF);
  CHK(cstr_to_double("", &d) ==  RES_BAD_ARG);
}

static void
test_float(void)
{
  float f;
  CHK(cstr_to_float(NULL, &f) ==  RES_BAD_ARG);
  CHK(cstr_to_float("a", &f) ==  RES_BAD_ARG);
  CHK(cstr_to_float(STR(PI), &f) ==  RES_OK);
  CHK(f ==  (float)PI);
  CHK(cstr_to_float("1.e-1", &f) ==  RES_OK);
  CHK(f ==  1.e-1f);
  CHK(cstr_to_float("+1.E+3", &f) ==  RES_OK);
  CHK(f ==  1.e3f);
  CHK(cstr_to_float("INF", &f) ==  RES_OK);
  CHK(f ==  (float)INF);
  CHK(cstr_to_float("INFINITY", &f) ==  RES_OK);
  CHK(f ==  (float)INF);
  CHK(cstr_to_float(STR(DBL_MAX), &f) ==  RES_BAD_ARG);
  CHK(cstr_to_float(STR(DBL_MIN), &f) ==  RES_BAD_ARG);
  CHK(cstr_to_float("0.0", &f) ==  RES_OK);
  CHK(f ==  0.f);
  CHK(cstr_to_float("-0.0", &f) ==  RES_OK);
  CHK(f ==  0.f);
}

static void
test_long(void)
{
  long l;
  CHK(cstr_to_long(NULL, &l) ==  RES_BAD_ARG);
  CHK(cstr_to_long("a", &l) ==  RES_BAD_ARG);
  CHK(cstr_to_long("1.e-3", &l) ==  RES_BAD_ARG);
  CHK(cstr_to_long("1", &l) ==  RES_OK);
  CHK(l ==  1);
  CHK(cstr_to_long("-1", &l) ==  RES_OK);
  CHK(l ==  -1);
  CHK(cstr_to_long("+1234567890", &l) ==  RES_OK);
  CHK(l ==  1234567890);
  CHK(cstr_to_long("     \t+1234567890  \t  ", &l) ==  RES_OK);
  CHK(l ==  1234567890);
  CHK(cstr_to_long("     \t+1234567890  \t  a", &l) ==  RES_BAD_ARG);
}

static void
test_int(void)
{
  char buf[128];
  int i;

  CHK(cstr_to_int(NULL, &i) ==  RES_BAD_ARG);
  CHK(cstr_to_int("a", &i) ==  RES_BAD_ARG);
  CHK(cstr_to_int("1.e-1", &i) ==  RES_BAD_ARG);
  CHK(cstr_to_int("1", &i) ==  RES_OK);
  CHK(i ==  1);
  CHK(cstr_to_int("-2", &i) ==  RES_OK);
  CHK(i ==  -2);
  CHK(cstr_to_int("\t-2  ", &i) ==  RES_OK);
  CHK(i ==  -2);
  sprintf(buf, "%d", INT_MAX);
  CHK(cstr_to_int(buf, &i) ==  RES_OK);
  CHK(i ==  INT_MAX);
  sprintf(buf, "%d", INT_MIN);
  CHK(cstr_to_int(buf, &i) ==  RES_OK);
  CHK(i ==  INT_MIN);
  sprintf(buf, "%lld", (long long)((int64_t)INT_MAX+1));
  CHK(cstr_to_int(buf, &i) ==  RES_BAD_ARG);
  sprintf(buf, "%lld", (long long)((int64_t)INT_MIN-1));
  CHK(cstr_to_int(buf, &i) ==  RES_BAD_ARG);
}

static void
test_uint(void)
{
  char buf[128];
  unsigned u;

  CHK(cstr_to_uint(NULL, &u) ==  RES_BAD_ARG);
  CHK(cstr_to_uint("a", &u) ==  RES_BAD_ARG);
  CHK(cstr_to_uint("-1", &u) ==  RES_OK);
  CHK(u ==  UINT_MAX);
  CHK(cstr_to_uint("0.", &u) ==  RES_BAD_ARG);
  CHK(cstr_to_uint("0", &u) ==  RES_OK);
  CHK(u ==  0);
  CHK(cstr_to_uint("+2", &u) ==  RES_OK);
  CHK(u ==  2);
  CHK(cstr_to_uint(" \t+123 \t ", &u) ==  RES_OK);
  CHK(u ==  123);
  sprintf(buf, "%u", UINT_MAX);
  CHK(cstr_to_uint(buf, &u) ==  RES_OK);
  CHK(u ==  UINT_MAX);
  sprintf(buf, "%llu", (unsigned long long)((uint64_t)UINT_MAX+1));
  CHK(cstr_to_uint(buf, &u) ==  RES_BAD_ARG);
}

static void
test_ulong(void)
{
  char buf[128];
  unsigned long ul;

  CHK(cstr_to_ulong(NULL, &ul) ==  RES_BAD_ARG);
  CHK(cstr_to_ulong("a", &ul) ==  RES_BAD_ARG);
  CHK(cstr_to_ulong("-1", &ul) ==  RES_OK);
  CHK(ul ==  ULONG_MAX);
  CHK(cstr_to_ulong("0.", &ul) ==  RES_BAD_ARG);
  CHK(cstr_to_ulong("0", &ul) ==  RES_OK);
  CHK(ul ==  0);
  CHK(cstr_to_ulong("+2", &ul) ==  RES_OK);
  CHK(ul ==  2);
  CHK(cstr_to_ulong(" \t+123 \t ", &ul) ==  RES_OK);
  CHK(ul ==  123);
  sprintf(buf, "%lu", ULONG_MAX);
  CHK(cstr_to_ulong(buf, &ul) ==  RES_OK);
  CHK(ul ==  ULONG_MAX);
  sprintf(buf, "%lu%c", ULONG_MAX, '0');
  CHK(cstr_to_ulong(buf, &ul) ==  RES_BAD_ARG);
}

static res_T
count_elements(const char* str, void* ptr)
{
  int* counter = ptr;
  *counter += 1;
  CHK(str && str[0] != '\0');
  return RES_OK;
}

static res_T
parse_elmt(const char* str, void* ptr)
{
  char buf[32];
  char* key = NULL;
  char* val = NULL;
  char* tk_ctx = NULL;
  int i;
  (void)ptr;

  CHK(str && str[0] != '\0');
  CHK(strlen(str)+1/*Null char*/ < sizeof(buf));

  strncpy(buf, str, sizeof(buf));
#if defined(OS_WINDOWS)
  key = strtok_s(buf, "=", &tk_ctx);
  val = strtok_s(NULL, "=", &tk_ctx);
#else
  key = strtok_r(buf, "=", &tk_ctx);
  val = strtok_r(NULL, "=", &tk_ctx);
#endif
  CHK(key);
  CHK(val);

  if(!strcmp(key, "good")) {
    CHK(cstr_to_int(val, &i) == RES_OK);
  } else if(!strcmp(key, "bad")) {
    CHK(cstr_to_int(val, &i) == RES_OK);
    i = !i;
  } else {
    FATAL("Unreachable code.\n");
  }
  return i ? RES_OK : RES_BAD_ARG;
}

static void
test_list(void)
{
  int n = 0;
  CHK(cstr_parse_list(NULL, ':', count_elements, &n) == RES_BAD_ARG);
  CHK(cstr_parse_list("", ':', NULL, NULL) == RES_BAD_ARG);
  CHK(cstr_parse_list("", ':', count_elements, &n) == RES_OK);
  CHK(n == 0);
  CHK(cstr_parse_list("Hello", ':', count_elements, &n) == RES_OK);
  CHK(n == 1);
  n = 0;
  CHK(cstr_parse_list("Hello, world!", ':', count_elements, &n) == RES_OK);
  CHK(n == 1);
  n = 0;
  CHK(cstr_parse_list("Hello, world!", ' ', count_elements, &n) == RES_OK);
  CHK(n == 2);
  n = 0;
  CHK(cstr_parse_list("1;2;3;1e-7;abcdef;0x32;key=value", ';', count_elements, &n) == RES_OK);
  CHK(n == 7);

  CHK(cstr_parse_list("good=1", ',', parse_elmt, NULL) == RES_OK);
  CHK(cstr_parse_list("bad=0", ',', parse_elmt, NULL) == RES_OK);
  CHK(cstr_parse_list("good=1,bad=0", ',', parse_elmt, NULL) == RES_OK);
  CHK(cstr_parse_list("good=0,bad=0", ',', parse_elmt, NULL) == RES_BAD_ARG);
  CHK(cstr_parse_list("good=1,bad=1", ',', parse_elmt, NULL) == RES_BAD_ARG);
  CHK(cstr_parse_list("good=0,bad=1", ',', parse_elmt, NULL) == RES_BAD_ARG);
  CHK(cstr_parse_list("bad=0,good=0", ',', parse_elmt, NULL) == RES_BAD_ARG);
  CHK(cstr_parse_list("bad=0,good=1", ',', parse_elmt, NULL) == RES_OK);
}

static void
test_list_double(void)
{
  double dlist[4];
  size_t len;

  CHK(cstr_to_list_double(NULL, ':', dlist, NULL, 3) ==  RES_BAD_ARG);
  CHK(cstr_to_list_double("a", ':', dlist, NULL, 3) ==  RES_BAD_ARG);
  CHK(cstr_to_list_double("1.e-3:2.0:"STR(PI), ':', dlist, NULL, 3) ==  RES_OK);
  CHK(dlist[0] ==  1.e-3);
  CHK(dlist[1] ==  2.0);
  CHK(dlist[2] ==  PI);
  CHK(cstr_to_list_double("1.e-3:2.0:", ':', dlist, &len, 3) ==  RES_OK);
  CHK(len ==  2);
  CHK(dlist[0] ==  1.e-3);
  CHK(dlist[1] ==  2.0);
  CHK(cstr_to_list_double("-1.0:0.5:1.2:4.3", ':', dlist, &len, 2) ==  RES_BAD_ARG);
  CHK(cstr_to_list_double("-1.0:0.5:1.2:4.3a", ':', NULL, &len, 0) ==  RES_BAD_ARG);
  CHK(cstr_to_list_double("-1.0:0.5:1.2:4.3", ':', NULL, &len, 0) ==  RES_OK);
  CHK(len ==  4);
  CHK(cstr_to_list_double("-1.0:0.5:1.2:4.3", ':', dlist, NULL, len) ==  RES_OK);
  CHK(dlist[0] ==  -1.0);
  CHK(dlist[1] ==  0.5);
  CHK(dlist[2] ==  1.2);
  CHK(dlist[3] ==  4.3);
  CHK(cstr_to_list_double("  \t -1.0,0.5,1.2,INF  \t", ':', dlist, NULL, 4) ==  RES_BAD_ARG);
  CHK(cstr_to_list_double("  \t -1.0,0.5,1.2,INF  \t", ',', dlist, NULL, 4) ==  RES_OK);
  CHK(dlist[0] ==  -1.0);
  CHK(dlist[1] ==  0.5);
  CHK(dlist[2] ==  1.2);
  CHK(dlist[3] ==  INF);
  CHK(cstr_to_list_double("-1.0.2.3", '.', dlist, NULL, 4) ==  RES_OK);
  CHK(dlist[0] ==  -1);
  CHK(dlist[1] ==  0);
  CHK(dlist[2] ==  2);
  CHK(dlist[3] ==  3);
  CHK(cstr_to_list_double("  \t -1.0:0.5:1.2:4.3  \ta", ':', dlist, NULL, 4) ==  RES_BAD_ARG);
  dlist[1] = dlist[2] = dlist[3] = -1.0;
  CHK(cstr_to_list_double("1.0", ':', dlist, NULL, 1) ==  RES_OK);
  CHK(dlist[0] ==  1.0);
  CHK(dlist[1] ==  -1.0);
  CHK(dlist[2] ==  -1.0);
  CHK(dlist[3] ==  -1.0);
}

static void
test_list_float(void)
{
  float flist[4];
  size_t len;

  CHK(cstr_to_list_float(NULL, ':', flist, NULL, 3) ==  RES_BAD_ARG);
  CHK(cstr_to_list_float("a", ':', flist, NULL, 3) ==  RES_BAD_ARG);
  CHK(cstr_to_list_float("1.e-3:2.0:"STR(PI), ':', flist, NULL, 3) ==  RES_OK);
  CHK(flist[0] ==  1.e-3f);
  CHK(flist[1] ==  2.0f);
  CHK(flist[2] ==  (float)PI);
  CHK(cstr_to_list_float("1.e-3:2.0:", ':', flist, &len, 3) ==  RES_OK);
  CHK(len ==  2);
  CHK(flist[0] ==  1.e-3f);
  CHK(flist[1] ==  2.0f);
  CHK(cstr_to_list_float("-1.0:0.5:1.2:4.3", ':', flist, &len, 2) ==  RES_BAD_ARG);
  CHK(cstr_to_list_float("-1.0:0.5:1.2:4.3a", ':', NULL, &len, 0) ==  RES_BAD_ARG);
  CHK(cstr_to_list_float("-1.0:0.5:1.2:4.3", ':', NULL, &len, 0) ==  RES_OK);
  CHK(len ==  4);
  CHK(cstr_to_list_float("-1.0:0.5:1.2:4.3", ':', flist, NULL, len) ==  RES_OK);
  CHK(flist[0] ==  -1.0f);
  CHK(flist[1] ==  0.5f);
  CHK(flist[2] ==  1.2f);
  CHK(flist[3] ==  4.3f);
  CHK(cstr_to_list_float("  \t -1.0,0.5,1.2,INF  \t", ':', flist, NULL, 4) ==  RES_BAD_ARG);
  CHK(cstr_to_list_float("  \t -1.0,0.5,1.2,INF  \t", ',', flist, NULL, 4) ==  RES_OK);
  CHK(flist[0] ==  -1.0f);
  CHK(flist[1] ==  0.5f);
  CHK(flist[2] ==  1.2f);
  CHK(flist[3] ==  (float)INF);
}

static void
test_list_uint(void)
{
  unsigned ulist[4];
  size_t len;

  CHK(cstr_to_list_uint(NULL, ':', ulist, NULL, 3) ==  RES_BAD_ARG);
  CHK(cstr_to_list_uint("a", ':', ulist, NULL, 3) ==  RES_BAD_ARG);
  CHK(cstr_to_list_uint("1:4:-1", ':', ulist, NULL, 3) ==  RES_OK);
  CHK(ulist[0] ==  1);
  CHK(ulist[1] ==  4);
  CHK(ulist[2] ==  (unsigned)-1);
  CHK(cstr_to_list_uint("1:2::", ':', ulist, &len, 3) ==  RES_OK);
  CHK(len ==  2);
  CHK(ulist[0] ==  1);
  CHK(ulist[1] ==  2);
  CHK(cstr_to_list_uint("1:5:2:4", ':', ulist, &len, 2) ==  RES_BAD_ARG);
  CHK(cstr_to_list_uint("1:5:2:4.2", ':', NULL, &len, 0) ==  RES_BAD_ARG);
  CHK(cstr_to_list_uint("1:5:2:4", ':', NULL, &len, 0) ==  RES_OK);
  CHK(len ==  4);
  CHK(cstr_to_list_uint("-1.5.2.3", ':', ulist, NULL, len) ==  RES_BAD_ARG);
  CHK(cstr_to_list_uint("-1.5.2.3", '.', ulist, NULL, len) ==  RES_OK);
  CHK(ulist[0] ==  (unsigned)-1);
  CHK(ulist[1] ==  5);
  CHK(ulist[2] ==  2);
  CHK(ulist[3] ==  3);
}

static void
test_res_to_cstr(void)
{
  printf("%s\n", res_to_cstr(RES_OK));
  printf("%s\n", res_to_cstr(RES_BAD_ARG));
  printf("%s\n", res_to_cstr(RES_MEM_ERR));
  printf("%s\n", res_to_cstr(RES_IO_ERR));
  printf("%s\n", res_to_cstr(RES_UNKNOWN_ERR));
  printf("%s\n", res_to_cstr(RES_BAD_OP));
  printf("%s\n", res_to_cstr(RES_EOF));
}

static INLINE size_t
size
  (const size_t teras,
   const size_t gigas,
   const size_t megas,
   const size_t kilos,
   const size_t bytes)
{
  return (size_t)1024*((size_t)1024*((size_t)1024*((size_t)1024*
    teras + gigas) + megas) + kilos) + bytes;
}

static void
test_size_to_cstr(void)
{
  char dump[512];
  size_t len;
  size_t sz;
  size_t dump_len;
  char* tk = 0;

  sz = size(2, 450, 987, 243, 42);

  size_to_cstr(sz, SIZE_ALL, &len, NULL, 0);
  CHK(len == 30);
  size_to_cstr(sz, SIZE_ALL, NULL, dump, sizeof(dump));
  printf("%s\n", dump);
  dump_len = strlen(dump);
  CHK(len == dump_len);

  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "2"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "TB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "450"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "GB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "987"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "MB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "243"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "KB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "42"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "B"));
  CHK((tk = strtok(NULL, " ")) == NULL);

  /* Check string truncation */
  size_to_cstr(sz, SIZE_ALL, &len, dump, dump_len - 3 + 1/*null char*/);
  CHK(len == 30);
  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "2"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "TB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "450"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "GB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "987"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "MB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "243"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "KB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "4"));
  CHK((tk = strtok(NULL, " ")) == NULL);

  size_to_cstr(sz, SIZE_GBYTE|SIZE_MBYTE, &len, dump, sizeof(dump));
  CHK(len == strlen(dump));
  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "2498"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "GB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "987"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "MB"));
  CHK((tk = strtok(NULL, " ")) == NULL);

  size_to_cstr(sz, SIZE_GBYTE|SIZE_KBYTE|SIZE_BYTE, &len, dump, sizeof(dump));
  CHK(len == strlen(dump));
  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "2498"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "GB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "1010931"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "KB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "42"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "B"));
  CHK((tk = strtok(NULL, " ")) == NULL);

  size_to_cstr(sz, 0, &len, dump, sizeof(dump));
  CHK(len == 0 && dump[0] == '\0');

  size_to_cstr(0, SIZE_ALL, NULL, dump, sizeof(dump));
  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "0"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "B"));
  CHK((tk = strtok(NULL, " ")) == NULL);

  size_to_cstr(0, SIZE_TBYTE, NULL, dump, sizeof(dump));
  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "0"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "TB"));
  CHK((tk = strtok(NULL, " ")) == NULL);

  sz = size(0, 3, 0, 17, 0);
  size_to_cstr(sz, SIZE_ALL, NULL, dump, sizeof(dump));
  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "3"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "GB"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "17"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "KB"));
  CHK((tk = strtok(NULL, " ")) == NULL);
}

int
main(int argc, char** argv)
{
  (void)argc, (void)argv;
  test_double();
  test_float();
  test_long();
  test_int();
  test_uint();
  test_ulong();
  test_list();
  test_list_double();
  test_list_float();
  test_list_uint();
  test_res_to_cstr();
  test_size_to_cstr();
  return 0;
}
