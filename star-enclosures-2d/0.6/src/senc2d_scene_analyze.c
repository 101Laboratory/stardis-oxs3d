/* Copyright (C) 2018-2021, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#include "senc2d.h"
#include "senc2d_device_c.h"
#include "senc2d_scene_c.h"
#include "senc2d_enclosure_data.h"
#include "senc2d_scene_analyze_c.h"
#include "senc2d_internal_types.h"

#include <rsys/rsys.h>
#include <rsys/float2.h>
#include <rsys/double22.h>
#include<rsys/mem_allocator.h>
#include <rsys/hash_table.h>
#include <rsys/dynamic_array.h>
#include <rsys/dynamic_array_uchar.h>
#include <rsys/clock_time.h>

#define _USE_MATH_DEFINES
#include <rsys_math.h>

/* Explicit declaration for math functions not in rsys/math.h */
#ifdef _MSC_VER
double atan2(double, double);
#endif

#include <star/s2d.h>

#include <omp.h>
#include <limits.h>
#include <stdlib.h>

#define CC_DESCRIPTOR_NULL__ {\
   0, 0, INT_MAX, VRTX_NULL__, 0,\
   CC_ID_NONE, CC_GROUP_ROOT_NONE, ENCLOSURE_NULL__,\
   { SEG_NULL__, 0},\
   NULL\
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

#define HTABLE_NAME overlap
#define HTABLE_KEY seg_id_t
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

static side_id_t
get_side_not_in_connex_component
  (const side_id_t last_side,
   const struct segside* segsides,
   const uchar* processed,
   side_id_t* first_side_not_in_component,
   const medium_id_t medium)
{
  ASSERT(segsides && processed && first_side_not_in_component);
  {
    side_id_t i = *first_side_not_in_component;
    while(i <= last_side
      && (segsides[i].medium != medium
        || (processed[SEGSIDE_2_SEG(i)] & SEGSIDE_2_SIDEFLAG(i))))
      ++i;

    *first_side_not_in_component = i + 1;
    if(i > last_side) return SIDE_NULL__;
    return i;
  }
}

/* Here unsigned are required by s2d API */
static void
get_scn_indices(const unsigned iseg, unsigned ids[2], void* ctx) {
  int i;
  const struct senc2d_scene* scene = ctx;
  const struct segment_in* seg =
    darray_segment_in_cdata_get(&scene->segments_in) + iseg;
  FOR_EACH(i, 0, 2) {
    ASSERT(seg->vertice_id[i] < scene->nverts);
    ASSERT(seg->vertice_id[i] <= UINT_MAX);
    ids[i] = (unsigned)seg->vertice_id[i]; /* Back to s2d API type */
  }
}

/* Here unsigned are required by s2d API */
static void
get_scn_position(const unsigned ivert, float pos[2], void* ctx) {
  const struct senc2d_scene* scene = ctx;
  const union double2* pt =
    darray_position_cdata_get(&scene->vertices) + ivert;
  f2_set_d2(pos, pt->vec);
}

static int
self_hit_filter
  (const struct s2d_hit* hit,
   const float ray_org[2],
   const float ray_dir[2],
   const float ray_range[2],
   void* ray_data,
   void* filter_data)
{
  const struct darray_segment_comp* segments_comp = filter_data;
  const component_id_t* origin_component = ray_data;
  const struct segment_comp* hit_seg_comp;

  (void)ray_org; (void)ray_dir; (void)ray_range;
  ASSERT(hit && segments_comp && origin_component);
  ASSERT(hit->prim.prim_id < darray_segment_comp_size_get(segments_comp));
  hit_seg_comp = darray_segment_comp_cdata_get(segments_comp)
    + hit->prim.prim_id;
  return (hit_seg_comp->component[SENC2D_FRONT] == *origin_component
    || hit_seg_comp->component[SENC2D_BACK] == *origin_component);
}

static void
extract_connex_components
  (struct senc2d_scene* scn,
   struct segside* segsides,
   struct darray_ptr_component_descriptor* connex_components,
   const struct darray_segment_tmp* segments_tmp_array,
   struct darray_segment_comp* segments_comp_array,
   struct s2d_scene_view** s2d_view,
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
  const union double2* positions;
  const struct segment_tmp* segments_tmp;
  struct segment_comp* segments_comp;
  /* An array to flag sides when processed */
  uchar* processed;
  /* An array to store the component being processed */
  struct darray_side_id current_component;
  /* A bool array to store media of the component being processed */
  uchar* current_media = NULL;
  size_t sz, ii;

  ASSERT(scn && segsides && connex_components && segments_tmp_array
    && segments_comp_array && s2d_view && component_count && p_res);
  alloc = scn->dev->allocator;
  positions = darray_position_cdata_get(&scn->vertices);
  segments_tmp = darray_segment_tmp_cdata_get(segments_tmp_array);
  segments_comp = darray_segment_comp_data_get(segments_comp_array);
  darray_side_id_init(alloc, &stack);
  darray_side_id_init(alloc, &current_component);
  processed = MEM_CALLOC(alloc, scn->nsegs, sizeof(uchar));
  if(!processed) {
    *p_res = RES_MEM_ERR;
    return;
  }

#ifndef NDEBUG
  #pragma omp single
  {
    seg_id_t s_;
    int s;
    ASSERT(darray_ptr_component_descriptor_size_get(connex_components) == 0);
    FOR_EACH(s_, 0, scn->nsegs) {
      const struct segment_in* seg_in =
        darray_segment_in_cdata_get(&scn->segments_in) + s_;
      const struct side_range* media_use
        = darray_side_range_cdata_get(&scn->media_use);
      FOR_EACH(s, 0, 2) {
        const side_id_t side = SEGIDxSIDE_2_SEGSIDE(s_, s);
        medium_id_t medium = seg_in->medium[s];
        m_idx = medium_id_2_medium_idx(medium);
        ASSERT(media_use[m_idx].first <= side && side
          <= media_use[m_idx].last);
      }
    }
  } /* Implicit barrier here */
#endif

  /* We loop on sides to build connex components. */
  #pragma omp for schedule(dynamic) nowait
  /* Process all media, including undef */
  for(m_idx = 0; m_idx < 1 + (int64_t)scn->next_medium_idx; m_idx++) {
    const medium_id_t medium = medium_idx_2_medium_id(m_idx);
    /* media_use 0 is for SENC2D_UNSPECIFIED_MEDIUM, n+1 is for n */
    const struct side_range* media_use =
      darray_side_range_cdata_get(&scn->media_use) + m_idx;
    /* Any not-already-used side is used as a starting point */
    side_id_t first_side_not_in_component = media_use->first;
    double max_ny;
    side_id_t max_ny_side_id;
    const side_id_t last_side = media_use->last;
    int component_canceled = 0, max_y_is_2sided = 0, fst_ny = 1;
    side_id_t cc_start_side_id = SIDE_NULL__;
    side_id_t cc_last_side_id = SIDE_NULL__;
    res_T tmp_res = RES_OK;
    ATOMIC id;

    if(*p_res != RES_OK) continue;
    if(first_side_not_in_component == SIDE_NULL__)
      continue; /* Unused medium */
    ASSERT(first_side_not_in_component < 2 * scn->nsegs);
    ASSERT(darray_side_id_size_get(&stack) == 0);
    ASSERT(darray_side_id_size_get(&current_component) == 0);
    for(;;) { /* Process all components for this medium */
      side_id_t crt_side_id = get_side_not_in_connex_component
        (last_side, segsides, processed, &first_side_not_in_component, medium);
      vrtx_id_t max_y_vrtx_id = VRTX_NULL__;
      struct cc_descriptor *cc;
      double max_y = -DBL_MAX;
      component_canceled = 0;
      ASSERT(crt_side_id == SIDE_NULL__ || crt_side_id < 2 * scn->nsegs);
      darray_side_id_clear(&current_component);

      if(*p_res != RES_OK) break;
      if(crt_side_id == SIDE_NULL__)
        break; /* start_side_id=SIDE_NULL__ => component done! */

      if(cc_start_side_id == SIDE_NULL__) {
        cc_start_side_id = cc_last_side_id = crt_side_id;
      } else {
        cc_start_side_id = MMIN(cc_start_side_id, crt_side_id);
        cc_last_side_id = MMAX(cc_last_side_id, crt_side_id);
      }

#ifndef NDEBUG
      {
        seg_id_t sid = SEGSIDE_2_SEG(crt_side_id);
        enum senc2d_side s = SEGSIDE_2_SIDE(crt_side_id);
        medium_id_t side_med
          = darray_segment_in_data_get(&scn->segments_in)[sid].medium[s];
        ASSERT(side_med == medium);
      }
#endif

      /* Reuse array if possible, or create a new one  */
      if(current_media) {
        /* current_media 0 is for SENC2D_UNSPECIFIED_MEDIUM, n+1 is for n */
        memset(current_media, 0, 1 + scn->next_medium_idx);
      } else {
        current_media = MEM_CALLOC(alloc, 1 + scn->next_medium_idx,
          sizeof(*current_media));
        if(!current_media) {
          *p_res = RES_MEM_ERR;
          continue;
        }
      }
      current_media[m_idx] = 1;
      for(;;) { /* Process all sides of this component */
        int i;
        enum side_flag crt_side_flag = SEGSIDE_2_SIDEFLAG(crt_side_id);
        struct segside* crt_side = segsides + crt_side_id;
        const seg_id_t crt_seg_id = SEGSIDE_2_SEG(crt_side_id);
        uchar* seg_used = processed + crt_seg_id;
        const struct segment_tmp* const seg_tmp = segments_tmp + crt_seg_id;
        ASSERT(crt_seg_id < scn->nsegs);

        if(*p_res != RES_OK) break;

        /* Record max_y information
         * Keep track of the appropriate vertex of the component in order
         * to cast a ray at the component grouping step of the algorithm.
         * The most appropriate vertex is (the) one with the greater Y
         * coordinate. */
        if(max_y < seg_tmp->max_y) {
          const struct segment_in* seg_in =
            darray_segment_in_cdata_get(&scn->segments_in) + crt_seg_id;
          /* New best vertex */
          max_y = seg_tmp->max_y;

          /* Select a vertex with y == max_y */
          FOR_EACH(i, 0, 2) {
            if(max_y == positions[seg_in->vertice_id[i]].pos.y) {
              max_y_vrtx_id = seg_in->vertice_id[i];
              break;
            }
          }
          ASSERT(i < 2); /* Found one */
        }

        /* Record crt_side both as component and segment level */
        if((*seg_used & crt_side_flag) == 0) {
          OK2(darray_side_id_push_back(&current_component, &crt_side_id));
          *seg_used = *seg_used | (uchar)crt_side_flag;
        }

        /* Store neighbour's sides in a waiting stack */
        FOR_EACH(i, 0, 2) {
          side_id_t neighbour_id = crt_side->facing_side_id[i];
          seg_id_t nbour_seg_id = SEGSIDE_2_SEG(neighbour_id);
          enum side_flag nbour_side_id = SEGSIDE_2_SIDEFLAG(neighbour_id);
          uchar* nbour_used = processed + nbour_seg_id;
          const struct segside* neighbour = segsides + neighbour_id;
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
              seg_id_t used_seg_id = SEGSIDE_2_SEG(used_side);
              enum side_flag used_side_flag
                = SEGSIDE_2_SIDEFLAG(used_side);
              uchar* used = processed + used_seg_id;
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
          current_media[nbour_med_idx] = 1;
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

      ASSERT(max_y_vrtx_id != VRTX_NULL__);
      cc_descriptor_init(alloc, cc);
      id = ATOMIC_INCR(component_count) - 1;
      ASSERT(id <= COMPONENT_MAX__);
      cc->cc_id = (component_id_t)id;
      sz = darray_side_id_size_get(&current_component);
      ASSERT(sz > 0 && sz <= SIDE_MAX__);
      cc->side_count = (side_id_t)sz;
      cc->side_range.first = cc_start_side_id;
      cc->side_range.last = cc_last_side_id;
      cc->max_y_vrtx_id = max_y_vrtx_id;
      /* Tranfer ownership of the array to component */
      ASSERT(!cc->media && current_media);
      cc->media = current_media;
      current_media = NULL;

      /* Reset for next component */
      cc_start_side_id = SIDE_NULL__;
      cc_last_side_id = SIDE_NULL__;

      /* Write component membership in the global structure
       * No need for sync here as an unique thread writes a given side */
      {STATIC_ASSERT(sizeof(cc->cc_id) >= 4, Cannot_write_IDs_sync_free);}
      ASSERT(IS_ALIGNED(segments_comp->component, sizeof(cc->cc_id)));
      FOR_EACH(ii, 0, sz) {
        const side_id_t s_id = darray_side_id_cdata_get(&current_component)[ii];
        seg_id_t seg_id = SEGSIDE_2_SEG(s_id);
        enum senc2d_side side = SEGSIDE_2_SIDE(s_id);
        component_id_t* cmp = segments_comp[seg_id].component;
        ASSERT(cmp[side] == COMPONENT_NULL__);
        ASSERT(medium_id_2_medium_idx(segsides[s_id].medium)
          >= medium_id_2_medium_idx(medium));
        cmp[side] = cc->cc_id;
      }

      /* Compute component area and volume, and record information on the
       * max_y side of the component to help find out if the component is
       * inner or outer */
      fst_ny = 1;
      max_ny = 0;
      max_ny_side_id = SIDE_NULL__;
      FOR_EACH(ii, 0, sz) {
        const side_id_t s_id = darray_side_id_cdata_get(&current_component)[ii];
        const seg_id_t seg_id = SEGSIDE_2_SEG(s_id);
        enum senc2d_side side = SEGSIDE_2_SIDE(s_id);
        const struct segment_comp* seg_comp = segments_comp + seg_id;
        const struct segment_tmp* const seg_tmp = segments_tmp + seg_id;
        const struct segment_in* seg_in =
          darray_segment_in_cdata_get(&scn->segments_in) + seg_id;
        const union double2* vertices =
          darray_position_cdata_get(&scn->vertices);
        double edge[2], normal[2], norm;
        const double* v0 = vertices[seg_in->vertice_id[0]].vec;
        const double* v1 = vertices[seg_in->vertice_id[1]].vec;
        int is_2sided = (seg_comp->component[SENC2D_FRONT]
          == seg_comp->component[SENC2D_BACK]);

        /* Compute component area and volume */
        d2_sub(edge, v1, v0);
        d2(normal, -edge[1], +edge[0]);
        norm = d2_normalize(normal, normal);
        ASSERT(norm);
        /* The area is a n dim concept, that in 2D is in m
         * Here the area contribution of the segment is its length */
        cc->area += norm;

        if(!is_2sided) {
          /* Build a triangle whose base is the segment and whose apex is
           * the coordinate system's origin. If the 2 sides of the segment
           * are part of the component, the contribution of the segment
           * must be 0. To achieve this, we just skip both sides */
          double _2t = d2_cross(v0, v1); /* 2 * area of the triangle */

          /* The volume is a n dim concept, that in 2D is in m^2
           * Here the volume contribution of the segment is the area of the
           * triangle v0,v1,O */
          if((seg_comp->component[SENC2D_FRONT] == cc->cc_id)
            == (scn->convention & SENC2D_CONVENTION_NORMAL_FRONT))
            cc->_2volume += _2t;
          else
            cc->_2volume -= _2t;
        }

        ASSERT(seg_comp->component[side] != COMPONENT_NULL__); (void)side;
        if(seg_tmp->max_y == max_y) {
          int i;
          /* Candidate to define the max_ny (segment using max_y_vrtx) */
          FOR_EACH(i, 0, 2) {
            if(cc->max_y_vrtx_id == seg_in->vertice_id[i]) {
              if(fst_ny || fabs(normal[1]) > fabs(max_ny)) {
                max_ny_side_id = s_id;
                max_ny = normal[1];
                max_y_is_2sided = is_2sided;
                fst_ny = 0;
                break;
              }
            }
          }
        }
      }
      ASSERT(!fst_ny);
      /* Determine if this component can be an inner part inside another
       * component (substracting a volume) as only these components will need
       * to search for their possible outer component when grouping
       * components to create enclosures.
       * The inner/outer property comes from the normal orientation of the
       * segment on top of the component, this segment being the one whose
       * |Ny| is maximal. If this segment has its 2 sides in the component,
       * the component is inner */
      if(max_ny == 0 || max_y_is_2sided) cc->is_outer_border = 0;
      else {
        ASSERT(max_ny_side_id != SIDE_NULL__);
        if(SEGSIDE_IS_FRONT(max_ny_side_id)
          == ((scn->convention & SENC2D_CONVENTION_NORMAL_FRONT) != 0)) {
          /* Geom normal points towards the component */
          cc->is_outer_border = (max_ny < 0);
        } else {
          /* Geom normal points away from the component */
          cc->is_outer_border = (max_ny > 0);
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

  /* The first thread here creates the s2d view */
  #pragma omp single nowait
  if(*p_res == RES_OK) {
    res_T res = RES_OK;
    struct s2d_device* s2d = NULL;
    struct s2d_scene* s2d_scn = NULL;
    struct s2d_shape* s2d_shp = NULL;
    struct s2d_vertex_data attribs;

    attribs.type = S2D_FLOAT2;
    attribs.usage = S2D_POSITION;
    attribs.get = get_scn_position;

    /* Put geometry in a 2D view */
    OK(s2d_device_create(scn->dev->logger, alloc, 0, &s2d));
    OK(s2d_scene_create(s2d, &s2d_scn));
    OK(s2d_shape_create_line_segments(s2d, &s2d_shp));

    /* Back to s2d API type for nsegs and nverts */
    ASSERT(scn->nsegs <= UINT_MAX);
    ASSERT(scn->nverts <= UINT_MAX);
    OK(s2d_line_segments_setup_indexed_vertices(s2d_shp,
      (unsigned)scn->nsegs, get_scn_indices,
      (unsigned)scn->nverts, &attribs, 1, scn));
    s2d_line_segments_set_hit_filter_function(s2d_shp, self_hit_filter,
      segments_comp_array);
    OK(s2d_scene_attach_shape(s2d_scn, s2d_shp));
    OK(s2d_scene_view_create(s2d_scn, S2D_TRACE, s2d_view));
  error:
    if(res != RES_OK) *p_res = res;
    if(s2d) S2D(device_ref_put(s2d));
    if(s2d_scn) S2D(scene_ref_put(s2d_scn));
    if(s2d_shp) S2D(shape_ref_put(s2d_shp));
  }

#ifndef NDEBUG
  /* Need to wait for all threads done to be able to check stuff */
  #pragma omp barrier
  if(*p_res != RES_OK) return;
  #pragma omp single
  {
    seg_id_t s_;
    component_id_t c;
    ASSERT(ATOMIC_GET(component_count) ==
      (int)darray_ptr_component_descriptor_size_get(connex_components));
    FOR_EACH(s_, 0, scn->nsegs) {
      struct segment_comp* seg_comp =
        darray_segment_comp_data_get(segments_comp_array) + s_;
      ASSERT(seg_comp->component[SENC2D_FRONT] != COMPONENT_NULL__);
      ASSERT(seg_comp->component[SENC2D_BACK] != COMPONENT_NULL__);
    }
    FOR_EACH(c, 0, (component_id_t)ATOMIC_GET(component_count)) {
      struct cc_descriptor** components =
        darray_ptr_component_descriptor_data_get(connex_components);
      ASSERT(components[c] != NULL && components[c]->cc_id == c);
    }
  } /* Implicit barrier here */
#endif
}

static void
group_connex_components
  (struct senc2d_scene* scn,
   struct darray_segment_comp* segments_comp,
   struct darray_ptr_component_descriptor* connex_components,
   struct s2d_scene_view* s2d_view,
   ATOMIC* next_enclosure_id,
   /* Shared error status.
    * We accept to overwrite an error with a different error */
   res_T* res)
{
  /* This function is called from an omp parallel block and executed
   * concurrently. */
  struct cc_descriptor** descriptors;
  const union double2* positions;
  size_t tmp;
  component_id_t cc_count;
  int64_t ccc;

  ASSERT(scn && segments_comp && connex_components && s2d_view
    && next_enclosure_id && res);
  ASSERT(scn->analyze.enclosures_count == 1);

  descriptors = darray_ptr_component_descriptor_data_get(connex_components);
  tmp = darray_ptr_component_descriptor_size_get(connex_components);
  ASSERT(tmp <= COMPONENT_MAX__);
  cc_count = (component_id_t)tmp;
  positions = darray_position_cdata_get(&scn->vertices);

  /* Cast rays to find links between connex components */
  #pragma omp for schedule(dynamic)
  for(ccc = 0; ccc < (int64_t)cc_count; ccc++) {
    res_T tmp_res = RES_OK;
    component_id_t c = (component_id_t)ccc;
    struct s2d_hit hit = S2D_HIT_NULL;
    float origin[2];
    const float dir[2] = { 0, 1 };
    const float range[2] = { 0, FLT_MAX };
    struct cc_descriptor* const cc = descriptors[c];
    component_id_t self_hit_component = cc->cc_id;
    const double* max_vrtx;

    if(*res != RES_OK) continue;
    ASSERT(cc->cc_id == c);
    ASSERT(cc->cc_group_root == CC_GROUP_ID_NONE);
    ASSERT(cc->max_y_vrtx_id < scn->nverts);

    max_vrtx = positions[cc->max_y_vrtx_id].vec;
    if(cc->is_outer_border) {
      ATOMIC id;
      /* No need to cast a ray */
      cc->cc_group_root = cc->cc_id; /* New group with self as root */
      id = ATOMIC_INCR(next_enclosure_id) - 1;
      ASSERT(id <= ENCLOSURE_MAX__);
      cc->enclosure_id = (enclosure_id_t)id;
      continue;
    }

    f2_set_d2(origin, max_vrtx);
    /* Self-hit data: self hit if hit this component "on the other side" */
    tmp_res = s2d_scene_view_trace_ray(s2d_view, origin, dir, range,
      &self_hit_component, &hit);
    if(tmp_res != RES_OK) {
      *res = tmp_res;
      continue;
    }
    /* If no hit, the component is facing an infinite medium */
    if(S2D_HIT_NONE(&hit)) {
      cc->cc_group_root = CC_GROUP_ROOT_INFINITE;
      cc->enclosure_id = 0;
    } else {
      /* If hit, group this component */
      const seg_id_t hit_seg_id = (seg_id_t)hit.prim.prim_id;
      const struct segment_comp* hit_seg_comp =
        darray_segment_comp_cdata_get(segments_comp) + hit_seg_id;
      enum senc2d_side hit_side =
        ((hit.normal[1] < 0) /* Facing geometrical normal of hit */
          == ((scn->convention & SENC2D_CONVENTION_NORMAL_FRONT) != 0))
        /* Warning: following Embree 2 convention for geometrical normals, 
         * the Star2D hit normal is left-handed while star-enclosure uses
         * right-handed convention */
        ? SENC2D_BACK : SENC2D_FRONT;
      ASSERT(hit.normal[1] != 0);
      ASSERT(hit_seg_id < scn->nsegs);

      /* Not really the root until following links */
      cc->cc_group_root = hit_seg_comp->component[hit_side];
      ASSERT(cc->cc_group_root < cc_count);
    }
  }
  /* Implicit barrier here */
  if(*res != RES_OK) return;

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
          scn->next_medium_idx + 1);
        if(tmp_res != RES_OK) {
          *res = tmp_res;
          break;
        }
      }
    }
  }
  /* Implicit barrier here */
}

static void
collect_and_link_neighbours
  (struct senc2d_scene* scn,
   struct segside* segsides,
   struct darray_segment_tmp* segments_tmp_array,
   struct darray_frontier_vertex* frontiers,
   struct htable_overlap* overlaps,
   /* Shared error status.
    * We accept to overwrite an error with a different error */
   res_T* res)
{
  /* This function is called from an omp parallel block and executed
   * concurrently. */
  const struct segment_in* segments_in;
  struct segment_tmp* segments_tmp;
  const union double2* vertices;
  const int thread_count = omp_get_num_threads();
  const int rank = omp_get_thread_num();
  const int front = ((scn->convention & SENC2D_CONVENTION_NORMAL_FRONT) != 0);
  /* Array to keep neighbourhood of vertices
   * Resize/Push operations on neighbourhood_by_vertex are valid in the
   * openmp block because a given neighbourhood is only processed
   * by a single thread */
  struct darray_neighbourhood neighbourhood_by_vertex;
  vrtx_id_t v;
  seg_id_t s;
  res_T tmp_res;

  ASSERT(scn && segsides && segments_tmp_array && frontiers && res);
  ASSERT((size_t)scn->nverts + (size_t)scn->nsegs + 2 <= EDGE_MAX__);

  darray_neighbourhood_init(scn->dev->allocator, &neighbourhood_by_vertex);
  OK2(darray_neighbourhood_resize(&neighbourhood_by_vertex, scn->nverts));

  segments_in = darray_segment_in_cdata_get(&scn->segments_in);
  segments_tmp = darray_segment_tmp_data_get(segments_tmp_array);
  vertices = darray_position_cdata_get(&scn->vertices);

  ASSERT(scn->nsegs == darray_segment_tmp_size_get(segments_tmp_array));

  /* Loop on segments to collect edges' neighbours.
   * All threads considering all the vertices and processing some */
  FOR_EACH(s, 0, scn->nsegs) {
    uchar vv;
    FOR_EACH(vv, 0, 2) {
      struct darray_neighbour* neighbourhood;
      struct neighbour_info* info;
      const vrtx_id_t vertex = segments_in[s].vertice_id[vv];
      size_t sz;
      /* Process only "my" vertices! */
      if((int64_t)vertex % thread_count != rank) continue;
      /* Find neighbourhood */
      ASSERT(vertex < darray_neighbourhood_size_get(&neighbourhood_by_vertex));
      neighbourhood =
        darray_neighbourhood_data_get(&neighbourhood_by_vertex) + vertex;
      sz = darray_neighbour_size_get(neighbourhood);
      /* Make room for this neighbour */
      if(darray_neighbour_capacity(neighbourhood) == sz) {
        /* 2 seems to be a good guess for initial capacity */
        size_t new_sz = sz ? sz + 1 : 2;
        tmp_res = darray_neighbour_reserve(neighbourhood, new_sz);
        if(tmp_res != RES_OK) {
          *res = tmp_res;
          return;
        }
      }
      tmp_res = darray_neighbour_resize(neighbourhood, 1 + sz);
      if(tmp_res != RES_OK) {
        *res = tmp_res;
        return;
      }
      /* Add neighbour info to vertex's neighbour list */
      info = darray_neighbour_data_get(neighbourhood) + sz;
      info->seg_id = s;
      info->common_vertex_rank = vv;
    }
  }
  /* When a thread has build his share of neighbourhoods
   * it can process them whithout waiting for other threads
   * (no barrier needed here). */

  if(*res != RES_OK) return;

  /* For each of "my" vertices sort segments sides by rotation angle
   * and connect neighbours.
   * All threads considering all the vertices and processing some */
  FOR_EACH(v, 0, scn->nverts) {
    const vrtx_id_t common_vrtx = v;
    vrtx_id_t other_vrtx;
    struct darray_neighbour* neighbourhood;
    side_id_t i, neighbour_count;
    double a;
    size_t sz;
    /* Process only "my" neighbourhoods! */
    if((int64_t)v % thread_count != rank) continue;
    neighbourhood
      = darray_neighbourhood_data_get(&neighbourhood_by_vertex) + v;
    sz = darray_neighbour_size_get(neighbourhood);
    /* sz can be 0 as a vertex can be unused */
    if(!sz) continue;
    ASSERT(sz <= SIDE_MAX__);
    neighbour_count = (side_id_t)sz;
    FOR_EACH(i, 0, neighbour_count) {
      double max_y, disp[2];
      struct neighbour_info* neighbour_info
        = darray_neighbour_data_get(neighbourhood) + i;
      const struct segment_in* seg_in = segments_in + neighbour_info->seg_id;
      struct segment_tmp* neighbour = segments_tmp + neighbour_info->seg_id;
      union double2 n; /* Geometrical normal to neighbour segment */
      const int is_reversed = neighbour_info->common_vertex_rank;

      other_vrtx =
        seg_in->vertice_id[(neighbour_info->common_vertex_rank + 1) % 2];
      max_y = MMAX(vertices[other_vrtx].pos.y, vertices[common_vrtx].pos.y);
      ASSERT(neighbour->max_y <= max_y);
      neighbour->max_y = max_y;
      /* Compute rotation angle around common vertex (in world system) */
      d2_sub(disp, vertices[other_vrtx].vec, vertices[common_vrtx].vec);
      ASSERT(disp[0] || disp[1]);
      neighbour_info->angle = atan2(disp[1], disp[0]); /* in ]-pi + pi]*/
      if(is_reversed)
        d2(n.vec, +disp[1], -disp[0]);
      else
        d2(n.vec, -disp[1], +disp[0]);

      /* Normal orientation calculation. */
      if(neighbour_info->angle > 3 * PI / 4) {
        /* DEBUG: Check if n.pos.y is actually zero */
        if(n.pos.y == 0.0) {
          fprintf(stderr, "DEBUG: n.pos.y=0, angle=%f, disp=[%f,%f], is_reversed=%d\n", 
                  neighbour_info->angle, disp[0], disp[1], is_reversed);
        }
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
    /* Sort segments by rotation angle */
    qsort(darray_neighbour_data_get(neighbourhood), neighbour_count,
      sizeof(struct neighbour_info), neighbour_cmp);
    /* Link sides.
     * Create cycles of sides by neighbourhood around common vertex. */
    a = -DBL_MAX;
    FOR_EACH(i, 0, neighbour_count) {
      /* Neighbourhood info for current pair of segments */
      const struct neighbour_info* current
        = darray_neighbour_cdata_get(neighbourhood) + i;
      const struct neighbour_info* ccw_neighbour
        = darray_neighbour_cdata_get(neighbourhood) + (i + 1) % neighbour_count;
      /* Rank of the end of interest in segments */
      const uchar crt_end = current->common_vertex_rank;
      /* Here ccw refers to the rotation around the common vertex
       * and has nothing to do with vertices order in segment definition
       * nor Front/Back side convention */
      const uchar ccw_end = ccw_neighbour->common_vertex_rank;
      /* User id of current segments */
      const seg_id_t crt_id = current->seg_id;
      const seg_id_t ccw_id = ccw_neighbour->seg_id;
      /* Facing sides of segments */
      const enum senc2d_side crt_side
        = (current->normal_toward_next_neighbour == front)
          ? SENC2D_FRONT : SENC2D_BACK;
      const enum senc2d_side ccw_side
        = (ccw_neighbour->normal_toward_next_neighbour == front)
          ? SENC2D_BACK : SENC2D_FRONT;
      /* Index of sides in segsides */
      const side_id_t crt_side_idx = SEGIDxSIDE_2_SEGSIDE(crt_id, crt_side);
      const side_id_t ccw_side_idx = SEGIDxSIDE_2_SEGSIDE(ccw_id, ccw_side);
      /* Side ptrs */
      struct segside* const p_crt_side = segsides + crt_side_idx;
      struct segside* const p_ccw_side = segsides + ccw_side_idx;
      /* Check that angle is a discriminant property */
      ASSERT(a <= current->angle); /* Is sorted */
      if(a == current->angle) {
        /* Two consecutive segments with same angle! Store them */
        const struct neighbour_info* previous;
        seg_id_t prev_id;
        previous = darray_neighbour_cdata_get(neighbourhood) + i - 1;
        prev_id = previous->seg_id;
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
      ASSERT(p_crt_side->facing_side_id[crt_end] == SIDE_NULL__);
      ASSERT(p_ccw_side->facing_side_id[ccw_end] == SIDE_NULL__);
      p_crt_side->facing_side_id[crt_end] = ccw_side_idx;
      p_ccw_side->facing_side_id[ccw_end] = crt_side_idx;
      /* Record media  */
      ASSERT(p_crt_side->medium == MEDIUM_NULL__
        || p_crt_side->medium == segments_in[crt_id].medium[crt_side]);
      ASSERT(p_ccw_side->medium == MEDIUM_NULL__
        || p_ccw_side->medium == segments_in[ccw_id].medium[ccw_side]);
      p_crt_side->medium = segments_in[crt_id].medium[crt_side];
      p_ccw_side->medium = segments_in[ccw_id].medium[ccw_side];
      ASSERT(p_crt_side->medium == SENC2D_UNSPECIFIED_MEDIUM
        || p_crt_side->medium < scn->next_medium_idx);
      ASSERT(p_ccw_side->medium == SENC2D_UNSPECIFIED_MEDIUM
        || p_ccw_side->medium < scn->next_medium_idx);
      /* Detect segments that could surround a hole:
       * - single segment on (one of) its end
       * - different media on its sides */
      if(neighbour_count == 1
        && p_crt_side->medium != p_ccw_side->medium)
      #pragma omp critical
      {
        struct frontier_vertex frontier_vertex;
        frontier_vertex.seg = crt_id;
        frontier_vertex.vrtx = v;
        darray_frontier_vertex_push_back(frontiers, &frontier_vertex);
      }
    }
  }

tmp_error:
  if(tmp_res != RES_OK) *res = tmp_res;
  /* Threads are allowed to return whitout sync. */
  darray_neighbourhood_release(&neighbourhood_by_vertex);
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

static void
build_result
  (struct senc2d_scene* scn,
   const struct darray_ptr_component_descriptor* connex_components,
   const struct darray_segment_comp* segments_comp_array,
   struct darray_frontier_vertex* frontiers,
   enclosure_id_t* ordered_ids,
   /* Shared error status.
    * We accept to overwrite an error with a different error */
   res_T* res)
{
  /* This function is called from an omp parallel block and executed
   * concurrently. */
  struct mem_allocator* alloc;
  struct cc_descriptor* const* cc_descriptors;
  struct enclosure_data* enclosures;
  const struct segment_in* segments_in;
  struct segment_enc* segments_enc;
  const struct segment_comp* segments_comp;
  struct htable_vrtx_id vtable;
  int output_normal_in, normals_front, normals_back;
  size_t cc_count;
  int64_t sg;
  int64_t ee;

  ASSERT(scn && connex_components && segments_comp_array && frontiers && res);

  alloc = scn->dev->allocator;
  output_normal_in = (scn->convention & SENC2D_CONVENTION_NORMAL_INSIDE) != 0;
  normals_front = (scn->convention & SENC2D_CONVENTION_NORMAL_FRONT) != 0;
  normals_back = (scn->convention & SENC2D_CONVENTION_NORMAL_BACK) != 0;
  ASSERT(normals_back != normals_front);
  ASSERT(output_normal_in
    != ((scn->convention & SENC2D_CONVENTION_NORMAL_OUTSIDE) != 0));
  cc_count = darray_ptr_component_descriptor_size_get(connex_components);
  ASSERT(cc_count <= COMPONENT_MAX__);
  cc_descriptors = darray_ptr_component_descriptor_cdata_get(connex_components);
  enclosures = darray_enclosure_data_get(&scn->analyze.enclosures);
  segments_in = darray_segment_in_cdata_get(&scn->segments_in);
  segments_comp = darray_segment_comp_cdata_get(segments_comp_array);

  #pragma omp single
  {
    enclosure_id_t e;
    size_t d;
    res_T tmp_res =
      darray_segment_enc_resize(&scn->analyze.segments_enc, scn->nsegs);
    if(tmp_res != RES_OK) {
      *res = tmp_res;
      goto single_err;
    }
    /* Store initial enclosure order */
    FOR_EACH(e, 0, scn->analyze.enclosures_count)
      enclosures[e].header.enclosure_id = e;
    /* Move enclosures by first side while keeping enclosure 0
     * at rank 0 (its a convention) */
    qsort(enclosures + 1, scn->analyze.enclosures_count - 1,
      sizeof(*enclosures), compare_enclosures);
    /* Make conversion table */
    FOR_EACH(e, 0, scn->analyze.enclosures_count) {
      enclosure_id_t rank = enclosures[e].header.enclosure_id;
      ordered_ids[rank] = e;
    }
    FOR_EACH(d, 0, cc_count) {
      enclosure_id_t new_id = ordered_ids[cc_descriptors[d]->enclosure_id];
      /* Update the enclosure ID in cc_descriptor */
      cc_descriptors[d]->enclosure_id = new_id;
      /* Sum up areas and volumes of components int oenclosures */
      enclosures[new_id].header.area += cc_descriptors[d]->area;
      enclosures[new_id].header.volume += cc_descriptors[d]->_2volume / 2;
    }
  single_err: (void)0;
  }/* Implicit barrier here. */
  if(*res != RES_OK) goto exit;
  segments_enc = darray_segment_enc_data_get(&scn->analyze.segments_enc);

  /* Build global enclosure information */
  #pragma omp for
  for(sg = 0; sg < (int64_t)scn->nsegs; sg++) {
    seg_id_t s = (seg_id_t)sg;
    const component_id_t cf_id = segments_comp[s].component[SENC2D_FRONT];
    const component_id_t cb_id = segments_comp[s].component[SENC2D_BACK];
    const struct cc_descriptor* cf = cc_descriptors[cf_id];
    const struct cc_descriptor* cb = cc_descriptors[cb_id];
    const enclosure_id_t ef_id = cf->enclosure_id;
    const enclosure_id_t eb_id = cb->enclosure_id;
    ASSERT(segments_enc[s].enclosure[SENC2D_FRONT] == ENCLOSURE_NULL__);
    segments_enc[s].enclosure[SENC2D_FRONT] = ef_id;
    ASSERT(segments_enc[s].enclosure[SENC2D_BACK] == ENCLOSURE_NULL__);
    segments_enc[s].enclosure[SENC2D_BACK] = eb_id;
  }
  /* Implicit barrier here */

  /* Resize/push operations on enclosure's fields are valid in the
   * openmp block as a given enclosure is processed by a single thread */
  htable_vrtx_id_init(alloc, &vtable);

  ASSERT(scn->analyze.enclosures_count <= ENCLOSURE_MAX__);
  #pragma omp for schedule(dynamic) nowait
  for(ee = 0; ee < (int64_t)scn->analyze.enclosures_count; ee++) {
    const enclosure_id_t e = (enclosure_id_t)ee;
    struct enclosure_data* enc = enclosures + e;
    seg_id_t fst_idx = 0;
    seg_id_t sgd_idx = enc->side_count;
    seg_id_t s;
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

    ASSERT(enc->header.enclosed_media_count < 1 + scn->next_medium_idx);
    OK2(bool_array_of_media_to_darray_media
      (&enc->enclosed_media, &enc->tmp_enclosed_media, scn->next_medium_idx));
    ASSERT(darray_media_size_get(&enc->enclosed_media) <= MEDIUM_MAX__);
    enc->header.enclosed_media_count
      = (medium_id_t)darray_media_size_get(&enc->enclosed_media);
    darray_uchar_purge(&enc->tmp_enclosed_media);

    /* Add this enclosure in relevant by-medium lists */
    FOR_EACH(m, 0, enc->header.enclosed_media_count) {
      medium_id_t medium = darray_media_cdata_get(&enc->enclosed_media)[m];
      size_t m_idx = medium_id_2_medium_idx(medium);
      struct darray_enc_id* enc_ids_array_by_medium;
      ASSERT(medium == SENC2D_UNSPECIFIED_MEDIUM || medium < scn->next_medium_idx);
      ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
        == 1 + scn->next_medium_idx);
      enc_ids_array_by_medium =
        darray_enc_ids_array_data_get(&scn->analyze.enc_ids_array_by_medium) + m_idx;
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
    /* Size is just a int */
    OK2(darray_vrtx_id_reserve(&enc->vertices,
      (size_t)(enc->side_count * 0.6)));
    /* New vertex numbering scheme local to the enclosure */
    htable_vrtx_id_clear(&vtable);
    ASSERT(scn->nsegs == darray_segment_in_size_get(&scn->segments_in));
    /* Put at the end the back-faces of segments that also have their
     * front-face in the list. */
    for(s = SEGSIDE_2_SEG(enc->side_range.first);
      s <= SEGSIDE_2_SEG(enc->side_range.last);
      s++)
    {
      const struct segment_in* seg_in = segments_in + s;
      struct side_enc* side_enc;
      vrtx_id_t vertice_id[2];
      int i;
      if(segments_enc[s].enclosure[SENC2D_FRONT] != e
        && segments_enc[s].enclosure[SENC2D_BACK] != e)
        continue;
      ++enc->header.unique_primitives_count;

      FOR_EACH(i, 0, 2) {
        vrtx_id_t* p_id = htable_vrtx_id_find(&vtable, seg_in->vertice_id + i);
        if(p_id) {
          vertice_id[i] = *p_id; /* Known vertex */
        } else {
          /* Create new association */
          size_t tmp = htable_vrtx_id_size_get(&vtable);
          ASSERT(tmp == darray_vrtx_id_size_get(&enc->vertices));
          ASSERT(tmp < VRTX_MAX__);
          vertice_id[i] = (vrtx_id_t)tmp;
          OK2(htable_vrtx_id_set(&vtable, seg_in->vertice_id + i,
            vertice_id + i));
          OK2(darray_vrtx_id_push_back(&enc->vertices, seg_in->vertice_id + i));
          ++enc->header.vertices_count;
        }
      }
      ASSERT(segments_enc[s].enclosure[SENC2D_FRONT] == e
        || segments_enc[s].enclosure[SENC2D_BACK] == e);
      if(segments_enc[s].enclosure[SENC2D_FRONT] == e) {
        /* Front side of the original segment is member of the enclosure */
        int input_normal_in = normals_front;
        int revert_segment = (input_normal_in != output_normal_in);
        ++enc->header.primitives_count;
        side_enc = darray_sides_enc_data_get(&enc->sides) + fst_idx++;
        FOR_EACH(i, 0, 2) {
          int ii = revert_segment ? 1 - i : i;
          side_enc->vertice_id[i] = vertice_id[ii];
        }
        side_enc->side_id = SEGIDxSIDE_2_SEGSIDE(s, SENC2D_FRONT);
      }
      if(segments_enc[s].enclosure[SENC2D_BACK] == e) {
        /* Back side of the original segment is member of the enclosure */
        int input_normal_in = normals_back;
        int revert_segment = (input_normal_in != output_normal_in);
        ++enc->header.primitives_count;
        /* If both sides are in the enclosure, put the second side at the end */
        side_enc = darray_sides_enc_data_get(&enc->sides) +
          ((segments_enc[s].enclosure[SENC2D_FRONT] == e) ? --sgd_idx : fst_idx++);
        FOR_EACH(i, 0, 2) {
          int ii = revert_segment ? 1 - i : i;
          side_enc->vertice_id[i] = vertice_id[ii];
        }
        side_enc->side_id = SEGIDxSIDE_2_SEGSIDE(s, SENC2D_BACK);
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
  darray_frontier_vertex_copy_and_clear(&scn->analyze.frontiers, frontiers);
  /* No barrier here */
exit:
  return;
}

/******************************************************************************
 * Exported functions
 *****************************************************************************/
res_T
scene_analyze
  (struct senc2d_scene* scn)
{
  /* By segment tmp data */
  struct darray_segment_tmp segments_tmp;
  char segments_tmp_initialized = 0;
  /* Array of connex components.
   * They are refered to by arrays of ids.  */
  struct darray_ptr_component_descriptor connex_components;
  char connex_components_initialized = 0;
  /* Array of frontier vertices */
  struct darray_frontier_vertex frontiers;
  char frontiers_initialized = 0;
  /* Htable used to store overlapping segments */
  struct htable_overlap overlaps;
  char overlaps_initialized = 0;
  /* Store by-segment components */
  struct darray_segment_comp segments_comp;
  char segments_comp_initialized = 0;
  /* Array of segment ends. */
  struct segside* segsides = NULL;
  struct s2d_scene_view* s2d_view = NULL;
  /* Atomic counters to share beetwen threads */
  ATOMIC component_count = 0;
  ATOMIC next_enclosure_id = 1;
  enclosure_id_t* ordered_ids = NULL;
  res_T res = RES_OK;
  res_T res2 = RES_OK;
  
  if(!scn) return RES_BAD_ARG;

  if(!scn->nsegs) goto exit;

  darray_segment_tmp_init(scn->dev->allocator, &segments_tmp);
  segments_tmp_initialized = 1;
  darray_frontier_vertex_init(scn->dev->allocator, &frontiers);
  frontiers_initialized = 1;
  htable_overlap_init(scn->dev->allocator, &overlaps);
  overlaps_initialized = 1;

  OK(darray_segment_tmp_resize(&segments_tmp, scn->nsegs));
  segsides
    = MEM_CALLOC(scn->dev->allocator, 2 * scn->nsegs, sizeof(struct segside));
  if(!segsides) {
    res = RES_MEM_ERR;
    goto error;
  }
#ifndef NDEBUG
  else {
    /* Initialise segsides to allow assert code */
    size_t i;
    FOR_EACH(i, 0, 2 * scn->nsegs)
      init_segside(scn->dev->allocator, segsides + i);
  }
#endif

  /* The end of the analyze is multithreaded */
  ASSERT(scn->dev->nthreads > 0);
  #pragma omp parallel num_threads(scn->dev->nthreads)
  {
    /* Step 1: build neighbourhoods */
    collect_and_link_neighbours(scn, segsides, &segments_tmp, &frontiers,
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
        2 + 2 * scn->next_medium_idx));
      darray_segment_comp_init(scn->dev->allocator, &segments_comp);
      segments_comp_initialized = 1;
      OK2(darray_segment_comp_resize(&segments_comp, scn->nsegs));
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
      tmp_res = darray_seg_id_reserve(&scn->analyze.overlapping_ids,
        htable_overlap_size_get(&overlaps));
      if(tmp_res != RES_OK) goto tmp_error2;
      while (!htable_overlap_iterator_eq(&it, &end)) {
        tmp_res = darray_seg_id_push_back(&scn->analyze.overlapping_ids,
          htable_overlap_iterator_key_get(&it));
        if(tmp_res != RES_OK) goto tmp_error2;
        htable_overlap_iterator_next(&it);
      }
      qsort(darray_seg_id_data_get(&scn->analyze.overlapping_ids),
        darray_seg_id_size_get(&scn->analyze.overlapping_ids),
        sizeof(*darray_seg_id_cdata_get(&scn->analyze.overlapping_ids)),
        cmp_seg_id);
      htable_overlap_release(&overlaps);
      overlaps_initialized = 0;
    tmp_error2:
      if (tmp_res != RES_OK) res2 = tmp_res;
    }

    if(darray_seg_id_size_get(&scn->analyze.overlapping_ids)) {
      /* Stop analysis here as the model is ill-formed */
      goto end_;
    }

    if(res != RES_OK || res2 != RES_OK) {
      #pragma omp single nowait
      {
        if(res != RES_OK) {
          log_err(scn->dev,
            LIB_NAME":%s: could not build neighbourhoods from scene.\n",
            FUNC_NAME);
        } else {
          res = res2;
        }
      }
      goto error_;
    }

    /* Step 2: extract segment connex components */
    extract_connex_components(scn, segsides, &connex_components,
      &segments_tmp, &segments_comp, &s2d_view, &component_count, &res);
    /* No barrier at the end of step 2: data used in step 2 cannot be
     * released / data produced by step 2 cannot be used
     * until next sync point */

    #pragma omp barrier
    /* Constraints on step 2 data are now met */
    
    if(res != RES_OK) {
      #pragma omp single nowait
      {
        log_err(scn->dev,
          LIB_NAME":%s: could not extract connex components from scene.\n",
          FUNC_NAME);
      } /* No barrier here */
      goto error_;
    }

    /* One thread releases some data before going to step 3,
     * the others go to step 3 without sync */
    #pragma omp single nowait
    {
      darray_segment_tmp_release(&segments_tmp);
      segments_tmp_initialized = 0;
    } /* No barrier here */

    /* Step 3: group components */
    group_connex_components(scn, &segments_comp, &connex_components, s2d_view,
      &next_enclosure_id, &res);
    /* Barrier at the end of step 3: data used in step 3 can be released /
     * data produced by step 3 can be used */

    if(res != RES_OK) {
      #pragma omp single nowait
      {
        log_err(scn->dev,
          LIB_NAME":%s: could not group connex components from scene.\n",
          FUNC_NAME);
      }
      goto error_;
    }

    /* One thread releases some data and allocate other data before going to
     * step 4, the others waiting for alloced data */
#pragma omp single
    {
      if(s2d_view) S2D(scene_view_ref_put(s2d_view));
      s2d_view = NULL;
      ordered_ids = MEM_ALLOC(scn->dev->allocator,
        sizeof(*ordered_ids) * scn->analyze.enclosures_count);
      if(!ordered_ids) res = RES_MEM_ERR;
    } /* Implicit barrier here */
    if(res != RES_OK) goto error_;

    /* Step 4: Build result */
    build_result(scn, &connex_components, &segments_comp, &frontiers,
      ordered_ids, &res);
    /* No barrier at the end of step 4: data used in step 4 cannot be
     * released / data produced by step 4 cannot be used
     * until next sync point */

    #pragma omp barrier
    /* Constraints on step 4 data are now met */

    if(res != RES_OK) {
      #pragma omp single nowait
      {
        log_err(scn->dev, LIB_NAME":%s: could not build result.\n", FUNC_NAME);
      }
      goto error_;
    }

    /* Some threads release data */
    #pragma omp sections nowait
    {
      #pragma omp section
      {
        MEM_RM(scn->dev->allocator, ordered_ids);
        custom_darray_ptr_component_descriptor_release(&connex_components);
        connex_components_initialized = 0;
      }
      #pragma omp section
      {
        darray_segment_comp_release(&segments_comp);
        segments_comp_initialized = 0;
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
  if(s2d_view) S2D(scene_view_ref_put(s2d_view));
  if(segments_tmp_initialized) darray_segment_tmp_release(&segments_tmp);
  if(segments_comp_initialized) darray_segment_comp_release(&segments_comp);
  if(frontiers_initialized) darray_frontier_vertex_release(&frontiers);
  if(overlaps_initialized) htable_overlap_release(&overlaps);
  if(segsides) MEM_RM(scn->dev->allocator, segsides);

  return res;
error:
  goto exit;
}
