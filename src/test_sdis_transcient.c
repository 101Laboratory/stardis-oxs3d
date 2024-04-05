/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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
#include "test_sdis_utils.h"

#include <string.h>

/*
 * The scene is composed of a solid cuboid whose temperature is fixed on its 6
 * faces. The test consist in checking that the estimated temperature at a
 * given temperature and time is compatible with the reference temperature
 * computed by analytically evaluating the green function.
 *
 * The test is performed on 3 scenes that actually represent the same system.
 * The first scene is simply the cuboid, as it. The second scene is the same
 * cuboid but this time formed by 2 sub cuboid with strictly the same physical
 * properties. Finally, the last scene is the same cube with successive
 * cubes into it used only to add fictive boundaries.
 */

static const double vertices[12/*#vertices*/*3/*#coords per vertex*/] = {
  0.0, 0.0, 0.0,
  0.5, 0.0, 0.0,
  0.0, 1.0, 0.0,
  0.5, 1.0, 0.0,
  0.0, 0.0, 1.0,
  0.5, 0.0, 1.0,
  0.0, 1.0, 1.0,
  0.5, 1.0, 1.0,
  1.0, 0.0, 0.0,
  1.0, 1.0, 0.0,
  1.0, 0.0, 1.0,
  1.0, 1.0, 1.0
};
static const size_t nvertices = sizeof(vertices) / (sizeof(double)*3);

/* The following array lists the indices toward the 3D vertices of each
 * triangle.
 *        ,2---,3       ,3---,9  |      ,2----3       ,3----9
 *      ,' | ,'/|     ,' | ,'/|  |    ,'/| \  |     ,'/| \  |          Y
 *    6----7' / |   7---11' / |  |  6' / |  \ |   7' / |  \ |          |
 *    |',  | / ,1   |',  | / ,8  |  | / ,0---,1   | / ,1---,8          o--X
 *    |  ',|/,'     |  ',|/,'    |  |/,' | ,'     |/,' | ,'           /
 *    4----5        5---10       |  4----5'       5---10'            Z
 */
static const size_t indices[22/*#triangles*/*3/*#indices per triangle*/] = {
  0, 4, 2, 2, 4, 6, /* X min */
  3, 7, 5, 5, 1, 3, /* X mid */
  9,11,10,10, 8, 9, /* X max */
  0, 5, 4, 0, 1, 5, 1,10, 5, 1, 8,10, /* Y min */
  2, 6, 7, 2, 7, 3, 3, 7,11, 3,11, 9, /* Y max */
  0, 2, 1, 1, 2, 3, 1, 3, 8, 8, 3, 9, /* Z min */
  4, 5, 6, 6, 5, 7, 5,10, 7, 7,10,11  /* Z max */
};
static const size_t ntriangles = sizeof(indices) / (sizeof(size_t)*3);

/*******************************************************************************
 * Box geometry functions
 ******************************************************************************/
struct context {
  const double* vertices;
  const size_t* indices;
  struct sdis_interface* interfs[22];
  const double* scale;
};

static void
get_position(const size_t ivert, double pos[3], void* context)
{
  struct context* ctx = context;
  CHK(ctx);
  pos[0] = ctx->vertices[ivert*3+0] * ctx->scale[0];
  pos[1] = ctx->vertices[ivert*3+1] * ctx->scale[1];
  pos[2] = ctx->vertices[ivert*3+2] * ctx->scale[2];
}

static void
get_indices(const size_t itri, size_t ids[3], void* context)
{
  struct context* ctx = context;
  CHK(ctx);
  ids[0] = ctx->indices[itri*3+0];
  ids[1] = ctx->indices[itri*3+1];
  ids[2] = ctx->indices[itri*3+2];
}

static void
get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  struct context* ctx = context;
  CHK(ctx);
  *bound = ctx->interfs[itri];
}

/*******************************************************************************
 * Setup a scene composed of a box that successively contains smaller boxes
 ******************************************************************************/
struct matriochka_context {
  struct sdis_interface* interfs[13];
  const double* scale;
  size_t nboxes;
};

static void
matriochka_position(const size_t ivert, double pos[3], void* context)
{
  struct matriochka_context* ctx = context;
  const size_t ibox = ctx->nboxes - ivert / box_nvertices - 1;
  const double* verts = box_vertices;
  const double box_szmin = 1.0 / (double)ctx->nboxes;
  const size_t i = ivert % box_nvertices;
  CHK(ibox <= ctx->nboxes);
  pos[0] = ((verts[i*3+0]-0.5)*(double)(ibox+1)*box_szmin + 0.5)*ctx->scale[0];
  pos[1] = ((verts[i*3+1]-0.5)*(double)(ibox+1)*box_szmin + 0.5)*ctx->scale[1];
  pos[2] = ((verts[i*3+2]-0.5)*(double)(ibox+1)*box_szmin + 0.5)*ctx->scale[2];
}

static void
matriochka_indices(const size_t itri, size_t ids[3], void* context)
{
  struct matriochka_context* ctx = context;
  const size_t i = itri % box_ntriangles;
  const size_t ibox = ctx->nboxes - itri / box_ntriangles - 1;
  CHK(ibox <= ctx->nboxes);
  (void)context;
  ids[0] = box_indices[i*3+0] + ibox*box_nvertices;
  ids[1] = box_indices[i*3+1] + ibox*box_nvertices;
  ids[2] = box_indices[i*3+2] + ibox*box_nvertices;
}

static void
matriochka_interface
  (const size_t itri, struct sdis_interface** bound, void* context)
{
  struct matriochka_context* ctx = context;
  const size_t ibox = ctx->nboxes - itri / box_ntriangles - 1;
  const size_t i = itri % box_ntriangles;
  CHK(ibox < ctx->nboxes);
  *bound = ibox != 0 ? ctx->interfs[12] : ctx->interfs[i];
}

static INLINE void
dump_matriochkas(FILE* stream, struct matriochka_context* ctx)
{
  size_t i;
  ASSERT(ctx && stream);
  FOR_EACH(i, 0, ctx->nboxes*box_nvertices) {
    double pos[3];
    matriochka_position(i, pos, ctx);
    fprintf(stream, "v %g %g %g\n", SPLIT3(pos));
  }
  FOR_EACH(i, 0, ctx->nboxes*box_ntriangles) {
    size_t ids[3];
    matriochka_indices(i, ids, ctx);
    fprintf(stream, "f %lu %lu %lu\n",
      (unsigned long)ids[0]+1,
      (unsigned long)ids[1]+1,
      (unsigned long)ids[2]+1);
  }
}

/*******************************************************************************
 * Solid medium
 ******************************************************************************/
struct solid {
  double cp;
  double lambda;
  double rho;
  double delta;
  double init_temperature;
};

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->cp;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->lambda;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->rho;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->delta;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  if(vtx->time <= 0) {
    return ((const struct solid*)sdis_data_cget(data))->init_temperature;
  } else {
    return SDIS_TEMPERATURE_NONE;
  }
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
struct interf {
  double temperature;
};

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && data);
  return interf->temperature;
}

static struct sdis_interface*
create_interface
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const struct sdis_interface_shader* interf_shader,
   const double temperature)
{
  struct sdis_data* data = NULL;
  struct sdis_interface* interf = NULL;
  struct interf* interf_props = NULL;

  OK(sdis_data_create
    (dev, sizeof(struct interf), ALIGNOF(struct interf), NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = temperature;
  OK(sdis_interface_create
    (dev, front, back, interf_shader, data, &interf));
  OK(sdis_data_ref_put(data));
  return interf;
}

/*******************************************************************************
 * Analytical solution
 ******************************************************************************/
static void
fourier_pq
  (const size_t nterms_pq,
   const double pos[3],
   const double sz[3],
   const int i0,
   const int i1,
   const int i2,
   double green[2])
{
  size_t p, q;
  CHK(green);

  green[0] = 0;
  green[1] = 0;

  FOR_EACH(p, 0, nterms_pq+1) {
    FOR_EACH(q, 0, nterms_pq+1) {
      const double p2 = (double)(2*p + 1);
      const double q2 = (double)(2*q + 1);
      double L_sqr, L, tmp;
      L_sqr = PI * PI * ((p2*p2)/(sz[i1]*sz[i1]) + (q2*q2)/(sz[i2]*sz[i2]));
      L = sqrt(L_sqr);
      tmp = sin(PI*p2*pos[i1]/sz[i1])
          * sin(PI*q2*pos[i2]/sz[i2])
          / (sinh(sz[i0]*L)*(p2*q2));
      if(tmp != 0) {
        green[0] += sinh(L*(sz[i0]-pos[i0]))*tmp;
        green[1] += sinh(L*pos[i0])*tmp;
      }
    }
  }
}

/* This function computes the Green function between a given probe
 * position/time in a parallelepipedic box and each face of this box (within
 * the model of a homogeneous boundary condition on each face). */
static void
green_analytical
  (const double box_size[3],
   const double probe[3],
   const double time,
   const double rho,
   const double cp,
   const double lambda,
   double green[7])
{
  const size_t nterms_fs = 20; /* #terms in the Fourier expansion series */
  const size_t nterms_pq = 100; /* #terms in double p/q sums */
  const size_t nt_pq = (nterms_fs - 1)/2;
  double Gs[7], Gi[7], Gtmp[7];
  size_t i, m, n, o, p, q;
  double a, b, c;
  double alpha;

  CHK(box_size && probe && time >= 0 && green);

  if(time == 0) {
    memset(green, 0, sizeof(double[7]));
    green[6] = 1;
    return;
  }

  memset(Gs, 0, sizeof(double[7]));
  memset(Gi, 0, sizeof(double[7]));
  memset(Gtmp, 0, sizeof(double[7]));

  /* Steady state solution */
  fourier_pq(nterms_pq, probe, box_size, 0, 1, 2, Gtmp+0); /* Faces 0 and 1 */
  fourier_pq(nterms_pq, probe, box_size, 1, 0, 2, Gtmp+2); /* Faces 2 and 3 */
  fourier_pq(nterms_pq, probe, box_size, 2, 1, 0, Gtmp+4); /* Faces 4 and 5 */
  FOR_EACH(i, 0, 6) Gs[i] += 16 * Gtmp[i] / (PI * PI);

  alpha=lambda/(rho*cp);
  a=box_size[0];
  b=box_size[1];
  c=box_size[2];

  /* Transient solution */
  FOR_EACH(m, 0, nterms_fs+1) {
    const double beta = PI*(double)m/a;
    const double beta_sqr = beta*beta;
    const int m_is_even = (m%2 == 0);

    FOR_EACH(n, 0, nterms_fs+1) {
      const double gamma = PI*(double)n/b;
      const double gamma_sqr = gamma * gamma;
      const int n_is_even = (n%2==0);

      FOR_EACH(o, 0, nterms_fs+1) {
        const double eta = PI*(double)o/c;
        const double eta_sqr = eta*eta;
        const double zeta = alpha*(beta_sqr+gamma_sqr+eta_sqr);
        const int o_is_even = (o%2==0);
        double Fxyzt;

        memset(Gtmp, 0, sizeof(double[7]));
        FOR_EACH(p, 0, nt_pq+1) {
          FOR_EACH(q, 0, nt_pq+1) {
            const double p2 = (double)(2*p + 1);
            const double q2 = (double)(2*q + 1);
            const double Lx_sqr = PI*PI*((p2*p2)/(b*b)+(q2*q2)/(c*c));
            const double Ly_sqr = PI*PI*((p2*p2)/(a*a)+(q2*q2)/(c*c));
            const double Lz_sqr = PI*PI*((p2*p2)/(b*b)+(q2*q2)/(a*a));
            const double pq = p2*q2;
            double itg[11] = {0};

            itg[1] = 2*q-o+1 == 0 ? -c/2.0 : 0;
            itg[2] = eta / (eta_sqr+Lz_sqr);
            itg[4] = 2*p-n+1 == 0 ? -b/2.0 : 0;
            itg[5] = gamma / (gamma_sqr+Ly_sqr);
            itg[7] = beta / (beta_sqr+Lx_sqr);
            itg[9] = 2*p-m+1 == 0 ? -a/2.0 : 0;
            itg[10] = 2*q-m+1 == 0 ? -a/2.0 : 0;

            if((2*q-o+1==0) && (2*p-n+1==0)) {
              const double z1 = itg[1]*itg[4]*itg[7];
              Gtmp[0] += z1/pq;
              Gtmp[1] += (z1*pow(-1.0,(double)(m+1)))/pq;
            }
            if((2*q-o+1==0) && (2*p-m+1==0)) {
              const double z2 = itg[1]*itg[9]*itg[5];
              Gtmp[2] += z2/pq;
              Gtmp[3] += (z2*pow(-1.0,(double)(n+1)))/pq;
            }
            if((2*p-n+1==0) && (2*q-m+1==0)) {
              const double z3 = itg[4]*itg[10]*itg[2];
              Gtmp[4] += z3/pq;
              Gtmp[5] += (z3*pow(-1.0,(double)(o+1)))/pq;
            }
          }
        }
        Fxyzt =
          sin(probe[0]*beta)
        * sin(probe[1]*gamma)
        * sin(probe[2]*eta)
        * exp(-zeta*time);

        FOR_EACH(i, 0, 6) {
          Gi[i] += Gtmp[i] * Fxyzt;
        }
        if((!m_is_even) && (!n_is_even) && (!o_is_even)) {
          Gi[6] += 8.0 * Fxyzt/(beta*gamma*eta);
        }
      }
    }
  }

  /* Gi[i], i=0,5: Green of boundary index i */
  FOR_EACH(i, 0, 6) {
    Gi[i] = -128.0 * Gi[i]/(a*b*c*PI*PI);
  }

  /* Gi[6]: Green of the initial condition */
  Gi[6] = 8.0*Gi[6]/(a*b*c);

  /* Computing total Green function */
  FOR_EACH(i, 0, 7) {
    green[i] = Gs[i] + Gi[i];
  }
}

static double
temperature_analytical
  (const double temperature_bounds[6],
   const double temperature_init,
   const double box_size[3],
   const double probe[3],
   const double time,
   const double rho,
   const double cp,
   const double lambda)
{
  double green[7];
  double temperature = 0;
  size_t i;
  CHK(temperature_bounds && temperature_init && box_size && probe);
  green_analytical(box_size, probe, time, rho, cp, lambda, green);

  FOR_EACH(i, 0, 6) {
    printf("Green for face %lu: %g\n", (unsigned long)i, green[i]);
    temperature += green[i] * temperature_bounds[i];
  }
  temperature += green[6] * temperature_init;
  return temperature;
}

/*******************************************************************************
 * Main function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_device* dev = NULL;
  struct sdis_scene* box_scn = NULL;
  struct sdis_scene* box2_scn = NULL;
  struct sdis_scene* box_matriochka_scn = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_data* data = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interfs[7] = {NULL};
  struct sdis_estimator* estimator = NULL;
  struct sdis_mc temperature = SDIS_MC_NULL;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct solid* solid_param = NULL;
  struct context ctx;
  struct matriochka_context matriochka_ctx;
  const size_t nrealisations = 10000;
  const size_t nmatriochkas = 5;
  size_t nfails = 0;
  double probe[3];
  double time[2];
  double Tbounds[6];
  double Tinit;
  double Tref;
  double boxsz[3];
  double rho, cp, lambda;
  (void)argc, (void)argv;

  /* System description */
  rho = 1700;
  cp = 800;
  lambda = 1.15;
  Tbounds[0] = 280; /* Xmin */
  Tbounds[1] = 290; /* Xmax */
  Tbounds[2] = 310; /* Ymin */
  Tbounds[3] = 270; /* Ymax */
  Tbounds[4] = 300; /* Zmin */
  Tbounds[5] = 320; /* Zmax */
  Tinit = 100;
  boxsz[0] = 0.3;
  boxsz[1] = 0.1;
  boxsz[2] = 0.2;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  /* Create the fluid medium */
  fluid_shader = DUMMY_FLUID_SHADER;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;

  /* Create the solid medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->rho = rho;
  solid_param->cp = cp;
  solid_param->lambda = lambda;
  solid_param->delta = 1.0/30.0 * MMIN(MMIN(boxsz[0], boxsz[1]), boxsz[2]);
  solid_param->init_temperature = Tinit;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));

  /* Setup the interface shader */
  interf_shader.front.temperature = interface_get_temperature;

  /* Create the interfaces */
  interfs[0] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[0]);
  interfs[1] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[1]);
  interfs[2] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[2]);
  interfs[3] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[3]);
  interfs[4] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[4]);
  interfs[5] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[5]);
  interfs[6] = create_interface(dev, solid, solid, &interf_shader, SDIS_TEMPERATURE_NONE);

  /* Setup the box scene context */
  ctx.indices = box_indices;
  ctx.vertices = box_vertices;
  ctx.interfs[0]  = ctx.interfs[1]  = interfs[4]; /* Zmin */
  ctx.interfs[2]  = ctx.interfs[3]  = interfs[0]; /* Xmin */
  ctx.interfs[4]  = ctx.interfs[5]  = interfs[5]; /* Zmax */
  ctx.interfs[6]  = ctx.interfs[7]  = interfs[1]; /* Xmax */
  ctx.interfs[8]  = ctx.interfs[9]  = interfs[3]; /* Ymax */
  ctx.interfs[10] = ctx.interfs[11] = interfs[2]; /* Ymin */
  ctx.scale = boxsz;

  /* Create the box scene */
  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = box_ntriangles;
  scn_args.nvertices = box_nvertices;
  scn_args.context = &ctx;
  OK(sdis_scene_create(dev, &scn_args, &box_scn));

  /* Setup the box2 scene context */
  ctx.indices = indices;
  ctx.vertices = vertices;
  ctx.interfs[0]  = ctx.interfs[1]  = interfs[0]; /* Xmin */
  ctx.interfs[2]  = ctx.interfs[3]  = interfs[6]; /* Xmid */
  ctx.interfs[4]  = ctx.interfs[5]  = interfs[1]; /* Xmax */
  ctx.interfs[6]  = ctx.interfs[7]  = interfs[2]; /* Ymin */
  ctx.interfs[8]  = ctx.interfs[9]  = interfs[2]; /* Ymin */
  ctx.interfs[10] = ctx.interfs[11] = interfs[3]; /* Ymax */
  ctx.interfs[12] = ctx.interfs[13] = interfs[3]; /* Ymax */
  ctx.interfs[14] = ctx.interfs[15] = interfs[4]; /* Zmin */
  ctx.interfs[16] = ctx.interfs[17] = interfs[4]; /* Zmin */
  ctx.interfs[18] = ctx.interfs[19] = interfs[5]; /* Zmax */
  ctx.interfs[20] = ctx.interfs[21] = interfs[5]; /* Zmax */
  ctx.scale = boxsz;

  /* Create the box2 scene */
  scn_args.nprimitives = ntriangles;
  scn_args.nvertices = nvertices;
  OK(sdis_scene_create(dev, &scn_args, &box2_scn));

  /* Setup the matriochka context */
  matriochka_ctx.interfs[0]  = matriochka_ctx.interfs[1]  = interfs[4]; /* Zmin */
  matriochka_ctx.interfs[2]  = matriochka_ctx.interfs[3]  = interfs[0]; /* Xmin */
  matriochka_ctx.interfs[4]  = matriochka_ctx.interfs[5]  = interfs[5]; /* Zmax */
  matriochka_ctx.interfs[6]  = matriochka_ctx.interfs[7]  = interfs[1]; /* Xmax */
  matriochka_ctx.interfs[8]  = matriochka_ctx.interfs[9]  = interfs[3]; /* Ymax */
  matriochka_ctx.interfs[10] = matriochka_ctx.interfs[11] = interfs[2]; /* Ymin */
  matriochka_ctx.interfs[12] = interfs[6]; /* The remaining internal triangles */
  matriochka_ctx.scale = boxsz;
  matriochka_ctx.nboxes = nmatriochkas;

  /* Create the matriochka scene */
  scn_args.get_indices = matriochka_indices;
  scn_args.get_interface = matriochka_interface;
  scn_args.get_position = matriochka_position;
  scn_args.nprimitives = box_ntriangles*nmatriochkas;
  scn_args.nvertices = box_nvertices*nmatriochkas;
  scn_args.context = &matriochka_ctx;
  OK(sdis_scene_create(dev, &scn_args, &box_matriochka_scn));

  /* Setup and run the simulation */
  probe[0] = 0.1;
  probe[1] = 0.06;
  probe[2] = 0.130;
  time[0] = time[1] = 500; /* Observation time range */

  /* Compute the solution */
  Tref = temperature_analytical
    (Tbounds, Tinit, boxsz, probe, time[0], rho, cp, lambda);

  /* Run simulation on regular scene */
  solid_param->delta = 1.0/30.0 * MMIN(MMIN(boxsz[0], boxsz[1]), boxsz[2]);
  solve_args.nrealisations = nrealisations;
  solve_args.position[0] = probe[0];
  solve_args.position[1] = probe[1];
  solve_args.position[2] = probe[2];
  solve_args.time_range[0] = time[0];
  solve_args.time_range[1] = time[1];
  OK(sdis_solve_probe(box_scn, &solve_args, &estimator));

  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &temperature));
  printf("Temperature at (%g, %g, %g) m at %g s (delta = %g) = %g ~ %g +/- %g\n",
    SPLIT3(probe), time[0], solid_param->delta, Tref, temperature.E,
    temperature.SE);
  printf("#failures = %lu/%lu\n",
    (unsigned long)nfails, (unsigned long)nrealisations);
  CHK(eq_eps(Tref, temperature.E, temperature.SE*3));
  OK(sdis_estimator_ref_put(estimator));

  /* Run simulation on split scene */
  solid_param->delta = 1.0/30.0 * MMIN(MMIN(boxsz[0]/2.0, boxsz[1]), boxsz[2]);
  OK(sdis_solve_probe(box2_scn, &solve_args, &estimator));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &temperature));
  printf("Temperature at (%g, %g, %g) m at %g s (delta = %g) = %g ~ %g +/- %g\n",
    SPLIT3(probe), time[0], solid_param->delta, Tref, temperature.E,
    temperature.SE);
  printf("#failures = %lu/%lu\n",
    (unsigned long)nfails, (unsigned long)nrealisations);
  CHK(eq_eps(Tref, temperature.E, temperature.SE*3));
  OK(sdis_estimator_ref_put(estimator));

  /* Run simulation on matriochkas */
  solid_param->delta = MMIN(MMIN(boxsz[0], boxsz[1]), boxsz[2]);
  solid_param->delta /= (double)nmatriochkas;
  solid_param->delta *= 1.0/30.0;
  OK(sdis_solve_probe(box_matriochka_scn, &solve_args, &estimator));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &temperature));
  printf("Temperature at (%g, %g, %g) m at %g s (delta = %g) = %g ~ %g +/- %g\n",
    SPLIT3(probe), time[0], solid_param->delta, Tref, temperature.E,
    temperature.SE);
  printf("#failures = %lu/%lu\n",
    (unsigned long)nfails, (unsigned long)nrealisations);
  CHK(eq_eps(Tref, temperature.E, temperature.SE*3));
  OK(sdis_estimator_ref_put(estimator));

  OK(sdis_interface_ref_put(interfs[0]));
  OK(sdis_interface_ref_put(interfs[1]));
  OK(sdis_interface_ref_put(interfs[2]));
  OK(sdis_interface_ref_put(interfs[3]));
  OK(sdis_interface_ref_put(interfs[4]));
  OK(sdis_interface_ref_put(interfs[5]));
  OK(sdis_interface_ref_put(interfs[6]));
  OK(sdis_data_ref_put(data));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_device_ref_put(dev));
  OK(sdis_scene_ref_put(box_scn));
  OK(sdis_scene_ref_put(box2_scn));
  OK(sdis_scene_ref_put(box_matriochka_scn));

  CHK(mem_allocated_size() == 0);
  return 0;
}
