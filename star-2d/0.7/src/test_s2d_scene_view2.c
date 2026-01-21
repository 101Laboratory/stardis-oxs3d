/* Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#include "s2d.h"
#include "test_s2d_utils.h"

#include <rsys/float2.h>
#include <rsys/stretchy_array.h>

#include <string.h>

#define NSAMPS 10000

struct ray_data {
  struct s2d_primitive prim;
  float ray_org[2];
  float ray_dir[2];
  float ray_range[2];
};

static INLINE float*
ran_semi_disk_cos_local(float samp[2])
{
  samp[0] = rand_canonic() * 2.f - 1.f;
  samp[1] = (float)sqrt(1 - samp[0]*samp[0]);
  return samp;
}

static INLINE float*
ran_semi_disk_cos(const float up[2], float samp[2])
{
  float tmp[2], v[2];
  CHK(f2_is_normalized(samp) == 1);
  ran_semi_disk_cos_local(tmp);
  v[0] = -up[1] * tmp[0] + up[0]*tmp[1];
  v[1] =  up[0] * tmp[0] + up[1]*tmp[1];;
  CHK(f2_is_normalized(v) == 1);
  return f2_set(samp, v);
}

static int
discard_self_hit
  (const struct s2d_hit* hit,
   const float org[2],
   const float dir[2],
   const float range[2],
   void* ray_data,
   void* filter_data)
{
  struct ray_data* data = ray_data;
  CHK(hit != NULL);
  CHK(org != NULL);
  CHK(dir != NULL);
  CHK(range != NULL);
  CHK((intptr_t)filter_data == (intptr_t)0xDECAFBAD);
  if(!ray_data) return 0;
  CHK(f2_eq(data->ray_org, org));
  CHK(f2_eq(data->ray_dir, dir));
  CHK(f2_eq(data->ray_range, range));
  return S2D_PRIMITIVE_EQ(&data->prim, &hit->prim);
}

static struct s2d_shape*
create_circle_shape
  (struct s2d_device* dev,
   const float radius,
   const float center[2],
   const unsigned nsteps)
{
  float* positions = NULL;
  unsigned* indices = NULL;
  struct s2d_shape* shape;
  struct s2d_vertex_data vdata;
  struct line_segments_desc desc = { NULL, NULL };
  const double step = 2.0*PI/(double)nsteps;
  unsigned i;

  CHK(s2d_shape_create_line_segments(dev, &shape) == RES_OK);
  CHK(s2d_line_segments_set_hit_filter_function
    (shape, discard_self_hit, (void*)0xDECAFBAD) == RES_OK);

  CHK(nsteps > 4);
  CHK(center != NULL);
  CHK(sa_add(positions, nsteps*2/*#coords per vertex*/) != NULL);
  CHK(sa_add(indices, nsteps*2/*#ids per segment*/) != NULL);

  FOR_EACH(i, 0, nsteps) {
    const double theta = i*step;
    const double x = radius*cos(theta) + center[0];
    const double y = radius*sin(theta) + center[1];
    positions[i*2 + 0] = (float)x;
    positions[i*2 + 1] = (float)y;
  }

  FOR_EACH(i, 0, nsteps) {
    indices[i*2 + 0] = i;
    indices[i*2 + 1] = (i+1) % nsteps;
  }

  desc.vertices = positions;
  desc.indices = indices;

  vdata.type = S2D_FLOAT2;
  vdata.usage = S2D_POSITION;
  vdata.get = line_segments_get_position;

  CHK(s2d_line_segments_setup_indexed_vertices
    (shape, nsteps, line_segments_get_ids, nsteps, &vdata, 1, (void*)&desc)
    == RES_OK);

  CHK(s2d_shape_flip_contour(shape) == RES_OK);

  sa_release(positions);
  sa_release(indices);
  return shape;
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct ray_data ray_data;
  struct s2d_device* dev;
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  struct s2d_shape* shape;
  struct s2d_primitive prim;
  unsigned shape_nsteps[3] = { 16, 8, 5 };
  unsigned shape_ids[3];
  int* shape_prims[3] ={ NULL, NULL, NULL };
  int* scene_prims = NULL;
  size_t i;
  size_t nprims;
  float tmp[2];
  float sum, sum_sqr;
  float E, V, SE;
  float area, length;
  int ishape;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s2d_device_create(NULL, &allocator, 0, &dev) == RES_OK);
  CHK(s2d_scene_create(dev, &scn) == RES_OK);

  CHK(sa_add(shape_prims[0], shape_nsteps[0]) != NULL);
  CHK(sa_add(shape_prims[1], shape_nsteps[1]) != NULL);
  CHK(sa_add(shape_prims[2], shape_nsteps[2]) != NULL);
  CHK(sa_add
    (scene_prims, shape_nsteps[0]+shape_nsteps[1]+shape_nsteps[2]) != NULL);

  shape = create_circle_shape(dev, 1.f, f2(tmp, 0.f, 0.f), shape_nsteps[0]);
  CHK(s2d_shape_get_id(shape, &shape_ids[0]) == RES_OK);
  CHK(shape_ids[0] != S2D_INVALID_ID);
  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s2d_shape_ref_put(shape) == RES_OK);

  shape = create_circle_shape(dev, 0.5f, f2(tmp, 2.f, 0.5f), shape_nsteps[1]);
  CHK(s2d_shape_get_id(shape, &shape_ids[1]) == RES_OK);
  CHK(shape_ids[1] != S2D_INVALID_ID);
  CHK(shape_ids[1] != shape_ids[0]);
  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s2d_shape_ref_put(shape) == RES_OK);

  shape = create_circle_shape(dev, 0.25f, f2(tmp, 1.5f, -0.25f), shape_nsteps[2]);
  CHK(s2d_shape_get_id(shape, &shape_ids[2]) == RES_OK);
  CHK(shape_ids[2] != S2D_INVALID_ID);
  CHK(shape_ids[2] != shape_ids[0]);
  CHK(shape_ids[2] != shape_ids[1]);
  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s2d_shape_ref_put(shape) == RES_OK);

  CHK(s2d_scene_view_create
    (scn, S2D_SAMPLE|S2D_GET_PRIMITIVE|S2D_TRACE, &scnview) == RES_OK);

  /* Test sampling */
  memset(shape_prims[0], 0, sa_size(shape_prims[0])*sizeof(shape_prims[0][0]));
  memset(shape_prims[1], 0, sa_size(shape_prims[1])*sizeof(shape_prims[1][0]));
  memset(shape_prims[2], 0, sa_size(shape_prims[2])*sizeof(shape_prims[2][0]));
  memset(scene_prims, 0, sa_size(scene_prims)*sizeof(scene_prims[0]));
  FOR_EACH(i, 0, 1024) {
    struct s2d_attrib attr;
    float s;
    CHK(s2d_scene_view_sample(scnview, rand_canonic(), rand_canonic(), &prim, &s)
      == RES_OK);
    CHK(s2d_primitive_get_attrib(&prim, S2D_POSITION, s, &attr) == RES_OK);
    CHK(attr.type == S2D_FLOAT2);

    FOR_EACH(ishape, 0, 3) if(prim.geom_id == shape_ids[ishape]) break;
    CHK(ishape != 3);

    /* Mark the shape primitive as sampled */
    CHK(prim.prim_id < shape_nsteps[ishape]);
    shape_prims[ishape][prim.prim_id] = 1;

    /* Mark the scene primitive as sampled */
    CHK(prim.scene_prim_id < sa_size(scene_prims));
    scene_prims[prim.scene_prim_id] = 1;
  }

  /* Check that all primitives were sampled */
  FOR_EACH(i, 0, sa_size(shape_prims[0])) CHK(shape_prims[0][i] == 1);
  FOR_EACH(i, 0, sa_size(shape_prims[1])) CHK(shape_prims[1][i] == 1);
  FOR_EACH(i, 0, sa_size(shape_prims[2])) CHK(shape_prims[2][i] == 1);
  FOR_EACH(i, 0, sa_size(scene_prims)) CHK(scene_prims[i] == 1);

  /* Check iteration */
  memset(shape_prims[0], 0, sa_size(shape_prims[0])*sizeof(shape_prims[0][0]));
  memset(shape_prims[1], 0, sa_size(shape_prims[1])*sizeof(shape_prims[1][0]));
  memset(shape_prims[2], 0, sa_size(shape_prims[2])*sizeof(shape_prims[2][0]));
  memset(scene_prims, 0, sa_size(scene_prims)*sizeof(scene_prims[0]));
  CHK(s2d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(sa_size(scene_prims) == nprims);
  FOR_EACH(i, 0, nprims) {

    CHK(s2d_scene_view_get_primitive(scnview, (unsigned)i, &prim) == RES_OK);
    FOR_EACH(ishape, 0, 3) if(prim.geom_id == shape_ids[ishape]) break;
    CHK(ishape != 3);

    /* Mark the shape primitive as visited */
    CHK(prim.prim_id < shape_nsteps[ishape]);
    shape_prims[ishape][prim.prim_id] = 1;

    /* Mark the scene primitive as visited */
    CHK(prim.scene_prim_id < sa_size(scene_prims));
    scene_prims[prim.scene_prim_id] = 1;
  }

  /* Check that all primitives were visited */
  FOR_EACH(i, 0, sa_size(shape_prims[0])) CHK(shape_prims[0][i] == 1);
  FOR_EACH(i, 0, sa_size(shape_prims[1])) CHK(shape_prims[1][i] == 1);
  FOR_EACH(i, 0, sa_size(shape_prims[2])) CHK(shape_prims[2][i] == 1);
  FOR_EACH(i, 0, sa_size(scene_prims)) CHK(scene_prims[i] == 1);

  /* Check the ray tracing by numerically compute PI*S/P aka 4V/S in 2D */
  sum = sum_sqr = 0;
  FOR_EACH(i, 0, NSAMPS) {
    const float range[2] = { 0.f, FLT_MAX };
    struct s2d_attrib attr;
    struct s2d_hit hit;
    float P[2], N[2];
    float s;

    CHK(s2d_scene_view_sample
      (scnview, rand_canonic(), rand_canonic(), &prim, &s) == RES_OK);

    CHK(s2d_primitive_get_attrib(&prim, S2D_POSITION, s, &attr) == RES_OK);
    CHK(attr.type == S2D_FLOAT2);
    f2_set(P, attr.value);

    CHK(s2d_primitive_get_attrib(&prim, S2D_GEOMETRY_NORMAL, s, &attr) == RES_OK);
    CHK(attr.type == S2D_FLOAT2);
    CHK(f2_normalize(N, attr.value) != 0.f);

    f2_normalize(tmp, f2(tmp, 1, 1));
    ran_semi_disk_cos(N, tmp);

    f2_set(ray_data.ray_org, P);
    f2_set(ray_data.ray_dir, tmp);
    f2_set(ray_data.ray_range, range);
    ray_data.prim = prim;
    CHK(s2d_scene_view_trace_ray(scnview, P, tmp, range, &ray_data, &hit) == RES_OK);
    CHK(S2D_HIT_NONE(&hit) == 0);

    sum += hit.distance;
    sum_sqr += hit.distance*hit.distance;
  }

  CHK(s2d_scene_view_compute_contour_length(scnview, &length) == RES_OK);
  CHK(s2d_scene_view_compute_area(scnview, &area) == RES_OK);

  E = sum / (float)NSAMPS;
  V = sum_sqr / (float)NSAMPS - E*E;
  SE = (float)sqrt(V/(float)NSAMPS);
  printf("PI*S / P = %g ~ %g +/- %g\n",(float)PI*area / length, E, SE);
  CHK(eq_epsf((float)PI*area / length, E, 3*SE) == 1);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_ref_put(scn) == RES_OK);
  CHK(s2d_device_ref_put(dev) == RES_OK);

  sa_release(scene_prims);
  sa_release(shape_prims[0]);
  sa_release(shape_prims[1]);
  sa_release(shape_prims[2]);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  return 0;
}

