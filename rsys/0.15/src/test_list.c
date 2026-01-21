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

#include "list.h"
#include "mem_allocator.h"
#include "rsys.h"

int
main(int argc, char** argv)
{
  struct elmt {
    struct list_node node;
    char c;
  } elmt0, elmt1, elmt2;
  struct list_node list, list1;
  struct list_node* n = NULL;
  struct list_node* tmp = NULL;
  int i = 0;

  (void)argc;
  (void)argv;

  list_init(&list);
  list_init(&list1);
  list_init(&elmt0.node);
  list_init(&elmt1.node);
  list_init(&elmt2.node);

  CHK(is_list_empty(&list) == 1);

  elmt0.c = 'a';
  list_add(&list,  &elmt0.node);
  CHK(is_list_empty(&list) == 0);
  CHK(list_head(&list) == &elmt0.node);

  elmt1.c = 'b';
  list_add(&list,  &elmt1.node);
  CHK(is_list_empty(&list) == 0);
  CHK(elmt1.node.next == &elmt0.node);
  CHK(elmt1.node.prev == &list);
  CHK(elmt1.node.next->prev == &elmt1.node);
  CHK(list_head(&list) == &elmt1.node);

  elmt2.c = 'c';
  list_add_tail(&list,  &elmt2.node);
  CHK(is_list_empty(&list) == 0);
  CHK(elmt2.node.next == &list);
  CHK(elmt2.node.prev == &elmt0.node);
  CHK(elmt2.node.prev->prev == &elmt1.node);
  CHK(elmt1.node.next->next == &elmt2.node);
  CHK(elmt0.node.next == &elmt2.node);
  CHK(list_head(&list) == &elmt1.node);
  CHK(list_tail(&list) == &elmt2.node);

  list_del(&elmt0.node);
  CHK(is_list_empty(&list) == 0);
  CHK(elmt2.node.next == &list);
  CHK(elmt2.node.prev == &elmt1.node);
  CHK(elmt1.node.next == &elmt2.node);
  CHK(elmt1.node.prev == &list);
  CHK(list_head(&list) == &elmt1.node);
  CHK(list_tail(&list) == &elmt2.node);

  list_del(&elmt2.node);
  CHK(is_list_empty(&list) == 0);
  CHK(elmt1.node.next == &list);
  CHK(elmt1.node.prev == &list);
  CHK(list_head(&list) == &elmt1.node);
  CHK(list_tail(&list) == &elmt1.node);

  list_del(&elmt1.node);
  CHK(is_list_empty(&list) == 1);

  list_add(&list,  &elmt2.node);
  list_add(&list,  &elmt1.node);
  list_add(&list,  &elmt0.node);
  CHK(is_list_empty(&list) == 0);
  CHK(elmt2.node.next == &list);
  CHK(elmt2.node.prev == &elmt1.node);
  CHK(elmt1.node.next == &elmt2.node);
  CHK(elmt1.node.prev == &elmt0.node);
  CHK(elmt0.node.next == &elmt1.node);
  CHK(elmt0.node.prev == &list);
  CHK(list_head(&list) == &elmt0.node);
  CHK(list_tail(&list) == &elmt2.node);

  CHK(is_list_empty(&list1) == 1);
  list_move(&elmt1.node, &list1);
  CHK(is_list_empty(&list) == 0);
  CHK(is_list_empty(&list1) == 0);
  CHK(elmt2.node.next == &list);
  CHK(elmt2.node.prev == &elmt0.node);
  CHK(elmt1.node.next == &list1);
  CHK(elmt1.node.prev == &list1);
  CHK(elmt0.node.next == &elmt2.node);
  CHK(elmt0.node.prev == &list);
  CHK(list_head(&list) == &elmt0.node);
  CHK(list_tail(&list) == &elmt2.node);
  CHK(list_head(&list1) == &elmt1.node);
  CHK(list_tail(&list1) == &elmt1.node);

  list_move_tail(&elmt2.node, &list1);
  CHK(is_list_empty(&list) == 0);
  CHK(is_list_empty(&list1) == 0);
  CHK(elmt2.node.next == &list1);
  CHK(elmt2.node.prev == &elmt1.node);
  CHK(elmt1.node.next == &elmt2.node);
  CHK(elmt1.node.prev == &list1);
  CHK(elmt0.node.next == &list);
  CHK(elmt0.node.prev == &list);
  CHK(list_head(&list) == &elmt0.node);
  CHK(list_tail(&list) == &elmt0.node);
  CHK(list_head(&list1) == &elmt1.node);
  CHK(list_tail(&list1) == &elmt2.node);

  list_move(&elmt0.node, &list1);
  CHK(is_list_empty(&list) == 1);
  CHK(is_list_empty(&list1) == 0);
  CHK(elmt2.node.next == &list1);
  CHK(elmt2.node.prev == &elmt1.node);
  CHK(elmt1.node.next == &elmt2.node);
  CHK(elmt1.node.prev == &elmt0.node);
  CHK(elmt0.node.next == &elmt1.node);
  CHK(elmt0.node.prev == &list1);
  CHK(list_head(&list1) == &elmt0.node);
  CHK(list_tail(&list1) == &elmt2.node);

  i = 0;
  LIST_FOR_EACH(n, &list1) {
    struct elmt* e = CONTAINER_OF(n, struct elmt, node);
    CHK(e->c == 'a' + i);
    ++i;
  }
  CHK(i == 3);

  i = 3;
  LIST_FOR_EACH_REVERSE(n, &list1) {
    struct elmt* e = CONTAINER_OF(n, struct elmt, node);
    --i;
    CHK(e->c == 'a' + i);
  }
  CHK(i == 0);

  i = 0;
  LIST_FOR_EACH_SAFE(n, tmp, &list1) {
    struct elmt* e = NULL;
    list_move_tail(n, &list);
    e = CONTAINER_OF(n, struct elmt, node);
    CHK(e->c == 'a' + i);
    ++i;
  }
  CHK(i == 3);
  CHK(is_list_empty(&list1) == 1);
  CHK(is_list_empty(&list) == 0);

  i = 3;
  LIST_FOR_EACH_REVERSE_SAFE(n, tmp, &list) {
    struct elmt* e = NULL;
    list_move(n, &list1);
    e = CONTAINER_OF(n, struct elmt, node);
    --i;
    CHK(e->c == 'a' + i);
  }
  CHK(i == 0);
  CHK(is_list_empty(&list1) == 0);
  CHK(is_list_empty(&list) == 1);

  i = 0;
  LIST_FOR_EACH(n, &list1) {
    struct elmt* e = CONTAINER_OF(n, struct elmt, node);
    CHK(e->c == 'a' + i);
    ++i;
  }
  CHK(i == 3);

  list_move(&elmt1.node, &list1);
  CHK(elmt2.node.next == &list1);
  CHK(elmt2.node.prev == &elmt0.node);
  CHK(elmt1.node.next == &elmt0.node);
  CHK(elmt1.node.prev == &list1);
  CHK(elmt0.node.next == &elmt2.node);
  CHK(elmt0.node.prev == &elmt1.node);
  CHK(list_head(&list1) == &elmt1.node);
  CHK(list_tail(&list1) == &elmt2.node);

  list_move_tail(&elmt0.node, &list1);
  CHK(elmt2.node.next == &elmt0.node);
  CHK(elmt2.node.prev == &elmt1.node);
  CHK(elmt1.node.next == &elmt2.node);
  CHK(elmt1.node.prev == &list1);
  CHK(elmt0.node.next == &list1);
  CHK(elmt0.node.prev == &elmt2.node);
  CHK(list_head(&list1) == &elmt1.node);
  CHK(list_tail(&list1) == &elmt0.node);

  CHK(MEM_ALLOCATED_SIZE(&mem_default_allocator) == 0);

  return 0;
}
