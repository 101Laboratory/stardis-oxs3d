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

#include "mem_allocator.h"
#include "signal.h"

struct ctxt {
  int sig0_func1_invoked;
  int sig0_func2_sum;
  int sig1_func_sum;
};

enum test_signal {
  SIG0,
  SIG1,
  SIG2,
  SIG3,
  SIGNALS_COUNT
};

static void
sig0_func1(struct ctxt* ctxt, void* data)
{
  CHK(data == NULL);
  ctxt->sig0_func1_invoked = 1;
}

static void
sig0_func2(struct ctxt* ctxt, void* data)
{
  CHK(data != NULL);
  ctxt->sig0_func2_sum += *((int*)data);
}

static void
sig1_func(struct ctxt* ctxt, void* data)
{
  CHK(data != NULL);
  ctxt->sig1_func_sum += *(int*)data;
}

static void
sig2_func( void* data )
{
  CHK(data != NULL);
  *(uint32_t*)data = 0xDECAFBAD;
}

static void
sig3_func(int i, float f, char c, void* data)
{
  CHK(data != NULL);
  CHK(i == 3);
  CHK(f == 4.0f);
  CHK(c == 'a');
  *(uint32_t*)data = 0xDECAFBAD;
}

int
main(int argc, char** argv)
{
  signal_T signals[SIGNALS_COUNT];

  CLBK(clbk_T, ARG1(struct ctxt*));
  CLBK(clbk2_T, ARG0());
  CLBK(clbk3_T, ARG3(int, float, char));

  struct ctxt ctxt;
  clbk_T clbk0_a;
  clbk_T clbk0_b;
  clbk_T clbk0_c;
  clbk_T clbk1_a;
  clbk_T clbk1_b;
  clbk2_T clbk2;
  clbk3_T clbk3;
  int i = 0;
  int array[] = { 12, -1, 2, 1 };
  uint32_t clbk_data = 0;

  (void)argc, (void)argv;

  FOR_EACH(i, 0, SIGNALS_COUNT)
    SIG_INIT(&signals[i]);

  CLBK_INIT(&clbk0_a);
  CLBK_INIT(&clbk0_b);
  CLBK_INIT(&clbk0_c);
  CLBK_INIT(&clbk1_a);
  CLBK_INIT(&clbk1_b);
  CLBK_SETUP(&clbk0_a, sig0_func1, NULL);
  CLBK_SETUP(&clbk0_b, sig0_func2, array + 0);
  CLBK_SETUP(&clbk0_c, sig0_func2, array + 1);
  CLBK_SETUP(&clbk1_a, sig1_func, array + 2);
  CLBK_SETUP(&clbk1_b, sig1_func, array + 3);

  ctxt.sig0_func1_invoked = 0;
  ctxt.sig0_func2_sum = 0;
  ctxt.sig1_func_sum = 0;

  SIG_BROADCAST(&signals[SIG0], clbk_T, ARG1(&ctxt));
  CHK(ctxt.sig0_func1_invoked == 0);
  CHK(ctxt.sig0_func2_sum == 0);
  CHK(ctxt.sig1_func_sum == 0);

  SIG_BROADCAST(&signals[SIG1], clbk_T, ARG1(&ctxt));
  CHK(ctxt.sig0_func1_invoked == 0);
  CHK(ctxt.sig0_func2_sum == 0);
  CHK(ctxt.sig1_func_sum == 0);

  SIG_CONNECT_CLBK(&signals[SIG0], &clbk0_a);
  SIG_CONNECT_CLBK(&signals[SIG0], &clbk0_b);
  SIG_CONNECT_CLBK(&signals[SIG0], &clbk0_c);
  SIG_BROADCAST(&signals[SIG0], clbk_T, ARG1(&ctxt));
  CHK(ctxt.sig0_func1_invoked == 1);
  CHK(ctxt.sig0_func2_sum == 11);
  CHK(ctxt.sig1_func_sum == 0);

  CLBK_DISCONNECT(&clbk0_c);
  ctxt.sig0_func1_invoked = 0;
  ctxt.sig0_func2_sum = 0;
  ctxt.sig1_func_sum = 0;
  SIG_BROADCAST(&signals[SIG0], clbk_T, ARG1(&ctxt));
  CHK(ctxt.sig0_func1_invoked == 1);
  CHK(ctxt.sig0_func2_sum == 12);
  CHK(ctxt.sig1_func_sum == 0);

  SIG_CONNECT_CLBK(&signals[SIG1], &clbk1_a);
  SIG_BROADCAST(&signals[SIG0], clbk_T, ARG1(&ctxt));
  CHK(ctxt.sig0_func1_invoked == 1);
  CHK(ctxt.sig0_func2_sum == 24);
  CHK(ctxt.sig1_func_sum == 0);

  SIG_BROADCAST(&signals[SIG1], clbk_T, ARG1(&ctxt));
  CHK(ctxt.sig0_func1_invoked == 1);
  CHK(ctxt.sig0_func2_sum == 24);
  CHK(ctxt.sig1_func_sum == 2);

  SIG_CONNECT_CLBK(&signals[SIG1], &clbk1_b);
  SIG_BROADCAST(&signals[SIG1], clbk_T, ARG1(&ctxt));
  CHK(ctxt.sig0_func1_invoked == 1);
  CHK(ctxt.sig0_func2_sum == 24);
  CHK(ctxt.sig1_func_sum == 5);

  CLBK_INIT(&clbk2);
  CLBK_SETUP(&clbk2, sig2_func, &clbk_data);
  SIG_CONNECT_CLBK(&signals[SIG2], &clbk2);
  CHK(clbk_data == 0);
  SIG_BROADCAST(&signals[SIG2], clbk2_T, ARG0());
  CHK(clbk_data == 0xDECAFBAD);
  CLBK_DISCONNECT(&clbk2);

  CLBK_INIT(&clbk3);
  CLBK_SETUP(&clbk3, &sig3_func, &clbk_data);
  SIG_CONNECT_CLBK(&signals[SIG3], &clbk3);
  clbk_data = 0;
  SIG_BROADCAST(&signals[SIG3], clbk3_T, ARG3(3, 4.0f, 'a'));
  CHK(clbk_data == 0xDECAFBAD);

  CHK(MEM_ALLOCATED_SIZE(&mem_default_allocator) == 0);

  return 0;
}
