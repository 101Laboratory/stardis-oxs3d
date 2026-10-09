/* Copyright (C) 2015-2023 |Méso|Star> (contact@meso-star.com)
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

#define _POSIX_C_SOURCE 200112L /* exp2f, fabsf, nextafterf */

#include "s3d.h"
#include "test_s3d_cbox.h"
#include "test_s3d_utils.h"

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/float33.h>
#include <limits.h>

#define ON_EDGE_EPSILON 1.e-4f
#define POSITION_EPSILON 1.e-3f

struct closest_pt {
  float pos[3];
  float normal[3];
  float dst;
  unsigned iprim;
  unsigned igeom;
  unsigned iinst;
};

#define CLOSEST_PT_NULL__ {                                                    \
  {0,0,0},                                                                     \
  {0,0,0},                                                                     \
  FLT_MAX,                                                                     \
  S3D_INVALID_ID,                                                              \
  S3D_INVALID_ID,                                                              \
  S3D_INVALID_ID                                                               \
}

static const struct closest_pt CLOSEST_PT_NULL = CLOSEST_PT_NULL__;

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
/* Function that computes the point onto the triangle that ensures the minimum
 * distance between the submitted `pos' and the triangle. Use this routine to
 * cross check the result of the s3d_scene_view_closest_point function that
 * internally relies on a more efficient implementation */
static float*
closest_point_triangle
  (const float p[3], /* Input pos */
   const float a[3], /* 1st triangle vertex */
   const float b[3], /* 2nd triangle vertex */
   const float c[3], /* 3rd triangle vertex */
   float pt[3]) /* Closest point of pos onto the triangle */
{
  float N[3]; /* Triangle normal */
  float Nab[3], Nbc[3], Nca[3]; /* Edge normals */
  float ab[3], ac[3], bc[3];
  float ap[3], bp[3], cp[3];
  float d1, d2, d3, d4, d5, d6, d;
  float lab, lac, lbc;
  CHK(p && a && b && c && pt);

  lab = f3_normalize(ab, f3_sub(ab, b, a));
  lac = f3_normalize(ac, f3_sub(ac, c, a));
  lbc = f3_normalize(bc, f3_sub(bc, c, b));

  /* Compute the triangle normal */
  f3_cross(N, ac, ab);

  /* Check if the nearest point is the 1st triangle vertex */
  f3_sub(ap, p, a);
  d1 = f3_dot(ab, ap);
  d2 = f3_dot(ac, ap);
  if(d1 <= 0 && d2 <= 0) return f3_set(pt, a);

  /* Check if the nearest point is the 2nd triangle vertex */
  f3_sub(bp, p, b);
  d3 = f3_dot(bc, bp);
  d4 =-f3_dot(ab, bp);
  if(d3 <= 0 && d4 <= 0) return f3_set(pt, b);

  /* Check if the nearest point is the 3rd triangle vertex */
  f3_sub(cp, p, c);
  d5 =-f3_dot(ac, cp);
  d6 =-f3_dot(bc, cp);
  if(d5 <= 0 && d6 <= 0) return f3_set(pt, c);

  /* Check if the nearest point is on the 1st triangle edge */
  f3_normalize(Nab, f3_cross(Nab, ab, N));
  if(f3_dot(Nab, ap) <= 0) {
    return f3_add(pt, a, f3_mulf(pt, ab, MMIN(d1, lab)));
  }

  /* Check if the nearest point is on the 2nd triangle edge */
  f3_normalize(Nbc, f3_cross(Nbc, bc, N));
  if(f3_dot(Nbc, bp) <= 0) {
    return f3_add(pt, b, f3_mulf(pt, bc, MMIN(d3, lbc)));
  }

  /* Check if the nearest point is on the 3rd triangle edge */
  f3_normalize(Nca, f3_cross(Nca, ac, N));
  f3_minus(Nca, Nca);
  if(f3_dot(Nca, cp) <= 0) {
    return f3_add(pt, c, f3_mulf(pt, ac,-MMIN(d5, lac)));
  }

  /* The nearest point is in the triangle */
  f3_normalize(N, N);
  d = f3_dot(N, ap);
  return f3_add(pt, p, f3_mulf(pt, N, -d));
}

static void
closest_point_mesh
  (const float pos[3],
   const float* verts,
   const unsigned* ids,
   const unsigned ntris,
   const unsigned geom_id,
   const unsigned inst_id,
   struct closest_pt* pt)
{
  unsigned itri;
  CHK(pos && verts && ids && pt);

  *pt = CLOSEST_PT_NULL;
  pt->igeom = geom_id;
  pt->iinst = inst_id;
  pt->dst = FLT_MAX;

  /* Find the closest point on the mesh */
  FOR_EACH(itri, 0, ntris) {
    float v0[3];
    float v1[3];
    float v2[3];
    float closest_pt[3];
    float vec[3];
    float dst;

    f3_set(v0, verts+ids[itri*3+0]*3);
    f3_set(v1, verts+ids[itri*3+1]*3);
    f3_set(v2, verts+ids[itri*3+2]*3);

    closest_point_triangle(pos, v0, v1, v2, closest_pt);
    dst = f3_len(f3_sub(vec, closest_pt, pos));

    if(dst < pt->dst) {
      float E0[3], E1[3];
      f3_set(pt->pos, closest_pt);
      pt->dst = dst;
      pt->iprim = itri;
      f3_sub(E0, v1, v0);
      f3_sub(E1, v2, v0);
      f3_cross(pt->normal, E1, E0);
      f3_normalize(pt->normal, pt->normal);
    }
  }
}

static void
closest_point_sphere
  (const float pos[3],
   const float sphere_org[3],
   const float sphere_radius,
   const unsigned geom_id,
   const unsigned inst_id,
   struct closest_pt* pt)
{
  float vec[3];
  float len;
  CHK(pos && sphere_org && sphere_radius > 0 && pt);

  f3_sub(vec, pos, sphere_org);
  len = f3_normalize(vec, vec);
  CHK(len > 0);

  pt->dst = (float)fabs(len - sphere_radius);
  f3_set(pt->normal, vec);
  f3_add(pt->pos, sphere_org, f3_mulf(pt->pos, vec, sphere_radius));
  pt->iprim = 0;
  pt->igeom = geom_id;
  pt->iinst = inst_id;
}

/* Check that `hit' roughly lies on an edge. */
static int
hit_on_edge(const struct s3d_hit* hit)
{
  struct s3d_attrib v0, v1, v2, pos;
  float E0[3], E1[3], N[3];
  float tri_2area;
  float hit_2area0;
  float hit_2area1;
  float hit_2area2;
  float hit_pos[3];

  CHK(hit && !S3D_HIT_NONE(hit));

  /* Retrieve the triangle vertices */
  CHK(s3d_triangle_get_vertex_attrib(&hit->prim, 0, S3D_POSITION, &v0)==RES_OK);
  CHK(s3d_triangle_get_vertex_attrib(&hit->prim, 1, S3D_POSITION, &v1)==RES_OK);
  CHK(s3d_triangle_get_vertex_attrib(&hit->prim, 2, S3D_POSITION, &v2)==RES_OK);

  /* Compute the triangle area * 2 */
  f3_sub(E0, v1.value, v0.value);
  f3_sub(E1, v2.value, v0.value);
  tri_2area = f3_len(f3_cross(N, E0, E1));

  /* Compute the hit position */
  CHK(s3d_primitive_get_attrib(&hit->prim, S3D_POSITION, hit->uv, &pos) == RES_OK);
  f3_set(hit_pos, pos.value);

  /* Compute areas */
  f3_sub(E0, v0.value, hit_pos);
  f3_sub(E1, v1.value, hit_pos);
  hit_2area0 = f3_len(f3_cross(N, E0, E1));
  f3_sub(E0, v1.value, hit_pos);
  f3_sub(E1, v2.value, hit_pos);
  hit_2area1 = f3_len(f3_cross(N, E0, E1));
  f3_sub(E0, v2.value, hit_pos);
  f3_sub(E1, v0.value, hit_pos);
  hit_2area2 = f3_len(f3_cross(N, E0, E1));

  if(hit_2area0 / tri_2area < ON_EDGE_EPSILON
  || hit_2area1 / tri_2area < ON_EDGE_EPSILON
  || hit_2area2 / tri_2area < ON_EDGE_EPSILON)
    return 1;

  return 0;
}

static void
check_closest_point
  (const struct s3d_hit* hit,
   const struct closest_pt* pt,
   const int hit_triangle) /* Define if `hit' lies on a triangle */
{
  struct s3d_attrib attr;
  float N[3];

  CHK(hit && pt);
  CHK(!S3D_HIT_NONE(hit));

  CHK(s3d_primitive_get_attrib
    (&hit->prim, S3D_POSITION, hit->uv, &attr) == RES_OK);
  f3_normalize(N, hit->normal);

  if(!hit_triangle || !hit_on_edge(hit)) {
    CHK(hit->prim.prim_id == pt->iprim);
  }

  if(hit->prim.prim_id == pt->iprim
  && hit->prim.geom_id == pt->igeom
  && hit->prim.inst_id == pt->iinst) {
    /* Due to numerical inaccuracies and/or the arbitrary order in which
     * primitives are treated, 2 points on different primitive can have the
     * same distance from the query position while their respective
     * coordinates are not equal wrt POSITION_EPSILON. To avoid wrong
     * assertion, we thus check the position returned by Star-3D against the
     * manually computed position only if these positions lies on the same
     * primitive */
    CHK(f3_eq_eps(pt->pos, attr.value, POSITION_EPSILON));
    CHK(f3_eq_eps(pt->normal, N, 1.e-4f));
  }
  CHK(eq_epsf(hit->distance, pt->dst, 1.e-3f));
}

/*******************************************************************************
 * Cornell box and sphere test
 ******************************************************************************/
struct instance {
  float translation[3];
  unsigned id;
};

static void
check_closest_point_cbox_sphere
  (const float pos[3],
   const float sphere_org[3],
   const float sphere_radius,
   const unsigned walls_id,
   const unsigned sphere_id,
   const struct instance* instances,
   const size_t ninstances,
   struct s3d_hit* hit)
{
  struct closest_pt pt_walls = CLOSEST_PT_NULL;
  struct closest_pt pt_sphere = CLOSEST_PT_NULL;
  const struct closest_pt* pt = NULL;
  CHK(pos && hit);

  if(!ninstances) {
    closest_point_mesh(pos, cbox_walls, cbox_walls_ids, cbox_walls_ntris,
      walls_id, S3D_INVALID_ID, &pt_walls);
    closest_point_sphere(pos, sphere_org, sphere_radius, sphere_id,
      S3D_INVALID_ID, &pt_sphere);
  } else {
    size_t iinst;

    pt_walls.dst = FLT_MAX;
    FOR_EACH(iinst, 0, ninstances) {
      struct closest_pt pt_walls_tmp;
      struct closest_pt pt_sphere_tmp;
      float pos_instance_space[3];

      /* Transform query position in instance space */
      f3_sub(pos_instance_space, pos, instances[iinst].translation);

      closest_point_mesh(pos_instance_space, cbox_walls, cbox_walls_ids,
        cbox_walls_ntris, walls_id, instances[iinst].id, &pt_walls_tmp);
      closest_point_sphere(pos_instance_space, sphere_org, sphere_radius,
        sphere_id, instances[iinst].id, &pt_sphere_tmp);

      if(pt_walls_tmp.dst < pt_walls.dst) {
        pt_walls = pt_walls_tmp;
        /* Transform query closest point in world space */
        f3_add(pt_walls.pos, pt_walls.pos, instances[iinst].translation);
      }
      if(pt_sphere_tmp.dst < pt_sphere.dst) {
        pt_sphere = pt_sphere_tmp;
        /* Transform query closest point in world space */
        f3_add(pt_sphere.pos, pt_sphere.pos, instances[iinst].translation);
      }
    }
  }

  if(pt_walls.dst< pt_sphere.dst) {
    pt = &pt_walls;
  } else {
    pt = &pt_sphere;
  }

  check_closest_point(hit, pt, hit->prim.geom_id == walls_id);
}

static void
test_cbox_sphere(struct s3d_device* dev)
{
  struct s3d_hit hit = S3D_HIT_NULL;
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  struct s3d_scene* scn = NULL;
  struct s3d_shape* walls = NULL;
  struct s3d_shape* sphere = NULL;
  struct s3d_shape* inst0 = NULL;
  struct s3d_shape* inst1 = NULL;
  struct s3d_scene_view* scnview = NULL;
  struct instance instances[2];
  struct cbox_desc cbox_desc;
  size_t i;
  float low[3], upp[3], mid[3], sz[3];
  float pos[3];
  float sphere_org[3];
  float sphere_radius;
  unsigned walls_id, sphere_id;

  CHK(s3d_scene_create(dev, &scn) == RES_OK);

  /* Setup the cornell box walls */
  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = cbox_get_position;
  cbox_desc.vertices = cbox_walls;
  cbox_desc.indices = cbox_walls_ids;
  CHK(s3d_shape_create_mesh(dev, &walls) == RES_OK);
  CHK(s3d_shape_get_id(walls, &walls_id) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, walls) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(walls, cbox_walls_ntris, cbox_get_ids,
    cbox_walls_nverts, &vdata, 1, &cbox_desc) == RES_OK);

  /* Compute the Cornell box AABB */
  CHK(s3d_scene_view_create(scn, S3D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s3d_scene_view_get_aabb(scnview, low, upp) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  /* Setup the sphere at the center of the cornell box */
  f3_mulf(mid, f3_add(mid, low, upp), 0.5f);
  f3_sub(sz, upp, low);
  f3_set(sphere_org, mid);
  sphere_radius = MMIN(MMIN(sz[0], sz[1]), sz[2]) * 0.125f; /* 1/8 of the box */
  CHK(s3d_shape_create_sphere(dev, &sphere) == RES_OK);
  CHK(s3d_shape_get_id(sphere, &sphere_id) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere) == RES_OK);
  CHK(s3d_sphere_setup(sphere, sphere_org, sphere_radius) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);

  /* Check point query on the scene */
  FOR_EACH(i, 0, 10000) {
    /* Randomly generate a point in a bounding box that is 2 times the size of
     * the scene AABB */
    pos[0] = mid[0] + (rand_canonic() * 2 - 1) * (upp[0] - low[0]);
    pos[1] = mid[1] + (rand_canonic() * 2 - 1) * (upp[1] - low[1]);
    pos[2] = mid[2] + (rand_canonic() * 2 - 1) * (upp[2] - low[2]);

    CHK(s3d_scene_view_closest_point(scnview, pos, (float)INF, NULL, &hit) == RES_OK);
    check_closest_point_cbox_sphere(pos, sphere_org, sphere_radius, walls_id,
      sphere_id, NULL, 0, &hit);
  }

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  /* Instantiate the cbox sphere scene */
  CHK(s3d_scene_instantiate(scn, &inst0) == RES_OK);
  CHK(s3d_scene_instantiate(scn, &inst1) == RES_OK);
  CHK(s3d_shape_get_id(inst0, &instances[0].id) == RES_OK);
  CHK(s3d_shape_get_id(inst1, &instances[1].id) == RES_OK);
  f3_mulf(instances[0].translation, sz, 0.5f);
  CHK(s3d_instance_translate
    (inst0, S3D_WORLD_TRANSFORM, instances[0].translation) == RES_OK);
  f3_mulf(instances[1].translation, sz,-0.5f);
  CHK(s3d_instance_translate
    (inst1, S3D_WORLD_TRANSFORM, instances[1].translation) == RES_OK);

  /* Create a new scene with instantiated cbox sphere scenes */
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, inst0) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, inst1) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_get_aabb(scnview, low, upp) == RES_OK);
  f3_mulf(mid, f3_add(mid, low, upp), 0.5f);

  /* Check point query on instances */
  FOR_EACH(i, 0, 10000) {
    /* Randomly generate a point in a bounding box that is 2 times the size of
     * the scene AABB */
    pos[0] = mid[0] + (rand_canonic() * 2 - 1) * (upp[0] - low[0]);
    pos[1] = mid[1] + (rand_canonic() * 2 - 1) * (upp[1] - low[1]);
    pos[2] = mid[2] + (rand_canonic() * 2 - 1) * (upp[2] - low[2]);

    CHK(s3d_scene_view_closest_point(scnview, pos, (float)INF, NULL, &hit) == RES_OK);
    check_closest_point_cbox_sphere(pos, sphere_org, sphere_radius, walls_id,
      sphere_id, instances, 2/*#instances*/, &hit);
  }

  /* Clean up */
  CHK(s3d_shape_ref_put(inst0) == RES_OK);
  CHK(s3d_shape_ref_put(inst1) == RES_OK);
  CHK(s3d_shape_ref_put(walls) == RES_OK);
  CHK(s3d_shape_ref_put(sphere) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
}

/*******************************************************************************
 * Sphere test
 ******************************************************************************/
struct sphere_filter_data {
  float query_pos[3];
  float query_radius;
};

static int
sphere_filter
  (const struct s3d_hit* hit,
   const float org[3],
   const float dir[3],
   const float range[2],
   void* query_data,
   void* filter_data)
{
  struct sphere_filter_data* data = query_data;
  struct s3d_attrib attr;
  float pos[3];
  float vec[3];

  CHK(hit && org && dir && range && !S3D_HIT_NONE(hit));
  CHK((intptr_t)filter_data == (intptr_t)0xDECAFBAD);
  CHK(f3_normalize(vec, dir) != 0);

  f3_add(pos, org, f3_mulf(pos, vec, hit->distance));
  CHK(s3d_primitive_get_attrib
    (&hit->prim, S3D_POSITION, hit->uv, &attr) == RES_OK);
  CHK(f3_eq_eps(attr.value, pos, POSITION_EPSILON));

  CHK(f3_eq_eps(data->query_pos, org, POSITION_EPSILON));
  CHK(range[0] == 0);
  CHK(range[1] == data->query_radius);

  return 1;
}

static void
test_sphere(struct s3d_device* dev)
{
  struct s3d_attrib attr;
  struct s3d_hit hit = S3D_HIT_NULL;
  struct s3d_shape* sphere = NULL;
  struct s3d_scene* scn = NULL;
  struct s3d_scene_view* scnview = NULL;
  struct sphere_filter_data filter_data;
  void* ptr = (void*)((intptr_t)0xDECAFBAD);
  size_t i;
  float sphere_pos[3];
  float query_pos[3];
  float sphere_radius;
  float pos[3];
  float dir[3];
  unsigned sphere_id;

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_shape_create_sphere(dev, &sphere) == RES_OK);
  CHK(s3d_shape_get_id(sphere, &sphere_id) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere) == RES_OK);

  f3_splat(sphere_pos, 1);
  sphere_radius = 2;
  f3_set(query_pos, sphere_pos);
  CHK(s3d_sphere_setup(sphere, query_pos, sphere_radius) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);

  /* Check a closest point query exactly at the center of the sphere */
  CHK(s3d_scene_view_closest_point
    (scnview, sphere_pos, (float)INF, NULL, &hit) == RES_OK);
  CHK(!S3D_HIT_NONE(&hit));
  CHK(s3d_primitive_get_attrib(&hit.prim, S3D_POSITION, hit.uv, &attr) == RES_OK);

  f3_normalize(dir, f3_sub(dir, attr.value, query_pos));
  f3_add(pos, attr.value, f3_mulf(pos, dir, -hit.distance));
  CHK(hit.distance == sphere_radius);
  CHK(f3_eq_eps(pos, sphere_pos, POSITION_EPSILON));

  /* Check the exclusive bound of the search radius */
  CHK(s3d_scene_view_closest_point
    (scnview, sphere_pos, sphere_radius, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit));

  /* Check closest point query on a sphere */
  FOR_EACH(i, 0, 10000) {
    struct closest_pt pt;
    float Ng[3];
    query_pos[0] = sphere_pos[0] + (rand_canonic() * 2 - 1) * sphere_radius;
    query_pos[1] = sphere_pos[1] + (rand_canonic() * 2 - 1) * sphere_radius;
    query_pos[2] = sphere_pos[2] + (rand_canonic() * 2 - 1) * sphere_radius;

    CHK(s3d_scene_view_closest_point
      (scnview, query_pos, (float)INF, NULL, &hit) == RES_OK);
    CHK(!S3D_HIT_NONE(&hit));
    CHK(s3d_primitive_get_attrib(&hit.prim, S3D_POSITION, hit.uv, &attr) == RES_OK);

    /* Cross check the closest point query result */
    closest_point_sphere(query_pos, sphere_pos, sphere_radius,
      sphere_id, S3D_INVALID_ID, &pt);

    f3_normalize(Ng, hit.normal);

    CHK(pt.dst == hit.distance);
    CHK(pt.iprim == hit.prim.prim_id);
    CHK(pt.igeom == hit.prim.geom_id);
    CHK(pt.iinst == hit.prim.inst_id);
    CHK(f3_eq_eps(pt.pos, attr.value, POSITION_EPSILON));
    CHK(f3_eq_eps(pt.normal, Ng, 1.e-4f));

    /* Check search radius exclusivity */
    CHK(s3d_scene_view_closest_point
      (scnview, query_pos, hit.distance, NULL, &hit) == RES_OK);
    CHK(S3D_HIT_NONE(&hit));
    hit.distance = nextafterf(hit.distance, 0.f);
    CHK(s3d_scene_view_closest_point
      (scnview, query_pos, hit.distance, NULL, &hit) == RES_OK);
    CHK(!S3D_HIT_NONE(&hit));
  }

  /* Check the filtering function */
  CHK(s3d_sphere_set_hit_filter_function(sphere, sphere_filter, ptr) == RES_OK);

  f3_splat(query_pos, 10);
  f3_set(filter_data.query_pos, query_pos);
  filter_data.query_radius = (float)INF;
  CHK(s3d_scene_view_closest_point
    (scnview, query_pos, (float)INF, &filter_data, &hit) == RES_OK);
  CHK(!S3D_HIT_NONE(&hit));

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_closest_point
    (scnview, query_pos, (float)INF, &filter_data, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit));

  CHK(s3d_shape_ref_put(sphere) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
}

/*******************************************************************************
 * Cornell box test
 ******************************************************************************/
enum cbox_geom {
  CBOX_WALLS,
  CBOX_TALL_BLOCK,
  CBOX_SHORT_BLOCK,
  CBOX_GEOMS_COUNT__
};

struct cbox_filter_data {
  float query_pos[3];
  float query_radius;
  unsigned geom_to_filter[3];
};

static int
cbox_filter
  (const struct s3d_hit* hit,
   const float org[3],
   const float dir[3],
   const float range[2],
   void* query_data,
   void* filter_data)
{
  struct cbox_filter_data* data = query_data;
  struct s3d_attrib attr;
  float pos[3];
  float vec[3];

  CHK(hit && org && dir && range && !S3D_HIT_NONE(hit));
  CHK((intptr_t)filter_data == (intptr_t)0xDECAFBAD);
  CHK(f3_normalize(vec, dir) != 0);

  f3_add(pos, org, f3_mulf(pos, vec, hit->distance));
  CHK(s3d_primitive_get_attrib
    (&hit->prim, S3D_POSITION, hit->uv, &attr) == RES_OK);
  CHK(f3_eq_eps(attr.value, pos, POSITION_EPSILON));

  if(!query_data) return 0;

  CHK(f3_eq_eps(data->query_pos, org, POSITION_EPSILON));
  CHK(range[0] == 0);
  CHK(range[1] == data->query_radius);

  return data->geom_to_filter[0] == hit->prim.geom_id
      || data->geom_to_filter[1] == hit->prim.geom_id
      || data->geom_to_filter[2] == hit->prim.geom_id;
}

static void
check_closest_point_cbox
  (const float pos[3],
   const unsigned geom_id[3],
   struct s3d_hit* hit)
{
  struct closest_pt pt[CBOX_GEOMS_COUNT__] = {
    CLOSEST_PT_NULL__, CLOSEST_PT_NULL__, CLOSEST_PT_NULL__
  };
  enum cbox_geom geom;

  CHK(pos && geom_id && hit);

  if(geom_id[CBOX_WALLS] != S3D_INVALID_ID) { /* Are the walls filtered */
    closest_point_mesh(pos, cbox_walls, cbox_walls_ids, cbox_walls_ntris,
      geom_id[CBOX_WALLS], S3D_INVALID_ID, &pt[CBOX_WALLS]);
  }
  if(geom_id[CBOX_TALL_BLOCK] != S3D_INVALID_ID) { /* Is the block filtered */
    closest_point_mesh(pos, cbox_tall_block, cbox_block_ids, cbox_block_ntris,
      geom_id[CBOX_TALL_BLOCK], S3D_INVALID_ID, &pt[CBOX_TALL_BLOCK]);
  }
  if(geom_id[CBOX_SHORT_BLOCK] != S3D_INVALID_ID) { /* Is the block filtered */
    closest_point_mesh(pos, cbox_short_block, cbox_block_ids, cbox_block_ntris,
      geom_id[CBOX_SHORT_BLOCK], S3D_INVALID_ID, &pt[CBOX_SHORT_BLOCK]);
  }
  geom = pt[CBOX_WALLS].dst < pt[CBOX_TALL_BLOCK].dst
    ? CBOX_WALLS : CBOX_TALL_BLOCK;
  geom = pt[CBOX_SHORT_BLOCK].dst < pt[geom].dst
    ? CBOX_SHORT_BLOCK : geom;

  if(pt[geom].dst >= FLT_MAX) { /* All geometries were filtered */
    CHK(S3D_HIT_NONE(hit));
  } else {
    check_closest_point(hit, &pt[geom], 1);
  }
}

static void
test_cbox(struct s3d_device* dev)
{
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  struct s3d_hit hit = S3D_HIT_NULL;
  struct s3d_scene* scn = NULL;
  struct s3d_shape* walls = NULL;
  struct s3d_shape* tall_block = NULL;
  struct s3d_shape* short_block = NULL;
  struct s3d_scene_view* scnview = NULL;
  struct cbox_desc walls_desc;
  struct cbox_desc tall_block_desc;
  struct cbox_desc short_block_desc;
  struct cbox_filter_data filter_data;
  void* ptr = (void*)((intptr_t)0xDECAFBAD);
  float pos[3];
  float low[3], upp[3], mid[3];
  unsigned geom_id[CBOX_GEOMS_COUNT__];
  size_t i;

  /* Create the Star-3D scene */
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &walls) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &tall_block) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &short_block) == RES_OK);
  CHK(s3d_shape_get_id(walls, &geom_id[CBOX_WALLS]) == RES_OK);
  CHK(s3d_shape_get_id(tall_block, &geom_id[CBOX_TALL_BLOCK]) == RES_OK);
  CHK(s3d_shape_get_id(short_block, &geom_id[CBOX_SHORT_BLOCK]) == RES_OK);
  CHK(s3d_mesh_set_hit_filter_function(walls, cbox_filter, ptr) == RES_OK);
  CHK(s3d_mesh_set_hit_filter_function(tall_block, cbox_filter, ptr) == RES_OK);
  CHK(s3d_mesh_set_hit_filter_function(short_block, cbox_filter, ptr) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, walls) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, tall_block) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, short_block) == RES_OK);

  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = cbox_get_position;

  /* Setup the Cornell box walls */
  walls_desc.vertices = cbox_walls;
  walls_desc.indices = cbox_walls_ids;
  CHK(s3d_mesh_setup_indexed_vertices(walls, cbox_walls_ntris, cbox_get_ids,
    cbox_walls_nverts, &vdata, 1, &walls_desc) == RES_OK);

  /* Setup the Cornell box tall block  */
  tall_block_desc.vertices = cbox_tall_block;
  tall_block_desc.indices = cbox_block_ids;
  CHK(s3d_mesh_setup_indexed_vertices(tall_block, cbox_block_ntris, cbox_get_ids,
    cbox_block_nverts, &vdata, 1, &tall_block_desc) == RES_OK);

  /* Setup the Cornell box short block */
  short_block_desc.vertices = cbox_short_block;
  short_block_desc.indices = cbox_block_ids;
  CHK(s3d_mesh_setup_indexed_vertices(short_block, cbox_block_ntris, cbox_get_ids,
    cbox_block_nverts, &vdata, 1, &short_block_desc) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_get_aabb(scnview, low, upp) == RES_OK);
  mid[0] = (low[0] + upp[0]) * 0.5f;
  mid[1] = (low[1] + upp[1]) * 0.5f;
  mid[2] = (low[2] + upp[2]) * 0.5f;

  /* Filter nothing */
  filter_data.geom_to_filter[0] = S3D_INVALID_ID;
  filter_data.geom_to_filter[1] = S3D_INVALID_ID;
  filter_data.geom_to_filter[2] = S3D_INVALID_ID;

  /* Check a specific position that exhibits a precision issues of the
   * closest_point_triangle test routine */
  {
    union { float f; uint32_t ui; } ucast;
    pos[0] = (ucast.ui = 0xc386cc9a, ucast.f);
    pos[1] = (ucast.ui = 0x43e635b8, ucast.f);
    pos[2] = (ucast.ui = 0x4319ab78, ucast.f);
    f3_set(filter_data.query_pos, pos);
    filter_data.query_radius = (float)INF;
    CHK(s3d_scene_view_closest_point
      (scnview, pos, (float)INF, &filter_data, &hit) == RES_OK);
    check_closest_point_cbox(pos, geom_id, &hit);
  }

  /* Check closest point query on Cornell box */
  FOR_EACH(i, 0, 10000) {
    /* Randomly generate a point in a bounding box that is 2 times the size of
     * the cornell box AABB */
    pos[0] = mid[0] + (rand_canonic() * 2 - 1) * (upp[0] - low[0]);
    pos[1] = mid[1] + (rand_canonic() * 2 - 1) * (upp[1] - low[1]);
    pos[2] = mid[2] + (rand_canonic() * 2 - 1) * (upp[2] - low[2]);

    CHK(s3d_scene_view_closest_point(scnview, pos, (float)INF, NULL, &hit) == RES_OK);
    check_closest_point_cbox(pos, geom_id, &hit);
  }

  /* Filter the Cornell box blocks */
  filter_data.geom_to_filter[0] = geom_id[CBOX_TALL_BLOCK];
  filter_data.geom_to_filter[1] = geom_id[CBOX_SHORT_BLOCK];
  filter_data.geom_to_filter[2] = S3D_INVALID_ID;
  geom_id[CBOX_TALL_BLOCK] = S3D_INVALID_ID;
  geom_id[CBOX_SHORT_BLOCK] = S3D_INVALID_ID;

  /* Check closest point query filtering */
  FOR_EACH(i, 0, 10000) {
    /* Randomly generate a point in a bounding box that is 2 times the size of
     * the cornell box AABB */
    pos[0] = mid[0] + (rand_canonic() * 2 - 1) * (upp[0] - low[0]);
    pos[1] = mid[1] + (rand_canonic() * 2 - 1) * (upp[1] - low[1]);
    pos[2] = mid[2] + (rand_canonic() * 2 - 1) * (upp[2] - low[2]);

    f3_set(filter_data.query_pos, pos);
    filter_data.query_radius = (float)INF;

    CHK(s3d_scene_view_closest_point
      (scnview, pos, (float)INF, &filter_data, &hit) == RES_OK);

    check_closest_point_cbox(pos, geom_id, &hit);
  }

  /* Clean up */
  CHK(s3d_shape_ref_put(walls) == RES_OK);
  CHK(s3d_shape_ref_put(tall_block) == RES_OK);
  CHK(s3d_shape_ref_put(short_block) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
}

/*******************************************************************************
 * Single triangle test
 ******************************************************************************/
static void
triangle_get_ids(const unsigned itri, unsigned ids[3], void* ctx)
{
  (void)ctx;
  CHK(itri == 0);
  CHK(ids);
  ids[0] = 0;
  ids[1] = 1;
  ids[2] = 2;
}

static void
triangle_get_pos(const unsigned ivert, float pos[3], void* ctx)
{
  float* vertices = ctx;
  CHK(ctx);
  CHK(ivert < 3);
  CHK(pos);
  switch(ivert) { /* Setup a random triangle */
    case 0: f3_set(pos, vertices+0); break;
    case 1: f3_set(pos, vertices+3); break;
    case 2: f3_set(pos, vertices+6); break;
    default: FATAL("Unreachable code\n"); break;
  }
}

static void
test_single_triangle(struct s3d_device* dev)
{
  float vertices[9];
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  struct s3d_hit hit = S3D_HIT_NULL;
  struct s3d_scene* scn = NULL;
  struct s3d_scene_view* view = NULL;
  struct s3d_shape* msh = NULL;
  struct s3d_attrib attr;
  float v0[3], v1[3], v2[3];
  float pos[3] = {0,0,0};
  float closest_pos[3] = {0,0,0};
  float low[3], upp[3], mid[3];
  union { float f; uint32_t ui32; } ucast;
  size_t a, i;

  f3(vertices+0, -0.5f, -0.3f,   0.1f);
  f3(vertices+3, -0.4f,  0.2f,   0.3f);
  f3(vertices+6,  0.7f,  0.01f, -0.5f);

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &msh) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, msh) == RES_OK);

  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = triangle_get_pos;
  CHK(s3d_mesh_setup_indexed_vertices
    (msh, 1, triangle_get_ids, 3, &vdata, 1, vertices) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);

  triangle_get_pos(0, v0, vertices);
  triangle_get_pos(1, v1, vertices);
  triangle_get_pos(2, v2, vertices);

  /* Compute the triangle AABB */
  low[0] = MMIN(MMIN(v0[0], v1[0]), v2[0]);
  low[1] = MMIN(MMIN(v0[1], v1[1]), v2[1]);
  low[2] = MMIN(MMIN(v0[2], v1[2]), v2[2]);
  upp[0] = MMAX(MMAX(v0[0], v1[0]), v2[0]);
  upp[1] = MMAX(MMAX(v0[1], v1[1]), v2[1]);
  upp[2] = MMAX(MMAX(v0[2], v1[2]), v2[2]);
  mid[0] = (low[0] + upp[0]) * 0.5f;
  mid[1] = (low[1] + upp[1]) * 0.5f;
  mid[2] = (low[2] + upp[2]) * 0.5f;

  FOR_EACH(i, 0, 10000) {
    /* Randomly generate a point in a bounding box that is 10 times the size of
     * the triangle AABB */
    pos[0] = mid[0] + (rand_canonic() * 2 - 1) * (upp[0] - low[0]) * 5.f;
    pos[1] = mid[1] + (rand_canonic() * 2 - 1) * (upp[1] - low[1]) * 5.f;
    pos[2] = mid[2] + (rand_canonic() * 2 - 1) * (upp[2] - low[2]) * 5.f;

    CHK(s3d_scene_view_closest_point(view, pos, (float)INF, NULL, &hit) == RES_OK);
    CHK(!S3D_HIT_NONE(&hit));
    CHK(s3d_primitive_get_attrib(&hit.prim, S3D_POSITION, hit.uv, &attr) == RES_OK);

    /* Cross check the closest point query result */
    closest_point_triangle(pos, v0, v1, v2, closest_pos);
    CHK(f3_eq_eps(closest_pos, attr.value, 1.e-4f));
  }

  FOR_EACH(i, 0, 10000) {
    float radius;

    /* Randomly generate a point in a bounding box that is 10 times the size of
     * the triangle AABB */
    pos[0] = mid[0] + (rand_canonic() * 2 - 1) * (upp[0] - low[0]) * 5.f;
    pos[1] = mid[1] + (rand_canonic() * 2 - 1) * (upp[1] - low[1]) * 5.f;
    pos[2] = mid[2] + (rand_canonic() * 2 - 1) * (upp[2] - low[2]) * 5.f;

    CHK(s3d_scene_view_closest_point(view, pos, (float)INF, NULL, &hit) == RES_OK);
    CHK(!S3D_HIT_NONE(&hit));

    /* Check that the radius is an exclusive upper bound */
    radius = hit.distance;
    CHK(s3d_scene_view_closest_point(view, pos, radius, NULL, &hit) == RES_OK);
    CHK(S3D_HIT_NONE(&hit));
    radius = nextafterf(radius, FLT_MAX);
    CHK(s3d_scene_view_closest_point(view, pos, radius, NULL, &hit) == RES_OK);
    CHK(!S3D_HIT_NONE(&hit));
    CHK(hit.distance == nextafterf(radius, 0.f));
  }
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Setup a triangle and a query position that exhibited a precision issue on
   * the returned barycentric coordinate and check that this bug is now fixed */
  ucast.ui32 = 0x40400000; vertices[0] = ucast.f;
  ucast.ui32 = 0xc1200000; vertices[1] = ucast.f;
  ucast.ui32 = 0xbfc00000; vertices[2] = ucast.f;
  ucast.ui32 = 0x40400000; vertices[3] = ucast.f;
  ucast.ui32 = 0xc1200000; vertices[4] = ucast.f;
  ucast.ui32 = 0x3fc00000; vertices[5] = ucast.f;
  ucast.ui32 = 0x3f6d5337; vertices[6] = ucast.f;
  ucast.ui32 = 0xc0e4b2d5; vertices[7] = ucast.f;
  ucast.ui32 = 0xbfc00000; vertices[8] = ucast.f;
  f3(pos, 2, -10, 1);

  CHK(s3d_mesh_setup_indexed_vertices
    (msh, 1, triangle_get_ids, 3, &vdata, 1, vertices) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
  CHK(s3d_scene_view_closest_point(view, pos, (float)INF, NULL, &hit) == RES_OK);
  CHK(!S3D_HIT_NONE(&hit));
  CHK(0 <= hit.uv[0] && hit.uv[0] <= 1);
  CHK(0 <= hit.uv[1] && hit.uv[1] <= 1);
  CHK(hit.uv[0] + hit.uv[1] <= 1);

  CHK(s3d_shape_ref_put(msh) == RES_OK);
  CHK(s3d_scene_view_ref_put(view) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);

  /* Check accuracy on a configuration whose analytic distance is known */
  FOR_EACH(a, 0, 16) {
    const float amplitude = exp2f((float)a);
    const float eps = 5e-6f * amplitude;
    FOR_EACH(i, 0, 1000) {
      float A[3], B[3], C[3], AB[3], AC[3], BC[3], N[3], hit_N[3];
      int j, n;

      /* Randomly generate a triangle ABC */
      FOR_EACH(n, 0, 3)
        A[n] = (rand_canonic() - 0.5f) * amplitude;
      do {
        FOR_EACH(n, 0, 3) B[n] = (rand_canonic() - 0.5f) * amplitude;
      } while (f3_eq_eps(A, B, eps));
      do {
        FOR_EACH(n, 0, 3) C[n] = (rand_canonic() - 0.5f) * amplitude;
      } while (f3_eq_eps(A, C, eps) || f3_eq_eps(B, C, eps));

      f3_sub(AB, B, A);
      f3_sub(AC, C, A);
      f3_sub(BC, C, B);
      f3_cross(N, AC, AB); /* Left hand convention */
      f3_normalize(N, N);

      f3_set(vertices + 0, A);
      f3_set(vertices + 3, B);
      f3_set(vertices + 6, C);

      CHK(s3d_scene_create(dev, &scn) == RES_OK);
      CHK(s3d_shape_create_mesh(dev, &msh) == RES_OK);
      CHK(s3d_scene_attach_shape(scn, msh) == RES_OK);

      vdata.usage = S3D_POSITION;
      vdata.type = S3D_FLOAT3;
      vdata.get = triangle_get_pos;
      CHK(s3d_mesh_setup_indexed_vertices
        (msh, 1, triangle_get_ids, 3, &vdata, 1, vertices) == RES_OK);

      CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);

      FOR_EACH(j, 0, 1000) {
        float proj[3]; /* Projection of pos on the line */
        float AP[3], BP[3], CP[3], tmp[3];
        float closest[3] = {0,0,0};
        float u, v, w, h, x, dist, d;

        /* Randomly generate a pos not on the triangle
         * with know position wrt the problem: pos = A + u.AB + v.AC + k.N */
        u = 3 * rand_canonic() - 1;
        v = 3 * rand_canonic() - 1;
        w = 1 - u - v;
        h = (2 * rand_canonic() - 1) * amplitude;
        f3_add(proj, A, f3_add(proj, f3_mulf(proj, AB, u), f3_mulf(tmp, AC, v)));
        f3_add(pos, proj, f3_mulf(pos, N, h));
        f3_sub(AP, proj, A);
        f3_sub(BP, proj, B);
        f3_sub(CP, proj, C);

        /* Compute closest point */
        CHK(s3d_scene_view_closest_point(view, pos, (float)INF, NULL, &hit)
          == RES_OK);
        CHK(!S3D_HIT_NONE(&hit));
        CHK(s3d_primitive_get_attrib(&hit.prim, S3D_POSITION, hit.uv, &attr)
          == RES_OK);

        /* Check result
         * Due to known uv lack of accuracy we mainly check distance */
        if(u >= 0 && v >= 0 && w >= 0) {
          /* proj is inside the triangle and is the closest point */
          f3_set(closest, proj);
          dist = fabsf(h);
        } else {
          /* proj is outside the triangle */
          float lab2 = f3_dot(AB, AB);
          float lac2 = f3_dot(AC, AC);
          float lbc2 = f3_dot(BC, BC);
          if(w >= 0 && u < 0) {
            /* proj is closest to either AB or AC */
            x = f3_dot(AP, AB);
            if(v < 0 && x > 0) {
              /* proj is closest to AB */
              f3_add(closest, A, f3_mulf(tmp, AB, MMIN(1, x / lab2)));
            } else {
              /* proj is closest to AC */
              f3_add(closest, A,
                f3_mulf(tmp, AC, MMIN(1, MMAX(0, f3_dot(AP, AC) / lac2))));
            }
          }
          else if(u >= 0 && v < 0) {
            /* proj is closest to either BC or BA */
            x = f3_dot(BP, BC);
            if(w < 0 && x > 0) {
              /* proj is closest to BC */
              f3_add(closest, B, f3_mulf(tmp, BC, MMIN(1, x / lbc2)));
            } else {
              /* proj is closest to BA */
              f3_add(closest, B,
                f3_mulf(tmp, AB, -MMIN(1, MMAX(0, -f3_dot(BP, AB) / lab2))));
            }
          }
          else if(v >= 0 && w < 0) {
            /* proj is closest to either CA or CB */
            x = -f3_dot(CP, AC);
            if(u < 0 && x > 0) {
              /* proj is closest to CA */
              f3_add(closest, C, f3_mulf(tmp, AC, -MMIN(1, x / lac2)));
            } else {
              /* proj is closest to CB */
              f3_add(closest, C,
                f3_mulf(tmp, BC, -MMIN(1, MMAX(0, -f3_dot(CP, BC) / lbc2))));
            }
          }
          else { FATAL("Unreachable code\n"); }
          dist = f3_len(f3_sub(tmp, pos, closest));
        }
        CHK(eq_epsf(hit.distance, dist, eps));
        /* Intersection-point's position is less accurate than hit distance */
        d = f3_len(f3_sub(tmp, closest, attr.value));
        CHK(d <= 10 * eps);
        f3_normalize(hit_N, hit.normal);
        CHK(f3_eq_eps(N, hit_N, FLT_EPSILON));
      }

      CHK(s3d_shape_ref_put(msh) == RES_OK);
      CHK(s3d_scene_view_ref_put(view) == RES_OK);
      CHK(s3d_scene_ref_put(scn) == RES_OK);
    }
  }
}

static void
test_single_triangle_instantiated(struct s3d_device* dev)
{
  union { float f; uint32_t u32; } ucast;
  struct s3d_scene* scn = NULL;
  struct s3d_shape* shape = NULL;
  struct s3d_scene_view* view0 = NULL;
  struct s3d_scene_view* view1 = NULL;
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  struct s3d_hit hit0 = S3D_HIT_NULL;
  struct s3d_hit hit1 = S3D_HIT_NULL;
  float transform[12];
  float vertices[9];
  float transformed_vertices[9];
  float query_pos[3];

  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = triangle_get_pos;

  /* Setup the query position. The following data are retrieved from a user
   * case and are thus setuped as it, in its raw binary format */
  query_pos[0] = (ucast.u32 = 0xc1dc7a9e, ucast.f);
  query_pos[1] = (ucast.u32 = 0xc382179f, ucast.f);
  query_pos[2] = (ucast.u32 = 0xc32181b0, ucast.f);

  f3(vertices+0, -28.5f, -298.5f, 69.964429f);
  f3(vertices+3, -27.0f, -298.5f, 69.899651f);
  f3(vertices+6, -27.0f, -297.0f, 69.204593f);

  /* Setup the triangle transformation */
  f33_rotation(transform, (float)MDEG2RAD(45.0), 0, 0);
  f3_splat(transform+9, 0);

  /* Transform the triangle directly */
  f33_mulf3(transformed_vertices+0, transform, vertices+0);
  f33_mulf3(transformed_vertices+3, transform, vertices+3);
  f33_mulf3(transformed_vertices+6, transform, vertices+6);
  f3_add(transformed_vertices+0, transformed_vertices+0, transform+9);
  f3_add(transformed_vertices+1, transformed_vertices+1, transform+9);
  f3_add(transformed_vertices+2, transformed_vertices+2, transform+9);

  /* Setup the scene with the pre-transformed triangle */
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &shape) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, 1, triangle_get_ids, 3, &vdata, 1, transformed_vertices) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view0) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);

  /* Setup the same scene with the transformation performed by Star-3D through
   * instantiation */
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &shape) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, 1, triangle_get_ids, 3, &vdata, 1, vertices) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);
  CHK(s3d_scene_instantiate(scn, &shape) == RES_OK);
  CHK(s3d_instance_set_transform(shape, transform) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view1) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);

  /* Find the closest point */
  CHK(s3d_scene_view_closest_point
    (view0, query_pos, (float)INF, NULL, &hit0) == RES_OK);
  CHK(s3d_scene_view_closest_point
    (view1, query_pos, (float)INF, NULL, &hit1) == RES_OK);

  /* Check that the found hits are the same */
  CHK(!S3D_HIT_NONE(&hit0));
  CHK(!S3D_HIT_NONE(&hit1));
  CHK(eq_epsf(hit0.distance, hit1.distance, 1.e-6f));

  CHK(s3d_scene_view_ref_put(view0) == RES_OK);
  CHK(s3d_scene_view_ref_put(view1) == RES_OK);
}

/*******************************************************************************
 * Miscellaneous test
 ******************************************************************************/
static void
test_api(struct s3d_device* dev)
{
  struct s3d_hit hit = S3D_HIT_NULL;
  struct s3d_scene* scn = NULL;
  struct s3d_scene_view* view = NULL;
  float pos[3] = {0,0,0};

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);

  CHK(s3d_scene_view_closest_point(NULL, pos, 1.f, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_closest_point(view, NULL, 1.f, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_closest_point(view, pos, 0.f, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_closest_point(view, pos, 1.f, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_closest_point(view, pos, 1.f, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit));

  CHK(s3d_scene_view_ref_put(view) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &view) == RES_OK);
  CHK(s3d_scene_view_closest_point(view, pos, 1.f, NULL, &hit) == RES_BAD_OP);

  CHK(s3d_scene_view_ref_put(view) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
}

/*******************************************************************************
 * Main function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3d_device* dev = NULL;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(s3d_device_create(NULL, &allocator, 1, &dev) == RES_OK);

  test_api(dev);
  //test_single_triangle(dev);
  test_single_triangle_instantiated(dev);
  test_cbox(dev);
  test_sphere(dev);
  test_cbox_sphere(dev);

  CHK(s3d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
