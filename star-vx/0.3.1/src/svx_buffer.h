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

#ifndef SVX_BUFFER_H
#define SVX_BUFFER_H

#include "svx_c.h"
#include <rsys/dynamic_array.h>

/*
 * Buffer containing the data of a tree. These data are partitioned in fixed
 * size memory pages whose capacity is defined on buffer initialisation with
 * respect to the page size of the system.
 *
 * The children of a node are stored consecutively into a page. The parent node
 * directly references its first valid children excepted if they lie in a
 * different page. In this case, the node references a `struct buffer_index',
 * stored into the same page, that defines the absolute position of its first
 * valid child into the whole list of node pages
 *
 * The data of the nodes are stored in their own memory pages. The attribs of
 * the children of a node are stored consecutively into a page. If the page
 * identifier of the attribs is the same of the page into which their parent
 * node lies, then the node saves the index toward the first valid attrib into
 * the page of attribs. In the other case, the node references a `struct
 * buffer_index', stored into the same page of the node, that defines the
 * absolute position of its first valid attrib into the buffer of attribs.
 */

#define BUFFER_XNODE_FLAG_FAR_INDEX (1u<<15)
#define BUFFER_XNODE_MAX_CHILDREN_OFFSET (BUFFER_XNODE_FLAG_FAR_INDEX-1)
#define BUFFER_XNODE_MASK BUFFER_XNODE_MAX_CHILDREN_OFFSET

struct buffer_xnode {
  /* Offset to retrieve the children. If the BUFFER_XNODE_FLAG_FAR_INDEX bit is
   * not set, the children are stored in the same page at the position `offset
   * & BUFFER_XNODE_MASK'. If BUFFER_XNODE_FLAG_FAR_INDEX is set, `offset &
   * BUFFER_XNODE_MASK' reference a buffer_index toward the node children */
  uint16_t node_offset;
  uint16_t attr_offset;
  uint8_t is_valid; /* Mask defining if the children are valid */
  uint8_t is_leaf; /* Mask defining if the children are leaves */
  uint16_t dummy__; /* Ensure a size of 8 Bytes */
};
STATIC_ASSERT(sizeof(struct buffer_xnode) == 8,
  Unexpected_sizeof_buffer_xnode);

#define BUFFER_INDEX_IPAGE_MAX UINT32_MAX
#define BUFFER_INDEX_INODE_MAX UINT16_MAX

struct buffer_index {
  uint32_t ipage; /* Identifier of the page */
  uint16_t inode; /* Identifier of the node in the page */
  uint16_t dummy__; /* Padding to ensure the tree index is 8 bytes lenght */
};
STATIC_ASSERT(sizeof(struct buffer_index) == 8,
  Unexpected_sizeof_buffer_index);
#define BUFFER_INDEX_NULL__ {UINT32_MAX, UINT16_MAX, UINT16_MAX}
static const struct buffer_index BUFFER_INDEX_NULL = BUFFER_INDEX_NULL__;
#define BUFFER_INDEX_EQ(A, B) ((A)->inode==(B)->inode && (A)->ipage==(B)->ipage)

/* Define the dynamic array of pages */
#define DARRAY_NAME page
#define DARRAY_DATA char*
#include <rsys/dynamic_array.h>

/* Current version the buffer index data structure */
static const int BUFFER_VERSION = 0;

struct buffer {
  size_t pagesize; /* Memory page size in bytes */
  size_t voxsize; /* Memory size of a voxel in bytes */

  struct darray_page node_pages; /* List of pages storing nodes */
  struct darray_page attr_pages; /* List of pages storing node attributes */
  struct buffer_index node_head; /* Index of the next valid node */
  struct buffer_index attr_head; /* Index of the next valid attr */

  struct mem_allocator* allocator;
};

extern LOCAL_SYM void
buffer_init
  (struct mem_allocator* allocator,
   const size_t sizeof_voxel, /* Size in bytes of a voxel */
   struct buffer* buf);

extern LOCAL_SYM void
buffer_release
  (struct buffer* buf);

extern LOCAL_SYM res_T
buffer_alloc_nodes
  (struct buffer* buf,
   const size_t nnodes,
   struct buffer_index* first_node); /* Index toward the 1st allocated node */

extern LOCAL_SYM res_T
buffer_alloc_attrs
  (struct buffer* buf,
   const size_t nattrs,
   struct buffer_index* first_attr); /* Index toward the 1st allocated attrib */

/* Allocate a buffer_index in the current buffer page. Return RES_MEM_ERR if
 * the node index cannot be allocated in the current page. In this case one
 * have to alloc new nodes */
extern LOCAL_SYM res_T
buffer_alloc_far_index
  (struct buffer* buf,
   struct buffer_index* id); /* Index toward the allocated far index */

extern LOCAL_SYM void
buffer_clear
  (struct buffer* buf);

extern LOCAL_SYM res_T
buffer_write
  (const struct buffer* buf,
   FILE* stream);

extern LOCAL_SYM res_T
buffer_read
  (struct buffer* buf,
   FILE* stream);

/* Check buffer data regarding a given tree root and tree dimension */
extern LOCAL_SYM res_T
buffer_check_tree
  (struct buffer* buffer,
   const struct buffer_index root, /* Root of the tree */
   const size_t tree_dimension, /* Dimension of the tree */
   size_t* nleaves); /* Overall #leaves of the tree */

static FINLINE int
buffer_is_empty(const struct buffer* buf)
{
  ASSERT(buf);
  return darray_page_size_get(&buf->node_pages) == 0;
}

static FINLINE struct buffer_xnode*
buffer_get_node
  (struct buffer* buf,
   const struct buffer_index id)
{
  char* mem;
  ASSERT(buf && id.inode < buf->pagesize/sizeof(struct buffer_xnode));
  ASSERT(id.ipage < darray_page_size_get(&buf->node_pages));
  mem = darray_page_data_get(&buf->node_pages)[id.ipage];
  mem += id.inode * sizeof(struct buffer_xnode);
  return (struct buffer_xnode*)mem;
}

static FINLINE void*
buffer_get_attr
  (struct buffer* buf,
   const struct buffer_index id)
{
  char* mem;
  ASSERT(buf && id.inode < buf->pagesize/buf->voxsize);
  ASSERT(id.ipage < darray_page_size_get(&buf->attr_pages));
  mem = darray_page_data_get(&buf->attr_pages)[id.ipage];
  mem += id.inode * buf->voxsize;
  return mem;
}

static FINLINE struct buffer_index*
buffer_get_far_index
  (struct buffer* buf,
   const struct buffer_index id)
{
  char* mem;
  ASSERT(buf && id.inode < buf->pagesize/sizeof(struct buffer_xnode));
  ASSERT(id.ipage < darray_page_size_get(&buf->node_pages));
  mem = darray_page_data_get(&buf->node_pages)[id.ipage];
  mem += id.inode * sizeof(struct buffer_xnode);
  return (struct buffer_index*)mem;
}

static FINLINE struct buffer_index
buffer_get_child_node_index
  (struct buffer* buf,
   const struct buffer_index id,
   const int ichild) /* in [0, 7] */
{
  struct buffer_index child_id = BUFFER_INDEX_NULL;
  struct buffer_xnode* node = NULL;
  uint16_t offset;
  const int ichild_flag = BIT(ichild);
  int ichild_off;
  uint8_t mask;

  ASSERT(ichild >= 0 && ichild < 8 && buf);

  node = buffer_get_node(buf, id);
  mask = (uint8_t)(node->is_valid & ~node->is_leaf);
  ASSERT(mask & ichild_flag);

  /* Compute the child offset from the first child node */
  ichild_off = popcount((uint8_t)((ichild_flag-1) & (int)mask));

  offset = node->node_offset & BUFFER_XNODE_MASK;
  if(!(node->node_offset & BUFFER_XNODE_FLAG_FAR_INDEX)) {
    child_id.ipage = id.ipage;
    child_id.inode = (uint16_t)(offset + ichild_off);
  } else {
    char* mem = darray_page_data_get(&buf->node_pages)[id.ipage];
    child_id = *(struct buffer_index*)(mem+offset*(sizeof(struct buffer_xnode)));
    child_id.inode = (uint16_t)(child_id.inode + ichild_off);
  }
  return child_id;
}

static FINLINE struct buffer_index
buffer_get_child_attr_index
  (struct buffer* buf,
   const struct buffer_index id,
   const int ichild) /* In [0, 7] */
{
  struct buffer_index child_id = BUFFER_INDEX_NULL;
  struct buffer_xnode* node = NULL;
  uint16_t offset;
  const int ichild_flag = BIT(ichild);
  int ichild_off;
  uint8_t mask;

  ASSERT(ichild >= 0 && ichild < 8 && buf);

  node = buffer_get_node(buf, id);
  mask = node->is_valid;
  ASSERT(mask & ichild_flag);

  /* Compute the attr offset from the first child node */
  ichild_off = popcount((uint8_t)((ichild_flag-1) & (int)mask));

  offset = node->attr_offset & BUFFER_XNODE_MASK;
  if(!(node->attr_offset & BUFFER_XNODE_FLAG_FAR_INDEX)) {
    child_id.ipage = id.ipage;
    child_id.inode = (uint16_t)(offset + ichild_off);
  } else {
    char* mem = darray_page_data_get(&buf->node_pages)[id.ipage];
    child_id = *(struct buffer_index*)(mem+offset*(sizeof(struct buffer_xnode)));
    child_id.inode = (uint16_t)(child_id.inode + ichild_off);
  }
  return child_id;
}

static FINLINE size_t
buffer_absolute_attr_index
  (const struct buffer* buf,
   const struct buffer_index index)
{
  ASSERT(buf);
  return index.ipage * buf->pagesize/buf->voxsize + index.inode;
}

#endif /* SVX_BUFFER_H */
