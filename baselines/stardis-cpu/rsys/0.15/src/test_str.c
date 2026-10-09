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

#include "str.h"
#include "test_utils.h"
#include <stdarg.h>
#include <string.h>

#ifdef COMPILER_GCC
__attribute__((format(printf, 2, 3)))
#endif
static void
chk_vprintf(struct str* str, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  CHK(str_vprintf(str, fmt, ap) == RES_OK);
  va_end(ap);
}

#ifdef COMPILER_GCC
__attribute__((format(printf, 2, 3)))
#endif
static void
chk_append_vprintf(struct str* str, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  CHK(str_append_vprintf(str, fmt, ap) == RES_OK);
  va_end(ap);
}


int
main(int argc, char** argv)
{
  struct mem_allocator allocator_proxy;
  struct str str, str2;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator_proxy, &mem_default_allocator);

  str_init(&allocator_proxy, &str);
  CHK(strcmp(str_get(&str), "") ==  0);
  CHK(str_len(&str) ==  0);
  CHK(str_is_empty(&str) ==  1);

  CHK(str_set(&str, "Foo") ==  RES_OK);
  CHK(strcmp(str_get(&str), "Foo") ==  0);
  CHK(str_len(&str) ==  strlen("Foo"));
  CHK(str_is_empty(&str) ==  0);

  str_clear(&str);
  CHK(strcmp(str_get(&str), "") ==  0);
  CHK(str_len(&str) ==  0);

  CHK(str_set(&str, __FILE__) ==  RES_OK);
  CHK(strcmp(str_get(&str), __FILE__) ==  0);

  str_release(&str);
  str_init(&allocator_proxy, &str);
  CHK(strcmp(str_get(&str), "") ==  0);
  CHK(str_len(&str) ==  0);

  CHK(str_set(&str, "Hello world!") ==  RES_OK);
  CHK(strcmp(str_get(&str), "Hello world!") ==  0);
  CHK(str_insert(&str, 13, " insert") != RES_OK);
  CHK(str_insert(&str, 12, " insert") ==  RES_OK);
  CHK(strcmp(str_get(&str), "Hello world! insert") ==  0);

  CHK(str_insert(&str, 6, "abcdefgh ") ==  RES_OK);
  CHK(strcmp(str_get(&str), "Hello abcdefgh world! insert") ==  0);
  CHK(str_insert(&str, 0, "ABC ") ==  RES_OK);
  CHK(strcmp(str_get(&str), "ABC Hello abcdefgh world! insert") ==  0);
  CHK(str_insert(&str, 11, "0123456789ABCDEF") ==  RES_OK);
  CHK(strcmp(str_get(&str),
    "ABC Hello a0123456789ABCDEFbcdefgh world! insert") ==  0);
  CHK(str_len(&str) ==
    strlen("ABC Hello a0123456789ABCDEFbcdefgh world! insert"));

  CHK(str_set(&str, "Hello world!") ==  RES_OK);
  CHK(str_append(&str, " Append") ==  RES_OK);
  CHK(strcmp(str_get(&str), "Hello world! Append") ==  0);
  CHK(str_append(&str, "!") ==  RES_OK);
  CHK(strcmp(str_get(&str), "Hello world! Append!") ==  0);

  CHK(str_insert_char(&str, 21, 'a') != RES_OK);
  CHK(strcmp(str_get(&str), "Hello world! Append!") ==  0);
  CHK(str_insert_char(&str, 20, 'a') ==  RES_OK);
  CHK(strcmp(str_get(&str), "Hello world! Append!a") ==  0);
  CHK(str_insert_char(&str, 0, '0') ==  RES_OK);
  CHK(strcmp(str_get(&str), "0Hello world! Append!a") ==  0);
  CHK(str_insert_char(&str, 13, 'A') ==  RES_OK);
  CHK(strcmp(str_get(&str), "0Hello world!A Append!a") ==  0);

  CHK(str_append_char(&str, 'z') ==  RES_OK);
  CHK(strcmp(str_get(&str), "0Hello world!A Append!az") ==  0);

  CHK(str_reserve(&str, 128) ==  RES_OK);
  CHK(str_reserve(&str, 0) ==  RES_OK);
  CHK(strcmp(str_get(&str), "0Hello world!A Append!az") ==  0);

  CHK(str_insert_char(&str, 13, '\0') ==  RES_OK);
  CHK(strcmp(str_get(&str), "0Hello world!") ==  0);
  CHK(str_len(&str) ==  strlen("0Hello world!"));
  CHK(str_append_char(&str, '\0') ==  RES_OK);
  CHK(strcmp(str_get(&str), "0Hello world!") ==  0);
  CHK(str_len(&str) ==  strlen("0Hello world!"));

  str_init(&allocator_proxy, &str2);
  str_copy(&str2, &str);
  CHK(strcmp(str_cget(&str2), "0Hello world!") ==  0);
  CHK(strcmp(str_cget(&str), "0Hello world!") ==  0);

  CHK(str_eq(&str, &str2) ==  1);

  CHK(str_hash(&str) ==  str_hash(&str2));
  str_clear(&str2);
  CHK(str_hash(&str) != str_hash(&str2));

  str_copy_and_clear(&str2, &str);
  CHK(strcmp(str_cget(&str2), "0Hello world!") ==  0);
  CHK(str_len(&str2) ==  strlen(str_cget(&str2)));
  CHK(str_len(&str) ==  0);
  str_copy_and_release(&str, &str2);
  CHK(strcmp(str_cget(&str), "0Hello world!") ==  0);

  str_init(&allocator_proxy, &str2);
  str_set(&str2, "ABC Hello a0123456789ABCDEFbcdefgh world! insert");
  CHK(str_eq(&str, &str2) ==  0);
  CHK(str_cmp(&str, &str2) == strcmp(str_cget(&str), str_cget(&str2)));

  CHK(str_hash(&str) != str_hash(&str2));
  str_copy_and_clear(&str, &str2);
  CHK(!strcmp
    (str_cget(&str), "ABC Hello a0123456789ABCDEFbcdefgh world! insert"));
  CHK(str_len(&str) ==  strlen(str_cget(&str)));

  str_set(&str2, "Hello world!");
  CHK(strcmp(str_cget(&str2), "Hello world!") ==  0);
  str_clear(&str2);
  str_copy_and_clear(&str, &str2);
  CHK(str_len(&str) ==  0);
  CHK(str_len(&str2) ==  0);
  CHK(strlen(str_cget(&str)) ==  0);

  str_copy_and_release(&str2, &str2);

  str_set(&str, "Hello World!");
  str_copy_and_clear(&str, &str);
  CHK(str_len(&str) ==  0);

  str_release(&str);

  str_init(&allocator_proxy, &str);
  CHK(str_printf(&str, "Hello world!") == RES_OK);
  CHK(!strcmp(str_cget(&str), "Hello world!"));
  CHK(str_printf(&str, "XARR-QRRC VA GUR QRNQ") == RES_OK);
  CHK(!strcmp(str_cget(&str), "XARR-QRRC VA GUR QRNQ"));
  CHK(str_printf(&str, "Rcvfbqr %d, GUR FUBERF BS URYY", 2) == RES_OK);
  CHK(!strcmp(str_cget(&str), "Rcvfbqr 2, GUR FUBERF BS URYY"));
  CHK(str_printf(&str, "%s:%lu:%s", __FILE__, 0xDECAFBADlu,__FILE__) == RES_OK);
  CHK(!strcmp(str_cget(&str), __FILE__":3737844653:"__FILE__));
  str_release(&str);

  str_init(&allocator_proxy, &str);
  CHK(str_printf(&str, "Hello, ") == RES_OK);
  CHK(str_append_printf(&str, "world!") == RES_OK);
  CHK(!strcmp(str_cget(&str), "Hello, world!"));
  CHK(str_printf(&str, "%s:%lu", __FILE__, 0xDECAFBADlu) == RES_OK);
  CHK(str_append_printf(&str, ":%s", __FILE__) == RES_OK);
  CHK(!strcmp(str_cget(&str), __FILE__":3737844653:"__FILE__));
  chk_vprintf(&str, "You should have received a copy of the %s %s %d",
    "GNU", "General Public License", 3);
  CHK(!strcmp(str_cget(&str),
    "You should have received a copy of the "
    "GNU General Public License 3"));
  chk_append_vprintf(&str, " along with the %s library. If not, see %c%s%c.",
    "RSys", '<', "http://www.gnu.org/licenses/", '>');
  CHK(!strcmp(str_cget(&str),
    "You should have received a copy of the GNU General Public License 3 "
    "along with the RSys library. If not, see <http://www.gnu.org/licenses/>."));
  str_release(&str);

  str_init(&allocator_proxy, &str);
  CHK(str_set(&str, "abcd") == RES_OK);
  CHK(str_len(&str) == 4);
  str_get(&str)[3] = '\0';
  CHK(str_len(&str) == 3);

  check_memory_allocator(&allocator_proxy);
  mem_shutdown_proxy_allocator(&allocator_proxy);
  CHK(mem_allocated_size() ==  0);
  return 0;
}
