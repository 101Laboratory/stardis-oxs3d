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

#include "clock_time.h"
#include <stdlib.h>
#include <string.h>

static struct time
build_time
  (const int64_t days,
   const int64_t hours,
   const int64_t mins,
   const int64_t secs,
   const int64_t msecs,
   const int64_t usecs,
   const int64_t nsecs)
{
  struct time t;

  t.sec  = days * 3600 * 24;
  t.sec += hours * 3600;
  t.sec += mins * 60;
  t.sec += secs;

  t.nsec  = msecs * 1000000;
  t.nsec += usecs * 1000;
  t.nsec += nsecs;

  return t;
}

int
main(int argc, char** argv)
{
  struct time start, end, res;
  char dump[512];
  char* tk;
  int64_t time = 0;
  int64_t i = 0;
  size_t dump_len;
  (void)argc, (void)argv;

  CHK(time_current(&start) == &start);
  FOR_EACH(i, 0, INT32_MAX / 64) {} /* Active wait */
  CHK(time_current(&end) == &end);

  CHK(time_sub(&res, &end, &start) == &res);
  time = time_val(&res, TIME_NSEC);
  CHK(time >= 0);
  CHK(time_val(&res, TIME_USEC) == time / 1000);
  CHK(time_val(&res, TIME_MSEC) == time / 1000000);
  CHK(time_val(&res, TIME_SEC) == time / 1000000000);

  time_dump
    (&res, TIME_SEC|TIME_MSEC|TIME_USEC, &dump_len, dump, sizeof(dump));
  CHK(dump_len == strlen(dump));
  printf("%s\n", dump);
  time_dump(&res, TIME_ALL, NULL, dump, sizeof(dump));
  printf("%s\n", dump);

  CHK(time_add(&res, &res, &res) == &res);
  CHK(time_val(&res, TIME_NSEC) == 2*time);
  time_dump(&res, TIME_ALL, NULL, dump, sizeof(dump));
  printf("%s\n", dump);

  time = time_val(&res, TIME_NSEC);
  CHK(time_zero(&start) == &start);
  CHK(time_val(&start, TIME_NSEC) == 0);
  CHK(time_add(&res, &res, &start) == &res);
  CHK(time_val(&res, TIME_NSEC) == time);
  CHK(time_add(&start, &res, &start) == &start);
  CHK(time_val(&start, TIME_NSEC) == time);
  CHK(time_sub(&res, time_zero(&start), &res) == &res);
  CHK(time_val(&res, TIME_NSEC) == -time);

  CHK(time_sub(&res, &end, &end) == &res);
  CHK(time_val(&res, TIME_NSEC) == 0);

  res = build_time(1, 10, 55, 30, 998, 763, 314);
  dump[0] = '?';
  time_dump(&res, TIME_ALL, &dump_len, dump, 0);
  CHK(dump[0] = '?');
  time_dump(&res, TIME_ALL, NULL, dump, sizeof(dump));
  CHK(dump_len == strlen(dump));

  printf("%s\n", dump);

  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "1"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "day"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "10"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "hours"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "55"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "mins"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "30"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "secs"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "998"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "msecs"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "763"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "usecs"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "314"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "nsecs"));
  CHK((tk = strtok(NULL, " ")) == NULL);

  /* Check string truncation */
  time_dump(&res, TIME_ALL, NULL, dump, dump_len - 27 + 1/*null char*/);
  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "1"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "day"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "10"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "hours"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "55"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "mins"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "30"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "secs"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "99"));
  CHK((tk = strtok(NULL, " ")) == NULL);

  time_dump(&res, TIME_MIN|TIME_MSEC, NULL, dump, sizeof(dump));
  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "2095"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "mins"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "30998"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "msecs"));
  CHK((tk = strtok(NULL, " ")) == NULL);

  time_dump(&res, 0, &dump_len, dump, sizeof(dump));
  CHK(dump_len == 0 && dump[0] == '\0');

  res = build_time(0, 0, 0, 0, 0, 0, 0);
  time_dump(&res, TIME_ALL, NULL, dump, sizeof(dump));
  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "0"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "nsec"));
  CHK((tk = strtok(NULL, " ")) == NULL);

  time_dump(&res, TIME_DAY, NULL, dump, sizeof(dump));
  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "0"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "day"));
  CHK((tk = strtok(NULL, " ")) == NULL);

  res = build_time(0, 0, 1, 0, 235, 10, 0);
  time_dump(&res, TIME_ALL, NULL, dump, sizeof(dump));
  CHK((tk = strtok(dump, " ")) && !strcmp(tk, "1"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "min"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "235"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "msecs"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "10"));
  CHK((tk = strtok(NULL, " ")) && !strcmp(tk, "usecs"));
  CHK((tk = strtok(NULL, " ")) == NULL);
  return 0;
}
