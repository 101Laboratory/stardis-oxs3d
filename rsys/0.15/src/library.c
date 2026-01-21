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

#include "library.h"

#if defined(OS_WINDOWS)
#include <Windows.h>
void*
library_open(const char* filename)
{
  if(!filename)
    return NULL;

  HMODULE h = LoadLibraryA(filename);
  if (!h) {
      DWORD err = GetLastError();
      printf("LoadLibraryA failed, error = %lu\n", err);
  }

  return (void*)h;
}

res_T
library_close(void* lib)
{
  BOOL b;
  if(!lib)
    return RES_BAD_ARG;

  b = FreeLibrary((HMODULE)lib);
  if(!b)
    return RES_UNKNOWN_ERR;

  return RES_OK;
}

void*
library_get_symbol(void* lib, const char* sym)
{
  union { FARPROC proc; void* ptr; } ucast;
  STATIC_ASSERT(sizeof(FARPROC) == sizeof(void*), Unexpected_type_size);
  ucast.proc = GetProcAddress((HMODULE)lib, sym);
  return ucast.ptr;
}

#elif defined(OS_UNIX) || defined(OS_MACH)
#include <dlfcn.h>
#include <stdio.h>

void*
library_open(const char* filename)
{
  if(!filename) return NULL;
  return dlopen(filename, RTLD_NOW|RTLD_GLOBAL);
}

void*
library_get_symbol(void* lib, const char* sym)
{
  if(!lib || !sym) return NULL;
  return dlsym(lib, sym);
}

res_T
library_close(void* handle)
{
  int err = 0;
  if(!handle) return RES_BAD_ARG;

  err = dlclose(handle);
  if(err) return RES_UNKNOWN_ERR;

  return RES_OK;
}
#endif /* OS_<XXX> */
