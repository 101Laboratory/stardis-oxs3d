/* Copyright (C) 2015-2025 |Méso|Star> (contact@meso-star.com)
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

#include <rsys/rsys_math.h>
#include "ssp_rng_c.h"


static FINLINE float
sinf2cosf(const float d)
{
  return sqrtf(MMAX(0, 1 - d*d));
}

static FINLINE float
cosf2sinf(const float d)
{
  return sinf2cosf(d);
}

/*******************************************************************************
 * Exported state free random variates
 ******************************************************************************/
double
ssp_ran_exp(struct ssp_rng* rng, const double mu)
{
  ASSERT(rng && mu >= 0);
  RAN_NAMESPACE::exponential_distribution<double> distribution(mu);
  return wrap_ran(*rng, distribution);
}

float
ssp_ran_exp_float(struct ssp_rng* rng, const float mu)
{
  ASSERT(rng && mu >= 0);
  RAN_NAMESPACE::exponential_distribution<float> distribution(mu);
  return wrap_ran(*rng, distribution);
}

double
ssp_ran_exp_pdf(const double x, const double mu)
{
  ASSERT(x >= 0 && mu > 0);
  return mu * exp(-x * mu);
}

float
ssp_ran_exp_float_pdf(const float x, const float mu)
{
  ASSERT(x >= 0 && mu > 0);
  return mu * expf(-x * mu);
}

double
ssp_ran_exp_truncated(struct ssp_rng* rng, const double mu, const double max)
{
  double u, r;
  ASSERT(rng && mu > 0 && max > 0);
  u = ssp_rng_canonical(rng);
  r = -log(1 - u * (1 - exp(-mu * max))) / mu;
  return r;
}

float
ssp_ran_exp_truncated_float(struct ssp_rng* rng, const float mu, const float max)
{
  float u, r;
  ASSERT(rng && mu > 0 && max > 0);
  u = ssp_rng_canonical_float(rng);
  r = -logf(1 - u * (1 - expf(-mu * max))) / mu;
  return r;
}

double
ssp_ran_exp_truncated_pdf(const double x, const double mu, const double max)
{
  ASSERT(x >= 0 && mu > 0 && max > 0);
  if(x > max)
    return 0;
  else {
    double norm = mu / (1 - exp(-max * mu));
    return norm * exp(-x * mu);
  }
}

float
ssp_ran_exp_truncated_float_pdf(const float x, const float mu, const float max)
{
  ASSERT(x >= 0 && mu > 0 && max > 0);
  if(x > max)
    return 0;
  else {
    float norm = mu / (1 - expf(-max * mu));
    return norm * expf(-x * mu);
  }
}

double
ssp_ran_gaussian
  (struct ssp_rng* rng,
   const double mu,
   const double sigma)
{
  ASSERT(rng);
  RAN_NAMESPACE::normal_distribution<double> distribution(mu, sigma);
  return wrap_ran(*rng, distribution);
}

float
ssp_ran_gaussian_float
  (struct ssp_rng* rng,
   const float mu,
   const float sigma)
{
  ASSERT(rng);
  RAN_NAMESPACE::normal_distribution<float> distribution(mu, sigma);
  return wrap_ran(*rng, distribution);
}

double
ssp_ran_gaussian_pdf
  (const double x,
   const double mu,
   const double sigma)
{
  const double tmp = (x - mu) / sigma;
  return 1 / (sigma * SQRT_2_PI) * exp(-0.5 * tmp * tmp);
}

float
ssp_ran_gaussian_float_pdf
  (const float x,
   const float mu,
   const float sigma)
{
  const float tmp = (x - mu) / sigma;
  return 1 / (sigma * (float)SQRT_2_PI) * expf(-0.5f * tmp * tmp);
}

double
ssp_ran_lognormal
  (struct ssp_rng* rng,
   const double zeta,
   const double sigma)
{
  ASSERT(rng);
  RAN_NAMESPACE::lognormal_distribution<double> distribution(zeta, sigma);
  return wrap_ran(*rng, distribution);
}

float
ssp_ran_lognormal_float
  (struct ssp_rng* rng,
   const float zeta,
   const float sigma)
{
  ASSERT(rng);
  RAN_NAMESPACE::lognormal_distribution<float> distribution(zeta, sigma);
  return wrap_ran(*rng, distribution);;
}

double
ssp_ran_lognormal_pdf
  (const double x,
   const double zeta,
   const double sigma)
{
  const double tmp = log(x) - zeta;
  return 1 / (sigma * x * SQRT_2_PI) * exp(-(tmp*tmp) / (2*sigma*sigma));
}

float
ssp_ran_lognormal_float_pdf
  (const float x,
   const float zeta,
   const float sigma)
{
  const float tmp = logf(x) - zeta;
  return 1 / (sigma * x * (float)SQRT_2_PI) * expf(-(tmp*tmp) / (2 * sigma*sigma));
}

double*
ssp_ran_sphere_uniform
  (struct ssp_rng* rng,
   double sample[3],
   double* pdf)
{
  double phi, cos_theta, sin_theta, v;
  ASSERT(rng && sample);
  phi = ssp_rng_uniform_double(rng, 0, 2 * PI);
  v = ssp_rng_canonical(rng);
  cos_theta = 1 - 2 * v;
  sin_theta = 2 * sqrt(v * (1 - v));
  sample[0] = cos(phi) * sin_theta;
  sample[1] = sin(phi) * sin_theta;
  sample[2] = cos_theta;
  if(pdf) *pdf = 1 / (4*PI);
  return sample;
}

float*
ssp_ran_sphere_uniform_float
  (struct ssp_rng* rng,
   float sample[3],
   float* pdf)
{
  float phi, cos_theta, sin_theta, v;
  ASSERT(rng && sample);
  phi = ssp_rng_uniform_float(rng, 0, 2 * (float)PI);
  v = ssp_rng_canonical_float(rng);
  cos_theta = 1 - 2 * v;
  sin_theta = 2 * sqrtf(v * (1 - v));
  sample[0] = cosf(phi) * sin_theta;
  sample[1] = sinf(phi) * sin_theta;
  sample[2] = cos_theta;
  if(pdf) *pdf = 1 / (4*(float)PI);
  return sample;
}

double*
ssp_ran_circle_uniform
  (struct ssp_rng* rng,
   double sample[2],
   double* pdf)
{
  double theta;
  ASSERT(rng && sample);
  theta = ssp_rng_uniform_double(rng, 0, 2*PI);
  sample[0] = cos(theta);
  sample[1] = sin(theta);
  if(pdf) *pdf = 1/(2*PI);
  return sample;
}

float*
ssp_ran_circle_uniform_float
  (struct ssp_rng* rng,
   float sample[2],
   float* pdf)
{
  float theta;
  ASSERT(rng && sample);
  theta = ssp_rng_uniform_float(rng, 0, 2*(float)PI);
  sample[0] = cosf(theta);
  sample[1] = sinf(theta);
  if(pdf) *pdf = 1/(2*(float)PI);
  return sample;
}

double*
ssp_ran_triangle_uniform
  (struct ssp_rng* rng,
   const double v0[3],
   const double v1[3],
   const double v2[3],
   double sample[3],
   double* pdf)
{
  double sqrt_u, v, one_minus_u;
  double vec0[3];
  double vec1[3];
  double tmp[3];
  ASSERT(rng && v0 && v1 && v2 && sample);

  sqrt_u = sqrt(ssp_rng_canonical(rng));
  v = ssp_rng_canonical(rng);
  one_minus_u = 1.0 - sqrt_u;

  d3_sub(vec0, v0, v2);
  d3_sub(vec1, v1, v2);
  sample[0] = v2[0] + one_minus_u * vec0[0] + v * sqrt_u * vec1[0];
  sample[1] = v2[1] + one_minus_u * vec0[1] + v * sqrt_u * vec1[1];
  sample[2] = v2[2] + one_minus_u * vec0[2] + v * sqrt_u * vec1[2];
  if(pdf) *pdf = 2 / d3_len(d3_cross(tmp, vec0, vec1));
  return sample;
}

float*
ssp_ran_triangle_uniform_float
  (struct ssp_rng* rng,
   const float v0[3],
   const float v1[3],
   const float v2[3],
   float sample[3],
   float* pdf)
{
  float sqrt_u, v, one_minus_u;
  float vec0[3];
  float vec1[3];
  float tmp[3];
  ASSERT(rng && v0 && v1 && v2 && sample);

  sqrt_u = sqrtf(ssp_rng_canonical_float(rng));
  v = ssp_rng_canonical_float(rng);
  one_minus_u = 1 - sqrt_u;

  f3_sub(vec0, v0, v2);
  f3_sub(vec1, v1, v2);
  sample[0] = v2[0] + one_minus_u * vec0[0] + v * sqrt_u * vec1[0];
  sample[1] = v2[1] + one_minus_u * vec0[1] + v * sqrt_u * vec1[1];
  sample[2] = v2[2] + one_minus_u * vec0[2] + v * sqrt_u * vec1[2];
  if(pdf) *pdf = 2 / f3_len(f3_cross(tmp, vec0, vec1));
  return sample;
}

double*
ssp_ran_tetrahedron_uniform
  (struct ssp_rng* rng,
   const double v0[3],
   const double v1[3],
   const double v2[3],
   const double v3[3],
   double sample[3],
   double* pdf)
{
  double a, s, t, u; /* Barycentric coordinates of the sampled point. */
  double tmp0[3], tmp1[3], tmp2[3], tmp3[3], tmp4[3], tmp5[3];
  ASSERT(rng && v0 && v1 && v2 && v3 && sample);

  s = ssp_rng_canonical(rng);
  t = ssp_rng_canonical(rng);
  u = ssp_rng_canonical(rng);

  if(s + t > 1) { /* Cut and fold the cube into a prism */
    s = 1 - s;
    t = 1 - t;
  }
  if(t + u > 1) { /* Cut and fold the prism into a tetrahedron */
    double swp = u;
    u = 1 - s - t;
    t = 1 - swp;
  }
  else if(s + t + u > 1) {
    double swp = u;
    u = s + t + u - 1;
    s = 1 - t - swp;
  }
  a = 1 - s - t - u;

  d3_add(sample,
    d3_add(tmp4, d3_muld(tmp0, v0, a), d3_muld(tmp1, v1, s)),
    d3_add(tmp5, d3_muld(tmp2, v2, t), d3_muld(tmp3, v3, u)));

  if(pdf)
    *pdf = ssp_ran_tetrahedron_uniform_pdf(v0, v1, v2, v3);

  return sample;
}

float*
ssp_ran_tetrahedron_uniform_float
  (struct ssp_rng* rng,
   const float v0[3],
   const float v1[3],
   const float v2[3],
   const float v3[3],
    float sample[3],
    float* pdf)
{
  float a, s, t, u; /* Barycentric coordinates of the sampled point. */
  float tmp0[3], tmp1[3], tmp2[3], tmp3[3], tmp4[3], tmp5[3];
  ASSERT(rng && v0 && v1 && v2 && v3 && sample);

  s = ssp_rng_canonical_float(rng);
  t = ssp_rng_canonical_float(rng);
  u = ssp_rng_canonical_float(rng);

  if(s + t > 1) { /* Cut and fold the cube into a prism */
    s = 1 - s;
    t = 1 - t;
  }
  if(t + u > 1) { /* Cut and fold the prism into a tetrahedron */
    float swp = u;
    u = 1 - s - t;
    t = 1 - swp;
  }
  else if(s + t + u > 1) {
    float swp = u;
    u = s + t + u - 1;
    s = 1 - t - swp;
  }
  a = 1 - s - t - u;

  f3_add(sample,
    f3_add(tmp4, f3_mulf(tmp0, v0, a), f3_mulf(tmp1, v1, s)),
    f3_add(tmp5, f3_mulf(tmp2, v2, t), f3_mulf(tmp3, v3, u)));

  if(pdf)
    *pdf = ssp_ran_tetrahedron_uniform_float_pdf(v0, v1, v2, v3);

  return sample;
}

double*
ssp_ran_hemisphere_uniform_local
  (struct ssp_rng* rng,
   double sample[3],
   double* pdf)
{
  double phi, cos_theta, sin_theta;
  ASSERT(rng && sample);
  phi = ssp_rng_uniform_double(rng, 0, 2 * PI);
  cos_theta = ssp_rng_canonical(rng);
  sin_theta = cos2sin(cos_theta);
  sample[0] = cos(phi) * sin_theta;
  sample[1] = sin(phi) * sin_theta;
  sample[2] = cos_theta;
  if(pdf) *pdf = 1 / (2*PI);
  return sample;
}

float*
ssp_ran_hemisphere_uniform_float_local
  (struct ssp_rng* rng,
   float sample[3],
   float* pdf)
{
  float phi, cos_theta, sin_theta;
  ASSERT(rng && sample);
  phi = ssp_rng_uniform_float(rng, 0, 2 * (float)PI);
  cos_theta = ssp_rng_canonical_float(rng);
  sin_theta = cosf2sinf(cos_theta);
  sample[0] = cosf(phi) * sin_theta;
  sample[1] = sinf(phi) * sin_theta;
  sample[2] = cos_theta;
  if(pdf) *pdf = 1 / (2*(float)PI);
  return sample;
}

double*
ssp_ran_hemisphere_cos_local
  (struct ssp_rng* rng,
   double sample[3],
   double* pdf)
{
  double phi, cos_theta, sin_theta, v;
  ASSERT(rng && sample);
  phi = ssp_rng_uniform_double(rng, 0, 2 * PI);
  v = ssp_rng_canonical(rng);
  cos_theta = sqrt(v);
  sin_theta = sqrt(1 - v);
  sample[0] = cos(phi) * sin_theta;
  sample[1] = sin(phi) * sin_theta;
  sample[2] = cos_theta;
  if(pdf) *pdf = cos_theta / PI;
  return sample;
}

float*
ssp_ran_hemisphere_cos_float_local
  (struct ssp_rng* rng,
   float sample[3],
   float* pdf)
{
  float phi, cos_theta, sin_theta, v;
  ASSERT(rng && sample);
  phi = ssp_rng_uniform_float(rng, 0, 2 * (float)PI);
  v = ssp_rng_canonical_float(rng);
  cos_theta = sqrtf(v);
  sin_theta = sqrtf(1 - v);
  sample[0] = cosf(phi) * sin_theta;
  sample[1] = sinf(phi) * sin_theta;
  sample[2] = cos_theta;
  if(pdf) *pdf = cos_theta / (float)PI;
  return sample;
}

double
ssp_ran_sphere_hg_local_pdf
  (const double g,
   const double sample[3])
{
  double epsilon_g, epsilon_mu;
  ASSERT(fabs(g) <= 1 && sample && d3_is_normalized(sample));
  if(g>0) {
    epsilon_g = 1 - g;
    epsilon_mu = 1 - sample[2];
  } else {
    epsilon_g = 1 + g;
    epsilon_mu = 1 + sample[2];
  }
  return 1 / (4 * PI)
    *epsilon_g*(2 - epsilon_g)
    *pow(epsilon_g*epsilon_g + 2 * epsilon_mu*(1 - epsilon_g), -1.5);
}

float
ssp_ran_sphere_hg_float_local_pdf
  (const float g,
   const float sample[3])
{
  float epsilon_g, epsilon_mu;
  ASSERT(fabsf(g) <= 1 && sample && f3_is_normalized(sample));
  if(g>0) {
    epsilon_g = 1 - g;
    epsilon_mu = 1 - sample[2];
  } else {
    epsilon_g = 1 + g;
    epsilon_mu = 1 + sample[2];
  }
  return 1 / (4 * (float)PI)
    *epsilon_g*(2 - epsilon_g)
    *powf(epsilon_g*epsilon_g + 2 * epsilon_mu*(1 - epsilon_g), -1.5f);
}

double*
ssp_ran_sphere_hg_local
  (struct ssp_rng* rng,
   const double g,
   double sample[3],
   double* pdf)
{
  double phi, R, cos_theta, sin_theta;
  ASSERT(fabs(g) <= 1 && rng && sample);
  if(fabs(g) == 1) {
    d3(sample, 0, 0, g);
    if(pdf) *pdf = INF;
  } else {
    phi = ssp_rng_uniform_double(rng, 0, 2 * PI);
    R = ssp_rng_canonical(rng);
    cos_theta = 2 * R*(1 + g)*(1 + g)*(g*(R - 1) + 1)
      / ((1 - g * (1 - 2 * R))*(1 - g * (1 - 2 * R))) - 1;
    sin_theta = cos2sin(cos_theta);
    sample[0] = cos(phi) * sin_theta;
    sample[1] = sin(phi) * sin_theta;
    sample[2] = cos_theta;
    if(pdf) *pdf = ssp_ran_sphere_hg_local_pdf(g, sample);
  }
  return sample;
}

float*
ssp_ran_sphere_hg_float_local
  (struct ssp_rng* rng,
   const float g,
   float sample[3],
   float* pdf)
{
  float phi, R, cos_theta, sin_theta;
  ASSERT(fabsf(g) <= 1 && rng && sample);
  if(fabsf(g) == 1) {
    f3(sample, 0, 0, g);
    if(pdf) *pdf = (float)INF;
  } else {
    phi = ssp_rng_uniform_float(rng, 0, 2 * (float)PI);
    R = ssp_rng_canonical_float(rng);
    cos_theta = 2 * R*(1 + g)*(1 + g)*(g*(R - 1) + 1)
      / ((1 - g * (1 - 2 * R))*(1 - g * (1 - 2 * R))) - 1;
    sin_theta = cosf2sinf(cos_theta);
    sample[0] = cosf(phi) * sin_theta;
    sample[1] = sinf(phi) * sin_theta;
    sample[2] = cos_theta;
    if(pdf) *pdf = ssp_ran_sphere_hg_float_local_pdf(g, sample);
  }
  return sample;
}

double
ssp_ran_sphere_hg_pdf
  (const double up[3],
   const double g,
   const double sample[3])
{
  double epsilon_g, epsilon_mu;
  ASSERT(-1 <= g && g <= +1 &&
    up && sample && d3_is_normalized(up) && d3_is_normalized(sample));
  if(fabs(g) == 1) return INF;
  if(g>0) {
    epsilon_g = 1 - g;
    epsilon_mu = 1 - d3_dot(sample, up);
  } else {
    epsilon_g = 1 + g;
    epsilon_mu = 1 + d3_dot(sample, up);
  }
  return 1 / (4 * PI)
    *epsilon_g*(2 - epsilon_g)
    *pow(epsilon_g*epsilon_g + 2 * epsilon_mu*(1 - epsilon_g), -1.5);
}

float
ssp_ran_sphere_hg_float_pdf
  (const float up[3],
   const float g,
   const float sample[3])
{
  float epsilon_g, epsilon_mu;
  ASSERT(-1 <= g && g <= +1 &&
    up && sample && f3_is_normalized(up) && f3_is_normalized(sample));
  if(fabsf(g) == 1) return (float)INF;
  if(g>0) {
    epsilon_g = 1 - g;
    epsilon_mu = 1 - f3_dot(sample, up);
  } else {
    epsilon_g = 1 + g;
    epsilon_mu = 1 + f3_dot(sample, up);
  }
  return 1 / (4 * (float)PI)
    *epsilon_g*(2 - epsilon_g)
    *powf(epsilon_g*epsilon_g + 2 * epsilon_mu*(1 - epsilon_g), -1.5f);
}

double*
ssp_ran_uniform_disk_local
  (struct ssp_rng* rng,
   const double radius,
   double pt[3],
   double* pdf)
{
  double theta, r;
  ASSERT(rng && pt && radius > 0);
  theta = ssp_rng_uniform_double(rng, 0, 2 * PI);
  r = radius * sqrt(ssp_rng_canonical(rng));
  pt[0] = r * cos(theta);
  pt[1] = r * sin(theta);
  pt[2] = 0;
  if(pdf) *pdf = 1 / (radius * radius);
  return pt;
}

float*
ssp_ran_uniform_disk_float_local
  (struct ssp_rng* rng,
   const float radius,
   float pt[3],
   float* pdf)
{
  float theta, r;
  ASSERT(rng && pt && radius > 0);
  theta = ssp_rng_uniform_float(rng, 0, 2 * (float)PI);
  r = radius * sqrtf(ssp_rng_canonical_float(rng));
  pt[0] = r * cosf(theta);
  pt[1] = r * sinf(theta);
  pt[2] = 0;
  if(pdf) *pdf = 1 / (radius * radius);
  return pt;
}

double*
ssp_ran_sphere_cap_uniform_local
  (struct ssp_rng* rng,
   const double height,
   double pt[3],
   double* pdf)
{
  ASSERT(rng && pt && (height >= -1 || height <= 1));

  if(height == 1) {
    d3(pt, 0, 0, 1);
    if(pdf) *pdf = INF;
  } else if(height == -1) {
    ssp_ran_sphere_uniform(rng, pt, pdf);
  } else {
    const double cos_aperture = height;
    double phi, cos_theta, sin_theta;
    phi = ssp_rng_uniform_double(rng, 0, 2.0*PI);
    cos_theta = ssp_rng_uniform_double(rng, cos_aperture, 1);
    sin_theta = cos2sin(cos_theta);
    pt[0] = cos(phi) * sin_theta;
    pt[1] = sin(phi) * sin_theta;
    pt[2] = cos_theta;
    if(pdf) *pdf = 1.0/(2.0*PI*(1.0-cos_aperture));
  }
  return pt;
}

float*
ssp_ran_sphere_cap_uniform_float_local
  (struct ssp_rng* rng,
   const float height,
   float pt[3],
   float* pdf)
{
  ASSERT(rng && pt && (height >= -1 || height <= 1));
  if(height == 1) {
    f3(pt, 0, 0, 1);
    if(pdf) *pdf = (float)INF;
  } else if(height == -1) {
    ssp_ran_sphere_uniform_float(rng, pt, pdf);
  } else {
    const float cos_aperture = height;
    float phi, cos_theta, sin_theta;
    phi = ssp_rng_uniform_float(rng, 0, 2.f*(float)PI);
    cos_theta = ssp_rng_uniform_float(rng, cos_aperture, 1);
    sin_theta = (float)cos2sin(cos_theta);
    pt[0] = cosf(phi) * sin_theta;
    pt[1] = sinf(phi) * sin_theta;
    pt[2] = cos_theta;
    if(pdf) *pdf = 1.f/(2.f*(float)PI*(1.f-cos_aperture));
  }
  return pt;
}

double*
ssp_ran_spherical_zone_uniform_local
  (struct ssp_rng* rng,
   const double height_range[2],
   double pt[3],
   double* pdf)
{
  ASSERT(rng && pt && height_range
    && height_range[0] <= height_range[1]
    && height_range[0] >= -1 && height_range[1] <= 1);

  if(height_range[1] == 1) {
    ssp_ran_sphere_cap_uniform_local(rng, height_range[0], pt, pdf);
  }
  else if(height_range[0] == height_range[1]) {
    double sin_theta = cos2sin(height_range[0]);
    ssp_ran_circle_uniform(rng, pt, pdf);
    pt[0] *= sin_theta;
    pt[1] *= sin_theta;
    pt[2] = height_range[0];
  } else {
    const double cos_aperture_min = height_range[0];
    const double cos_aperture_max = height_range[1];
    double phi, cos_theta, sin_theta;
    phi = ssp_rng_uniform_double(rng, 0, 2.0*PI);
    cos_theta = ssp_rng_uniform_double(rng, cos_aperture_min, cos_aperture_max);
    sin_theta = cos2sin(cos_theta);
    pt[0] = cos(phi) * sin_theta;
    pt[1] = sin(phi) * sin_theta;
    pt[2] = cos_theta;
    if(pdf) *pdf = 1.0/(2.0*PI*(cos_aperture_max-cos_aperture_min));
  }
  return pt;
}

float*
ssp_ran_spherical_zone_uniform_float_local
  (struct ssp_rng* rng,
   const float height_range[2],
   float pt[3],
   float* pdf)
{
  ASSERT(rng && pt && height_range
    && height_range[0] <= height_range[1]
    && height_range[0] >= -1 && height_range[1] <= 1);

  if(height_range[1] == 1) {
    ssp_ran_sphere_cap_uniform_float_local(rng, height_range[0], pt, pdf);
  }
  else if(height_range[0] == height_range[1]) {
    float sin_theta = (float)cos2sin(height_range[0]);
    ssp_ran_circle_uniform_float(rng, pt, pdf);
    pt[0] *= sin_theta;
    pt[1] *= sin_theta;
    pt[2] = height_range[0];
  } else {
    const float cos_aperture_min = height_range[0];
    const float cos_aperture_max = height_range[1];
    float phi, cos_theta, sin_theta;
    phi = ssp_rng_uniform_float(rng, 0, 2.f*(float)PI);
    cos_theta = ssp_rng_uniform_float(rng, cos_aperture_min, cos_aperture_max);
    sin_theta = (float)cos2sin(cos_theta);
    pt[0] = cosf(phi) * sin_theta;
    pt[1] = sinf(phi) * sin_theta;
    pt[2] = cos_theta;
    if(pdf) *pdf = 1.f/(2.f*(float)PI*(cos_aperture_max-cos_aperture_min));
  }
  return pt;
}

