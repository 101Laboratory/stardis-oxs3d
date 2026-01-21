/* Copyright (C) 2018, 2020-2025 |Méso|Star> (contact@meso-star.com)
 * Copyright (C) 2018 Université Paul Sabatier
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include "svx_buffer.h"

#include <rsys/math.h>
#include<rsys/mem_allocator.h>

#ifdef COMPILER_CL
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <unistd.h>
#endif

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE res_T
ensure_allocated_nodes(struct buffer* buf, const size_t nnodes)
{
  char* node_page = NULL;
  size_t nnode_pages = 0;
  res_T res = RES_OK;
  ASSERT(buf);

  if(buf->node_head.ipage != BUFFER_INDEX_NULL.ipage
  && buf->node_head.inode + nnodes <= buf->pagesize/sizeof(struct buffer_xnode))
    goto exit;

  nnode_pages = darray_page_size_get(&buf->node_pages);
  if(nnode_pages > UINT32_MAX) { res = RES_MEM_ERR; goto error; }
  ASSERT(nnode_pages == buf->node_head.ipage + 1);

  /* Alloc and register a node page containing the nodes and the far indices */
  node_page = MEM_CALLOC(buf->allocator, 1, buf->pagesize);
  if(!node_page) { res = RES_MEM_ERR; goto error; }
  res = darray_page_push_back(&buf->node_pages, &node_page);
  if(res != RES_OK) goto error;

  buf->node_head.inode = 0;
  buf->node_head.ipage = (uint32_t)nnode_pages;

exit:
  return res;
error:
  if(node_page) MEM_RM(buf->allocator, node_page);
  CHK(darray_page_resize(&buf->node_pages, nnode_pages) == RES_OK);
  goto exit;
}

static INLINE res_T
ensure_allocated_attrs(struct buffer* buf, const size_t nattrs)
{
  char* attr_page = NULL;
  size_t nattr_pages = 0;
  res_T res = RES_OK;
  ASSERT(buf);

  if(buf->attr_head.ipage != BUFFER_INDEX_NULL.ipage
  && buf->attr_head.inode + nattrs <= buf->pagesize/buf->voxsize)
    goto exit;

  nattr_pages = darray_page_size_get(&buf->attr_pages);
  if(nattr_pages > UINT32_MAX) { res = RES_MEM_ERR; goto error; }
  ASSERT(nattr_pages == buf->attr_head.ipage + 1);

  /* Alloc and register a attr page */
  attr_page = MEM_CALLOC(buf->allocator, 1, buf->pagesize);
  if(!attr_page) { res = RES_MEM_ERR; goto error; }
  res = darray_page_push_back(&buf->attr_pages, &attr_page);
  if(res != RES_OK) goto error;

  buf->attr_head.inode = 0;
  buf->attr_head.ipage = (uint32_t)nattr_pages;

exit:
  return res;
error:
  if(attr_page) MEM_RM(buf->allocator, attr_page);
  CHK(darray_page_resize(&buf->attr_pages, nattr_pages) == RES_OK);
  goto exit;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
void
buffer_init
  (struct mem_allocator* allocator,
   const size_t voxel_size,
   struct buffer* buf)
{
  ASSERT(buf && allocator);
  memset(buf, 0, sizeof(struct buffer));
#ifdef COMPILER_CL
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  buf->pagesize = si.dwPageSize;
#else
  buf->pagesize = (size_t)sysconf(_SC_PAGESIZE);
#endif
  buf->voxsize = voxel_size;
  darray_page_init(allocator, &buf->node_pages);
  darray_page_init(allocator, &buf->attr_pages);
  buf->node_head = BUFFER_INDEX_NULL;
  buf->attr_head = BUFFER_INDEX_NULL;
  buf->allocator = allocator;
  CHK(buf->voxsize <= buf->pagesize);
}

void
buffer_release(struct buffer* buf)
{
  ASSERT(buf);
  buffer_clear(buf);
  darray_page_release(&buf->node_pages);
  darray_page_release(&buf->attr_pages);
}

res_T
buffer_alloc_nodes
  (struct buffer* buf,
   const size_t nnodes,
   struct buffer_index* first_node)
{
  res_T res = RES_OK;
  ASSERT(buf && first_node);

  if(nnodes > buf->pagesize / sizeof(struct buffer_xnode))
    return RES_MEM_ERR;

  res = ensure_allocated_nodes(buf, nnodes);
  if(res != RES_OK) return res;

  *first_node = buf->node_head;
  buf->node_head.inode = (uint16_t)(buf->node_head.inode + nnodes);
  return RES_OK;
}

res_T
buffer_alloc_attrs
  (struct buffer* buf,
   const size_t nattrs,
   struct buffer_index* first_attr)
{
  res_T res = RES_OK;
  ASSERT(buf && first_attr);

  if(nattrs > buf->pagesize / buf->voxsize) return RES_MEM_ERR;

  res = ensure_allocated_attrs(buf, nattrs);
  if(res != RES_OK) return res;

  *first_attr = buf->attr_head;
  buf->attr_head.inode = (uint16_t)(buf->attr_head.inode + nattrs);
  return RES_OK;
}

res_T
buffer_alloc_far_index
  (struct buffer* buf,
   struct buffer_index* id)
{
  size_t remaining_size;
  size_t skipped_nnodes;
  STATIC_ASSERT(sizeof(struct buffer_index) >= sizeof(struct buffer_xnode),
    Unexpected_type_size);

  remaining_size = buf->pagesize - buf->node_head.inode*sizeof(struct buffer_xnode);

  /* Not enough memory in the current page */
  if(sizeof(struct buffer_index) > remaining_size) return RES_MEM_ERR;

  *id = buf->node_head;
  skipped_nnodes = sizeof(struct buffer_index) / sizeof(struct buffer_xnode);
  buf->node_head.inode = (uint16_t)(buf->node_head.inode + skipped_nnodes);
  return RES_OK;
}

void
buffer_clear(struct buffer* buf)
{
  size_t i;
  ASSERT(buf);
  FOR_EACH(i, 0, darray_page_size_get(&buf->node_pages)) {
    MEM_RM(buf->allocator, darray_page_data_get(&buf->node_pages)[i]);
  }
  FOR_EACH(i, 0, darray_page_size_get(&buf->attr_pages)) {
    MEM_RM(buf->allocator, darray_page_data_get(&buf->attr_pages)[i]);
  }
  darray_page_purge(&buf->node_pages);
  darray_page_purge(&buf->attr_pages);
  buf->node_head = BUFFER_INDEX_NULL;
  buf->attr_head = BUFFER_INDEX_NULL;
}

res_T
buffer_write(const struct buffer* buf, FILE* stream)
{
  size_t ipage = 0;
  size_t npages = 0;
  res_T res = RES_OK;
  ASSERT(buf && stream);

  #define WRITE(Var, N) {                                                      \
    if(fwrite((Var), sizeof(*(Var)), (N), stream) != (N)) {                    \
      res = RES_IO_ERR;                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0
  WRITE(&BUFFER_VERSION, 1);
  WRITE(&buf->pagesize, 1);
  WRITE(&buf->voxsize, 1);
  WRITE(&buf->node_head, 1);
  WRITE(&buf->attr_head, 1);

  npages = darray_page_size_get(&buf->node_pages);
  WRITE(&npages, 1);
  FOR_EACH(ipage, 0, npages) {
    WRITE(darray_page_cdata_get(&buf->node_pages)[ipage], buf->pagesize);
  }

  npages = darray_page_size_get(&buf->attr_pages);
  WRITE(&npages, 1);
  FOR_EACH(ipage, 0, npages) {
    WRITE(darray_page_cdata_get(&buf->attr_pages)[ipage], buf->pagesize);
  }
  #undef WRITE

exit:
  return res;
error:
  goto exit;
}

res_T
buffer_read(struct buffer* buf, FILE* stream)
{
  int version = 0;
  char* page = NULL;
  size_t ipage = 0;
  size_t npages = 0;
  res_T res = RES_OK;
  ASSERT(buf && stream);

  buffer_clear(buf);

  #define READ(Var, N) {                                                       \
    if(fread((Var), sizeof(*(Var)), (N), stream) != (N)) {                     \
      if(feof(stream)) {                                                       \
        res = RES_BAD_ARG;                                                     \
      } else if(ferror(stream)) {                                              \
        res = RES_IO_ERR;                                                      \
      } else {                                                                 \
        res = RES_UNKNOWN_ERR;                                                 \
      }                                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0

  /* Currently only one version of the buffer data structure could be
   * serialized. The version management is thus as simple as rejecting any
   * buffer  data structure whose version is not the current version. */
  READ(&version, 1);
  if(version != BUFFER_VERSION) {
    res = RES_BAD_ARG;
    goto error;
  }

  READ(&buf->pagesize, 1);
  READ(&buf->voxsize, 1);
  READ(&buf->node_head, 1);
  READ(&buf->attr_head, 1);

  READ(&npages, 1);
  res = darray_page_reserve(&buf->node_pages, npages);
  if(res != RES_OK) goto error;

  /* Read the pages of nodes */
  FOR_EACH(ipage, 0, npages) {
    page = MEM_ALLOC(buf->allocator, buf->pagesize);
    if(!page) { res = RES_MEM_ERR; goto error; }

    READ(page, buf->pagesize);
    CHK(darray_page_push_back(&buf->node_pages, &page) == RES_OK);
    page = NULL;
  }

  READ(&npages, 1);
  res = darray_page_reserve(&buf->attr_pages, npages);
  if(res != RES_OK) goto error;

  /* Read the pages of attribs */
  FOR_EACH(ipage, 0, npages) {
    page = MEM_ALLOC(buf->allocator, buf->pagesize);
    if(!page) { res = RES_MEM_ERR; goto error; }

    READ(page, buf->pagesize);
    CHK(darray_page_push_back(&buf->attr_pages, &page) == RES_OK);
    page = NULL;
  }
  #undef READ

exit:
  return res;
error:
  if(page) MEM_RM(buf->allocator, page);
  buffer_clear(buf);
  goto exit;
}

res_T
buffer_check_tree
  (struct buffer* buf,
   const struct buffer_index root,
   const size_t tree_dimension,
   size_t* nleaves)
{
  const struct buffer_xnode* node;
  const int nchildren = BIT((int)tree_dimension);
  int ichild;
  res_T res = RES_OK;
  ASSERT(buf);
  ASSERT(0 < tree_dimension && tree_dimension <= 3);

  node = buffer_get_node(buf, root);
  FOR_EACH(ichild, 0, nchildren) {
    const int ichild_flag = BIT(ichild);
    if((node->is_valid & ichild_flag) == 0) continue;

    if(node->is_leaf & ichild_flag) {
      struct buffer_index iattr;
      iattr = buffer_get_child_attr_index(buf, root, ichild);
      if(buffer_get_attr(buf, iattr) == NULL)
        return RES_BAD_ARG;
      *nleaves += 1;
    } else {
      struct buffer_index child;
      child = buffer_get_child_node_index(buf, root, ichild);
      res = buffer_check_tree(buf, child, tree_dimension, nleaves);
      if(res != RES_OK) return res;
    }
  }
  return RES_OK;
}

