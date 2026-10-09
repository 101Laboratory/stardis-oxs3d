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

#define _POSIX_C_SOURCE 200112L /* getopt/close support */

#include "suvm.h"
#include <star/smsh.h>
#include <rsys/clock_time.h>
#include <rsys/cstr.h>
#include <rsys/mem_allocator.h>

#include <string.h>
#include <unistd.h> /* getopt & close functions */

struct args {
  const char* input_filename;
  const char* output_filename;
  unsigned def[3];
  int precompute_normals;
  int quit;
  int verbose;
};

static const struct args ARGS_DEFAULT = {
  NULL, /* Input file */
  NULL, /* Output file */
  {64, 64, 64}, /* Definition */
  0, /* Precompute normals */
  0, /* Quit */
  0 /* Verbose */
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE void
usage(FILE* stream)
{
  fprintf(stream,
    "usage: suvm-voxelize [-hnv] [-d x,y,z] [-o output] [input]\n");
}

static void
args_release(struct args* args)
{
  ASSERT(args);
  *args = ARGS_DEFAULT;
}

static res_T
args_init(struct args* args, const int argc, char** argv)
{
  size_t n;
  int opt;
  res_T res = RES_OK;
  ASSERT(args);

  *args = ARGS_DEFAULT;

  while((opt = getopt(argc, argv, "d:hno:v")) != -1) {
    switch(opt) {
      case 'd':
        res = cstr_to_list_uint(optarg, ',', args->def, &n, 3);
        if(res == RES_OK && n != 3) res = RES_BAD_ARG;
        if(!args->def[0] || !args->def[1] || !args->def[2]) res = RES_BAD_ARG;
        break;
      case 'h':
        usage(stdin);
        args_release(args);
        args->quit = 1;
        break;
      case 'n':
        args->precompute_normals = 1;
        break;
      case 'o':
        args->output_filename = optarg;
        break;
      case 'v': args->verbose = 1; break;
      default: res = RES_BAD_ARG; break;
    }
    if(res != RES_OK) {
      if(optarg) {
        fprintf(stderr, "%s: invalid option argument '%s' -- '%c'\n",
          argv[0], optarg, opt);
      }
      goto error;
    }
  }

  if(optind < argc) {
    args->input_filename = argv[optind];
  }

exit:
  optind = 1;
  return res;
error:
  usage(stderr);
  args_release(args);
  goto exit;
}

static void
get_indices(const size_t itetra, size_t ids[4], void* ctx)
{
  const uint64_t* ui64;
  struct smsh_desc* desc = ctx;
  ASSERT(desc && desc->dcell == 4);
  ui64 = smsh_desc_get_cell(desc, itetra);
  ids[0] = (size_t)ui64[0];
  ids[1] = (size_t)ui64[1];
  ids[2] = (size_t)ui64[2];
  ids[3] = (size_t)ui64[3];
}

static void
get_position(const size_t ivert, double pos[3], void* ctx)
{
  const double* dbl;
  struct smsh_desc* desc = ctx;
  ASSERT(desc && desc->dnode == 3);
  dbl = smsh_desc_get_node(desc, ivert);
  pos[0] = dbl[0];
  pos[1] = dbl[1];
  pos[2] = dbl[2];
}

static void
write_voxels
  (FILE* stream,
   const int* vxls,
   const unsigned def[3],
   const double vxl_sz[3],
   const double org[3])
{
  size_t ix, iy, iz;
  size_t ivxl;
  size_t i;
  ASSERT(stream && vxls && def && def[0] && def[1] && def[2]);

  fprintf(stream, "# vtk DataFile Version 2.0\n");
  fprintf(stream, "Volumetric grid\n");
  fprintf(stream, "ASCII\n");

  fprintf(stream, "DATASET RECTILINEAR_GRID\n");
  fprintf(stream, "DIMENSIONS %u %u %u\n", def[0]+1, def[1]+1, def[2]+1);
  fprintf(stream, "X_COORDINATES %u float\n", def[0]+1);
  FOR_EACH(i, 0, def[0]+1) fprintf(stream, "%g ", (double)i*vxl_sz[0] + org[0]);
  fprintf(stream, "\n");
  fprintf(stream, "Y_COORDINATES %u float\n", def[1]+1);
  FOR_EACH(i, 0, def[1]+1) fprintf(stream, "%g ", (double)i*vxl_sz[1] + org[1]);
  fprintf(stream, "\n");
  fprintf(stream, "Z_COORDINATES %u float\n", def[2]+1);
  FOR_EACH(i, 0, def[2]+1) fprintf(stream, "%g ", (double)i*vxl_sz[2] + org[2]);
  fprintf(stream, "\n");

  fprintf(stream, "CELL_DATA %u\n", def[0]*def[1]*def[2]);
  fprintf(stream, "SCALARS nintersections int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");

  ivxl = 0;
  FOR_EACH(iz, 0, def[2]) {
    FOR_EACH(iy, 0, def[1]) {
      FOR_EACH(ix, 0, def[0]) {
        fprintf(stream, "%i\n", vxls[ivxl]);
        ++ivxl;
      }
    }
  }
}

static res_T
voxelize_suvm_volume
  (const struct suvm_volume* vol,
   const unsigned def[3],
   int* vxls)
{
  double low[3], upp[3];
  double vxl_sz[3];
  size_t iprim;
  size_t nprims;
  int progress = 0;
  res_T res = RES_OK;
  ASSERT(vol && def && def[0] && def[1] && def[2] && vxls);

  /* Compute the voxel size */
  res = suvm_volume_get_aabb(vol, low, upp);
  if(res != RES_OK) goto error;
  vxl_sz[0] = (upp[0] - low[0]) / (double)def[0];
  vxl_sz[1] = (upp[1] - low[1]) / (double)def[1];
  vxl_sz[2] = (upp[2] - low[2]) / (double)def[2];

  res = suvm_volume_get_primitives_count(vol, &nprims);
  if(res != RES_OK) goto error;

  FOR_EACH(iprim, 0, nprims) {
    struct suvm_primitive prim = SUVM_PRIMITIVE_NULL;
    struct suvm_polyhedron poly;
    size_t ivxl_low[3];
    size_t ivxl_upp[3];
    size_t ivxl[3];
    size_t i = 0;
    int pcent;

    CHK(suvm_volume_get_primitive(vol, iprim, &prim) == RES_OK);
    CHK(suvm_primitive_setup_polyhedron(&prim, &poly) == RES_OK);

    /* Transform the polyhedron AABB in voxel space */
    ivxl_low[0] = (size_t)((poly.lower[0] - low[0]) / vxl_sz[0]);
    ivxl_low[1] = (size_t)((poly.lower[1] - low[1]) / vxl_sz[1]);
    ivxl_low[2] = (size_t)((poly.lower[2] - low[2]) / vxl_sz[2]);
    ivxl_upp[0] = (size_t)ceil((poly.upper[0] - low[0]) / vxl_sz[0]);
    ivxl_upp[1] = (size_t)ceil((poly.upper[1] - low[1]) / vxl_sz[1]);
    ivxl_upp[2] = (size_t)ceil((poly.upper[2] - low[2]) / vxl_sz[2]);
    CHK(ivxl_low[0] < def[0] && ivxl_upp[0] <= def[0]);
    CHK(ivxl_low[1] < def[1] && ivxl_upp[1] <= def[1]);
    CHK(ivxl_low[2] < def[2] && ivxl_upp[2] <= def[2]);

    FOR_EACH(ivxl[2], ivxl_low[2], ivxl_upp[2]) {
      float vxl_low[3];
      float vxl_upp[3];
      vxl_low[0] = (float)low[0];
      vxl_low[1] = (float)low[1];
      vxl_low[2] = (float)((double)ivxl[2] * vxl_sz[2] + low[2]);
      vxl_upp[0] = (float)upp[0];
      vxl_upp[1] = (float)upp[1];
      vxl_upp[2] = vxl_low[2] + (float)vxl_sz[2];

      FOR_EACH(ivxl[1], ivxl_low[1], ivxl_upp[1]) {
        vxl_low[1] = (float)((double)ivxl[1] * vxl_sz[1] + low[1]);
        vxl_upp[1] = vxl_low[1] + (float)vxl_sz[1];
        FOR_EACH(ivxl[0], ivxl_low[0], ivxl_upp[0]) {
          vxl_low[0] = (float)((double)ivxl[0] * vxl_sz[0] + low[0]);
          vxl_upp[0] = vxl_low[0] + (float)vxl_sz[0];

          i = ivxl[0] + ivxl[1]*def[0] + ivxl[2]*def[0]*def[1];
          vxls[i] += (int)suvm_polyhedron_intersect_aabb(&poly, vxl_low, vxl_upp);
        }
      }
    }
    pcent = (int)((double)iprim * 100 / (double)nprims + 0.5/*round*/);
    if(pcent > progress) {
      progress = pcent;
      fprintf(stderr, "Voxelizing: %3d%%\r", progress);
      fflush(stderr);
    }
  }

  fprintf(stderr, "Voxelizing: %3d%%\n", progress);

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Main functions
 ******************************************************************************/
int
main(int argc, char** argv)
{
  char dump[128];
  struct args args = ARGS_DEFAULT;
  FILE* stream_out = stdout;
  FILE* stream_in = stdin;
  const char* stream_out_name = "stdout";
  const char* stream_in_name = "stdin";
  struct smsh* smsh = NULL;
  struct smsh_create_args smsh_args = SMSH_CREATE_ARGS_DEFAULT;
  struct smsh_load_stream_args load_stream_args = SMSH_LOAD_STREAM_ARGS_NULL;
  struct smsh_desc desc = SMSH_DESC_NULL;
  struct suvm_device* suvm = NULL;
  struct suvm_volume* vol = NULL;
  struct suvm_tetrahedral_mesh_args msh_args = SUVM_TETRAHEDRAL_MESH_ARGS_NULL;
  struct time t0, t1;
  int* vxls = NULL;
  double low[3];
  double upp[3];
  double vxl_sz[3];
  res_T res = RES_OK;
  int err = 0;

  res = args_init(&args, argc, argv);
  if(res != RES_OK) goto error;
  if(args.quit) goto exit;

  /* Setup input stream */
  if(args.input_filename) {
    stream_in = fopen(args.input_filename, "r");
    if(!stream_in) {
      fprintf(stderr, "Could not open the input file '%s' -- %s.\n",
        args.input_filename, strerror(errno));
      goto error;
    }
    stream_in_name = args.input_filename;
  }

  /* Setup output stream */
  if(args.output_filename) {
    stream_out = fopen(args.output_filename, "w");
    if(!stream_out) {
      fprintf(stderr, "Could not open the output file '%s' -- %s.\n",
        args.output_filename, strerror(errno));
      goto error;
    }
    stream_out_name = args.output_filename;
    (void)stream_out_name;
  }


  /* Load the submitted file */
  smsh_args.verbose = args.verbose;
  res = smsh_create(&smsh_args, &smsh);
  if(res != RES_OK) goto error;
  load_stream_args.stream = stream_in;
  load_stream_args.name = stream_in_name;
  load_stream_args.memory_mapping = stream_in != stdin;
  res = smsh_load_stream(smsh, &load_stream_args);
  if(res != RES_OK) goto error;
  res = smsh_get_desc(smsh, &desc);
  if(res != RES_OK) goto error;
  if(desc.dnode != 3 || desc.dcell != 4) {
    fprintf(stderr, "Only tetrahedrical mesh are supported\n");
    res = RES_BAD_ARG;
    goto error;
  }
  if(args.verbose) {
    fprintf(stderr, "#nodes: %u; #tetrahedra: %u\n", desc.dnode, desc.dcell);
  }

  res = suvm_device_create(NULL, NULL, args.verbose, &suvm);
  if(res != RES_OK) goto error;

  msh_args.nvertices = desc.nnodes;
  msh_args.ntetrahedra = desc.ncells;
  msh_args.get_indices = get_indices;
  msh_args.get_position = get_position;
  msh_args.context = &desc;
  msh_args.precompute_normals = args.precompute_normals;

  /* Setup the volume from the loaded data */
  if(args.verbose) {
    fprintf(stderr, "Partitionning the tetrahedral mesh '%s'\n", stream_in_name);
  }
  time_current(&t0);
  res = suvm_tetrahedral_mesh_create(suvm, &msh_args, &vol);
  if(res != RES_OK) goto error;
  time_sub(&t0, time_current(&t1), &t0);
  time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));
  if(args.verbose) {
    fprintf(stderr, "Build acceleration structure in %s\n", dump);
  }

  /* Allocate the list of voxels */
  vxls = mem_calloc(args.def[0]*args.def[1]*args.def[2], sizeof(*vxls));
  if(!vxls) {
    fprintf(stderr, "Could not allocate the list of voxels.\n");
    res = RES_MEM_ERR;
    goto error;
  }

  /* Voxelize the volume */
  time_current(&t0);
  res = voxelize_suvm_volume(vol, args.def, vxls);
  if(res != RES_OK) goto error;
  time_sub(&t0, time_current(&t1), &t0);
  time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));
  if(args.verbose) {
    fprintf(stderr, "Volume voxelized in %s\n", dump);
  }

  /* Write the voxels */
  if(args.verbose) {
    fprintf(stderr, "Writing voxels in '%s'\n", stream_out_name);
  }
  SUVM(volume_get_aabb(vol, low, upp));
  vxl_sz[0] = (upp[0] - low[0]) / (double)args.def[0];
  vxl_sz[1] = (upp[1] - low[1]) / (double)args.def[1];
  vxl_sz[2] = (upp[2] - low[2]) / (double)args.def[2];
  time_current(&t0);
  write_voxels(stream_out, vxls, args.def, vxl_sz, low);
  time_sub(&t0, time_current(&t1), &t0);
  time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));
  if(args.verbose) {
    fprintf(stderr, "Voxels written in %s\n", dump);
  }

exit:
  if(stream_out && stream_out != stdout) fclose(stream_out);
  if(stream_in && stream_in != stdin) fclose(stream_in);
  if(suvm) SUVM(device_ref_put(suvm));
  if(vol) SUVM(volume_ref_put(vol));
  if(smsh) SMSH(ref_put(smsh));
  if(vxls) mem_rm(vxls);
  args_release(&args);
  if(mem_allocated_size() != 0) {
    fprintf(stderr, "Memory leaks: %lu Bytes.\n",
      (unsigned long)mem_allocated_size());
  }
  return err;
error:
  err = -1;
  goto exit;
}
