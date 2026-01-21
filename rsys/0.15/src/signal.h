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

#ifndef SIGNAL_H
#define SIGNAL_H

#include "list.h"
#include "rsys.h"

/*******************************************************************************
 * Simple callback data structure
 ******************************************************************************/
/* Declare a the callback data structure named `Type'. Its function arguments
 * are `Args' and a void pointer toward its user defined data */
#define CLBK(Type, Args)                                                       \
  typedef struct                                                               \
  {                                                                            \
    struct list_node node;                                                     \
    void (*func)(LIST_##Args COMMA_##Args void* data );                        \
    void* data; /* Data attached to the callback */                            \
  } Type

/* Initialise the callback data structure. Must be called before any callback
 * operation */
#define CLBK_INIT(Clbk)                                                        \
  {                                                                            \
    list_init(&(Clbk)->node);                                                  \
    (Clbk)->func = NULL;                                                       \
    (Clbk)->data = NULL;                                                       \
  } (void)0

/* Set the callback function and user defined data */
#define CLBK_SETUP(Clbk, Func, Data) (Clbk)->func = Func, (Clbk)->data = Data

/* Disconnect the callback from the signal from which it may be attached */
#define CLBK_DISCONNECT(Clbk) list_del(&(Clbk)->node)

/******************************************************************************
 * Minimalist signal data structure
 ******************************************************************************/
typedef struct list_node signal_T;

/* Initialise the signal. Must be called before any signal operation */
#define SIG_INIT(Sig) list_init(Sig)

/* Attach the callback to the signal. If it was already attached, Clbk is
 * firstly disconnected from the previous signal before its connection to Sig */
#define SIG_CONNECT_CLBK(Sig, Clbk)                                            \
  is_list_empty(&(Clbk)->node)                                                 \
  ? list_add((Sig), &(Clbk)->node)                                             \
  : list_move(&(Clbk)->node, (Sig))

/* Invoke the callback attached to `Sig' with the argument `Args' */
#define SIG_BROADCAST(Sig, ClbkType, Args)                                     \
  {                                                                            \
    struct list_node *pos, *tmp;                                               \
    LIST_FOR_EACH_SAFE(pos, tmp, (Sig)) {                                      \
      ClbkType* clbk = CONTAINER_OF(pos, ClbkType, node);                      \
      ASSERT(clbk->func);                                                      \
      clbk->func(LIST_##Args COMMA_##Args clbk->data);                         \
    }                                                                          \
  } (void)0

#endif /* SIGNAL_H */
