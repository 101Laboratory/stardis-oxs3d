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

#ifndef TIME_H
#define TIME_H

#include "rsys.h"

struct time {
  /* Internal data */
  int64_t sec;
  int64_t nsec;
};

/* time_dump flag constant used to auto-format the time_dump message */
#define TIME_ALL (-1)

enum time_unit {
  TIME_NSEC = BIT(0),
  TIME_USEC = BIT(1),
  TIME_MSEC = BIT(2),
  TIME_SEC = BIT(3),
  TIME_MIN = BIT(4),
  TIME_HOUR = BIT(5),
  TIME_DAY = BIT(6)
};

static FINLINE struct time*
time_zero(struct time* time)
{
  ASSERT(time);
  time->sec = time->nsec = 0;
  return time;
}

BEGIN_DECLS

RSYS_API struct time*
time_current
  (struct time* time);

RSYS_API struct time*
time_sub
  (struct time* res,
   const struct time* a,
   const struct time* b);

RSYS_API struct time*
time_add
  (struct time* res,
   const struct time* a,
   const struct time* b);

RSYS_API int64_t
time_val
  (const struct time* time,
   enum time_unit unit);

RSYS_API void
time_dump
  (const struct time* time,
   int flag, /* Combination of time_unit or TIME_ALL */
   size_t* real_dump_len, /* May be NULL (wo '\0') */
   char* dump, /* May be NULL */
   size_t max_dump_len); /* With '\0' */

END_DECLS

#endif /* TIME_H */
