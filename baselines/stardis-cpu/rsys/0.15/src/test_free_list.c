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

#include "rsys.h"
#include "free_list.h"

struct object {
  FITEM;
  unsigned int i;
};

#define FITEM_TYPE object
#include "free_list.h"

int
main(int argc, char** argv)
{
  #define NB_OBJ 1024
  struct flist_object list;
  struct object* obj = NULL;
  struct fid id[NB_OBJ];
  char find[NB_OBJ];
  size_t nitems;
  int i = 0;
  (void)argc, (void)argv;

  FOR_EACH(i, 0, NB_OBJ) {
    id[i] = FID_NULL;
  }

  flist_object_init(NULL, &list);
  CHK(flist_object_is_empty(&list));
  CHK(flist_object_hold(&list, id[0]) == 0);
  CHK(flist_object_get(&list, id[0]) == NULL);

  id[0] = flist_object_add(&list);
  CHK(id[0].index == 0);
  CHK(flist_object_is_empty(&list) == 0);
  flist_object_clear(&list);

  id[0] = flist_object_add(&list);
  id[1] = flist_object_add(&list);
  CHK(id[0].index == 0);
  CHK(id[1].index == 1);
  flist_object_del(&list, id[0]);
  CHK(id[1].index == 1);
  id[0] = flist_object_add(&list);
  CHK(id[0].index == 0);
  CHK(id[1].index == 1);
  flist_object_del(&list, id[0]);
  flist_object_del(&list, id[1]);
  id[0] = flist_object_add(&list);
  id[1] = flist_object_add(&list);
  flist_object_clear(&list);
  CHK(id[0].index != id[1].index);
  CHK(id[0].index <= 1);
  CHK(id[1].index <= 1);
  CHK(flist_object_is_empty(&list) == 1);

  FOR_EACH(i, 0, NB_OBJ / 2) {
    struct fid tmp_id;
    id[i] = flist_object_add(&list);
    CHK(flist_object_hold(&list, id[i]) == 1);
    obj = flist_object_get(&list, id[i]);
    tmp_id = object_id_get(obj);
    CHK(id[i].index == (unsigned)i);
    CHK(FID_EQ(tmp_id, id[i]) == 1);
    CHK(obj != NULL);
    obj->i = 0xDECAF000 + (unsigned)i;
  }
  CHK(flist_object_is_empty(&list) == 0);

  FOR_EACH(i, 0, NB_OBJ * 2 / 3) {
    const float rand_f /* in [0, 1] */ = (float)rand() / (float)RAND_MAX;
    const int ii = (int)(rand_f * (NB_OBJ - 1));
    flist_object_del(&list, id[ii]);
    id[ii] = FID_NULL;
  }

  FOR_EACH(i, NB_OBJ / 2, NB_OBJ) {
    id[i] = flist_object_add(&list);
    CHK(flist_object_hold(&list, id[i]) == 1);
    obj = flist_object_get(&list, id[i]);
    CHK(obj != NULL);
    obj->i = 0xDECAF000 + (unsigned)i;
  }

  FOR_EACH(i, 0, NB_OBJ) {
    if(IS_FID_NULL(id[i])) {
      CHK(flist_object_hold(&list, id[i]) == 0);
      CHK(flist_object_get(&list, id[i]) == NULL);
    } else {
      CHK(flist_object_hold(&list, id[i]) == 1);
      obj = flist_object_get(&list, id[i]);
      CHK(obj->i == 0xDECAF000 + (unsigned)i);
    }
  }

  flist_object_release(&list);
  flist_object_init(NULL, &list);

  FOR_EACH(i, 0, NB_OBJ) {
    id[i] = flist_object_add(&list);
    obj = flist_object_get(&list, id[i]);
    obj->i = (unsigned)i;
  }

  nitems = 0;
  memset(find, 0, NB_OBJ * sizeof(char));
  FLIST_FOR_EACH(obj, &list) {
    CHK(find[obj->i] == 0);
    find[obj->i] = 1;
    ++nitems;
  }
  CHK(nitems == NB_OBJ);

  FOR_EACH(i, 0, NB_OBJ / 2)
    flist_object_del(&list, id[i*2]);

  nitems = 0;
  memset(find, 0, NB_OBJ * sizeof(char));
  FLIST_FOR_EACH(obj, &list) {
    CHK(find[obj->i] == 0);
    find[obj->i] = 1;
    ++nitems;
  }
  CHK(nitems == NB_OBJ/2);
  FOR_EACH(i, 0, NB_OBJ) {
    if(i%2) {
      CHK(flist_object_hold(&list, id[i]) == 1);
      CHK(find[i] == 1);
    } else {
      CHK(flist_object_hold(&list, id[i]) == 0);
      CHK(find[i] == 0);
      CHK(IS_FID_NULL(object_id_get(list.items + id[i].index)) == 1);
    }
  }

  CHK(flist_object_is_empty(&list) == 0);
  flist_object_clear(&list);
  CHK(flist_object_is_empty(&list) == 1);
  nitems = 0;
  FLIST_FOR_EACH(obj, &list)
    ++nitems;
  CHK(nitems == 0);

  FOR_EACH(i, 0, NB_OBJ)
    CHK(flist_object_hold(&list, id[i]) == 0);

  FOR_EACH(i, 0, NB_OBJ/4) {
    id[i] = flist_object_add(&list);
    obj = flist_object_get(&list, id[i]);
    obj->i = (unsigned)i*2;
  }

  nitems = 0;
  memset(find, 0, NB_OBJ * sizeof(char));
  FLIST_FOR_EACH(obj, &list) {
    CHK(obj->i % 2 == 0);
    find[obj->i/2] = 1;
    ++nitems;
  }
  CHK(nitems == NB_OBJ / 4);
  FOR_EACH(i, 0, NB_OBJ/4) {
    CHK(find[i] == 1);
    CHK(flist_object_hold(&list, id[i]) == 1);
    CHK(flist_object_get(&list, id[i])->i == (unsigned)i * 2);
  }

  flist_object_release(&list);
  CHK(MEM_ALLOCATED_SIZE(&mem_default_allocator) == 0);
  return 0;
}
