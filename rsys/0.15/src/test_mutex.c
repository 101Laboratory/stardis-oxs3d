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

#if !defined(OS_WINDOWS)
  #define _POSIX_C_SOURCE 199506L
#endif

#include "clock_time.h"
#include "mutex.h"

#include <string.h>

#if defined(OS_WINDOWS)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <process.h>
#else
  #include <pthread.h>

#endif

static const char src_str[] = {
  'R','c','v','f','b','q','r',' ','1',',',' ','X','A','R','R',
  '-','Q','R','R','C',' ','V','A',' ','G','U','R',' ','Q','R',
  'N','Q',':','\n','-','-','-','-','-','-','-','-','-','-','-',
  '-','-','-','-','-','-','-','-','-','-','-','-','-','-','-',
  '-','-','-','-','-','-','-','-','\n','\n','B','A','P','R',' ',
  'L','B','H',' ','O','R','N','G',' ','G','U','R',' ','O','V',
  'T',' ','O','N','Q','N','F','F','R','F',' ','N','A','Q',' ',
  'P','Y','R','N','A',' ','B','H','G',' ','G','U','R',' ','Z',
  'B','B','A',' ','O','N','F','R',' ','L','B','H','\'','E','R',
  ' ','F','H','C','C','B','F','R','Q',' ','G','B','\n','J','V',
  'A',',',' ','N','E','R','A','\'','G',' ','L','B','H','?',' ',
  'N','E','R','A','\'','G',' ','L','B','H','?',' ','J','U','R',
  'E','R','\'','F',' ','L','B','H','E',' ','S','N','G',' ','E',
  'R','J','N','E','Q',' ','N','A','Q',' ','G','V','P','X','R',
  'G',' ','U','B','Z','R','?',' ','J','U','N','G','\n','G','U',
  'R',' ','U','R','Y','Y',' ','V','F',' ','G','U','V','F','?',
  ' ','V','G','\'','F',' ','A','B','G',' ','F','H','C','C','B',
  'F','R','Q',' ','G','B',' ','R','A','Q',' ','G','U','V','F',
  ' ','J','N','L','!','\n','\n','V','G',' ','F','G','V','A','X',
  'F',' ','Y','V','X','R',' ','E','B','G','G','R','A',' ','Z',
  'R','N','G',',',' ','O','H','G',' ','Y','B','B','X','F',' ',
  'Y','V','X','R',' ','G','U','R',' ','Y','B','F','G',' ','Q',
  'R','V','Z','B','F',' ','O','N','F','R','.',' ','Y','B','B',
  'X','F',' ','Y','V','X','R','\n','L','B','H','\'','E','R',' ',
  'F','G','H','P','X',' ','B','A',' ','G','U','R',' ','F','U',
  'B','E','R','F',' ','B','S',' ','U','R','Y','Y','.',' ','G',
  'U','R',' ','B','A','Y','L',' ','J','N','L',' ','B','H','G',
  ' ','V','F',' ','G','U','E','B','H','T','U','.','\n','\n','G',
  'B',' ','P','B','A','G','V','A','H','R',' ','G','U','R',' ',
  'Q','B','B','Z',' ','R','K','C','R','E','V','R','A','P','R',
  ',',' ','C','Y','N','L',' ','G','U','R',' ','F','U','B','E',
  'R','F',' ','B','S',' ','U','R','Y','Y',' ','N','A','Q',' ',
  'V','G','F',' ','N','Z','N','M','V','A','T','\n','F','R','D',
  'H','R','Y',',',' ','V','A','S','R','E','A','B','!','\n','\n',
  'R','c','v','f','b','q','r',' ','2',',',' ','G','U','R',' ',
  'F','U','B','E','R','F',' ','B','S',' ','U','R','Y','Y',':',
  '\n','-','-','-','-','-','-','-','-','-','-','-','-','-','-',
  '-','-','-','-','-','-','-','-','-','-','-','-','-','-','-',
  '-','\n','\n','L','B','H','\'','I','R',' ','Q','B','A','R',' ',
  'V','G','!',' ','G','U','R',' ','U','V','Q','R','B','H','F',
  ' ','P','L','O','R','E','-',' ','Q','R','Z','B','A',' ','Y',
  'B','E','Q',' ','G','U','N','G',' ','E','H','Y','R','Q',' ',
  'G','U','R',' ','Y','B','F','G',' ','Q','R','V','Z','B','F',
  ' ','Z','B','B','A','\n','O','N','F','R',' ','U','N','F',' ',
  'O','R','R','A',' ','F','Y','N','V','A',' ','N','A','Q',' ',
  'L','B','H',' ','N','E','R',' ','G','E','V','H','Z','C','U',
  'N','A','G','!',' ','O','H','G',' ','.','.','.',' ','J','U',
  'R','E','R',' ','N','E','R',' ','L','B','H','?',' ','L','B',
  'H','\n','P','Y','N','Z','O','R','E',' ','G','B',' ','G','U',
  'R',' ','R','Q','T','R',' ','B','S',' ','G','U','R',' ','Z',
  'B','B','A',' ','N','A','Q',' ','Y','B','B','X',' ','Q','B',
  'J','A',' ','G','B',' ','F','R','R',' ','G','U','R',' ','N',
  'J','S','H','Y',' ','G','E','H','G','U','.','\n','\n','Q','R',
  'V','Z','B','F',' ','S','Y','B','N','G','F',' ','N','O','B',
  'I','R',' ','U','R','Y','Y',' ','V','G','F','R','Y','S','!',
  ' ',' ','L','B','H','\'','I','R',' ','A','R','I','R','E',' ',
  'U','R','N','E','Q',' ','B','S',' ','N','A','L','B','A','R',
  ' ','R','F','P','N','C','V','A','T',' ','S','E','B','Z','\n',
  'U','R','Y','Y',',',' ','O','H','G',' ','L','B','H','\'','Y',
  'Y',' ','Z','N','X','R',' ','G','U','R',' ','O','N','F','G',
  'N','E','Q','F',' ','F','B','E','E','L',' ','G','U','R','L',
  ' ','R','I','R','E',' ','U','R','N','E','Q',' ','B','S',' ',
  'L','B','H','!',' ','D','H','V','P','X','Y','L',',',' ','L',
  'B','H','\n','E','N','C','C','R','Y',' ','Q','B','J','A',' ',
  'G','B',' ','G','U','R',' ','F','H','E','S','N','P','R',' ',
  'B','S',' ','U','R','Y','Y','.','\n','\n','A','B','J',',',' ',
  'V','G','\'','F',' ','B','A',' ','G','B',' ','G','U','R',' ',
  'S','V','A','N','Y',' ','P','U','N','C','G','R','E',' ','B',
  'S',' ','Q','B','B','Z','!',' ','-','-',' ','V','A','S','R',
  'E','A','B','.','\n','\n','R','c','v','f','b','q','r',' ','3',
  ',',' ','V','A','S','R','E','A','B',':','\n','-','-','-','-',
  '-','-','-','-','-','-','-','-','-','-','-','-','-','-','-',
  '\n','\n','G','U','R',' ','Y','B','N','G','U','F','B','Z','R',
  ' ','F','C','V','Q','R','E','Q','R','Z','B','A',' ','G','U',
  'N','G',' ','Z','N','F','G','R','E','Z','V','A','Q','R','Q',
  ' ','G','U','R',' ','V','A','I','N','F','V','B','A',' ','B',
  'S',' ','G','U','R',' ','Z','B','B','A',' ','O','N','F','R',
  'F','\n','N','A','Q',' ','P','N','H','F','R','Q',' ','F','B',
  ' ','Z','H','P','U',' ','Q','R','N','G','U',' ','U','N','F',
  ' ','U','N','Q',' ','V','G','F',' ','N','F','F',' ','X','V',
  'P','X','R','Q',' ','S','B','E',' ','N','Y','Y',' ','G','V',
  'Z','R','.','\n','\n','N',' ','U','V','Q','Q','R','A',' ','Q',
  'B','B','E','J','N','L',' ','B','C','R','A','F',' ','N','A',
  'Q',' ','L','B','H',' ','R','A','G','R','E','.',' ',' ','L',
  'B','H','\'','I','R',' ','C','E','B','I','R','A',' ','G','B',
  'B',' ','G','B','H','T','U',' ','S','B','E',' ','U','R','Y',
  'Y',' ','G','B','\n','P','B','A','G','N','V','A',',',' ','N',
  'A','Q',' ','A','B','J',' ','U','R','Y','Y',' ','N','G',' ',
  'Y','N','F','G',' ','C','Y','N','L','F',' ','S','N','V','E',
  ' ','-','-',' ','S','B','E',' ','L','B','H',' ','R','Z','R',
  'E','T','R',' ','S','E','B','Z',' ','G','U','R',' ','Q','B',
  'B','E',' ','G','B','\n','F','R','R',' ','G','U','R',' ','T',
  'E','R','R','A',' ','S','V','R','Y','Q','F',' ','B','S',' ',
  'R','N','E','G','U','!',' ',' ','U','B','Z','R',' ','N','G',
  ' ','Y','N','F','G','.','\n','\n','L','B','H',' ','J','B','A',
  'Q','R','E',' ','J','U','N','G','\'','F',' ','O','R','R','A',
  ' ','U','N','C','C','R','A','V','A','T',' ','B','A',' ','R',
  'N','E','G','U',' ','J','U','V','Y','R',' ','L','B','H',' ',
  'J','R','E','R',' ','O','N','G','G','Y','V','A','T',' ','R',
  'I','V','Y','\n','H','A','Y','R','N','F','U','R','Q','.',' ',
  'V','G','\'','F',' ','T','B','B','Q',' ','G','U','N','G',' ',
  'A','B',' ','U','R','Y','Y','-',' ','F','C','N','J','A',' ',
  'P','B','H','Y','Q',' ','U','N','I','R',' ','P','B','Z','R',
  ' ','G','U','E','B','H','T','U',' ','G','U','N','G',' ','Q',
  'B','B','E','\n','J','V','G','U',' ','L','B','H',' ','.','.',
  '.','\n','\n','R','c','v','f','b','q','r',' ','4',',',' ','G',
  'U','L',' ','S','Y','R','F','U',' ','P','B','A','F','H','Z',
  'R','Q',':','\n','-','-','-','-','-','-','-','-','-','-','-',
  '-','-','-','-','-','-','-','-','-','-','-','-','-','-','-',
  '-','-','-','-','\n','\n','G','U','R',' ','F','C','V','Q','R',
  'E',' ','Z','N','F','G','R','E','Z','V','A','Q',' ','Z','H',
  'F','G',' ','U','N','I','R',' ','F','R','A','G',' ','S','B',
  'E','G','U',' ','V','G','F',' ','Y','R','T','V','B','A','F',
  ' ','B','S',' ','U','R','Y','Y','F','C','N','J','A',' ','O',
  'R','S','B','E','R','\n','L','B','H','E',' ','S','V','A','N',
  'Y',' ','P','B','A','S','E','B','A','G','N','G','V','B','A',
  ' ','J','V','G','U',' ','G','U','N','G',' ','G','R','E','E',
  'V','O','Y','R',' ','O','R','N','F','G',' ','S','E','B','Z',
  ' ','U','R','Y','Y','.',' ','O','H','G',' ','L','B','H',' ',
  'F','G','R','C','C','R','Q','\n','S','B','E','J','N','E','Q',
  ' ','N','A','Q',' ','O','E','B','H','T','U','G',' ','S','B',
  'E','G','U',' ','R','G','R','E','A','N','Y',' ','Q','N','Z',
  'A','N','G','V','B','A',' ','N','A','Q',' ','F','H','S','S',
  'R','E','V','A','T',' ','H','C','B','A',' ','G','U','R',' ',
  'U','B','E','Q','R',' ','N','F',' ','N','\n','G','E','H','R',
  ' ','U','R','E','B',' ','J','B','H','Y','Q',' ','V','A',' ',
  'G','U','R',' ','S','N','P','R',' ','B','S',' ','F','B','Z',
  'R','G','U','V','A','T',' ','F','B',' ','R','I','V','Y','.',
  '\n','\n','O','R','F','V','Q','R','F',',',' ','F','B','Z','R',
  'B','A','R',' ','J','N','F',' ','T','B','A','A','N',' ','C',
  'N','L',' ','S','B','E',' ','J','U','N','G',' ','U','N','C',
  'C','R','A','R','Q',' ','G','B',' ','Q','N','V','F','L',',',
  ' ','L','B','H','E',' ','C','R','G',' ','E','N','O','O','V',
  'G','.','\n','\n','O','H','G',' ','A','B','J',',',' ','L','B',
  'H',' ','F','R','R',' ','F','C','E','R','N','Q',' ','O','R',
  'S','B','E','R',' ','L','B','H',' ','Z','B','E','R',' ','C',
  'B','G','R','A','G','V','N','Y',' ','C','N','V','A',' ','N',
  'A','Q',' ','T','V','O','O','V','G','H','Q','R',' ','N','F',
  ' ','N','\n','A','N','G','V','B','A',' ','B','S',' ','Q','R',
  'Z','B','A','F',' ','E','H','A',' ','N','Z','B','X',' ','V',
  'A',' ','B','H','E',' ','P','V','G','V','R','F','.','\n','\n',
  'A','R','K','G',' ','F','G','B','C',',',' ','U','R','Y','Y',
  ' ','B','A',' ','R','N','E','G','U','!','\n','\0'
};

struct string
{
  struct mutex* mutex;
  char str[sizeof(src_str)/sizeof(char) + 1 /* +1 <=< '\0'*/ ];
  int i;
};

static void*
string_write(void* arg)
{
  struct string* string = arg;
  ASSERT(string);

  for(;;) {
    int i = 0;
    mutex_lock(string->mutex);

    i = string->i;

#if defined(OS_WINDOWS)
    Sleep(0);
#else
    {
      struct timespec time;
      time.tv_sec = 0;
      time.tv_nsec = 10;
      CHK(nanosleep(&time, NULL) == 0);
    }
#endif

    if((unsigned)i >= sizeof(src_str)/sizeof(char) + 1) {
      mutex_unlock(string->mutex);
      break;
    }

    string->str[string->i] = src_str[i];
    string->i = i+1;

    mutex_unlock(string->mutex);
  }
  return NULL;
}

static void
test_mutex(void)
{
  struct string string;
  struct time time_start, time_end, time_res;
#if defined(OS_WINDOWS)
  HANDLE thread;
#else
  pthread_t thread;
#endif
  char dump[32];

  string.str[0] = '\0';
  string.i = 0;

  string.mutex = mutex_create();
  CHK(string.mutex != NULL);

  time_current(&time_start);

  /* Run a sub-thread */
#if defined(OS_WINDOWS)
  thread = (HANDLE)_beginthreadex(NULL, 0,
    (unsigned (__stdcall*)(void*))string_write, &string, 0, NULL);
  CHK(thread != NULL);
  string_write(&string);
  CHK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
  CloseHandle(thread);
#else
  CHK(pthread_create(&thread, NULL, string_write, &string) == 0);
  string_write(&string);
  CHK(pthread_join(thread, NULL) == 0);
#endif

  time_current(&time_end);
  time_sub(&time_res, &time_end, &time_start);

  time_dump
    (&time_res,
     TIME_MSEC|TIME_USEC,
     NULL,
     dump,
     sizeof(dump)/sizeof(char));
  printf("%s\n", dump);

  CHK(string.i == sizeof(src_str)/sizeof(char) + 1);
  CHK(strcmp(string.str, src_str) == 0);

  mutex_destroy(string.mutex);
}

int
main(int argc, char** argv)
{
  (void)argc, (void)argv;
  test_mutex();
  return 0;
}
