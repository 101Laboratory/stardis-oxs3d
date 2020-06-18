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

#define UNKNOWN_TEMPERATURE -1

/*******************************************************************************
 * Geometry functions
 ******************************************************************************/
struct context {
  struct sdis_interface* interfs[12];
  const double* boxsz;
};

static void
get_scaled_position(const size_t ivert, double pos[3], void* context)
{
  struct context* ctx = context;
  CHK(ctx);
  box_get_position(ivert, pos, context);
  pos[0] *= ctx->boxsz[0];
  pos[1] *= ctx->boxsz[1];
  pos[2] *= ctx->boxsz[2];
}

static void
get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  struct context* ctx = context;
  CHK(ctx);
  *bound = ctx->interfs[itri];
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
    return UNKNOWN_TEMPERATURE;
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
  struct mem_allocator allocator;
  struct sdis_device* dev = NULL;
  struct sdis_scene* box_scn = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_data* data = NULL;
  struct sdis_fluid_shader fluid_shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interfs[6] = {NULL};
  struct sdis_estimator* estimator = NULL;
  struct sdis_mc temperature = SDIS_MC_NULL;
  struct solid* solid_param = NULL;
  struct context ctx;
  const size_t nrealisations = 100000;
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
  Tinit = 300;
  boxsz[0] = 0.3;
  boxsz[1] = 0.1;
  boxsz[2] = 0.2;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev));

  /* Create the fluid medium */
  fluid_shader = DUMMY_FLUID_SHADER;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;

  /* Create the solid medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->rho = rho;
  solid_param->cp = cp;
  solid_param->lambda = lambda;
  solid_param->delta = 1.0/20.0 * MMIN(MMIN(boxsz[0], boxsz[1]), boxsz[2]);
  solid_param->init_temperature = Tinit;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  /* Setup the interface shader */
  interf_shader.front.temperature = interface_get_temperature;

  /* Create the interfaces */
  interfs[0] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[0]);
  interfs[1] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[1]);
  interfs[2] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[2]);
  interfs[3] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[3]);
  interfs[4] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[4]);
  interfs[5] = create_interface(dev, solid, fluid, &interf_shader, Tbounds[5]);

  /* Setup the scene context */
  ctx.interfs[0]  = ctx.interfs[1]  = interfs[4]; /* Zmin */
  ctx.interfs[2]  = ctx.interfs[3]  = interfs[0]; /* Xmin */
  ctx.interfs[4]  = ctx.interfs[5]  = interfs[5]; /* Zmax */
  ctx.interfs[6]  = ctx.interfs[7]  = interfs[1]; /* Xmax */
  ctx.interfs[8]  = ctx.interfs[9]  = interfs[3]; /* Ymax */
  ctx.interfs[10] = ctx.interfs[11] = interfs[2]; /* Ymin */
  ctx.boxsz = boxsz;

  /* Create the box scene */
  OK(sdis_scene_create(dev, box_ntriangles, box_get_indices, get_interface,
    box_nvertices, get_scaled_position, &ctx, &box_scn));

  /* Setup and run the simulation */
  probe[0] = 0.1;
  probe[1] = 0.06;
  probe[2] = 0.130;
  time[0] = time[1] = 1000; /* Observation time range */
  OK(sdis_solve_probe(box_scn, nrealisations, probe, time, 1, 0, 290,
    SDIS_HEAT_PATH_NONE, &estimator));

  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &temperature));

  Tref = temperature_analytical
    (Tbounds, Tinit, boxsz, probe, time[0], rho, cp, lambda);

  printf("Temperature at (%g, %g, %g) m at %g s = %g ~ %g +/- %g\n",
    SPLIT3(probe), time[0], Tref, temperature.E, temperature.SE);
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
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_device_ref_put(dev));
  OK(sdis_scene_ref_put(box_scn));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
