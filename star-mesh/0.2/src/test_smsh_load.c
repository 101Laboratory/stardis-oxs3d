/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#include "smsh.h"

#include <rsys/double3.h>
#include <rsys/math.h>
#include<rsys/mem_allocator.h>
#include <rsys/rsys.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
check_smsh_desc
  (const struct smsh_desc* desc,
   const uint64_t nnodes,
   const uint64_t ncells,
   const uint64_t dnode,
   const uint64_t dcell)
{
  size_t i, j;
  CHK(desc);
  CHK(nnodes);
  CHK(ncells);
  CHK(dnode);
  CHK(dcell);

  CHK(desc->nnodes == nnodes);
  CHK(desc->ncells == ncells);
  CHK(desc->dnode == dnode);
  CHK(desc->dcell == dcell);

  FOR_EACH(i, 0, nnodes) {
    const double* node0 = desc->nodes + i*dnode;
    const double* node1 = smsh_desc_get_node(desc, i);
    FOR_EACH(j, 0, dnode) {
      CHK(node0[j] == node1[j]);
      CHK(node0[j] == (double)i + (double)j*0.1);
    }
  }

  FOR_EACH(i, 0, ncells) {
    const uint64_t* cell0 = desc->cells + i*dcell;
    const uint64_t* cell1 = smsh_desc_get_cell(desc, i);
    FOR_EACH(j, 0, dcell) {
      CHK(cell0[j] == cell1[j]);
      CHK(cell0[j] == (i*dcell+j)%nnodes);
    }
  }
}

static void
test_load_mesh(struct smsh* smsh, const uint32_t dnode, const uint32_t dcell)
{
  hash256_T hash0;
  hash256_T hash1;
  struct smsh_desc desc = SMSH_DESC_NULL;
  struct smsh_load_args args = SMSH_LOAD_ARGS_NULL;
  struct smsh_load_stream_args stream_args = SMSH_LOAD_STREAM_ARGS_NULL;
  FILE* fp = NULL;
  const char* filename = "test_file.smsh";
  const uint64_t pagesize = 16384;
  const uint64_t nnodes = 287;
  const uint64_t ncells = 192;
  size_t i, j;
  char byte = 0;
  ASSERT(smsh);

  fp = fopen(filename, "w+");
  CHK(fp);

  /* Write file header */
  CHK(fwrite(&pagesize, sizeof(pagesize), 1, fp) == 1);
  CHK(fwrite(&nnodes, sizeof(nnodes), 1, fp) == 1);
  CHK(fwrite(&ncells, sizeof(ncells), 1, fp) == 1);
  CHK(fwrite(&dnode, sizeof(dnode), 1, fp) == 1);
  CHK(fwrite(&dcell, sizeof(dcell), 1, fp) == 1);

  /* Padding */
  CHK(fseek(fp, (long)ALIGN_SIZE((size_t)ftell(fp), pagesize), SEEK_SET) == 0);

  /* Write vertex nodes */
  FOR_EACH(i, 0, nnodes) {
    FOR_EACH(j, 0, dnode) {
      const double dbl = (double)i + (double)j * 0.1;
      CHK(fwrite(&dbl, sizeof(dbl), 1, fp) == 1);
    }
  }

  /* Padding */
  CHK(fseek(fp, (long)ALIGN_SIZE((size_t)ftell(fp), pagesize), SEEK_SET) == 0);

  /* Write tetrahedra */
  FOR_EACH(i, 0, ncells) {
    FOR_EACH(j, 0, dcell) {
      const uint64_t ui64 = (i*dcell + j) % nnodes;
      CHK(fwrite(&ui64, sizeof(ui64), 1, fp) == 1);
    }
  }

  /* Padding. Write one char to position the EOF indicator */
  CHK(fseek(fp, (long)ALIGN_SIZE((size_t)ftell(fp), pagesize)-1, SEEK_SET) == 0);
  CHK(fwrite(&byte, sizeof(byte), 1, fp) == 1);

  rewind(fp);

  stream_args.stream = fp;
  stream_args.name = filename;
  CHK(smsh_load_stream(NULL, &stream_args) == RES_BAD_ARG);
  CHK(smsh_load_stream(smsh, NULL) == RES_BAD_ARG);
  stream_args.stream = NULL;
  CHK(smsh_load_stream(smsh, &stream_args) == RES_BAD_ARG);
  stream_args.stream = fp;
  stream_args.name = NULL;
  CHK(smsh_load_stream(smsh, &stream_args) == RES_BAD_ARG);
  stream_args.name = filename;
  CHK(smsh_load_stream(smsh, &stream_args) == RES_OK);
  CHK(smsh_get_desc(NULL, &desc) == RES_BAD_ARG);
  CHK(smsh_get_desc(smsh, NULL) == RES_BAD_ARG);
  CHK(smsh_get_desc(smsh, &desc) == RES_OK);
  check_smsh_desc(&desc, nnodes, ncells, dnode, dcell);

  CHK(smsh_desc_compute_hash(NULL, hash0) == RES_BAD_ARG);
  CHK(smsh_desc_compute_hash(&desc, NULL) == RES_BAD_ARG);
  CHK(smsh_desc_compute_hash(&desc, hash0) == RES_OK);

  rewind(fp);
  stream_args.name = SMSH_LOAD_STREAM_ARGS_NULL.name;
  stream_args.memory_mapping = 1;
  CHK(smsh_load_stream(smsh, &stream_args) == RES_OK);
  CHK(smsh_get_desc(smsh, &desc) == RES_OK);
  check_smsh_desc(&desc, nnodes, ncells, dnode, dcell);

  CHK(smsh_desc_compute_hash(&desc, hash1) == RES_OK);
  CHK(hash256_eq(hash0, hash1));
  CHK(fclose(fp) == 0);

  args.path = filename;
  CHK(smsh_load(NULL, &args) == RES_BAD_ARG);
  CHK(smsh_load(smsh, NULL) == RES_BAD_ARG);
  args.path = NULL;
  CHK(smsh_load(smsh, &args) == RES_BAD_ARG);
  args.path = "nop";
  CHK(smsh_load(smsh, &args) == RES_IO_ERR);
  args.path = filename;
  CHK(smsh_load(smsh, &args) == RES_OK);
  CHK(smsh_get_desc(smsh, &desc) == RES_OK);
  check_smsh_desc(&desc, nnodes, ncells, dnode, dcell);
  CHK(smsh_desc_compute_hash(&desc, hash1) == RES_OK);
  CHK(hash256_eq(hash0, hash1));

  args.memory_mapping = 1;
  CHK(smsh_load(smsh, &args) == RES_OK);
  CHK(smsh_get_desc(smsh, &desc) == RES_OK);
  check_smsh_desc(&desc, nnodes, ncells, dnode, dcell);
  CHK(smsh_desc_compute_hash(&desc, hash1) == RES_OK);
  CHK(hash256_eq(hash0, hash1));
}

static void
test_load_fail(struct smsh* smsh)
{
  struct smsh_load_stream_args args = SMSH_LOAD_STREAM_ARGS_NULL;
  const char byte = 0;
  FILE* fp = NULL;
  uint64_t pagesize;
  uint64_t nnodes;
  uint64_t ncells;
  unsigned dnode;
  unsigned dcell;

  /* Wrong pagesize */
  fp = tmpfile();
  CHK(fp);
  pagesize = 1023;
  nnodes = 10;
  ncells = 10;
  dnode = 3;
  dcell = 4;
  CHK(fwrite(&pagesize, sizeof(pagesize), 1, fp) == 1);
  CHK(fwrite(&nnodes, sizeof(nnodes), 1, fp) == 1);
  CHK(fwrite(&ncells, sizeof(ncells), 1, fp) == 1);
  CHK(fwrite(&dnode, sizeof(dnode), 1, fp) == 1);
  CHK(fwrite(&dcell, sizeof(dcell), 1, fp) == 1);
  CHK(fseek(fp, (long)ALIGN_SIZE((size_t)ftell(fp), pagesize), SEEK_SET) == 0);
  CHK(fseek(fp, (long)(sizeof(double[3])*nnodes), SEEK_CUR) == 0);
  CHK(fseek(fp, (long)ALIGN_SIZE((size_t)ftell(fp), pagesize), SEEK_SET) == 0);
  CHK(fseek(fp, (long)(sizeof(uint64_t[4])*ncells), SEEK_CUR) == 0);
  CHK(fseek(fp, (long)ALIGN_SIZE((size_t)ftell(fp), pagesize)-1, SEEK_SET) == 0);
  CHK(fwrite(&byte, sizeof(byte), 1, fp) == 1);
  rewind(fp);
  args.stream = fp;
  CHK(smsh_load_stream(smsh, &args) == RES_BAD_ARG);
  CHK(fclose(fp) == 0);

  /* Wrong size */
  fp = tmpfile();
  CHK(fp);
  pagesize = (uint64_t)sysconf(_SC_PAGESIZE);
  nnodes = 10;
  ncells = 10;
  dnode = 3;
  dcell = 4;
  CHK(fwrite(&pagesize, sizeof(pagesize), 1, fp) == 1);
  CHK(fwrite(&nnodes, sizeof(nnodes), 1, fp) == 1);
  CHK(fwrite(&ncells, sizeof(ncells), 1, fp) == 1);
  CHK(fwrite(&dnode, sizeof(dnode), 1, fp) == 1);
  CHK(fwrite(&dcell, sizeof(dcell), 1, fp) == 1);
  CHK(fseek(fp, (long)ALIGN_SIZE((size_t)ftell(fp), pagesize), SEEK_SET) == 0);
  CHK(fseek(fp, (long)(sizeof(double[3])*nnodes), SEEK_CUR) == 0);
  CHK(fseek(fp, (long)ALIGN_SIZE((size_t)ftell(fp), pagesize), SEEK_SET) == 0);
  CHK(fseek(fp, (long)(sizeof(uint64_t[4])*ncells), SEEK_CUR) == 0);
  CHK(fseek(fp, (long)ALIGN_SIZE((size_t)ftell(fp), pagesize)-2, SEEK_SET) == 0);
  CHK(fwrite(&byte, sizeof(byte), 1, fp) == 1);
  rewind(fp);
  args.stream = fp;
  CHK(smsh_load_stream(smsh, &args) == RES_IO_ERR);
  CHK(fclose(fp) == 0);
}

static void
test_load_files(struct smsh* smsh, int argc, char** argv)
{
  hash256_T hash;
  int i;
  CHK(smsh);
  FOR_EACH(i, 1, argc) {
    struct smsh_desc desc = SMSH_DESC_NULL;
    size_t j;
    size_t inode;
    size_t icell;

    /* Load from file */
    if(strcmp(argv[i], "-") != 0) {
      struct smsh_load_args args = SMSH_LOAD_ARGS_NULL;
      printf("Load %s\n", argv[i]);
      args.path = argv[i];
      args.memory_mapping = 1;
      CHK(smsh_load(smsh, &args) == RES_OK);

    /* Load from stdin */
    } else {
      struct smsh_load_stream_args args = SMSH_LOAD_STREAM_ARGS_NULL;
      printf("Load from stdin\n");
      args.stream = stdin;
      args.name = "stdin";
      args.memory_mapping = 1;
      CHK(smsh_load_stream(smsh, &args) == RES_BAD_ARG);
      args.memory_mapping = 0;
      CHK(smsh_load_stream(smsh, &args) == RES_OK);
    }
    CHK(smsh_get_desc(smsh, &desc) == RES_OK);
    CHK(smsh_desc_compute_hash(&desc, hash) == RES_OK);

    FOR_EACH(inode, 0, desc.nnodes) {
      const double* node = smsh_desc_get_node(&desc, inode);
      FOR_EACH(j, 0, desc.dnode) {
        CHK(node[j] == node[j]); /* !NaN */
        CHK(!IS_INF(node[j]));
      }
    }
    FOR_EACH(icell, 0, desc.ncells) {
      const uint64_t* ids = smsh_desc_get_cell(&desc, icell);
      FOR_EACH(j, 0, desc.dcell) {
        size_t k;
        FOR_EACH(k, 0, desc.dcell) {
          if(k == j) continue;
          CHK(ids[j] != ids[k]); /* Check non degenerated cell */
        }
      }
    }
  }
}

/*******************************************************************************
 * Main function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct smsh_create_args args = SMSH_CREATE_ARGS_DEFAULT;
  struct smsh* smsh = NULL;
  (void)argc, (void)argv;

  args.verbose = 1;
  CHK(smsh_create(&args, &smsh) == RES_OK);

  if(argc > 1) {
    test_load_files(smsh, argc, argv);
  } else {
    test_load_mesh(smsh, 3, 4); /* Tetrahedra */
    test_load_mesh(smsh, 3, 3); /* Triangles 3D */
    test_load_mesh(smsh, 2, 3); /* Triangles 2D */
    test_load_fail(smsh);
  }

  CHK(smsh_ref_put(smsh) == RES_OK);
  CHK(mem_allocated_size() == 0);
  return 0;
}
