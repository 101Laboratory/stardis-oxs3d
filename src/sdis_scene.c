/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

#include "sdis.h"
#include "sdis_interface_c.h"
#include "sdis_scene_c.h"
#include "sdis_source_c.h"

#include <float.h>
#include <limits.h>

/* Generate the Generic functions of the scene */
#define SDIS_XD_DIMENSION 2
#include "sdis_scene_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_scene_Xd.h"

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static void
project_position
  (const double V0[3],
   const double E0[3],
   const double N[3],
   const double NxE1[3],
   const double rcp_det,
   const double pos[3],
   double uvw[3])
{
  double T[3], Q[3], k;
  ASSERT(V0 && E0 && N && NxE1 && pos && uvw);

  /* Use Moller/Trumbore intersection test the compute the parametric
   * coordinates of the intersection between the triangle and the ray
   * `r = pos + N*d' */
  d3_sub(T, pos, V0);
  uvw[0] = d3_dot(T, NxE1) * rcp_det;
  d3_cross(Q, T, E0);
  uvw[1] = d3_dot(Q, N) * rcp_det;
  uvw[2] = 1.0 - uvw[0] - uvw[1];

  if(uvw[0] >= 0 && uvw[1] >= 0 && uvw[2] >= 0) {/* The ray hits the triangle */
    ASSERT(eq_eps(uvw[0] + uvw[1] + uvw[2], 1.0, 1.e-6));
    return;
  }

  /* Clamp barycentric coordinates to triangle edges */
  if(uvw[0] >= 0) {
    if(uvw[1] >= 0) {
      k = 1.0 / (uvw[0] + uvw[1]);
      uvw[0] *= k;
      uvw[1] *= k;
      uvw[2] = 0;
    } else if( uvw[2] >= 0) {
      k = 1.0 / (uvw[0] + uvw[2]);
      uvw[0] *= k;
      uvw[1] = 0;
      uvw[2] *= k;
    } else {
      ASSERT(uvw[0] >= 1.f);
      d3(uvw, 1, 0, 0);
    }
  } else if(uvw[1] >= 0) {
    if(uvw[2] >= 0) {
      k = 1.0 / (uvw[1] + uvw[2]);
      uvw[0] = 0;
      uvw[1] *= k;
      uvw[2] *= k;
    } else {
      ASSERT(uvw[1] >= 1);
      d3(uvw, 0, 1, 0);
    }
  } else {
    ASSERT(uvw[2] >= 1);
    d3(uvw, 0, 0, 1);
  }
}

static void
scene_release(ref_T * ref)
{
  struct sdis_device* dev = NULL;
  struct sdis_scene* scn = NULL;
  ASSERT(ref);
  scn = CONTAINER_OF(ref, struct sdis_scene, ref);
  dev = scn->dev;
  clear_properties(scn);
  darray_interf_release(&scn->interfaces);
  darray_medium_release(&scn->media);
  darray_prim_prop_release(&scn->prim_props);
  htable_enclosure_release(&scn->enclosures);
  htable_d_release(&scn->tmp_hc_ub);
  htable_key2prim2d_release(&scn->key2prim2d);
  htable_key2prim3d_release(&scn->key2prim3d);
  if(scn->s2d_view) S2D(scene_view_ref_put(scn->s2d_view));
  if(scn->s3d_view) S3D(scene_view_ref_put(scn->s3d_view));
  if(scn->senc2d_scn) SENC2D(scene_ref_put(scn->senc2d_scn));
  if(scn->senc3d_scn) SENC3D(scene_ref_put(scn->senc3d_scn));
  if(scn->source) SDIS(source_ref_put(scn->source));
  if(scn->radenv) SDIS(radiative_env_ref_put(scn->radenv));
  MEM_RM(dev->allocator, scn);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_scene_create
  (struct sdis_device* dev,
   const struct sdis_scene_create_args* args,
   struct sdis_scene** out_scn)
{
  return scene_create_3d(dev, args, out_scn);
}

res_T
sdis_scene_2d_create
  (struct sdis_device* dev,
   const struct sdis_scene_create_args* args,
   struct sdis_scene** out_scn)
{
  return scene_create_2d(dev, args, out_scn);
}

res_T
sdis_scene_ref_get(struct sdis_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_get(&scn->ref);
  return RES_OK;
}

res_T
sdis_scene_ref_put(struct sdis_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_put(&scn->ref, scene_release);
  return RES_OK;
}

res_T
sdis_scene_get_aabb
  (const struct sdis_scene* scn,
   double lower[],
   double upper[])
{
  float low[3], upp[3];
  res_T res = RES_OK;
  if(!scn || !lower || !upper) return RES_BAD_ARG;

  if(scene_is_2d(scn)) {
    res = s2d_scene_view_get_aabb(scn->s2d_view, low, upp);
    if(res != RES_OK) return res;
    d2_set_f2(lower, low);
    d2_set_f2(upper, upp);
  } else {
    res = s3d_scene_view_get_aabb(scn->s3d_view, low, upp);
    if(res != RES_OK) return res;
    d3_set_f3(lower, low);
    d3_set_f3(upper, upp);
  }
  return RES_OK;
}

res_T
sdis_scene_get_fp_to_meter
  (const struct sdis_scene* scn,
   double* fp_to_meter)
{
  if(!scn || !fp_to_meter) return RES_BAD_ARG;
  *fp_to_meter = scn->fp_to_meter;
  return RES_OK;
}

res_T
sdis_scene_set_fp_to_meter
  (struct sdis_scene* scn,
   const double fp_to_meter)
{
  if(!scn || fp_to_meter <= 0) return RES_BAD_ARG;
  scn->fp_to_meter = fp_to_meter;
  return RES_OK;
}

res_T
sdis_scene_get_temperature_range
  (const struct sdis_scene* scn,
   double t_range[2])
{
  if(!scn || !t_range) return RES_BAD_ARG;
  t_range[0]  = scn->tmin;
  t_range[1]  = scn->tmax;
  return RES_OK;
}

res_T
sdis_scene_set_temperature_range
  (struct sdis_scene* scn,
   const double t_range[2])
{
  if(!scn || !t_range) return RES_BAD_ARG;
  scn->tmin = t_range[0];
  scn->tmax = t_range[1];
  return RES_OK;
}

res_T
sdis_scene_find_closest_point
  (const struct sdis_scene* scn,
   const struct sdis_scene_find_closest_point_args* args,
   size_t* iprim,
   double uv[])
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return scene_find_closest_point_2d(scn, args, iprim, uv);
  } else {
    return scene_find_closest_point_3d(scn, args, iprim, uv);
  }
}

res_T
sdis_scene_get_boundary_position
  (const struct sdis_scene* scn,
   const size_t iprim,
   const double uv[],
   double pos[])
{
  if(!scn || !uv || !pos) return RES_BAD_ARG;
  if(iprim >= scene_get_primitives_count(scn)) return RES_BAD_ARG;

  if(scene_is_2d(scn)) {
    struct s2d_primitive prim;
    struct s2d_attrib attr;
    float s = (float)uv[0];

    S2D(scene_view_get_primitive(scn->s2d_view, (unsigned int)iprim, &prim));
    S2D(primitive_get_attrib(&prim, S2D_POSITION, s, &attr));
    d2_set_f2(pos, attr.value);
  } else {
    struct s3d_primitive prim;
    struct s3d_attrib attr;
    float st[2];

    f2_set_d2(st, uv);
    S3D(scene_view_get_primitive(scn->s3d_view, (unsigned int)iprim, &prim));
    S3D(primitive_get_attrib(&prim, S3D_POSITION, st, &attr));
    d3_set_f3(pos, attr.value);
  }
  return RES_OK;
}

res_T
sdis_scene_boundary_project_position
  (const struct sdis_scene* scn,
   const size_t iprim,
   const double pos[],
   double uv[])
{
  if(!scn || !pos || !uv) return RES_BAD_ARG;
  if(iprim >= scene_get_primitives_count(scn)) return RES_BAD_ARG;

  if(scene_is_2d(scn)) {
    struct s2d_primitive prim;
    struct s2d_attrib a;
    double V[2][2]; /* Vertices */
    double E[2][3]; /* V0->V1 and V0->pos */
    double proj;

    /* Retrieve the segment vertices */
    S2D(scene_view_get_primitive(scn->s2d_view, (unsigned int)iprim, &prim));
    S2D(segment_get_vertex_attrib(&prim, 0, S2D_POSITION, &a)); d2_set_f2(V[0], a.value);
    S2D(segment_get_vertex_attrib(&prim, 1, S2D_POSITION, &a)); d2_set_f2(V[1], a.value);

    /* Compute the parametric coordinate of the project of `pos' onto the
     * segment.*/
    d2_sub(E[0], V[1], V[0]);
    d2_normalize(E[0], E[0]);
    d2_sub(E[1], pos,  V[0]);
    proj = d2_dot(E[0], E[1]);

    uv[0] = CLAMP(proj, 0, 1); /* Clamp the parametric coordinate in [0, 1] */

  } else {
    struct s3d_primitive prim;
    struct s3d_attrib a;
    double V[3][3]; /* Vertices */
    double E[2][3]; /* V0->V1 and V0->V2 edges */
    double N[3]; /* Normal */
    double NxE1[3], rcp_det; /* Muller/Trumboer triangle parameters */
    double uvw[3];

    S3D(scene_view_get_primitive(scn->s3d_view, (unsigned int)iprim, &prim));
    S3D(triangle_get_vertex_attrib(&prim, 0, S3D_POSITION, &a)); d3_set_f3(V[0], a.value);
    S3D(triangle_get_vertex_attrib(&prim, 1, S3D_POSITION, &a)); d3_set_f3(V[1], a.value);
    S3D(triangle_get_vertex_attrib(&prim, 2, S3D_POSITION, &a)); d3_set_f3(V[2], a.value);
    d3_sub(E[0], V[1], V[0]);
    d3_sub(E[1], V[2], V[0]);
    d3_cross(N, E[0], E[1]);

    /* Muller/Trumbore triangle parameters */
    d3_cross(NxE1, N, E[1]);
    rcp_det = 1.0 / d3_dot(NxE1, E[0]);

    /* Use the Muller/Trumbore intersection test to project `pos' onto the
     * triangle and to retrieve the parametric coordinates of the projection
     * point */
    project_position(V[0], E[0], N, NxE1, rcp_det, pos, uvw);

    uv[0] = uvw[2];
    uv[1] = uvw[0];
  }
  return RES_OK;
}

res_T
sdis_scene_get_senc2d_scene
  (struct sdis_scene* scn,
   struct senc2d_scene** senc2d_scn)
{
  if(!scn || !senc2d_scn) return RES_BAD_ARG;
  if(!scn->senc2d_scn) return RES_BAD_ARG; /* Scene is 3D */
  *senc2d_scn = scn->senc2d_scn;
  return RES_OK;
}

res_T
sdis_scene_get_senc3d_scene
  (struct sdis_scene* scn,
   struct senc3d_scene** senc3d_scn)
{
  if(!scn || !senc3d_scn) return RES_BAD_ARG;
  if(!scn->senc3d_scn) return RES_BAD_ARG; /* Scene is 2D */
  *senc3d_scn = scn->senc3d_scn;
  return RES_OK;
}

res_T
sdis_scene_get_s2d_scene_view
  (struct sdis_scene* scn,
   struct s2d_scene_view** s2d_view)
{
  if(!scn || !s2d_view) return RES_BAD_ARG;
  if(!scn->s2d_view) return RES_BAD_ARG; /* Scene is 3D */
  *s2d_view = scn->s2d_view;
  return RES_OK;
}

res_T
sdis_scene_get_s3d_scene_view
  (struct sdis_scene* scn,
   struct s3d_scene_view** s3d_view)
{
  if(!scn || !s3d_view) return RES_BAD_ARG;
  if(!scn->s3d_view) return RES_BAD_ARG; /* Scene is 2D */
  *s3d_view = scn->s3d_view;
  return RES_OK;
}

res_T
sdis_scene_get_dimension
  (const struct sdis_scene* scn, enum sdis_scene_dimension* dim)
{
  if(!scn || !dim) return RES_BAD_ARG;
  *dim = scene_is_2d(scn) ? SDIS_SCENE_2D : SDIS_SCENE_3D;
  return RES_OK;
}

res_T
sdis_scene_get_medium_spread
  (struct sdis_scene* scn,
   const struct sdis_medium* mdm,
   double* out_spread)
{
  struct htable_enclosure_iterator it, end;
  double spread = 0;
  res_T res = RES_OK;

  if(!scn || !mdm || !out_spread) {
    res = RES_BAD_ARG;
    goto error;
  }

  htable_enclosure_begin(&scn->enclosures, &it);
  htable_enclosure_end(&scn->enclosures, &end);
  while(!htable_enclosure_iterator_eq(&it, &end)) {
    const struct enclosure* enc = htable_enclosure_iterator_data_get(&it);
    htable_enclosure_iterator_next(&it);
    if(sdis_medium_get_id(mdm) == enc->medium_id) {
      spread += enc->V;
    }
  }
  *out_spread = spread;

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_scene_get_device(struct sdis_scene* scn, struct sdis_device** device)
{
  if(!scn || !device) return RES_BAD_ARG;
  *device = scn->dev;
  return RES_OK;
}

res_T
sdis_scene_get_source(struct sdis_scene* scn, struct sdis_source** source)
{
  if(!scn || !source) return RES_BAD_ARG;
  *source = scn->source;
  return RES_OK;
}

res_T
sdis_scene_get_radiative_env
  (struct sdis_scene* scn,
   struct sdis_radiative_env** radenv)
{
  if(!scn || !radenv) return RES_BAD_ARG;
  *radenv = scn->radenv;
  return RES_OK;
}

res_T
sdis_scene_get_s2d_primitive
  (struct sdis_scene* scn,
   const struct sdis_primkey* key,
   struct s2d_primitive* out_prim)
{
  struct s2d_primitive* prim = NULL;

  if(!scn || !key || !out_prim || !scene_is_2d(scn)) return RES_BAD_ARG;

  if((prim = htable_key2prim2d_find(&scn->key2prim2d, key)) == NULL)
    return RES_BAD_ARG;
  *out_prim = *prim;
  return RES_OK;
}

res_T
sdis_scene_get_s3d_primitive
  (struct sdis_scene* scn,
   const struct sdis_primkey* key,
   struct s3d_primitive* out_prim)
{
  struct s3d_primitive* prim = NULL;

  if(!scn || !key || !out_prim || scene_is_2d(scn)) return RES_BAD_ARG;

  if((prim = htable_key2prim3d_find(&scn->key2prim3d, key)) == NULL)
    return RES_BAD_ARG;
  *out_prim = *prim;
  return RES_OK;
}

/*******************************************************************************
 * Local miscellaneous function
 ******************************************************************************/
struct sdis_interface*
scene_get_interface(const struct sdis_scene* scn, const unsigned iprim)
{
  ASSERT(scn && iprim < darray_prim_prop_size_get(&scn->prim_props));
  return darray_prim_prop_cdata_get(&scn->prim_props)[iprim].interf;
}

res_T
scene_get_enclosure_id
  (struct sdis_scene* scn,
   const double pos[],
   unsigned* enc_id)
{
  return scene_is_2d(scn)
    ? scene_get_enclosure_id_2d(scn, pos, enc_id)
    : scene_get_enclosure_id_3d(scn, pos, enc_id);
}

res_T
scene_get_enclosure_id_in_closed_boundaries
  (struct sdis_scene* scn,
   const double pos[],
   unsigned* enc_id)
{
  return scene_is_2d(scn)
    ? scene_get_enclosure_id_in_closed_boundaries_2d(scn, pos, enc_id)
    : scene_get_enclosure_id_in_closed_boundaries_3d(scn, pos, enc_id);
}

res_T
scene_get_enclosure_medium
  (struct sdis_scene* scn,
   const struct enclosure* enc,
   struct sdis_medium** out_mdm)
{
  struct sdis_medium* mdm = NULL;
  res_T res = RES_OK;

  ASSERT(scn && enc && out_mdm);

  /* Check that the enclosure doesn't surround multiple media */
  if(enc->medium_id == MEDIUM_ID_MULTI) {
    log_warn(scn->dev,
       "%s: invalid medium request. The enclosure includes several media.\n",
       FUNC_NAME);
    res = RES_BAD_OP;
    goto error;
  }

  /* Obtain enclosure medium */
  ASSERT(enc->medium_id < darray_medium_size_get(&scn->media));
  mdm = darray_medium_data_get(&scn->media)[enc->medium_id];

error:
  *out_mdm = mdm;
  goto exit;
exit:
  mdm = NULL;
  return res;
}

res_T
scene_compute_hash(const struct sdis_scene* scn, hash256_T hash)
{
  struct sha256_ctx sha256_ctx;
  size_t iprim, nprims;
  int has_radenv = 0;
  res_T res = RES_OK;
  ASSERT(scn && hash);

  sha256_ctx_init(&sha256_ctx);

  if(scene_is_2d(scn)) {
    S2D(scene_view_primitives_count(scn->s2d_view, &nprims));
  } else {
    S3D(scene_view_primitives_count(scn->s3d_view, &nprims));
  }
  #define SHA256_UPD(Var, Nb) \
    sha256_ctx_update(&sha256_ctx, (const char*)(Var), sizeof(*Var)*(Nb))

  has_radenv = scn->radenv != NULL;

  SHA256_UPD(&has_radenv, 1);
  SHA256_UPD(&scn->tmax, 1);
  SHA256_UPD(&scn->fp_to_meter, 1);

  if(scn->source) {
    hash256_T src_hash;
    source_compute_signature(scn->source, src_hash);
    sha256_ctx_update(&sha256_ctx, src_hash, sizeof(hash256_T));
  }

  FOR_EACH(iprim, 0, nprims) {
    struct sdis_interface* interf = NULL;
    size_t ivert;

    if(scene_is_2d(scn)) {
      struct s2d_primitive prim;
      S2D(scene_view_get_primitive(scn->s2d_view, (unsigned)iprim, &prim));
      FOR_EACH(ivert, 0, 2) {
        struct s2d_attrib attr;
        S2D(segment_get_vertex_attrib(&prim, ivert, S2D_POSITION, &attr));
        SHA256_UPD(attr.value, 2);
      }
    } else {
      struct s3d_primitive prim;
      S3D(scene_view_get_primitive(scn->s3d_view, (unsigned)iprim, &prim));
      FOR_EACH(ivert, 0, 3) {
        struct s3d_attrib attr;
        S3D(triangle_get_vertex_attrib(&prim, ivert, S3D_POSITION, &attr));
        SHA256_UPD(attr.value, 3);
      }
    }

    interf = scene_get_interface(scn, (unsigned)iprim);
    SHA256_UPD(&interf->medium_front->type, 1);
    SHA256_UPD(&interf->medium_front->id, 1);
    SHA256_UPD(&interf->medium_back->type, 1);
    SHA256_UPD(&interf->medium_back->id, 1);
  }
  #undef SHA256_UPD
  sha256_ctx_finalize(&sha256_ctx, hash);

  return res;
}

res_T
scene_check_primitive_index(const struct sdis_scene* scn, const size_t iprim)
{
  res_T res = RES_OK;
  ASSERT(scn);

  if(iprim >= scene_get_primitives_count(scn)) {
    log_err(scn->dev,
      "%s: invalid primitive identifier `%lu'. "
      "It must be in the [0 %lu] range.\n",
      FUNC_NAME,
      (unsigned long)iprim,
      (unsigned long)scene_get_primitives_count(scn)-1);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
scene_check_dimensionality_2d(const struct sdis_scene* scn)
{
  res_T res = RES_OK;
  ASSERT(scn);
  if(scene_is_2d(scn) == 0) {
    log_err(scn->dev,
      "%s: expects a 2D scene while the input scene is 3D.\n",
      FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }
exit:
  return res;
error:
  goto exit;
}

res_T
scene_check_dimensionality_3d(const struct sdis_scene* scn)
{
  res_T res = RES_OK;
  ASSERT(scn);
  if(scene_is_2d(scn) != 0) {
    log_err(scn->dev,
      "%s: expects a 3D scene while the input scene is 2D.\n",
      FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }
exit:
  return res;
error:
  goto exit;
}

res_T
scene_check_temperature_range(const struct sdis_scene* scn)
{
  res_T res = RES_OK;
  ASSERT(scn);

  if(SDIS_TEMPERATURE_IS_UNKNOWN(scn->tmin)) {
    log_err(scn->dev,
      "%s the defined minimum temperature is unknown "
      "when it is expected to be known.\n",
      FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  if(SDIS_TEMPERATURE_IS_UNKNOWN(scn->tmax)) {
    log_err(scn->dev,
      "%s the defined maximum temperature is unknown "
      "when it is expected to be known.\n",
      FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  if(scn->tmin > scn->tmax) {
    log_err(scn->dev,
      "%s: defined temperature range degenerated -- [%g, %g] K\n",
      FUNC_NAME, scn->tmin, scn->tmax);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}
