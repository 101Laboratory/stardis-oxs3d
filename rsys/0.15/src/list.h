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

#ifndef LIST_H
#define LIST_H

#include "rsys.h"

struct list_node {
  struct list_node* next;
  struct list_node* prev;
};

/******************************************************************************
 * Private functions
 ******************************************************************************/
static FINLINE void
add_node__
  (struct list_node* node,
   struct list_node* prev,
   struct list_node* next)
{
  ASSERT(node && prev && next);
  next->prev = node;
  node->next = next;
  node->prev = prev;
  prev->next = node;
}

static FINLINE void
del_node__(struct list_node* prev, struct list_node* next)
{
  ASSERT(prev && next);
  next->prev = prev;
  prev->next = next;
}

/******************************************************************************
 * Helper macros
 ******************************************************************************/
#define LIST_FOR_EACH(Pos, List)                                               \
  for(Pos = (List)->next; Pos != (List); Pos = Pos->next)

#define LIST_FOR_EACH_REVERSE(Pos, List)                                       \
  for(Pos = (List)->prev; Pos != (List); Pos = Pos->prev)

/* Safe against removal of list entry. */
#define LIST_FOR_EACH_SAFE(Pos, Tmp, List)                                     \
  for((Pos) = (List)->next, (Tmp) = (Pos)->next;                               \
      (Pos) != (List);                                                         \
      (Pos) = Tmp, Tmp = (Pos)->next)

/* Safe against removal of list entry. */
#define LIST_FOR_EACH_REVERSE_SAFE(Pos, Tmp, List)                             \
  for((Pos) = (List)->prev, (Tmp) = (Pos)->prev;                               \
      (Pos) != (List);                                                         \
      (Pos) = Tmp, Tmp = (Pos)->prev)

/******************************************************************************
 * Node list functions
 ******************************************************************************/
static FINLINE void
list_init(struct list_node* node)
{
  ASSERT(node);
  node->next = node;
  node->prev = node;
}

static FINLINE char
is_list_empty(const struct list_node* node)
{
  ASSERT(node);
  return node->next == node;
}

static FINLINE struct list_node*
list_head(struct list_node* node)
{
  ASSERT(node && !is_list_empty(node));
  return node->next;
}

static FINLINE struct list_node*
list_tail(struct list_node* node)
{
  ASSERT(node && !is_list_empty(node));
  return node->prev;
}

static FINLINE void
list_add(struct list_node* list, struct list_node* node)
{
  ASSERT(list && node && is_list_empty(node));
  add_node__(node, list, list->next);
}

static FINLINE void
list_add_tail(struct list_node* list, struct list_node* node)
{
  ASSERT(list && node && is_list_empty(node));
  add_node__(node, list->prev, list);
}

static FINLINE void
list_del(struct list_node* node)
{
  ASSERT(node);
  del_node__(node->prev, node->next);
  list_init(node);
}

static FINLINE void
list_move(struct list_node* node, struct list_node* list)
{
  ASSERT(node && list);
  del_node__(node->prev, node->next);
  add_node__(node, list, list->next);
}

static FINLINE void
list_move_tail(struct list_node* node, struct list_node* list)
{
  ASSERT(node && list);
  del_node__(node->prev, node->next);
  add_node__(node, list->prev, list);
}

#endif /* LIST_H */
