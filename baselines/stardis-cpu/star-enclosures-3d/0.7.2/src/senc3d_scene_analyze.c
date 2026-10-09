/* Copyright (C) 2018-2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#define _POSIX_C_SOURCE 200112L

#include "senc3d.h"
#include "senc3d_device_c.h"
#include "senc3d_scene_c.h"
#include "senc3d_scene_analyze_c.h"
#include "senc3d_internal_types.h"

#include <rsys/rsys.h>
#include <rsys/float3.h>
#include <rsys/double33.h>
#include <rsys/mem_allocator.h>
#include <rsys/hash_table.h>
#include <rsys/dynamic_array.h>
#include <rsys/dynamic_array_uint.h>
#include <rsys/dynamic_array_uchar.h>
#include <rsys/clock_time.h>
#include <rsys/str.h>

#include <star/s3d.h>

#include <omp.h>
#include <rsys_math.h>
#include <limits.h>
#include <stdlib.h>

#define CC_DESCRIPTOR_NULL__ {\
   0, 0, {{DBL_MAX,-DBL_MAX}, {DBL_MAX,-DBL_MAX}, {DBL_MAX,-DBL_MAX}}, NULL,\
   0, INT_MAX, VRTX_NULL__, 0,\
   CC_ID_NONE, CC_GROUP_ROOT_NONE, ENCLOSURE_NULL__,\
   { TRG_NULL__, 0}\
}
#ifdef COMPILER_GCC
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
const struct cc_descriptor CC_DESCRIPTOR_NULL = CC_DESCRIPTOR_NULL__;
#ifdef COMPILER_GCC
  #pragma GCC diagnostic pop
#endif

#define DARRAY_NAME component_id
#define DARRAY_DATA component_id_t
#include <rsys/dynamic_array.h>

#define HTABLE_NAME component_id
#define HTABLE_KEY component_id_t
#define HTABLE_DATA char
#include <rsys/hash_table.h>

/* A threshold for dot products:
 * If ray.normal < threshold we suspect accuracy could be a problem */
#define DOT_THRESHOLD 0.0001f

enum ctx_type {
  CTX0,
  CTX1,
  CTX2
};

struct filter_ctx {
  enum ctx_type type;
  struct senc3d_scene* scn;
  struct s3d_scene_view* view;
  component_id_t origin_component;
  struct darray_triangle_comp* triangles_comp;
  struct darray_ptr_component_descriptor* components;
  /* Tmp data used across filter calls */
  double current_6volume;
  int cpt;
  float s;
  /* Result of CTX1 hit */
  component_id_t hit_component;
  float hit_dir[3], hit_dist;
  struct s3d_primitive hit_prim;
};

#define HTABLE_NAME overlap
#define HTABLE_KEY trg_id_t
#define HTABLE_DATA char
#include <rsys/hash_table.h>

/******************************************************************************
 * Helper function
 *****************************************************************************/
static INLINE int
neighbour_cmp(const void* w1, const void* w2)
{
  const double a1 = ((struct neighbour_info*)w1)->angle;
  const double a2 = ((struct neighbour_info*)w2)->angle;
  return (a1 > a2) - (a1 < a2);
}

/* Returns 1 if cc2 is inside cc1, 0 otherwise */
static FINLINE int
is_component_inside
  (struct cc_descriptor* cc1,
   struct cc_descriptor* cc2,
   struct filter_ctx* ctx)
{
  int i;
  side_id_t side;
  const struct triangle_in* trg = NULL;
  double pt[3] = { 0, 0, 0 };
  float org[3], dir[3] = { 0, 0, 1 }, rg[2] = { 0, FLT_MAX };
  struct filter_ctx ctx2;
  struct s3d_hit hit = S3D_HIT_NULL;
  const struct triangle_comp*
    trg_comp = darray_triangle_comp_cdata_get(ctx->triangles_comp);
  const struct triangle_in*
    triangles = darray_triangle_in_cdata_get(&ctx->scn->triangles_in);
  const union double3* vertices = darray_position_cdata_get(&ctx->scn->vertices);
  ASSERT(cc1 && cc2 && ctx);
  /* Volume must be compatible */
  if(fabs(cc2->_6volume) >= fabs(cc1->_6volume))
    return 0;
  /* Bbox must be compatible */
  for(i = 0; i < 3; i++) {
    if(cc2->bbox[i][0] < cc1->bbox[i][0] || cc2->bbox[i][1] > cc1->bbox[i][1])
      return 0;
  }
  /* Check if component cc2 is inside component cc1.
   * We already know that bbox and volume allow cc2 to fit inside component
   * ccc1, but it is not enough.
   * The method is to cast a ray from cc2 and count the number of times it
   * crosses component cc1; as components must not overlap, testing from a
   * single point is OK, as long as the point is not on cc1 boundary (it is on
   * cc2 boundary, though). */
  for(side = cc2->side_range.first; side <= cc2->side_range.last; side++) {
    /* Find a triangle on cc2 boundary that is not on cc1 boundary (it exists,
     * as the 2 components cannot share all their triangles) */
    trg_id_t t = TRGSIDE_2_TRG(side);
    const component_id_t* candidate_comp = trg_comp[t].component;
    if(candidate_comp[SENC3D_FRONT] != cc2->cc_id
        && candidate_comp[SENC3D_BACK] != cc2->cc_id)
      continue;
    if(candidate_comp[SENC3D_FRONT] == cc1->cc_id
        || candidate_comp[SENC3D_BACK] == cc1->cc_id)
      continue;
    /* This triangle is OK */
    trg = triangles + t;
    break;
  }
  ASSERT(trg != NULL);
  /* Any point on trg can do the trick: use the barycenter */
  FOR_EACH(i, 0, 3) {
    vrtx_id_t v = trg->vertice_id[i];
    ASSERT(v < darray_position_size_get(&ctx->scn->vertices));
    d3_add(pt, pt, vertices[v].vec);
  }
  d3_divd(pt, pt, 3);
  f3_set_d3(org, pt);
  /* Trace a ray and count intersections with component c */
  ctx2.type = CTX2;
  ctx2.triangles_comp = ctx->triangles_comp;
  ctx2.cpt = 0;
  ctx2.origin_component = cc1->cc_id;
  S3D(scene_view_trace_ray(ctx->view, org, dir, rg, &ctx2, &hit));
  /* cc2 is not inside cc1 if cpt is even */
  if(ctx2.cpt % 2 == 0) return 0;
  return 1;
}

static side_id_t
get_side_not_in_connex_component
  (const side_id_t last_side,
   const struct trgside* trgsides,
   const uchar* processed,
   side_id_t* first_side_not_in_component,
   const medium_id_t medium)
{
  ASSERT(trgsides && processed && first_side_not_in_component);
  {
    side_id_t i = *first_side_not_in_component;
    while(i <= last_side
      && (trgsides[i].medium != medium
        || (processed[TRGSIDE_2_TRG(i)] & TRGSIDE_2_SIDEFLAG(i))))
      ++i;

    *first_side_not_in_component = i + 1;
    if(i > last_side) return SIDE_NULL__;
    return i;
  }
}

/* Here unsigned are required by s3d API */
static void
get_scn_indices
  (const unsigned itri,
   unsigned ids[3],
   void* ctx)
{
  int i;
  const struct senc3d_scene* scene = ctx;
  const struct triangle_in* trg =
    darray_triangle_in_cdata_get(&scene->triangles_in) + itri;
  FOR_EACH(i, 0, 3) {
    ASSERT(trg->vertice_id[i] < scene->nverts);
    ASSERT(trg->vertice_id[i] <= UINT_MAX);
    ids[i] = (unsigned)trg->vertice_id[i]; /* Back to s3d API type */
  }
}

/* Here unsigned are required by s3d API */
static void
get_scn_position
  (const unsigned ivert,
   float pos[3],
   void* ctx)
{
  const struct senc3d_scene* scene = ctx;
  const union double3* pt =
    darray_position_cdata_get(&scene->vertices) + ivert;
  f3_set_d3(pos, pt->vec);
}

static int
self_hit_filter
  (const struct s3d_hit* hit,
   const float ray_org[3],
   const float ray_dir[3],
   const float ray_range[2],
   void* ray_data,
   void* filter_data)
{
  struct filter_ctx* ctx = ray_data;

  (void)ray_org; (void)ray_range; (void)filter_data;
  ASSERT(ctx);
  ASSERT(hit->uv[0] == CLAMP(hit->uv[0], 0, 1));
  ASSERT(hit->uv[1] == CLAMP(hit->uv[1], 0, 1));

  switch (ctx->type) {
    default: FATAL("Invalid");

    case CTX2: {
      /* The filter is used to count the hits on some component along an
       * infinite ray */
      const struct triangle_comp* trg_comp;
      const component_id_t* hit_comp;
      ASSERT(hit->prim.prim_id
        < darray_triangle_comp_size_get(ctx->triangles_comp));
      trg_comp = darray_triangle_comp_cdata_get(ctx->triangles_comp);
      hit_comp = trg_comp[hit->prim.prim_id].component;
      if(hit_comp[SENC3D_FRONT] == ctx->origin_component
        || hit_comp[SENC3D_BACK] == ctx->origin_component)
      {
        ctx->cpt++;
      }
      return 1; /* Reject to continue counting */
    }

    case CTX0: {
      /* This filter is called from a closest point query from a point belonging
       * to origin_component. The returned hit is used to determine the search
       * radius for CTX1 main computation. */
      const struct triangle_comp*
        trg_comp = darray_triangle_comp_cdata_get(ctx->triangles_comp);
      const component_id_t*
        hit_comp = trg_comp[hit->prim.prim_id].component;
      const component_id_t oc = ctx->origin_component;
      vrtx_id_t other_id;
      struct cc_descriptor* const* comp_descriptors
        = darray_ptr_component_descriptor_cdata_get(ctx->components);
      size_t compsz = darray_ptr_component_descriptor_size_get(ctx->components);
      const union double3*
        vertices = darray_position_cdata_get(&ctx->scn->vertices);
      const double org_z = vertices[comp_descriptors[oc]->max_z_vrtx_id].pos.z;
      float s = 0, hit_normal[3], rdir[3];
      enum senc3d_side hit_side;
      const int log_components =
        ctx->scn->convention & SENC3D_LOG_COMPONENTS_INFORMATION;

      ASSERT(hit->prim.prim_id
        < darray_triangle_comp_size_get(ctx->triangles_comp));
      ASSERT(hit_comp[SENC3D_FRONT] < compsz);
      ASSERT(hit_comp[SENC3D_BACK] < compsz);
      (void)compsz; /* Avoid "unused variable" warning */

      /* Hit acceptance must be coherent at CTX0 and FTCX1 stages:
       * the search radius as found at stage CTX0 must include the hit that
       * stage CTX1 will select using an infinite radius */
      if(hit_comp[SENC3D_FRONT] == oc || hit_comp[SENC3D_BACK] == oc) {
        return 1; /* Self hit, reject */
      }
      if(hit->distance == 0) {
        /* origin component is in contact with some other components
         * We will need further exploration to know if they should be considered
         * Accepting hit at distance 0 => radius is definitively 0 */
        int n;

        /* If same component, process only once */
        FOR_EACH(n, 0, (hit_comp[SENC3D_FRONT] == hit_comp[SENC3D_BACK] ? 1 : 2)) {
          const enum senc3d_side sides[2] = { SENC3D_FRONT, SENC3D_BACK };
          component_id_t c = hit_comp[sides[n]];
          ASSERT(c < darray_ptr_component_descriptor_size_get(ctx->components));
          if(comp_descriptors[c]->is_outer_border) {
            /* The inner component we are trying to link can only be linked to
             * an outer component if it is inside */
            if(!is_component_inside(comp_descriptors[c],
                  comp_descriptors[oc], ctx))
            {
              continue;
            }

            if(log_components) {
              #pragma omp critical
              printf("Component #%u: decreasing search radius "
                  "(R=%g, n=%g,%g,%g, components: %u, %u)\n",
                  oc, hit->distance, SPLIT3(hit->normal),
                  hit_comp[SENC3D_FRONT], hit_comp[SENC3D_BACK]);
            }
            return 0;
          } else {
            /* c is an inner component */
            vrtx_id_t c_z_id;
            /* The inner component we are trying to link can only be linked to
             * another inner component if (at least partly) above and not inside */
            c_z_id = comp_descriptors[c]->max_z_vrtx_id;
            ASSERT(c_z_id < darray_position_size_get(&ctx->scn->vertices));
            ASSERT(vertices[c_z_id].pos.z >= org_z);
            if(vertices[c_z_id].pos.z == org_z) {
              continue; /* Not above */
            }
            if(is_component_inside(comp_descriptors[c],
                  comp_descriptors[oc], ctx))
            {
              continue; /* Inside */
            }
            if(log_components) {
              #pragma omp critical
              printf("Component #%u: decreasing search radius "
                  "(R=%g, n=%g,%g,%g, components: %u, %u)\n",
                  oc, hit->distance, SPLIT3(hit->normal),
                  hit_comp[SENC3D_FRONT], hit_comp[SENC3D_BACK]);
            }
            return 0;
          }
        }
        return 1;
      }

      ASSERT(hit->distance > 0);
      if(hit_comp[SENC3D_FRONT] == hit_comp[SENC3D_BACK]) {
        /* Hit component is known, check if above */
        other_id = comp_descriptors[hit_comp[SENC3D_FRONT]]->max_z_vrtx_id;
        ASSERT(other_id < darray_position_size_get(&ctx->scn->vertices));
        if(vertices[other_id].pos.z <= org_z) {
          return 1;
        }
        if(log_components) {
          #pragma omp critical
          printf("Component #%u: decreasing search radius "
              "(R=%g, n=%g,%g,%g, components: %u, %u)\n",
              oc, hit->distance, SPLIT3(hit->normal),
              hit_comp[SENC3D_FRONT], hit_comp[SENC3D_BACK]);
        }
        return 0;
      }

      /* Compute hit side */
      /* For s to be comparable, vectors must be normalized */
      f3_normalize(hit_normal, hit->normal);
      f3_normalize(rdir, ray_dir);
      s = f3_dot(rdir, hit_normal); /* Can be NaN for tiny distances */
      if(isnan(s)) {
        /* Try to fix it */
        f3_divf(rdir, ray_dir, hit->distance);
        f3_normalize(rdir, rdir);
        s = f3_dot(rdir, hit_normal);
        ASSERT(!isnan(s));
      }

      if(fabsf(s) < DOT_THRESHOLD) {
        /* We cannot know for sure which side to consider */
        vrtx_id_t i1 = comp_descriptors[hit_comp[SENC3D_FRONT]]->max_z_vrtx_id;
        vrtx_id_t i2 = comp_descriptors[hit_comp[SENC3D_BACK]]->max_z_vrtx_id;
        double possible_z = MMIN(vertices[i1].pos.z, vertices[i2].pos.z);
        if(possible_z > org_z) {
          /* Both components are above origin component => keep */
          if(log_components) {
            #pragma omp critical
            printf("Component #%u: decreasing search radius "
                "(R=%g, n=%g,%g,%g, components: %u, %u)\n",
                oc, hit->distance, SPLIT3(hit->normal),
                hit_comp[SENC3D_FRONT], hit_comp[SENC3D_BACK]);
          }
          return 0;
        }
        /* Cannot be sure => the safest choice is to reject */
        return 1;
      }
      /* Determine which side was hit */
      hit_side =
        ((s < 0) /* Facing geometrical normal of hit */
         == ((ctx->scn->convention & SENC3D_CONVENTION_NORMAL_FRONT) != 0))
        /* Warning: following Embree 2 convention for geometrical normals,
         * the Star3D hit normal is left-handed while star-enclosures-3d uses
         * right-handed convention */
        ? SENC3D_BACK : SENC3D_FRONT;
      other_id = comp_descriptors[hit_comp[hit_side]]->max_z_vrtx_id;
      ASSERT(other_id < darray_position_size_get(&ctx->scn->vertices));
      if(vertices[other_id].pos.z <= org_z) {
        return 1;
      }
      if(log_components) {
        #pragma omp critical
        printf("Component #%u: decreasing search radius "
            "(R=%g, n=%g,%g,%g, components: %u, %u)\n",
            oc, hit->distance, SPLIT3(hit->normal),
            hit_comp[SENC3D_FRONT], hit_comp[SENC3D_BACK]);
      }
      return 0;
    }

    case CTX1: {
      /* This filter is called from a closest point query from a point belonging
       * to origin_component. The returned hit is used to determine a component
       * to which origin_component is linked. At a later stage the algorithm
       * process linked components to determine their relative inclusions.
       *
       * This filter is called with a search distance that has been ajusted in
       * CTX0 filter. This distance must be left unchanged to ensure visiting
       * all the surfaces at the determined distance: allways reject hits to
       * avoid decreasing search distance.
       *
       * For each hit, the filter computes if the hit is on a component above
       * origin_component (that is with >= Z).
       * If the hit is distant (dist>0), we just keep the hit as a valid candidate,
       * but things get more tricky when dist==0 (ray_org is a vertex where some
       * other components can be in contact with origin_component).
       * In this case, one of the other components can include the origin_component
       * (greater volume needed), or they can be disjoint, with (at least) ray_org
       * as a common vertex (they can also partially intersect, but this is invalid
       * and remains undetected by star enclosures). */
      struct cc_descriptor* const* comp_descriptors
        = darray_ptr_component_descriptor_cdata_get(ctx->components);
      const struct triangle_comp*
        trg_comp = darray_triangle_comp_cdata_get(ctx->triangles_comp);
      const component_id_t*
        hit_comp = trg_comp[hit->prim.prim_id].component;
      const union double3*
        vertices = darray_position_cdata_get(&ctx->scn->vertices);
      enum senc3d_side hit_side;
      float s = 0, hit_normal[3], rdir[3];
      const int log_components =
        ctx->scn->convention & SENC3D_LOG_COMPONENTS_INFORMATION;
      const component_id_t oc = ctx->origin_component;
      /* Links must be upward to avoid creating loops
       * Instead of comparing hit.z VS origin.z, compare max_Z of components
       * Here we keep max_z of the origin component that will be used for these
       * comparisons */
      const double org_z = vertices[comp_descriptors[oc]->max_z_vrtx_id].pos.z;
      vrtx_id_t other_id;

      ASSERT(hit->prim.prim_id
        < darray_triangle_comp_size_get(ctx->triangles_comp));

      if(log_components) {
        #pragma omp critical
        printf("Component #%u: investigating hit "
            "(d=%g, n=%g,%g,%g, components: %u, %u)\n",
            oc, hit->distance, SPLIT3(hit->normal),
            hit_comp[SENC3D_FRONT], hit_comp[SENC3D_BACK]);
      }

      /* Has CTX1 filter do not reduce search radius, hits can be processed
       * that are at farther distance than the current best candidate: we need
       * to reject them here */
      if(hit->distance > ctx->hit_dist) {
        /* No improvement */
        if(log_components) {
          #pragma omp critical
          printf("Component #%u: further away (%g): reject\n",
              oc, ctx->hit_dist);
        }
        return 1;
      }

      /* Hit acceptance must be coherent at CTX0 and FTCX1 stages:
       * the search radius as found at stage CTX0 must include the hit that
       * stage CTX1 will select using an infinite radius */
      if(hit_comp[SENC3D_FRONT] == oc || hit_comp[SENC3D_BACK] == oc) {
        /* Self hit */
        if(log_components) {
          #pragma omp critical
          printf("Component #%u: self hit: reject\n", oc);
        }
        return 1;
      }

      if(hit->distance == 0) {
        /* origin_component is in contact with some other components
         * We will need further exploration to know if they should be considered */
        int n;

        /* If same component, process only once */
        FOR_EACH(n, 0, (hit_comp[SENC3D_FRONT] == hit_comp[SENC3D_BACK] ? 1 : 2)) {
          const enum senc3d_side sides[2] = { SENC3D_FRONT, SENC3D_BACK };
          component_id_t c = hit_comp[sides[n]];
          ASSERT(c < darray_ptr_component_descriptor_size_get(ctx->components));
          if(c == ctx->hit_component) {
            /* Cannot change ctx->hit_component */
            if(log_components) {
              #pragma omp critical
              printf("Component #%u: hit component #%u and already linked to it:"
                 " reject\n",
                 oc, c);
            }
            continue;
          }
          if(comp_descriptors[c]->is_outer_border) {
            double v;
            /* The inner component we are trying to link can only be linked to
             * an outer component if it is inside */
            if(log_components) {
              #pragma omp critical
              printf("Component #%u: hit outer component #%u\n", oc, c);
            }
            if(!is_component_inside(comp_descriptors[c],
                  comp_descriptors[oc], ctx))
            {
              if(log_components) {
                #pragma omp critical
                printf("Component #%u: not inside: reject\n", oc);
              }
              continue;
            }
            v = fabs(comp_descriptors[c]->_6volume);
            /* If already linked to an inner component, prefer an outer one
             * regardless of their respective volumes.
             * If origin_component is inside several outer components, the one
             * we are looking for is the smallest one (to manage outer component
             * inside another outer component). */
            if((ctx->hit_component != COMPONENT_NULL__
                  && !comp_descriptors[ctx->hit_component]->is_outer_border)
                || v < ctx->current_6volume) {
              ctx->hit_component = c;
              ctx->current_6volume = v;
              ctx->hit_dist = 0;
              ctx->hit_prim = hit->prim;
              if(log_components) {
                if(v < ctx->current_6volume) {
                  #pragma omp critical
                  printf("Component #%u: currently the smaller one: "
                      "keep component #%u\n",
                      oc, ctx->hit_component);
                } else {
                  #pragma omp critical
                  printf("Component #%u: change from inner to outer: "
                      "keep component #%u\n",
                      oc, ctx->hit_component);
                }
              }
            } else {
              if(log_components) {
                #pragma omp critical
                printf("Component #%u: not the smaller one: reject\n", oc);
              }
              continue;
            }
          } else {
            /* c is an inner component */
            vrtx_id_t c_z_id;
            double v;
            /* If we've already found a valid outer component, inner components
             * should not be considered anymore */
            if(log_components) {
              #pragma omp critical
              printf("Component #%u: hit inner component #%u\n", oc, c);
            }
            if(ctx->hit_component != COMPONENT_NULL__
                && comp_descriptors[ctx->hit_component]->is_outer_border)
            {
              if(log_components) {
                #pragma omp critical
                printf("Component #%u: already in an outer component: reject\n",
                    oc);
              }
              continue;
            }
            /* The inner component we are trying to link can only be linked to
             * another inner component if (at least partly) above it and not
             * inside */
            c_z_id = comp_descriptors[c]->max_z_vrtx_id;
            ASSERT(c_z_id < darray_position_size_get(&ctx->scn->vertices));
            ASSERT(vertices[c_z_id].pos.z >= org_z);
            if(vertices[c_z_id].pos.z == org_z) {
              if(log_components) {
                #pragma omp critical
                printf("Component #%u: not (even in part) above: reject\n", oc);
              }
              continue; /* Not above */
            }
            if(is_component_inside(comp_descriptors[c],
                  comp_descriptors[oc], ctx))
            {
              if(log_components) {
                #pragma omp critical
                printf("Component #%u: not outside: reject\n", oc);
              }
              continue; /* Inside */
            }
            v = fabs(comp_descriptors[c]->_6volume);
            /* If origin_component is facing several inner components, the one
             * we are looking for is the largest one (to manage inner component
             * inside another inner component) */
            if(ctx->current_6volume == DBL_MAX || v > ctx->current_6volume) {
              ctx->hit_component = c;
              ctx->current_6volume = v;
              ctx->hit_dist = 0;
              ctx->hit_prim = hit->prim;
              if(log_components) {
                #pragma omp critical
                printf("Component #%u: currently the bigger one: "
                    "keep component #%u\n",
                    oc, ctx->hit_component);
              }
            } else {
              if(log_components) {
                #pragma omp critical
                printf("Component #%u: not the bigger one: reject\n", oc);
              }
              continue;
            }
          }
        }
        return 1;
      }

      ASSERT(hit->distance > 0);
      if(hit_comp[SENC3D_FRONT] == hit_comp[SENC3D_BACK]) {
        /* Easy case and hit component is known */
        other_id = comp_descriptors[hit_comp[SENC3D_FRONT]]->max_z_vrtx_id;
        ASSERT(other_id < darray_position_size_get(&ctx->scn->vertices));
        if(vertices[other_id].pos.z <= org_z) {
          if(log_components) {
            #pragma omp critical
            printf("Component #%u: 2 sides, not above: reject\n", oc);
          }
          return 1;
        }
        ctx->hit_component = hit_comp[SENC3D_FRONT];
        ctx->s = 1;
        f3_set(ctx->hit_dir, ray_dir);
        ctx->hit_dist = hit->distance;
        ctx->hit_prim = hit->prim;
        if(log_components) {
          #pragma omp critical
          printf("Component #%u: 2 sides with same component: "
              "keep component #%u\n",
              oc, ctx->hit_component);
        }
        return 1;
      }

      /* Compute hit side */
      /* For s to be comparable, vectors must be normalized */
      f3_normalize(hit_normal, hit->normal);
      f3_normalize(rdir, ray_dir);
      s = f3_dot(rdir, hit_normal); /* Can be NaN for tiny distances */
      if(isnan(s)) {
        /* Try to fix it */
        f3_divf(rdir, ray_dir, hit->distance);
        f3_normalize(rdir, rdir);
        s = f3_dot(rdir, hit_normal);
        ASSERT(!isnan(s));
        if(log_components) {
          #pragma omp critical
          printf("Component #%u: had to fix s (was NaN)\n", oc);
        }
      }

      if(ctx->hit_dist == hit->distance && fabsf(ctx->s) >= fabsf(s)) {
        /* Same distance with no s improvement: keep the previous hit */
        if(log_components) {
          #pragma omp critical
          printf("Component #%u: not improving s (%g VS %g): reject\n",
              oc, s, ctx->s);
        }
        return 1;
      }

      if(fabsf(s) < DOT_THRESHOLD) {
        /* We cannot know for sure which side to consider */
        vrtx_id_t i1 = comp_descriptors[hit_comp[SENC3D_FRONT]]->max_z_vrtx_id;
        vrtx_id_t i2 = comp_descriptors[hit_comp[SENC3D_BACK]]->max_z_vrtx_id;
        double possible_z = MMAX(vertices[i1].pos.z, vertices[i2].pos.z);
        if(possible_z <= org_z) {
          /* None of the components are above origin component => reject */
          if(log_components) {
            #pragma omp critical
            printf("Component #%u: none of the components above: reject\n",
                oc);
          }
          return 1;
        }
        ctx->hit_component = COMPONENT_NULL__;
        ctx->s = s;
        f3_set(ctx->hit_dir, ray_dir);
        ctx->hit_dist = hit->distance;
        ctx->hit_prim = hit->prim;
        if(log_components) {
          #pragma omp critical
          printf("Component #%u: tiny s (%g): "
              "keep but don't know the component\n",
              oc, s);
        }
        return 1;
      }
      /* Determine which side was hit */
      hit_side =
        ((s < 0) /* Facing geometrical normal of hit */
         == ((ctx->scn->convention & SENC3D_CONVENTION_NORMAL_FRONT) != 0))
        /* Warning: following Embree 2 convention for geometrical normals,
         * the Star3D hit normal is left-handed while star-enclosures-3d uses
         * right-handed convention */
        ? SENC3D_BACK : SENC3D_FRONT;
      other_id = comp_descriptors[hit_comp[hit_side]]->max_z_vrtx_id;
      ASSERT(other_id < darray_position_size_get(&ctx->scn->vertices));
      if(vertices[other_id].pos.z <= org_z) {
        if(log_components) {
          #pragma omp critical
          printf("Component #%u: standard s, not (even in part) above: reject\n",
              oc);
        }
        return 1;
      }
      ctx->hit_component = hit_comp[hit_side];
      ctx->s = s;
      f3_set(ctx->hit_dir, ray_dir);
      ctx->hit_dist = hit->distance;
      ctx->hit_prim = hit->prim;
      if(log_components) {
        #pragma omp critical
        printf("Component #%u: standard s, (%g): keep component #%u\n",
            oc, s, ctx->hit_component);
      }
      return 1;
    }
  }
}

static void
extract_connex_components
  (struct senc3d_scene* scn,
   struct trgside* trgsides,
   struct darray_ptr_component_descriptor* connex_components,
   const struct darray_triangle_tmp* triangles_tmp_array,
   struct darray_triangle_comp* triangles_comp_array,
   struct s3d_scene_view** s3d_view,
   ATOMIC* component_count,
   /* Shared error status.
    * We accept to overwrite an error with a different error */
   res_T* p_res)
{
  /* This function is called from an omp parallel block and executed
   * concurrently. */
  struct mem_allocator* alloc;
  int64_t m_idx; /* OpenMP requires a signed type for the for loop variable */
  struct darray_side_id stack;
  const union double3* positions;
  const struct triangle_tmp* triangles_tmp;
  struct triangle_comp* triangles_comp;
  /* An array to flag sides when processed */
  uchar* processed;
  /* An array to store the component being processed */
  struct darray_side_id current_component;
  /* A bool array to store media of the component being processed */
  uchar* current_media = NULL;
  size_t sz, ii;

  ASSERT(scn && trgsides && connex_components && triangles_tmp_array
    && triangles_comp_array && s3d_view && component_count && p_res);
  alloc = scn->dev->allocator;
  positions = darray_position_cdata_get(&scn->vertices);
  triangles_tmp = darray_triangle_tmp_cdata_get(triangles_tmp_array);
  triangles_comp = darray_triangle_comp_data_get(triangles_comp_array);
  darray_side_id_init(alloc, &stack);
  darray_side_id_init(alloc, &current_component);
  processed = MEM_CALLOC(alloc, scn->ntris, sizeof(uchar));
  if(!processed) {
    *p_res = RES_MEM_ERR;
    return;
  }

#ifndef NDEBUG
  #pragma omp single
  {
    trg_id_t t_;
    int s;
    ASSERT(darray_ptr_component_descriptor_size_get(connex_components) == 0);
    FOR_EACH(t_, 0, scn->ntris) {
      const struct triangle_in* trg_in =
        darray_triangle_in_cdata_get(&scn->triangles_in) + t_;
      const struct side_range* media_use
        = darray_side_range_cdata_get(&scn->media_use);
      FOR_EACH(s, 0, 2) {
        const side_id_t side = TRGIDxSIDE_2_TRGSIDE(t_, (enum senc3d_side)s);
        medium_id_t medium = trg_in->medium[s];
        m_idx = medium_id_2_medium_idx(medium);
        ASSERT(media_use[m_idx].first <= side && side
          <= media_use[m_idx].last);
      }
    }
  } /* Implicit barrier here */
#endif

  /* We loop on sides to build connex components. */
  ASSERT(darray_side_range_size_get(&scn->media_use) <= INT64_MAX);
  #pragma omp for schedule(dynamic) nowait
  /* Process all media, including unspecified */
  for(m_idx = 0; m_idx < (int64_t)darray_side_range_size_get(&scn->media_use);
    m_idx++)
  {
    const medium_id_t medium = medium_idx_2_medium_id(m_idx);
    /* media_use 0 is for SENC3D_UNSPECIFIED_MEDIUM, n+1 is for n */
    const struct side_range* media_use =
      darray_side_range_cdata_get(&scn->media_use) + m_idx;
    /* Any not-already-used side is used as a starting point */
    side_id_t first_side_not_in_component = media_use->first;
    double max_nz;
    side_id_t max_nz_side_id;
    const side_id_t last_side = media_use->last;
    int component_canceled = 0, max_z_is_2sided = 0, fst_nz = 1;
    res_T tmp_res = RES_OK;
    ATOMIC id;

    if(*p_res != RES_OK) continue;
    if(first_side_not_in_component == SIDE_NULL__)
      continue; /* Unused medium */
    ASSERT(first_side_not_in_component < 2 * scn->ntris);
    ASSERT(darray_side_id_size_get(&stack) == 0);
    ASSERT(darray_side_id_size_get(&current_component) == 0);
    for(;;) {
      /* Process all components for this medium
       * Here we start from a side of the currently processed medium that is
       * not member of any component yet; by exploring neighbourhood the
       * process can harvest sides with different media */
      side_id_t crt_side_id = get_side_not_in_connex_component
        (last_side, trgsides, processed, &first_side_not_in_component, medium);
      side_id_t cc_start_side_id = crt_side_id;
      side_id_t cc_last_side_id = crt_side_id;
      vrtx_id_t max_z_vrtx_id = VRTX_NULL__;
      struct cc_descriptor *cc;
      unsigned media_count;
      double max_z = -DBL_MAX;
      component_canceled = 0;
      ASSERT(crt_side_id == SIDE_NULL__ || crt_side_id < 2 * scn->ntris);
      darray_side_id_clear(&current_component);

      if(*p_res != RES_OK) break;
      if(crt_side_id == SIDE_NULL__)
        break; /* start_side_id=SIDE_NULL__ => component done! */

#ifndef NDEBUG
      {
        trg_id_t tid = TRGSIDE_2_TRG(crt_side_id);
        enum senc3d_side s = TRGSIDE_2_SIDE(crt_side_id);
        medium_id_t side_med
          = darray_triangle_in_data_get(&scn->triangles_in)[tid].medium[s];
        ASSERT(side_med == medium);
      }
#endif

      /* Reuse array if possible, or create a new one  */
      if(current_media) {
        /* current_media 0 is for SENC3D_UNSPECIFIED_MEDIUM, n+1 is for n */
        memset(current_media, 0, darray_side_range_size_get(&scn->media_use));
      } else {
        current_media = MEM_CALLOC(alloc,
          darray_side_range_size_get(&scn->media_use), sizeof(*current_media));
        if(!current_media) {
          *p_res = RES_MEM_ERR;
          continue;
        }
      }
      current_media[m_idx] = 1;
      media_count = 1;
      for(;;) { /* Process all sides of this component */
        int i;
        enum side_flag crt_side_flag = TRGSIDE_2_SIDEFLAG(crt_side_id);
        struct trgside* crt_side = trgsides + crt_side_id;
        const trg_id_t crt_trg_id = TRGSIDE_2_TRG(crt_side_id);
        uchar* trg_used = processed + crt_trg_id;
        const struct triangle_tmp* const trg_tmp = triangles_tmp + crt_trg_id;
        ASSERT(crt_trg_id < scn->ntris);

        if(*p_res != RES_OK) break;

        /* Record max_z information
         * Keep track of the appropriate vertex of the component in order to
         * start upwards closest point query at the component grouping step of
         * the algorithm. The most appropriate vertex is (the) one with the
         * greater Z coordinate. */
        if(max_z < trg_tmp->max_z) {
          /* New best vertex */
          const struct triangle_in* trg_in =
            darray_triangle_in_cdata_get(&scn->triangles_in) + crt_trg_id;
          max_z = trg_tmp->max_z;
          /* Select a vertex with z == max_z */
          FOR_EACH(i, 0, 3) {
            if(max_z == positions[trg_in->vertice_id[i]].pos.z) {
              max_z_vrtx_id = trg_in->vertice_id[i];
              break;
            }
          }
          ASSERT(i < 3); /* Found one */
        }

        /* Record crt_side both as component and triangle level */
        if((*trg_used & crt_side_flag) == 0) {
          OK2(darray_side_id_push_back(&current_component, &crt_side_id));
          *trg_used = *trg_used | (uchar)crt_side_flag;
        }

        /* Store neighbour's sides in a waiting stack */
        FOR_EACH(i, 0, 3) {
          side_id_t neighbour_id = crt_side->facing_side_id[i];
          trg_id_t nbour_trg_id = TRGSIDE_2_TRG(neighbour_id);
          enum side_flag nbour_side_id = TRGSIDE_2_SIDEFLAG(neighbour_id);
          uchar* nbour_used = processed + nbour_trg_id;
          const struct trgside* neighbour = trgsides + neighbour_id;
          medium_id_t nbour_med_idx = medium_id_2_medium_idx(neighbour->medium);
          if((int64_t)nbour_med_idx < m_idx
            || (*nbour_used & SIDE_CANCELED_FLAG(nbour_side_id)))
          {
            /* 1) Not the same medium.
             * Neighbour's medium idx is less than current medium: the whole
             * component is to be processed by another thread (possibly the one
             * associated with neighbour's medium).
             * 2) Neighbour was canceled: no need to replay the component
             * again as it will eventually rediscover the side with low medium
             * id and recancel all the work in progress */
            component_canceled = 1;
            darray_side_id_clear(&stack);
            /* Don't cancel used flags as all these sides will get us back to
             * (at least) the neighbour side we have just discovered, that will
             * cancel them again and again */
            sz = darray_side_id_size_get(&current_component);
            FOR_EACH(ii, 0, sz) {
              side_id_t used_side
                = darray_side_id_cdata_get(&current_component)[ii];
              trg_id_t used_trg_id = TRGSIDE_2_TRG(used_side);
              enum side_flag used_side_flag = TRGSIDE_2_SIDEFLAG(used_side);
              uchar* used = processed + used_trg_id;
              ASSERT(*used & (uchar)used_side_flag);
              /* Set the used flag for sides in cancelled component as leading
               * to further cancellations */
              *used |= SIDE_CANCELED_FLAG(used_side_flag);
            }
            goto canceled;
          }
          if(*nbour_used & nbour_side_id) continue; /* Already processed */
          /* Mark neighbour as processed and stack it */
          *nbour_used |= (uchar)nbour_side_id;
          OK2(darray_side_id_push_back(&stack, &neighbour_id));
          OK2(darray_side_id_push_back(&current_component, &neighbour_id));
          if(!current_media[nbour_med_idx]) {
            current_media[nbour_med_idx] = 1;
            media_count++;
          }
        }
        sz = darray_side_id_size_get(&stack);
        if(sz == 0) break; /* Empty stack => component is done! */
        crt_side_id = darray_side_id_cdata_get(&stack)[sz - 1];
        darray_side_id_pop_back(&stack);
        cc_start_side_id = MMIN(cc_start_side_id, crt_side_id);
        cc_last_side_id = MMAX(cc_last_side_id, crt_side_id);
      }
    canceled:
      if(component_canceled) continue;

      /* Register the new component and get it initialized */
      cc = MEM_ALLOC(alloc, sizeof(struct cc_descriptor));
      if(!cc) *p_res = RES_MEM_ERR;
      if(*p_res != RES_OK) break;

      ASSERT(max_z_vrtx_id != VRTX_NULL__);
      cc_descriptor_init(alloc, cc);
      id = ATOMIC_INCR(component_count) - 1;
      ASSERT(id <= COMPONENT_MAX__);
      cc->cc_id = (component_id_t)id;
      sz = darray_side_id_size_get(&current_component);
      ASSERT(sz > 0 && sz <= SIDE_MAX__);
      cc->side_count = (side_id_t)sz;
      cc->side_range.first = cc_start_side_id;
      cc->side_range.last = cc_last_side_id;
      cc->max_z_vrtx_id = max_z_vrtx_id;
      /* Tranfer ownership of the array to component */
      ASSERT(!cc->media && current_media);
      cc->media = current_media;
      cc->media_count = media_count;
      current_media = NULL;

      /* Write component membership in the global structure
       * No need for sync here as an unique thread writes a given side */
      {STATIC_ASSERT(sizeof(cc->cc_id) >= 4, Cannot_write_IDs_sync_free);}
      ASSERT(IS_ALIGNED(triangles_comp->component, sizeof(cc->cc_id)));
      FOR_EACH(ii, 0, sz) {
        const side_id_t s_id = darray_side_id_cdata_get(&current_component)[ii];
        trg_id_t t_id = TRGSIDE_2_TRG(s_id);
        enum senc3d_side side = TRGSIDE_2_SIDE(s_id);
        component_id_t* cmp = triangles_comp[t_id].component;
        ASSERT(cmp[side] == COMPONENT_NULL__);
        ASSERT(medium_id_2_medium_idx(trgsides[s_id].medium)
          >= medium_id_2_medium_idx(medium));
        cmp[side] = cc->cc_id;
      }
      /* Compute component's bbox, area and volume, and record information on
       * the max_z side of the component to help find out if the component is
       * inner or outer */
      fst_nz = 1;
      max_nz = 0;
      max_nz_side_id = SIDE_NULL__;
      FOR_EACH(ii, 0, sz) {
        const side_id_t s_id = darray_side_id_cdata_get(&current_component)[ii];
        trg_id_t t_id = TRGSIDE_2_TRG(s_id);
        enum senc3d_side side = TRGSIDE_2_SIDE(s_id);
        struct triangle_comp* trg_comp = triangles_comp + t_id;
        const struct triangle_tmp* const trg_tmp = triangles_tmp + t_id;
        const struct triangle_in* trg_in =
          darray_triangle_in_cdata_get(&scn->triangles_in) + t_id;
        const union double3* vertices =
          darray_position_cdata_get(&scn->vertices);
        double edge0[3], edge1[3], normal[3], norm;
        const double* v0 = vertices[trg_in->vertice_id[0]].vec;
        const double* v1 = vertices[trg_in->vertice_id[1]].vec;
        const double* v2 = vertices[trg_in->vertice_id[2]].vec;
        int n, is_2sided = (trg_comp->component[SENC3D_FRONT]
          == trg_comp->component[SENC3D_BACK]);

        /* Compute component's bbox, area and volume */
        for(n = 0; n < 3; n++) {
          cc->bbox[n][0] = MMIN(cc->bbox[n][0], v0[n]);
          cc->bbox[n][0] = MMIN(cc->bbox[n][0], v1[n]);
          cc->bbox[n][0] = MMIN(cc->bbox[n][0], v2[n]);
          cc->bbox[n][1] = MMAX(cc->bbox[n][1], v0[n]);
          cc->bbox[n][1] = MMAX(cc->bbox[n][1], v1[n]);
          cc->bbox[n][1] = MMAX(cc->bbox[n][1], v2[n]);
        }
        d3_sub(edge0, v1, v0);
        d3_sub(edge1, v2, v0);
        d3_cross(normal, edge0, edge1);
        norm = d3_normalize(normal, normal);
        ASSERT(norm);
        cc->_2area += norm;

        if(!is_2sided) {
          /* Build a tetrahedron whose base is the triangle and whose apex is
           * the coordinate system's origin. If the 2 sides of the triangle
           * are part of the component, the contribution of the triangle
           * must be 0. To achieve this, we just skip both sides */
          double _2base = norm; /* 2 * base area */
          double h = -d3_dot(normal, v0); /* Height of the tetrahedron */
          if((trg_comp->component[SENC3D_FRONT] == cc->cc_id)
            == (scn->convention & SENC3D_CONVENTION_NORMAL_FRONT))
            cc->_6volume += (h * _2base);
          else
            cc->_6volume -= (h * _2base);
        }

        ASSERT(trg_comp->component[side] != COMPONENT_NULL__); (void)side;
        if(trg_tmp->max_z == max_z) {
          int i;
          /* Candidate to define max_nz (triangle using max_z_vrtx) */
          FOR_EACH(i, 0, 3) {
            if(cc->max_z_vrtx_id == trg_in->vertice_id[i]) {
              if(fst_nz || fabs(normal[2]) > fabs(max_nz)) {
                max_nz_side_id = s_id;
                max_nz = normal[2];
                max_z_is_2sided = is_2sided;
                fst_nz = 0;
              }
              break;
            }
          }
        }
      }
      ASSERT(!fst_nz);
      /* Determine if this component can be an inner part inside another
       * component (substracting a volume) as only these components will need
       * to search for their possible outer component when grouping
       * components to create enclosures.
       * The inner/outer property comes from the normal orientation of the
       * triangle on top of the component, this triangle being the one whose
       * |Nz| is maximal. If this triangle has its 2 sides in the component,
       * the component is inner */
      if(max_nz == 0 || max_z_is_2sided) cc->is_outer_border = 0;
      else {
        ASSERT(max_nz_side_id != SIDE_NULL__);
        if(TRGSIDE_IS_FRONT(max_nz_side_id)
          == ((scn->convention & SENC3D_CONVENTION_NORMAL_FRONT) != 0)) {
          /* Geom normal points towards the component */
          cc->is_outer_border = (max_nz < 0);
        } else {
          /* Geom normal points away from the component */
          cc->is_outer_border = (max_nz > 0);
        }
      }

      /* Need to synchronize connex_components growth as this global structure
       * is accessed by multipe threads */
      #pragma omp critical
      {
        struct cc_descriptor** components;
        sz = darray_ptr_component_descriptor_size_get(connex_components);
        if(sz <= cc->cc_id) {
          tmp_res = darray_ptr_component_descriptor_resize(connex_components,
              1 + cc->cc_id);
          if(tmp_res != RES_OK) *p_res = tmp_res;
        }
        if(*p_res == RES_OK) {
          /* Don't set the pointer before resize as this can lead to move data */
          components =
            darray_ptr_component_descriptor_data_get(connex_components);
          ASSERT(components[cc->cc_id] == NULL);
          components[cc->cc_id] = cc;
        }
      }
    }
  tmp_error:
    if(tmp_res != RES_OK) *p_res = tmp_res;
  } /* No barrier here */

  MEM_RM(alloc, processed);
  MEM_RM(alloc, current_media);
  darray_side_id_release(&current_component);
  darray_side_id_release(&stack);

  /* The first thread here creates the s3d view */
  #pragma omp single nowait
  if(*p_res == RES_OK) {
    res_T res = RES_OK;
    struct s3d_device* s3d = NULL;
    struct s3d_scene* s3d_scn = NULL;
    struct s3d_shape* s3d_shp = NULL;
    struct s3d_vertex_data attribs;

    attribs.type = S3D_FLOAT3;
    attribs.usage = S3D_POSITION;
    attribs.get = get_scn_position;

    /* Put geometry in a 3D view */
    OK(s3d_device_create(scn->dev->logger, alloc, 0, &s3d));
    OK(s3d_scene_create(s3d, &s3d_scn));
    OK(s3d_shape_create_mesh(s3d, &s3d_shp));

    /* Back to s3d API type for ntris and nverts */
    ASSERT(scn->ntris <= UINT_MAX);
    ASSERT(scn->nverts <= UINT_MAX);
    OK(s3d_mesh_setup_indexed_vertices(s3d_shp,
      (unsigned)scn->ntris, get_scn_indices,
      (unsigned)scn->nverts, &attribs, 1, scn));
    OK(s3d_mesh_set_hit_filter_function(s3d_shp, self_hit_filter, NULL));
    OK(s3d_scene_attach_shape(s3d_scn, s3d_shp));
    OK(s3d_scene_view_create(s3d_scn, S3D_TRACE, s3d_view));
  error:
    if(res != RES_OK) *p_res = res;
    if(s3d) S3D(device_ref_put(s3d));
    if(s3d_scn) S3D(scene_ref_put(s3d_scn));
    if(s3d_shp) S3D(shape_ref_put(s3d_shp));
  }

#ifndef NDEBUG
  /* Need to wait for all threads done to be able to check stuff */
  #pragma omp barrier
  if(*p_res != RES_OK) return;
  #pragma omp single
  {
    trg_id_t t_;
    component_id_t c;
    ASSERT(ATOMIC_GET(component_count) ==
      (int)darray_ptr_component_descriptor_size_get(connex_components));
    FOR_EACH(t_, 0, scn->ntris) {
      struct triangle_comp* trg_comp =
        darray_triangle_comp_data_get(triangles_comp_array) + t_;
      ASSERT(trg_comp->component[SENC3D_FRONT] != COMPONENT_NULL__);
      ASSERT(trg_comp->component[SENC3D_BACK] != COMPONENT_NULL__);
    }
    FOR_EACH(c, 0, (component_id_t)ATOMIC_GET(component_count)) {
      struct cc_descriptor** components =
        darray_ptr_component_descriptor_data_get(connex_components);
      ASSERT(components[c] != NULL && components[c]->cc_id == c);
    }
  } /* Implicit barrier here */
#endif
}

#ifndef NDEBUG
static int
compare_components
  (const void* ptr1, const void* ptr2)
{
  const struct cc_descriptor* const* cc1 = ptr1;
  const struct cc_descriptor* const* cc2 = ptr2;
  ASSERT((*cc1)->side_range.first != (*cc2)->side_range.first);
  return (int)(*cc1)->side_range.first - (int)(*cc2)->side_range.first;
}

static res_T
reorder_components
  (struct senc3d_scene* scn,
   struct darray_ptr_component_descriptor* connex_components,
   struct darray_triangle_comp* triangles_comp_array)
{
  struct cc_descriptor** cc_descriptors;
  struct triangle_comp* triangles_comp;
  component_id_t c;
  component_id_t* new_ids = NULL;
  size_t t, cc_count;
  res_T res = RES_OK;

  ASSERT(connex_components && triangles_comp_array);

  cc_count = darray_ptr_component_descriptor_size_get(connex_components);
  ASSERT(cc_count <= COMPONENT_MAX__);
  cc_descriptors = darray_ptr_component_descriptor_data_get(connex_components);

  /* Single thread code: can allocate here */
  new_ids = MEM_ALLOC(scn->dev->allocator, sizeof(*new_ids) * cc_count);
  if(!new_ids) {
    res = RES_MEM_ERR;
    goto error;
  }

  FOR_EACH(c, 0, cc_count) ASSERT(cc_descriptors[c]->cc_id == c);
  /* Sort components by first side */
  qsort(cc_descriptors, cc_count, sizeof(*cc_descriptors), compare_components);
  /* Make conversion table */
  FOR_EACH(c, 0, cc_count) {
    component_id_t rank = cc_descriptors[c]->cc_id;
    new_ids[rank] = c;
  }
  triangles_comp = darray_triangle_comp_data_get(triangles_comp_array);
  FOR_EACH(c, 0, cc_count) {
    ASSERT(new_ids[cc_descriptors[c]->cc_id] == c);
    /* Update the enclosure ID in cc_descriptor */
    cc_descriptors[c]->cc_id = c;
  }
  FOR_EACH(t, 0, scn->ntris) {
    struct triangle_comp* comp = triangles_comp + t;
    component_id_t fc = comp->component[SENC3D_FRONT];
    component_id_t bc = comp->component[SENC3D_BACK];
    ASSERT(new_ids[fc] < cc_count);
    comp->component[SENC3D_FRONT] = new_ids[fc];
    ASSERT(new_ids[bc] < cc_count);
    comp->component[SENC3D_BACK] = new_ids[bc];
  }

error:
  if(new_ids) MEM_RM(scn->dev->allocator, new_ids);
  return res;
}
#endif

static void
group_connex_components
  (struct senc3d_scene* scn,
   struct darray_triangle_comp* triangles_comp,
   struct darray_ptr_component_descriptor* connex_components,
   struct s3d_scene_view* s3d_view,
   ATOMIC* next_enclosure_id,
   /* Shared error status.
    * We accept to overwrite an error with a different error */
   res_T* res)
{
  /* This function is called from an omp parallel block and executed
   * concurrently. */
  struct cc_descriptor** descriptors;
  const union double3* positions;
  size_t tmp;
  component_id_t cc_count;
  int64_t ccc;
  struct filter_ctx ctx0, ctx1;
  float lower[3], upper[3];
  const int log_components = scn->convention & SENC3D_LOG_COMPONENTS_INFORMATION;
  const int dump_components = scn->convention & SENC3D_DUMP_COMPONENTS_STL;

  ASSERT(scn && triangles_comp && connex_components
    && s3d_view && next_enclosure_id && res);
  ASSERT(scn->analyze.enclosures_count == 1);

#ifndef NDEBUG
  #pragma omp single
  {
    /* Ensure reproducible cc_ids for debugging purpose */
    *res = reorder_components(scn, connex_components, triangles_comp);
  }
  if(*res != RES_OK) return;
#endif
  descriptors = darray_ptr_component_descriptor_data_get(connex_components);
  tmp = darray_ptr_component_descriptor_size_get(connex_components);
  ASSERT(tmp <= COMPONENT_MAX__);
  cc_count = (component_id_t)tmp;
  positions = darray_position_cdata_get(&scn->vertices);

  #pragma omp single
  if(dump_components) {
    /* Do it now before any other problem can occur (fingers crossed).
     * Do it sequential and not optimized as it is debug code.
     * Don't throw errors, just skip to the next component. */
    static unsigned scene_cpt = 0;
    struct str name;
    res_T tmp_res;
    const struct triangle_comp* tc =
      darray_triangle_comp_cdata_get(triangles_comp);
    const struct triangle_in* tin = darray_triangle_in_cdata_get(&scn->triangles_in);
    const int output_normal_in =
      (scn->convention & SENC3D_CONVENTION_NORMAL_INSIDE) != 0;
    str_init(scn->dev->allocator, &name);
    printf("Dumping components for scene #%u\n", scene_cpt);
    for(ccc = 0; ccc < (int64_t)cc_count; ccc++) {
      FILE* f;
      trg_id_t t;
      component_id_t c = (component_id_t)ccc;
      tmp_res = str_printf(&name, "scn_%u_comp_%u.stl", scene_cpt, c);
      if(tmp_res != RES_OK) continue;
      f = fopen(str_cget(&name), "w");
      if(!f) continue;
      fprintf(f, "solid %s\n", str_cget(&name));
      for(t = 0; t < scn->ntris; t++) {
        const component_id_t cf_id = tc[t].component[SENC3D_FRONT];
        const component_id_t cb_id = tc[t].component[SENC3D_BACK];
        if(cf_id == c || cb_id == c) {
          const vrtx_id_t* vertice_id = tin[t].vertice_id;
          double n[3], e1[3], e2[3];
          const int input_normal_in = (cf_id == c);
          const int revert_triangle = (input_normal_in != output_normal_in);
          const vrtx_id_t i0 = vertice_id[0];
          const vrtx_id_t i1 = vertice_id[revert_triangle ? 2 : 1];
          const vrtx_id_t i2 = vertice_id[revert_triangle ? 1 : 2];
          /* This triangle is in component #c */
          if(cf_id == cb_id) { /* Both sides in component */
            /* Could add some log */
          }
          d3_sub(e1, positions[i1].vec, positions[i0].vec);
          d3_sub(e2, positions[i2].vec, positions[i0].vec);
          d3_normalize(n, d3_cross(n, e1, e2));
          fprintf(f, "  facet normal %16g %16g %16g\n", SPLIT3(n));
          fprintf(f, "    outer loop\n");
          fprintf(f, "      vertex %16g %16g %16g\n", SPLIT3(positions[i0].vec));
          fprintf(f, "      vertex %16g %16g %16g\n", SPLIT3(positions[i1].vec));
          fprintf(f, "      vertex %16g %16g %16g\n", SPLIT3(positions[i2].vec));
          fprintf(f, "    endloop\n");
          fprintf(f, "  endfacet\n");
        }
      }
        printf("Dumped component #%u in file %s\n", c, str_cget(&name));
      fprintf(f, "endsolid %s\n", str_cget(&name));
      fclose(f);
    }
    str_release(&name);
    scene_cpt++;
  }

  ctx0.type = CTX0;
  ctx0.scn = scn;
  ctx0.view = s3d_view;
  ctx0.triangles_comp = triangles_comp;
  ctx0.components = connex_components;
  ctx1.type = CTX1;
  ctx1.scn = scn;
  ctx1.view = s3d_view;
  ctx1.triangles_comp = triangles_comp;
  ctx1.components = connex_components;
  *res = s3d_scene_view_get_aabb(s3d_view, lower, upper);
  if(*res != RES_OK) goto end;
  #pragma omp for schedule(dynamic)
  for(ccc = 0; ccc < (int64_t)cc_count; ccc++) {
    res_T tmp_res = RES_OK;
    component_id_t c = (component_id_t)ccc;
    struct s3d_hit hit = S3D_HIT_NULL;
    float origin[3], rrr[3], r;
    struct cc_descriptor* const cc = descriptors[c];
    const double* max_vrtx;
    int i;

    if(*res != RES_OK) continue;
    ASSERT(cc->cc_id == c);
    ASSERT(cc->cc_group_root == CC_GROUP_ID_NONE);
    ASSERT(cc->max_z_vrtx_id < scn->nverts);

    if(cc->is_outer_border) {
      ATOMIC id;
      /* No need to create a link from this CC: inner CC are doing the job */
      cc->cc_group_root = cc->cc_id; /* New group with self as root */
      id = ATOMIC_INCR(next_enclosure_id) - 1;
      ASSERT(id <= ENCLOSURE_MAX__);
      cc->enclosure_id = (enclosure_id_t)id;
      if(log_components) {
        #pragma omp critical
        printf("Component #%u: is outer, not processed\n", c);
      }
      continue;
    }

    /* First step is to determine the distance of the closest upward geometry */
    max_vrtx = positions[cc->max_z_vrtx_id].vec;
    f3_set_d3(origin, max_vrtx);
    /* Limit search radius. Only upwards moves (+Z) will be considered.
     * Use a radius allowing to reach the closest top vertex of scene's AABB */
    FOR_EACH(i, 0, 2) {
      ASSERT(lower[i] <= origin[i] && origin[i] <= upper[i]);
      rrr[i] = MMIN(origin[i] - lower[i], upper[i] - origin[i]);
    }
    rrr[2] = upper[2] - origin[2];
    r = f3_len(rrr) * (1 + FLT_EPSILON) + FLT_EPSILON; /* Ensure r > 0 */
    ctx0.origin_component = cc->cc_id;
    ctx0.current_6volume = DBL_MAX;
    ctx0.hit_dist = FLT_MAX;
    ctx0.hit_component = COMPONENT_NULL__;
    tmp_res = s3d_scene_view_closest_point(s3d_view, origin, r, &ctx0, &hit);
    if(tmp_res != RES_OK) {
      *res = tmp_res;
      continue;
    }
    /* If no hit, the component is facing an infinite medium */
    if(S3D_HIT_NONE(&hit)) {
      cc->cc_group_root = CC_GROUP_ROOT_INFINITE;
      cc->enclosure_id = 0;
      if(log_components) {
        #pragma omp critical
        printf("Component #%u: is part of enclosure #0\n", c);
      }
      continue;
    }

    /* Second step is to determine which component faces component #c */
    ctx1.origin_component = cc->cc_id;
    ctx1.current_6volume = DBL_MAX;
    ctx1.hit_dist = FLT_MAX;
    ctx1.hit_component = COMPONENT_NULL__;
    /* New search radius is hit.distance + some margin to cope with numerical
     * issues (and r==0 is an error) */
    r = hit.distance * (1 + FLT_EPSILON) + FLT_EPSILON;
    if(log_components) {
      #pragma omp critical
      printf("Component #%u: starting search for components (R=%g) from %g %g %g\n",
          c, r, SPLIT3(origin));
    }
    /* Cast a sphere to find links between connex components */
    tmp_res = s3d_scene_view_closest_point(s3d_view, origin, r, &ctx1, &hit);
    if(tmp_res != RES_OK) {
      *res = tmp_res;
      continue;
    }
    /* As CTX1 filter rejects any hit, do not rely on hit but use result as
     * stored in ctx1 */
    /* If no hit is accepted, the component is facing an infinite medium */
    if(ctx1.hit_dist == FLT_MAX) {
      cc->cc_group_root = CC_GROUP_ROOT_INFINITE;
      cc->enclosure_id = 0;
      if(log_components) {
        #pragma omp critical
        printf("Component #%u: is part of enclosure #0\n", c);
      }
      continue;
    }
    else if(ctx1.hit_component == COMPONENT_NULL__) {
      /* The selected triangle was nearly parallel to the line of sight:
       * FRONT/BACK discrimination was not reliable enough and should be done
       * differently. */
      /* Could try something; now just report a failure */
      log_err(scn->dev, LIB_NAME": %s: %d: search failed\n", FUNC_NAME,
          __LINE__ );
      *res = RES_BAD_OP;
      continue;
    } else {
      /* If hit, group this component */
      cc->cc_group_root = ctx1.hit_component;
      ASSERT(cc->cc_group_root < cc_count);
      if(log_components) {
        #pragma omp critical
        printf("Component #%u: linked to component #%u\n", c, cc->cc_group_root);
      }
    }
  }
  /* Implicit barrier here */
  if(*res != RES_OK) goto end;

  /* One thread post-processes links to group connex components */
  #pragma omp single
  {
    res_T tmp_res = RES_OK;
    size_t ec = (size_t)ATOMIC_GET(next_enclosure_id);
    ASSERT(ec <= ENCLOSURE_MAX__);
    scn->analyze.enclosures_count = (enclosure_id_t)ec;
    tmp_res = darray_enclosure_resize(&scn->analyze.enclosures,
      scn->analyze.enclosures_count);
    if(tmp_res != RES_OK) {
      *res = tmp_res;
    } else {
      struct enclosure_data* enclosures
        = darray_enclosure_data_get(&scn->analyze.enclosures);
      FOR_EACH(ccc, 0, (int64_t)cc_count) {
        component_id_t c = (component_id_t)ccc;
        struct cc_descriptor* const cc = descriptors[c];
        const struct cc_descriptor* other_desc = cc;
        struct enclosure_data* enc;
#ifndef NDEBUG
        component_id_t cc_cpt = 0;
#endif

        while(other_desc->enclosure_id == CC_GROUP_ID_NONE) {
          ASSERT(other_desc->cc_group_root < cc_count);
          other_desc = descriptors[other_desc->cc_group_root];
          /* Cannot have more components in cc than cc_count! */
          ASSERT(++cc_cpt <= cc_count);
        }
        ASSERT(other_desc->cc_group_root != CC_GROUP_ROOT_NONE);
        ASSERT(other_desc->enclosure_id != CC_GROUP_ID_NONE);
        cc->cc_group_root = other_desc->cc_group_root;
        cc->enclosure_id = other_desc->enclosure_id;
        enc = enclosures + cc->enclosure_id;
        ++enc->cc_count;
        /* Linked list of componnents */
        enc->first_component = cc->cc_id;
        enc->side_range.first = MMIN(enc->side_range.first, cc->side_range.first);
        enc->side_range.last = MMAX(enc->side_range.last, cc->side_range.last);
        enc->side_count += cc->side_count;
        tmp_res = bool_array_of_media_merge(&enc->tmp_enclosed_media, cc->media,
          cc->media_count, darray_side_range_size_get(&scn->media_use));
        if(tmp_res != RES_OK) {
          *res = tmp_res;
          break;
        }
      }
    }
  }
  /* Implicit barrier here */
end:
  return;
}

static void
collect_and_link_neighbours
  (struct senc3d_scene* scn,
   struct trgside* trgsides,
   struct darray_triangle_tmp* triangles_tmp_array,
   struct darray_frontier_edge* frontiers,
   struct htable_overlap* overlaps,
   /* Shared error status.
    * We accept to overwrite an error with a different error */
   res_T* res)
{
  /* This function is called from an omp parallel block and executed
   * concurrently. */
  const struct triangle_in* triangles_in;
  struct triangle_tmp* triangles_tmp;
  const union double3* vertices;
  const int thread_count = omp_get_num_threads();
  const int rank = omp_get_thread_num();
  const int front = ((scn->convention & SENC3D_CONVENTION_NORMAL_FRONT) != 0);
  /* Htable used to give an id to edges */
  struct htable_edge_id edge_ids;
  /* Array to keep neighbourhood of edges
   * Resize/Push operations on neighbourhood_by_edge are valid in the
   * openmp block because a given neighbourhood is only processed
   * by a single thread */
  struct darray_neighbourhood neighbourhood_by_edge;
  edge_id_t edge_count;
  edge_id_t nbedges_guess;
  edge_id_t e;
  trg_id_t t;
  size_t sz;
  res_T tmp_res;

  ASSERT(scn && trgsides && triangles_tmp_array && frontiers && res);
  ASSERT((size_t)scn->nverts + (size_t)scn->ntris + 2 <= EDGE_MAX__);

  htable_edge_id_init(scn->dev->allocator, &edge_ids);
  darray_neighbourhood_init(scn->dev->allocator, &neighbourhood_by_edge);

  triangles_in = darray_triangle_in_cdata_get(&scn->triangles_in);
  triangles_tmp = darray_triangle_tmp_data_get(triangles_tmp_array);
  vertices = darray_position_cdata_get(&scn->vertices);

  ASSERT(scn->ntris == darray_triangle_tmp_size_get(triangles_tmp_array));

  /* Make some room for edges. */
  nbedges_guess = 4 + (thread_count == 1
    ? (edge_id_t)(scn->nverts + scn->ntris)
    : (edge_id_t)((scn->nverts + scn->ntris) / (0.75 * thread_count)));
  OK2(darray_neighbourhood_reserve(&neighbourhood_by_edge, nbedges_guess));
  OK2(htable_edge_id_reserve(&edge_ids, nbedges_guess));

  /* Loop on triangles to register edges.
   * All threads considering all the edges and processing some */
  FOR_EACH(t, 0, scn->ntris) {
    struct trg_edge edge;
    uchar ee;
    FOR_EACH(ee, 0, 3) {
      edge_id_t* p_id;
      size_t n_sz;
      struct edge_neighbourhood* neighbourhood;
      struct neighbour_info* info;
      const vrtx_id_t v0 = triangles_in[t].vertice_id[ee];
      const vrtx_id_t v1 = triangles_in[t].vertice_id[(ee + 1) % 3];
      /* Process only "my" edges! */
      const int64_t h =
        /* v0,v1 and v1,v0 must give the same hash!!! */
        v0 + v1 + (int64_t)MMIN(v0, v1);
      if(h % thread_count != rank) continue;
      /* Create edge. */
      set_edge(v0, v1, &edge, &triangles_tmp[t].reversed_edge[ee]);
      /* Find edge id; create it if not already done. */
      p_id = htable_edge_id_find(&edge_ids, &edge);
      if(p_id) {
        neighbourhood =
          darray_neighbourhood_data_get(&neighbourhood_by_edge) + *p_id;
        ASSERT(neighbourhood->edge.vrtx0 == edge.vrtx0
          && neighbourhood->edge.vrtx1 == edge.vrtx1);
      } else {
        /* Create id */
        edge_id_t id;
        sz = htable_edge_id_size_get(&edge_ids);
        ASSERT(sz <= EDGE_MAX__);
        id = (edge_id_t)sz;
        ASSERT(htable_edge_id_size_get(&edge_ids)
          == darray_neighbourhood_size_get(&neighbourhood_by_edge));
        OK2(htable_edge_id_set(&edge_ids, &edge, &id));
        OK2(darray_neighbourhood_resize(&neighbourhood_by_edge, 1 + sz));
        neighbourhood = darray_neighbourhood_data_get(&neighbourhood_by_edge) + sz;
        /* Add neighbour info to a newly created edge's neighbour list */
        neighbourhood->edge = edge;
        ASSERT(darray_neighbour_size_get(&neighbourhood->neighbours) == 0);
        /* Just a guess: few edges will have less than 2 neighbours  */
        OK2(darray_neighbour_reserve(&neighbourhood->neighbours, 2));
      }
      /* Add neighbour info to neighbourhood */
      n_sz = darray_neighbour_size_get(&neighbourhood->neighbours);
      OK2(darray_neighbour_resize(&neighbourhood->neighbours, 1 + n_sz));
      info = darray_neighbour_data_get(&neighbourhood->neighbours) + n_sz;
      info->trg_id = t;
      info->common_edge_rank = ee;
    }
  } /* No barrier here. */

  /* Loop on collected edges.
   * For each edge sort triangle sides by rotation angle
   * and connect neighbours. */
  sz = darray_neighbourhood_size_get(&neighbourhood_by_edge);
  ASSERT(sz <= EDGE_MAX__);
  edge_count = (edge_id_t)sz;
  FOR_EACH(e, 0, edge_count) {
    double edge[3], common_edge[3], basis[9], norm, max_z, maxz_edge, a;
    vrtx_id_t v0, v1, v2;
    struct edge_neighbourhood* neighbourhood
      = darray_neighbourhood_data_get(&neighbourhood_by_edge) + e;
    struct darray_neighbour* neighbour_list = &neighbourhood->neighbours;
    side_id_t i, neighbour_count;
    sz = darray_neighbour_size_get(neighbour_list);
    ASSERT(sz > 0 && sz <= SIDE_MAX__);
    neighbour_count = (side_id_t)sz;
    ASSERT(neighbour_count);
    v0 = neighbourhood->edge.vrtx0;
    v1 = neighbourhood->edge.vrtx1;
    d3_sub(common_edge, vertices[v1].vec, vertices[v0].vec);
    maxz_edge = MMAX(vertices[v0].pos.z, vertices[v1].pos.z);
    norm = d3_normalize(common_edge, common_edge);
    ASSERT(norm); (void)norm;
    d33_basis(basis, common_edge);
    d33_inverse(basis, basis);
    FOR_EACH(i, 0, neighbour_count) {
      struct neighbour_info* neighbour_info
        = darray_neighbour_data_get(neighbour_list) + i;
      const trg_id_t crt_id = neighbour_info->trg_id;
      const uchar crt_edge = neighbour_info->common_edge_rank;
      const struct triangle_in* trg_in = triangles_in + crt_id;
      struct triangle_tmp* neighbour = triangles_tmp + crt_id;
      union double3 n; /* Geometrical normal to neighbour triangle */
      const int is_reversed = neighbour->reversed_edge[crt_edge];
      v2 = trg_in->vertice_id[(crt_edge + 2) % 3];
      max_z = MMAX(vertices[v2].pos.z, maxz_edge);
      ASSERT(neighbour->max_z <= max_z);
      neighbour->max_z = max_z;
      /* Compute rotation angle around common edge */
      d3_sub(edge, vertices[v2].vec, vertices[v0].vec);
      d33_muld3(edge, basis, edge);
      ASSERT(d3_len(edge) != 0 && (edge[0] != 0 || edge[1] != 0));
      neighbour_info->angle = atan2(edge[1], edge[0]); /* in ]-pi + pi]*/
      if(is_reversed)
        d3(n.vec, +edge[1], -edge[0], 0);
      else
        d3(n.vec, -edge[1], +edge[0], 0);

      /* Normal orientation calculation. */
      if(neighbour_info->angle > 3 * PI / 4) {
        ASSERT(n.pos.y);
        neighbour_info->normal_toward_next_neighbour = (n.pos.y < 0);
      } else if(neighbour_info->angle > PI / 4) {
        ASSERT(n.pos.x);
        neighbour_info->normal_toward_next_neighbour = (n.pos.x < 0);
      } else if(neighbour_info->angle > -PI / 4) {
        ASSERT(n.pos.y);
        neighbour_info->normal_toward_next_neighbour = (n.pos.y > 0);
      } else if(neighbour_info->angle > -3 * PI / 4) {
        ASSERT(n.pos.x);
        neighbour_info->normal_toward_next_neighbour = (n.pos.x > 0);
      } else {
        ASSERT(n.pos.y);
        neighbour_info->normal_toward_next_neighbour = (n.pos.y < 0);
      }
    }
    /* Sort triangles by rotation angle */
    qsort(darray_neighbour_data_get(neighbour_list), neighbour_count,
      sizeof(struct neighbour_info), neighbour_cmp);
    /* Link sides.
     * Create cycles of sides by neighbourhood around common edge. */
    a = -DBL_MAX;
    FOR_EACH(i, 0, neighbour_count) {
      /* Neighbourhood info for current pair of triangles */
      const struct neighbour_info* current
        = darray_neighbour_cdata_get(neighbour_list) + i;
      const struct neighbour_info* ccw_neighbour
        = darray_neighbour_cdata_get(neighbour_list) + (i + 1) % neighbour_count;
      /* Rank of the edge of interest in triangles */
      const uchar crt_edge = current->common_edge_rank;
      /* Here ccw refers to the rotation around the common edge
       * and has nothing to do with vertices order in triangle definition
       * nor Front/Back side convention */
      const uchar ccw_edge = ccw_neighbour->common_edge_rank;
      /* User id of current triangles */
      const trg_id_t crt_id = current->trg_id;
      const trg_id_t ccw_id = ccw_neighbour->trg_id;
      /* Facing sides of triangles */
      const enum senc3d_side crt_side
        = (current->normal_toward_next_neighbour == front)
          ? SENC3D_FRONT : SENC3D_BACK;
      const enum senc3d_side ccw_side
        = (ccw_neighbour->normal_toward_next_neighbour == front)
          ? SENC3D_BACK : SENC3D_FRONT;
      /* Index of sides in trgsides */
      const side_id_t crt_side_idx = TRGIDxSIDE_2_TRGSIDE(crt_id, crt_side);
      const side_id_t ccw_side_idx = TRGIDxSIDE_2_TRGSIDE(ccw_id, ccw_side);
      /* Side ptrs */
      struct trgside* const p_crt_side = trgsides + crt_side_idx;
      struct trgside* const p_ccw_side = trgsides + ccw_side_idx;
      /* Check that angle is a discriminant property */
      ASSERT(a <= current->angle); /* Is sorted */
      if(a == current->angle) {
        /* Two consecutive triangles with same angle! Store them */
        const struct neighbour_info* previous;
        trg_id_t prev_id;
        previous = darray_neighbour_cdata_get(neighbour_list) + i - 1;
        prev_id = previous->trg_id;
        #pragma omp critical
        {
          char one = 1;
          tmp_res = htable_overlap_set(overlaps, &crt_id, &one);
          if(tmp_res == RES_OK)
            tmp_res = htable_overlap_set(overlaps, &prev_id, &one);
        }
        if(tmp_res != RES_OK) goto tmp_error;
      }
      a = current->angle;
      /* Link sides */
      ASSERT(p_crt_side->facing_side_id[crt_edge] == SIDE_NULL__);
      ASSERT(p_ccw_side->facing_side_id[ccw_edge] == SIDE_NULL__);
      p_crt_side->facing_side_id[crt_edge] = ccw_side_idx;
      p_ccw_side->facing_side_id[ccw_edge] = crt_side_idx;
      /* Record media  */
      ASSERT(p_crt_side->medium == MEDIUM_NULL__
        || p_crt_side->medium == triangles_in[crt_id].medium[crt_side]);
      ASSERT(p_ccw_side->medium == MEDIUM_NULL__
        || p_ccw_side->medium == triangles_in[ccw_id].medium[ccw_side]);
      p_crt_side->medium = triangles_in[crt_id].medium[crt_side];
      p_ccw_side->medium = triangles_in[ccw_id].medium[ccw_side];
      ASSERT(p_crt_side->medium == SENC3D_UNSPECIFIED_MEDIUM
        || p_crt_side->medium < darray_side_range_size_get(&scn->media_use) - 1);
      ASSERT(p_ccw_side->medium == SENC3D_UNSPECIFIED_MEDIUM
        || p_ccw_side->medium < darray_side_range_size_get(&scn->media_use) - 1);
      /* Detect triangles that could surround a hole:
       * - single triangle on (one of) its edge
       * - different media on its sides */
      if(neighbour_count == 1
        && p_crt_side->medium != p_ccw_side->medium)
      #pragma omp critical
      {
        struct frontier_edge frontier_edge;
        frontier_edge.trg = crt_id;
        frontier_edge.vrtx0 = v0;
        frontier_edge.vrtx1 = v1;
        darray_frontier_edge_push_back(frontiers, &frontier_edge);
      }
    }
  }

tmp_error:
  if(tmp_res != RES_OK) *res = tmp_res;
  /* Threads are allowed to return whitout sync. */
  htable_edge_id_release(&edge_ids);
  darray_neighbourhood_release(&neighbourhood_by_edge);
}

static int
compare_enclosures
  (const void* ptr1, const void* ptr2)
{
  const struct enclosure_data* e1 = ptr1;
  const struct enclosure_data* e2 = ptr2;
  ASSERT(e1->side_range.first != e2->side_range.first);
  return (int)e1->side_range.first - (int)e2->side_range.first;
}

static res_T
reorder_enclosures
  (struct senc3d_scene* scn,
   const struct darray_ptr_component_descriptor* connex_components)
{
  struct enclosure_data* enclosures;
  struct cc_descriptor* const* cc_descriptors;
  enclosure_id_t* new_ids;
  enclosure_id_t e;
  size_t c, cc_count;
  res_T res = RES_OK;

  /* Single thread code: can allocate here */
  new_ids = MEM_ALLOC(scn->dev->allocator,
    sizeof(*new_ids) * scn->analyze.enclosures_count);
  if(!new_ids) {
    res = RES_MEM_ERR;
    goto error;
  }
  cc_count = darray_ptr_component_descriptor_size_get(connex_components);
  ASSERT(cc_count <= COMPONENT_MAX__);
  cc_descriptors = darray_ptr_component_descriptor_cdata_get(connex_components);
  enclosures = darray_enclosure_data_get(&scn->analyze.enclosures);
  /* Store initial enclosure order */
  FOR_EACH(e, 0, scn->analyze.enclosures_count)
    enclosures[e].header.enclosure_id = e;
  /* Sort enclosures by first side while keeping enclosure 0
   * at rank 0 (its a convention) */
  qsort(enclosures + 1, scn->analyze.enclosures_count - 1, sizeof(*enclosures),
    compare_enclosures);
  /* Make conversion table */
  FOR_EACH(e, 0, scn->analyze.enclosures_count) {
    enclosure_id_t rank = enclosures[e].header.enclosure_id;
    new_ids[rank] = e;
  }
  FOR_EACH(e, 0, scn->analyze.enclosures_count) {
    ASSERT(new_ids[enclosures[e].header.enclosure_id] == e);
    enclosures[e].header.enclosure_id = e;
  }
  FOR_EACH(c, 0, cc_count) {
    /* Update the enclosure ID in cc_descriptor */
    enclosure_id_t new_id = new_ids[cc_descriptors[c]->enclosure_id];
    ASSERT(new_id < scn->analyze.enclosures_count);
    cc_descriptors[c]->enclosure_id = new_id;
  }
error:
  if(new_ids) MEM_RM(scn->dev->allocator, new_ids);
  return res;
}

static void
build_result
  (struct senc3d_scene* scn,
   const struct darray_ptr_component_descriptor* connex_components,
   const struct darray_triangle_comp* triangles_comp_array,
   struct darray_frontier_edge* frontiers,
   /* Shared error status.
    * We accept to overwrite an error with a different error */
   res_T* res)
{
  /* This function is called from an omp parallel block and executed
   * concurrently. */
  struct mem_allocator* alloc;
  struct cc_descriptor* const* cc_descriptors;
  struct enclosure_data* enclosures;
  const struct triangle_in* triangles_in;
  struct triangle_enc* triangles_enc;
  const struct triangle_comp* triangles_comp;
  struct htable_vrtx_id vtable;
  int output_normal_in, normals_front, normals_back;
  size_t cc_count;
  int64_t tt;
  int64_t ee;

  ASSERT(scn && connex_components && triangles_comp_array && frontiers && res);

  alloc = scn->dev->allocator;
  output_normal_in = (scn->convention & SENC3D_CONVENTION_NORMAL_INSIDE) != 0;
  normals_front = (scn->convention & SENC3D_CONVENTION_NORMAL_FRONT) != 0;
  normals_back = (scn->convention & SENC3D_CONVENTION_NORMAL_BACK) != 0;
  ASSERT(normals_back != normals_front);
  ASSERT(output_normal_in
    != ((scn->convention & SENC3D_CONVENTION_NORMAL_OUTSIDE) != 0));
  cc_count = darray_ptr_component_descriptor_size_get(connex_components);
  ASSERT(cc_count <= COMPONENT_MAX__);
  cc_descriptors = darray_ptr_component_descriptor_cdata_get(connex_components);
  enclosures = darray_enclosure_data_get(&scn->analyze.enclosures);
  triangles_in = darray_triangle_in_cdata_get(&scn->triangles_in);
  triangles_comp = darray_triangle_comp_cdata_get(triangles_comp_array);

  #pragma omp single
  {
    size_t c;
    enclosure_id_t e;
    res_T tmp_res =
      darray_triangle_enc_resize(&scn->analyze.triangles_enc, scn->ntris);
    if(tmp_res != RES_OK)
      *res = tmp_res;
    /* Sort enclosures by first side to ensure reproducible results */
    else *res = reorder_enclosures(scn, connex_components);
    if(*res != RES_OK) goto single_err;
    /* Compute area and volume */
    FOR_EACH(c, 0, cc_count) {
      struct enclosure_data* enc;
      ASSERT(cc_descriptors[c]->enclosure_id < scn->analyze.enclosures_count);
      enc = enclosures + cc_descriptors[c]->enclosure_id;
      /* Sum up areas and volumes of components int enclosures */
      enc->header.area += cc_descriptors[c]->_2area;
      enc->header.volume += cc_descriptors[c]->_6volume;
    }
    FOR_EACH(e, 0, scn->analyze.enclosures_count) {
      struct enclosure_data* enc = enclosures + e;
      enc->header.area /= 2;
      enc->header.volume /= 6;
    }
  single_err: (void)0;
  }/* Implicit barrier here. */
  if(*res != RES_OK) goto exit;
  triangles_enc = darray_triangle_enc_data_get(&scn->analyze.triangles_enc);

  /* Build global enclosure information */
  #pragma omp for
  for(tt = 0; tt < (int64_t)scn->ntris; tt++) {
    trg_id_t t = (trg_id_t)tt;
    const component_id_t cf_id = triangles_comp[t].component[SENC3D_FRONT];
    const component_id_t cb_id = triangles_comp[t].component[SENC3D_BACK];
    const struct cc_descriptor* cf = cc_descriptors[cf_id];
    const struct cc_descriptor* cb = cc_descriptors[cb_id];
    const enclosure_id_t ef_id = cf->enclosure_id;
    const enclosure_id_t eb_id = cb->enclosure_id;
    ASSERT(triangles_enc[t].enclosure[SENC3D_FRONT] == ENCLOSURE_NULL__);
    triangles_enc[t].enclosure[SENC3D_FRONT] = ef_id;
    ASSERT(triangles_enc[t].enclosure[SENC3D_BACK] == ENCLOSURE_NULL__);
    triangles_enc[t].enclosure[SENC3D_BACK] = eb_id;
  }
  /* Implicit barrier here */

  /* Resize/push operations on enclosure's fields are valid in the
   * openmp block as a given enclosure is processed by a single thread */
  htable_vrtx_id_init(alloc, &vtable);

  ASSERT(scn->analyze.enclosures_count <= ENCLOSURE_MAX__);
  #pragma omp for schedule(dynamic) nowait
  for(ee = 0; ee < (int64_t)scn->analyze.enclosures_count; ee++) {
    const enclosure_id_t e = (enclosure_id_t)ee;
    struct enclosure_data* enc = enclosures + ee;
    trg_id_t fst_idx = 0;
    trg_id_t sgd_idx = enc->side_count;
    trg_id_t t;
    medium_id_t m;
    res_T tmp_res = RES_OK;
    ASSERT(enc->first_component < cc_count);
    ASSERT(cc_descriptors[enc->first_component]->cc_id
      == enc->first_component);

    if(*res != RES_OK) continue;
    ASSERT(e <= ENCLOSURE_MAX__);
    enc->header.enclosure_id = (unsigned)ee; /* Back to API type */
    ASSERT(cc_descriptors[enc->first_component]->enclosure_id
      == enc->header.enclosure_id);
    enc->header.is_infinite = (e == 0);

    ASSERT(enc->header.enclosed_media_count
      < darray_side_range_size_get(&scn->media_use));
    OK2(bool_array_of_media_to_darray_media(&enc->enclosed_media,
      &enc->tmp_enclosed_media, darray_side_range_size_get(&scn->media_use)));
    ASSERT(darray_media_size_get(&enc->enclosed_media) <= MEDIUM_MAX__);
    enc->header.enclosed_media_count
      = (medium_id_t)darray_media_size_get(&enc->enclosed_media);
    darray_uchar_purge(&enc->tmp_enclosed_media);

    /* Add this enclosure in relevant by-medium lists */
    FOR_EACH(m, 0, enc->header.enclosed_media_count) {
      medium_id_t medium = darray_media_cdata_get(&enc->enclosed_media)[m];
      size_t m_idx = medium_id_2_medium_idx(medium);
      struct darray_enc_id* enc_ids_array_by_medium;
      ASSERT(medium == SENC3D_UNSPECIFIED_MEDIUM
        || medium < darray_side_range_size_get(&scn->media_use) - 1);
      ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
        == darray_side_range_size_get(&scn->media_use));
      enc_ids_array_by_medium =
        darray_enc_ids_array_data_get(&scn->analyze.enc_ids_array_by_medium)
        + m_idx;
      #pragma omp critical
      {
        tmp_res = darray_enc_id_push_back(enc_ids_array_by_medium, &e);
      }
      if(tmp_res != RES_OK) {
        *res = tmp_res;
        break;
      }
    }
    if(*res != RES_OK) continue;

    /* Build side and vertex lists. */
    OK2(darray_sides_enc_resize(&enc->sides, enc->side_count));
    /* Size is just a hint */
    OK2(darray_vrtx_id_reserve(&enc->vertices,
      (size_t)(enc->side_count * 0.6)));
    /* New vertex numbering scheme local to the enclosure */
    htable_vrtx_id_clear(&vtable);
    ASSERT(scn->ntris == darray_triangle_in_size_get(&scn->triangles_in));
    /* Put at the end the back-faces of triangles that also have their
     * front-face in the list. */
    for(t = TRGSIDE_2_TRG(enc->side_range.first);
      t <= TRGSIDE_2_TRG(enc->side_range.last);
      t++)
    {
      const struct triangle_in* trg_in = triangles_in + t;
      struct side_enc* side_enc;
      vrtx_id_t vertice_id[3];
      int i;
      if(triangles_enc[t].enclosure[SENC3D_FRONT] != e
        && triangles_enc[t].enclosure[SENC3D_BACK] != e)
        continue;
      ++enc->header.unique_primitives_count;

      FOR_EACH(i, 0, 3) {
        vrtx_id_t* p_id = htable_vrtx_id_find(&vtable, trg_in->vertice_id + i);
        if(p_id) {
          vertice_id[i] = *p_id; /* Known vertex */
        } else {
          /* Create new association */
          size_t tmp = htable_vrtx_id_size_get(&vtable);
          ASSERT(tmp == darray_vrtx_id_size_get(&enc->vertices));
          ASSERT(tmp < VRTX_MAX__);
          vertice_id[i] = (vrtx_id_t)tmp;
          OK2(htable_vrtx_id_set(&vtable, trg_in->vertice_id + i,
            vertice_id + i));
          OK2(darray_vrtx_id_push_back(&enc->vertices, trg_in->vertice_id + i));
          ++enc->header.vertices_count;
        }
      }
      ASSERT(triangles_enc[t].enclosure[SENC3D_FRONT] == e
        || triangles_enc[t].enclosure[SENC3D_BACK] == e);
      if(triangles_enc[t].enclosure[SENC3D_FRONT] == e) {
        /* Front side of the original triangle is member of the enclosure */
        int input_normal_in = normals_front;
        int revert_triangle = (input_normal_in != output_normal_in);
        ++enc->header.primitives_count;
        side_enc = darray_sides_enc_data_get(&enc->sides) + fst_idx++;
        FOR_EACH(i, 0, 3) {
          int ii = revert_triangle ? 2 - i : i;
          side_enc->vertice_id[i] = vertice_id[ii];
        }
        side_enc->side_id = TRGIDxSIDE_2_TRGSIDE(t, SENC3D_FRONT);
      }
      if(triangles_enc[t].enclosure[SENC3D_BACK] == e) {
        /* Back side of the original triangle is member of the enclosure */
        int input_normal_in = normals_back;
        int revert_triangle = (input_normal_in != output_normal_in);
        ++enc->header.primitives_count;
        /* If both sides are in the enclosure, put the second side at the end */
        side_enc = darray_sides_enc_data_get(&enc->sides) +
          ((triangles_enc[t].enclosure[SENC3D_FRONT] == e) ? --sgd_idx : fst_idx++);
        FOR_EACH(i, 0, 3) {
          int ii = revert_triangle ? 2 - i : i;
          side_enc->vertice_id[i] = vertice_id[ii];
        }
        side_enc->side_id = TRGIDxSIDE_2_TRGSIDE(t, SENC3D_BACK);
      }
      if(fst_idx == sgd_idx) break;
    }
    continue;
  tmp_error:
    ASSERT(tmp_res != RES_OK);
    *res = tmp_res;
  } /* No barrier here */
  htable_vrtx_id_release(&vtable);
  /* The first thread here copies frontiers into descriptor */
#pragma omp single nowait
  darray_frontier_edge_copy_and_clear(&scn->analyze.frontiers, frontiers);
  /* No barrier here */
exit:
  return;
}

/******************************************************************************
 * Exported functions
 *****************************************************************************/
res_T
scene_analyze
  (struct senc3d_scene* scn)
{
  /* By triangle tmp data */
  struct darray_triangle_tmp triangles_tmp;
  char triangles_tmp_initialized = 0;
  /* Array of connex components.
   * They are refered to by arrays of ids.  */
  struct darray_ptr_component_descriptor connex_components;
  char connex_components_initialized = 0;
  /* Array of frontiers edges */
  struct darray_frontier_edge frontiers;
  char frontiers_initialized = 0;
  /* Htable used to store overlapping segments */
  struct htable_overlap overlaps;
  char overlaps_initialized = 0;
  /* Store by-triangle components */
  struct darray_triangle_comp triangles_comp;
  char triangles_comp_initialized = 0;
  /* Array of triangle sides. */
  struct trgside* trgsides = NULL;
  struct s3d_scene_view* s3d_view = NULL;
  /* Atomic counters to share beetwen threads */
  ATOMIC component_count = 0;
  ATOMIC next_enclosure_id = 1;
  res_T res = RES_OK;
  res_T res2 = RES_OK;

  if(!scn) return RES_BAD_ARG;

  if(!scn->ntris) goto exit;

  darray_triangle_tmp_init(scn->dev->allocator, &triangles_tmp);
  triangles_tmp_initialized = 1;
  darray_frontier_edge_init(scn->dev->allocator, &frontiers);
  frontiers_initialized = 1;
  htable_overlap_init(scn->dev->allocator, &overlaps);
  overlaps_initialized = 1;

  OK(darray_triangle_tmp_resize(&triangles_tmp, scn->ntris));
  trgsides
    = MEM_CALLOC(scn->dev->allocator, 2 * scn->ntris, sizeof(struct trgside));
  if(!trgsides) {
    res = RES_MEM_ERR;
    goto error;
  }
#ifndef NDEBUG
  else {
    /* Initialise trgsides to allow assert code */
    size_t i;
    FOR_EACH(i, 0, 2 * scn->ntris)
      init_trgside(scn->dev->allocator, trgsides + i);
  }
#endif

  /* The end of the analyze is multithreaded */
  ASSERT(scn->dev->nthreads > 0);
  #pragma omp parallel num_threads(scn->dev->nthreads)
  {
    /* Step 1: build neighbourhoods */
    collect_and_link_neighbours(scn, trgsides, &triangles_tmp, &frontiers,
      &overlaps, &res);
    /* No barrier at the end of step 1: data used in step 1 cannot be
     * released / data produced by step 1 cannot be used
     * until next sync point */

    /* The first thread here allocates some data.
     * Barrier needed at the end to ensure data created before any use. */
    #pragma omp single
    {
      res_T tmp_res = RES_OK;
      darray_ptr_component_descriptor_init(scn->dev->allocator,
        &connex_components);
      connex_components_initialized = 1;
      /* Just a hint; to limit contention */
      OK2(darray_ptr_component_descriptor_reserve(&connex_components,
        2 * darray_side_range_size_get(&scn->media_use)));
      darray_triangle_comp_init(scn->dev->allocator, &triangles_comp);
      triangles_comp_initialized = 1;
      OK2(darray_triangle_comp_resize(&triangles_comp, scn->ntris));
    tmp_error:
      if(tmp_res != RES_OK) res2 = tmp_res;
    }
    /* Implicit barrier here: constraints on step 1 data are now met */

#pragma omp single
    {
      /* Save all the overlapping segments in a darray */
      res_T tmp_res = RES_OK;
      struct htable_overlap_iterator it, end;
      ASSERT(overlaps_initialized);
      htable_overlap_begin(&overlaps, &it);
      htable_overlap_end(&overlaps, &end);
      tmp_res = darray_trg_id_reserve(&scn->analyze.overlapping_ids,
        htable_overlap_size_get(&overlaps));
      if(tmp_res != RES_OK) goto tmp_error2;
      while(!htable_overlap_iterator_eq(&it, &end)) {
        tmp_res = darray_trg_id_push_back(&scn->analyze.overlapping_ids,
          htable_overlap_iterator_key_get(&it));
        if(tmp_res != RES_OK) goto tmp_error2;
        htable_overlap_iterator_next(&it);
      }
      /* Sort overlapping triangle ids */
      qsort(darray_trg_id_data_get(&scn->analyze.overlapping_ids),
        darray_trg_id_size_get(&scn->analyze.overlapping_ids),
        sizeof(*darray_trg_id_cdata_get(&scn->analyze.overlapping_ids)),
        cmp_trg_id);
      htable_overlap_release(&overlaps);
      overlaps_initialized = 0;
    tmp_error2:
      if(tmp_res != RES_OK) res2 = tmp_res;
    }

    if(darray_trg_id_size_get(&scn->analyze.overlapping_ids)) {
      /* Stop analysis here as the model is ill-formed */
      goto end_;
    }

    if(res != RES_OK || res2 != RES_OK) {
      #pragma omp single nowait
      {
        if(res != RES_OK) {
          log_err(scn->dev,
            LIB_NAME":%s: could not build neighbourhoods from scene\n",
            FUNC_NAME);
        } else {
          res = res2;
        }
      }
      goto error_;
    }

    /* Step 2: extract triangle connex components */
    extract_connex_components(scn, trgsides, &connex_components,
      &triangles_tmp, &triangles_comp, &s3d_view, &component_count, &res);
    /* No barrier at the end of step 2: data used in step 2 cannot be
     * released / data produced by step 2 cannot be used
     * until next sync point */

    #pragma omp barrier
    /* Constraints on step 2 data are now met */

    if(res != RES_OK) {
      #pragma omp single nowait
      {
        log_err(scn->dev,
          LIB_NAME":%s: could not extract connex components from scene\n",
          FUNC_NAME);
      } /* No barrier here */
      goto error_;
    }

    /* One thread releases some data before going to step 3,
     * the others go to step 3 without sync */
    #pragma omp single nowait
    {
      darray_triangle_tmp_release(&triangles_tmp);
      triangles_tmp_initialized = 0;
    } /* No barrier here */

    /* Step 3: group components */
    group_connex_components(scn, &triangles_comp, &connex_components, s3d_view,
      &next_enclosure_id, &res);
    /* Barrier at the end of step 3: data used in step 3 can be released /
     * data produced by step 3 can be used */

    if(res != RES_OK) {
      #pragma omp single nowait
      {
        log_err(scn->dev,
          LIB_NAME":%s: could not group connex components from scene\n",
          FUNC_NAME);
      }
      goto error_;
    }

    /* One thread releases some data and allocate other data before going to
     * step 4, the others waiting for alloced data */
    #pragma omp single
    {
      if(s3d_view) S3D(scene_view_ref_put(s3d_view));
      s3d_view = NULL;
    } /* Implicit barrier here */
    if(res != RES_OK) goto error_;

    /* Step 4: Build result */
    build_result(scn, &connex_components, &triangles_comp, &frontiers, &res);
    /* No barrier at the end of step 4: data used in step 4 cannot be
     * released / data produced by step 4 cannot be used
     * until next sync point */

    #pragma omp barrier
    /* Constraints on step 4 data are now met */

    if(res != RES_OK) {
      #pragma omp single nowait
      {
        log_err(scn->dev,
          LIB_NAME":%s: could not build result\n", FUNC_NAME);
      }
      goto error_;
    }

    /* Some threads release data */
    #pragma omp sections nowait
    {
      #pragma omp section
      {
        custom_darray_ptr_component_descriptor_release(&connex_components);
        connex_components_initialized = 0;
      }
      #pragma omp section
      {
        darray_triangle_comp_release(&triangles_comp);
        triangles_comp_initialized = 0;
      }
    } /* No barrier here */

end_:
error_:
    ;
  } /* Implicit barrier here */

  if(res != RES_OK) goto error;
exit:
  if(connex_components_initialized)
    custom_darray_ptr_component_descriptor_release(&connex_components);
  if(s3d_view) S3D(scene_view_ref_put(s3d_view));
  if(triangles_tmp_initialized) darray_triangle_tmp_release(&triangles_tmp);
  if(triangles_comp_initialized) darray_triangle_comp_release(&triangles_comp);
  if(frontiers_initialized) darray_frontier_edge_release(&frontiers);
  if(overlaps_initialized) htable_overlap_release(&overlaps);
  if(trgsides) MEM_RM(scn->dev->allocator, trgsides);

  return res;
error:
  goto exit;
}
