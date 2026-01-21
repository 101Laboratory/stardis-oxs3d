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

#define _POSIX_C_SOURCE 200112L /* snprintf support */
#include "rsys.h"

#if defined(OS_WINDOWS)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

#include <time.h>

#include "clock_time.h"
#include <string.h>

#define TIME_TO_NSEC(Time) ((Time)->nsec + (Time)->sec * 1000000000L)
#define NSEC_PER_USEC__ (int64_t)1000
#define NSEC_PER_MSEC__ ((int64_t)1000 * NSEC_PER_USEC__)
#define NSEC_PER_SEC__ ((int64_t)1000 * NSEC_PER_MSEC__)
#define NSEC_PER_MIN__ ((int64_t)60 * NSEC_PER_SEC__)
#define NSEC_PER_HOUR__ ((int64_t)60 * NSEC_PER_MIN__)
#define NSEC_PER_DAY__ ((int64_t)24 * NSEC_PER_HOUR__)

struct time*
time_current(struct time* t)
{
  ASSERT(t);

#if defined(OS_WINDOWS)
  {
    FILETIME ft;
    ULARGE_INTEGER uli;
    uint64_t t100ns = 0;
    uint64_t unix100ns = 0;
    static FARPROC get_time_proc = NULL;

    if(!get_time_proc) {
      HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
      get_time_proc = GetProcAddress(k32, "GetSystemTimePreciseAsFileTime");
      if(!get_time_proc) {
        get_time_proc = GetProcAddress(k32, "GetSystemTimeAsFileTime");
      }
    }

    ((void (WINAPI*)(LPFILETIME))get_time_proc)(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    t100ns = (uint64_t)uli.QuadPart;
    unix100ns = t100ns - (uint64_t)116444736000000000ULL;

    t->sec = (int64_t)(unix100ns / (uint64_t)10000000ULL);
    t->nsec = (int64_t)((unix100ns % (uint64_t)10000000ULL) * (uint64_t)100ULL);
  }
#else
  {
    struct timespec time;
    int err = 0; (void)err;

    err = clock_gettime(CLOCK_REALTIME, &time);
    ASSERT(err == 0);
    t->sec = (int64_t)time.tv_sec;
    t->nsec = (int64_t)time.tv_nsec;
  }
#endif
  return t;
}

struct time*
time_sub(struct time* res, const struct time* a, const struct time* b)
{
  ASSERT(res && a && b);
  res->sec = a->sec - b->sec;
  res->nsec = a->nsec - b->nsec;
  if(res->nsec < 0) {
    --res->sec;
    res->nsec += 1000000000L;
  }
  return res;
}

struct time*
time_add(struct time* res, const struct time* a, const struct time* b)
{
  ASSERT(res && a && b);

  res->sec = a->sec + b->sec;
  res->nsec = a->nsec + b->nsec;
  if(res->nsec >= 1000000000L) {
	  ++res->sec;
    res->nsec -= 1000000000L;
  }
  return res;
}

int64_t
time_val(const struct time* time, enum time_unit unit)
{
  int64_t val = TIME_TO_NSEC(time);
  switch(unit) {
    case TIME_NSEC:
      /* Do nothing. */
      break;
    case TIME_USEC:
      val /= NSEC_PER_USEC__;
      break;
    case TIME_MSEC:
      val /= NSEC_PER_MSEC__;
      break;
    case TIME_SEC:
      val /= NSEC_PER_SEC__;
      break;
    case TIME_MIN:
      val /= NSEC_PER_MIN__;
      break;
    case TIME_HOUR:
      val /= NSEC_PER_HOUR__;
      break;
    case TIME_DAY:
      val /= NSEC_PER_DAY__;
      break;
    default: ASSERT(0); break;
  }
  return val;
}

void
time_dump
  (const struct time* time,
   int flag,
   size_t* real_dump_len,
   char* dump,
   size_t max_dump_len)
{
  size_t available_dump_space = max_dump_len;
  int64_t time_nsec = 0;
  char* dst = dump;

  ASSERT(time && (!max_dump_len || dump));
  if(real_dump_len) *real_dump_len = 0;
  if(max_dump_len > 0) dump[0] = '\0';
  if(!flag) return;

  #define DUMP(Time, Suffix)                                                   \
    {                                                                          \
      const int len = snprintf                                                 \
        (dst, available_dump_space,                                            \
         "%li %s", (long)Time, Time > 1 ? Suffix "s ": Suffix " ");            \
      ASSERT(len >= 0);                                                        \
      if(real_dump_len) {                                                      \
        *real_dump_len += (size_t)len;                                         \
      }                                                                        \
      if((size_t)len < available_dump_space) {                                 \
        dst += len;                                                            \
        available_dump_space -= (size_t)len;                                   \
      } else if(dst) {                                                         \
        available_dump_space = 0;                                              \
        dst = NULL;                                                            \
      }                                                                        \
    } (void) 0

  time_nsec = TIME_TO_NSEC(time);
  if(flag & TIME_DAY) {
    const int64_t nb_days = time_nsec / NSEC_PER_DAY__;
    if(nb_days) DUMP(nb_days, "day");
    time_nsec -= nb_days * NSEC_PER_DAY__;
  }
  if(flag & TIME_HOUR) {
    const int64_t nb_hours = time_nsec / NSEC_PER_HOUR__;
    if(nb_hours) DUMP(nb_hours, "hour");
    time_nsec -= nb_hours * NSEC_PER_HOUR__;
  }
  if(flag & TIME_MIN) {
    const int64_t nb_mins = time_nsec / NSEC_PER_MIN__;
    if(nb_mins) DUMP(nb_mins, "min");
    time_nsec -= nb_mins * NSEC_PER_MIN__;
  }
  if(flag & TIME_SEC) {
    const int64_t nb_secs = time_nsec / NSEC_PER_SEC__;
    if(nb_secs) DUMP(nb_secs, "sec");
    time_nsec -= nb_secs * NSEC_PER_SEC__;
  }
  if(flag & TIME_MSEC) {
    const int64_t nb_msecs = time_nsec / NSEC_PER_MSEC__;
    if(nb_msecs) DUMP(nb_msecs, "msec");
    time_nsec -= nb_msecs * NSEC_PER_MSEC__;
  }
  if(flag & TIME_USEC) {
    const int64_t nb_usecs = time_nsec / NSEC_PER_USEC__;
    if(nb_usecs) DUMP(nb_usecs, "usec");
    time_nsec -= nb_usecs * NSEC_PER_USEC__;
  }
  if(flag & TIME_NSEC) {
    if(time_nsec) DUMP(time_nsec, "nsec");
  }

  /* Remove last space */
  if(real_dump_len) *real_dump_len -= 1;

  if(max_dump_len > 0) {
    size_t dump_len = strlen(dump);
    if(!dump_len && flag) {
      if(flag & TIME_NSEC) { DUMP(0, "nsec");
      } else if(flag & TIME_USEC) { DUMP(0, "usec");
      } else if(flag & TIME_MSEC) { DUMP(0, "msec");
      } else if(flag & TIME_SEC) { DUMP(0, "sec");
      } else if(flag & TIME_MIN) { DUMP(0, "min");
      } else if(flag & TIME_HOUR) { DUMP(0, "hour");
      } else if(flag & TIME_DAY) { DUMP(0, "day");
      }
      dump_len = strlen(dump);
    }
    /* Remove last space */
    if(dump[dump_len-1] == ' ') {
      dump[dump_len-1] = '\0';
    }
  }
  #undef DUMP
}

#undef TIME_TO_NSEC
#undef NSEC_PER_USEC__
#undef NSEC_PER_MSEC__
#undef NSEC_PER_SEC__
#undef NSEC_PER_MIN__
#undef NSEC_PER_HOUR__
#undef NSEC_PER_DAY__
