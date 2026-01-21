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

#include "ssp_rng_c.h"

#include <rsys/dynamic_array_char.h>
#include <rsys/mem_allocator.h>

#include <sstream>
#include <cstring>
#include <limits>

/*******************************************************************************
 * KISS PRNG
 ******************************************************************************/
struct rng_kiss { uint32_t x, y, z, c; };

static res_T
rng_kiss_set(void* data, const uint64_t seed)
{
  struct rng_kiss* kiss = (struct rng_kiss*)data;
  RAN_NAMESPACE::mt19937 rng_mt(uint32_t(seed % UINT32_MAX));
  ASSERT(kiss);
  kiss->x = (uint32_t)rng_mt();
  do {
    kiss->y = (uint32_t)rng_mt();
  } while(kiss->y == 0); /* Y must be != 0 */
  kiss->z = (uint32_t)rng_mt();
  /* Offset c by 1 to avoid z=c=0; should be less than 698769069 */
  kiss->c = (uint32_t)rng_mt() % 698769068 + 1;
  return RES_OK;
}

static uint64_t
rng_kiss_get(void* data)
{
  struct rng_kiss* kiss = (struct rng_kiss*)data;
  uint64_t t;
  ASSERT(kiss);

  kiss->x = 314527869 * kiss->x + 1234567;
  kiss->y ^= kiss->y << 5;
  kiss->y ^= kiss->y >> 7;
  kiss->y ^= kiss->y << 22;
  t = 4294584393ULL * kiss->z + kiss->c;
  kiss->c = (uint32_t)(t >> 32);
  kiss->z = (uint32_t)(t & 0xFFFFFFFF);
  return (uint32_t)(kiss->x + kiss->y + kiss->z);
}

static res_T
rng_kiss_read(void* data, FILE* file)
{
  uint32_t val[4];
  struct rng_kiss* rng = (struct rng_kiss*)data;
  size_t n;
  ASSERT(data && file);
  n = fread(val, sizeof(*val), sizeof(val)/sizeof(*val), file);
  if(n != 4) return RES_IO_ERR;
  rng->x = val[0];
  rng->y = val[1];
  rng->z = val[2];
  rng->c = val[3];
  return RES_OK;
}

static res_T
rng_kiss_read_cstr(void* data, const char* cstr)
{
  struct rng_kiss* rng = (struct rng_kiss*)data;
  uint32_t val[4];
  size_t sz;
  ASSERT(data && cstr);
  sz = strlen(cstr);
  if(sz < sizeof(val)) return RES_IO_ERR;
  memcpy(val, cstr, sizeof(val));
  rng->x = val[0];
  rng->y = val[1];
  rng->z = val[2];
  rng->c = val[3];
  return RES_OK;
}

static res_T
rng_kiss_write(const void* data, FILE* file)
{
  const struct rng_kiss* rng = (const struct rng_kiss*)data;
  uint32_t val[4];
  size_t n;
  ASSERT(data && file);
  val[0] = rng->x;
  val[1] = rng->y;
  val[2] = rng->z;
  val[3] = rng->c;
  n = fwrite(val, sizeof(*val), sizeof(val)/sizeof(*val), file);
  return (n != 4 ? RES_IO_ERR : RES_OK);
}

static res_T
rng_kiss_write_cstr
  (const void* data,
   char* buf,
   const size_t bufsz,
   size_t* out_len)
{
  const struct rng_kiss* rng = (const struct rng_kiss*)data;
  uint32_t val[4];
  ASSERT(data);

  val[0] = rng->x;
  val[1] = rng->y;
  val[2] = rng->z;
  val[3] = rng->c;
  if(bufsz >= sizeof(val) + 1/*null char*/) {
    memcpy(buf, val, sizeof(val));
    buf[sizeof(val)] = '\0';
  }
  if(out_len) *out_len = sizeof(val);
  return RES_OK;
}

static res_T
rng_kiss_init(struct mem_allocator* allocator, void* data)
{
  (void)allocator;
  rng_kiss_set(data, 0);
  return RES_OK;
}

static void
rng_kiss_release(void* data)
{
  (void)data;
}

static double
rng_kiss_entropy(const void* data)
{
  (void)data;
  return 0.;
}

static res_T
rng_kiss_discard(void* data, uint64_t n)
{
  while (n-- > 0) {
    rng_kiss_get(data);
  }
  return RES_OK;
}

/* Exported type */
static const struct rng_desc rng_kiss = {
  rng_kiss_init,
  rng_kiss_release,
  rng_kiss_set,
  rng_kiss_get,
  rng_kiss_discard,
  rng_kiss_read,
  rng_kiss_read_cstr,
  rng_kiss_write,
  rng_kiss_write_cstr,
  rng_kiss_entropy,
  0,
  UINT32_MAX,
  sizeof(struct rng_kiss),
  16
};

/*******************************************************************************
 * C++11 PRNG
 ******************************************************************************/
template<typename RNG>
static res_T
rng_cxx_set(void* data, const uint64_t seed)
{
  RNG* rng = (RNG*)data;
  ASSERT(rng);
  rng->seed((typename RNG::result_type)seed);
  return RES_OK;
}

template<>
res_T
rng_cxx_set<RAN_NAMESPACE::random_device>(void* data, const uint64_t seed)
{
  (void) data; (void) seed;
  return RES_BAD_OP;
}

template<typename RNG>
static uint64_t
rng_cxx_get(void* data)
{
  RNG* rng = (RNG*)data;
  ASSERT(rng);
  return (*rng)();
}

template<typename RNG>
static res_T
rng_cxx_write(const void* data, FILE* file)
{
  size_t i;
  std::stringstream stream;
  RNG* rng = (RNG*)data;
  ASSERT(rng);
  stream << *rng << std::endl;
  i = fwrite(stream.str().c_str(), 1, stream.str().size(), file);
  return i == stream.str().size() ? RES_OK : RES_IO_ERR;
}

template<>
res_T
rng_cxx_write<RAN_NAMESPACE::random_device>(const void* data, FILE* file)
{
  (void) data; (void) file;
  return RES_BAD_OP;
}

template<typename RNG>
static res_T
rng_cxx_write_cstr
  (const void* data,
   char* buf,
   const size_t bufsz,
   size_t* out_len)
{
  int len = 0;
  std::stringstream stream;
  RNG* rng = (RNG*)data;
  ASSERT(rng);
  stream << *rng << std::endl;
  len = snprintf(buf, bufsz, "%s", stream.str().c_str());
  CHK(len > 0);
  if((size_t)len >= (bufsz - 1/*null char*/)) {
    buf[bufsz-1] = '\0';
  }
  if(out_len) *out_len = (size_t)len;
  return RES_OK;
}

template<>
res_T
rng_cxx_write_cstr<RAN_NAMESPACE::random_device>
  (const void* data, char* buf, const size_t bufsz, size_t* out_len)
{
  (void)data; (void)buf, (void)bufsz, (void)out_len;
  return RES_BAD_OP;
}

template<typename RNG>
static res_T
rng_cxx_read(void* data, FILE* file)
{
  std::stringstream stream;
  char buf[512];
  char* s;
  RNG* rng = (RNG*)data;
  ASSERT(rng);
  while((s = fgets(buf, (int)sizeof(buf), file))) {
    stream << std::string(s);
    if(s[strlen(s)-1] == '\n') break;
  }
  stream >> *rng;
  return stream.fail() ? RES_IO_ERR : RES_OK;
}

template<>
res_T
rng_cxx_read<RAN_NAMESPACE::random_device>(void* data, FILE* file)
{
  (void) data; (void)file;
  return RES_BAD_OP;
}

template<typename RNG>
static res_T
rng_cxx_read_cstr(void* data, const char* cstr)
{
  std::stringstream stream;
  RNG* rng = (RNG*)data;
  ASSERT(rng && cstr && cstr[strlen(cstr)-1] == '\n');
  stream << std::string(cstr);
  stream >> *rng;
  return stream.fail() ? RES_IO_ERR : RES_OK;
}

template<>
res_T
rng_cxx_read_cstr<RAN_NAMESPACE::random_device>(void* data, const char* cstr)
{
  (void)data, (void)cstr;
  return RES_BAD_OP;
}

template<typename RNG>
static res_T
rng_cxx_init(struct mem_allocator* allocator, void* data)
{
  (void)allocator;
  ASSERT(data);
  new (data) RNG;
  return RES_OK;
}

template<typename RNG>
static void
rng_cxx_release(void* data)
{
  RNG* rng = (RNG*)data;
  (void)rng;
  ASSERT(rng);
  rng->~RNG();
}

template<typename RNG>
static double
rng_cxx_entropy(const void* data)
{
  (void)data;
  return 0;
}

template<typename RNG>
res_T
rng_cxx_discard(void* data, uint64_t n)
{
  RNG* rng = (RNG*) data;
  ASSERT(rng);
  rng->discard(n);
  return RES_OK;
}

template<>
res_T
rng_cxx_discard<RAN_NAMESPACE::random_device>(void* data, uint64_t n)
{
  (void) data; (void) n;
  return RES_BAD_OP;
}

template<>
double
rng_cxx_entropy<RAN_NAMESPACE::random_device>(const void* data)
{
  const RAN_NAMESPACE::random_device* rng =
    (const RAN_NAMESPACE::random_device*)data;
  ASSERT(rng);
  return rng->entropy();
}

/* 64-bits Mersenne Twister PRNG */
static const struct rng_desc rng_mt19937_64 = {
  rng_cxx_init<RAN_NAMESPACE::mt19937_64>,
  rng_cxx_release<RAN_NAMESPACE::mt19937_64>,
  rng_cxx_set<RAN_NAMESPACE::mt19937_64>,
  rng_cxx_get<RAN_NAMESPACE::mt19937_64>,
  rng_cxx_discard<RAN_NAMESPACE::mt19937_64>,
  rng_cxx_read<RAN_NAMESPACE::mt19937_64>,
  rng_cxx_read_cstr<RAN_NAMESPACE::mt19937_64>,
  rng_cxx_write<RAN_NAMESPACE::mt19937_64>,
  rng_cxx_write_cstr<RAN_NAMESPACE::mt19937_64>,
  rng_cxx_entropy<RAN_NAMESPACE::mt19937_64>,
  RAN_NAMESPACE::mt19937_64::min(),
  RAN_NAMESPACE::mt19937_64::max(),
  sizeof(RAN_NAMESPACE::mt19937_64),
  16
};

/* 48-bits RANLUX PRNG */
static const struct rng_desc rng_ranlux48 = {
  rng_cxx_init<RAN_NAMESPACE::ranlux48>,
  rng_cxx_release<RAN_NAMESPACE::ranlux48>,
  rng_cxx_set<RAN_NAMESPACE::ranlux48>,
  rng_cxx_get<RAN_NAMESPACE::ranlux48>,
  rng_cxx_discard<RAN_NAMESPACE::ranlux48>,
  rng_cxx_read<RAN_NAMESPACE::ranlux48>,
  rng_cxx_read_cstr<RAN_NAMESPACE::ranlux48>,
  rng_cxx_write<RAN_NAMESPACE::ranlux48>,
  rng_cxx_write_cstr<RAN_NAMESPACE::ranlux48>,
  rng_cxx_entropy<RAN_NAMESPACE::ranlux48>,
  RAN_NAMESPACE::ranlux48::min(),
  RAN_NAMESPACE::ranlux48::max(),
  sizeof(RAN_NAMESPACE::ranlux48),
  16
};

/* random_device generator */
static const struct rng_desc rng_random_device = {
  rng_cxx_init<RAN_NAMESPACE::random_device>,
  rng_cxx_release<RAN_NAMESPACE::random_device>,
  rng_cxx_set<RAN_NAMESPACE::random_device>,
  rng_cxx_get<RAN_NAMESPACE::random_device>,
  rng_cxx_discard<RAN_NAMESPACE::random_device>,
  rng_cxx_read<RAN_NAMESPACE::random_device>,
  rng_cxx_read_cstr<RAN_NAMESPACE::random_device>,
  rng_cxx_write<RAN_NAMESPACE::random_device>,
  rng_cxx_write_cstr<RAN_NAMESPACE::random_device>,
  rng_cxx_entropy<RAN_NAMESPACE::random_device>,
  RAN_NAMESPACE::random_device::min(),
  RAN_NAMESPACE::random_device::max(),
  sizeof(RAN_NAMESPACE::random_device),
  16
};

/*******************************************************************************
 * Random123 Counter Based RNG
 ******************************************************************************/
typedef r123::Engine<r123::Threefry4x64> threefry_T;

#ifdef WITH_R123_AES
typedef r123::Engine<r123::AESNI4x32> aes_T;

template<>
res_T
rng_cxx_init<aes_T>(struct mem_allocator* allocator, void* data)
{
  (void) allocator;

  if(!haveAESNI()) {
    /* AES-NI instructions not available on this hardware */
    return RES_BAD_OP;
  }
  ASSERT(data);
  new (data) aes_T;
  return RES_OK;
}
#endif

/* threefry generator */
static const struct rng_desc rng_threefry = {
  rng_cxx_init<threefry_T>,
  rng_cxx_release<threefry_T>,
  rng_cxx_set<threefry_T>,
  rng_cxx_get<threefry_T>,
  rng_cxx_discard<threefry_T>,
  rng_cxx_read<threefry_T>,
  rng_cxx_read_cstr<threefry_T>,
  rng_cxx_write<threefry_T>,
  rng_cxx_write_cstr<threefry_T>,
  rng_cxx_entropy<threefry_T>,
  threefry_T::min(),
  threefry_T::max(),
  sizeof(threefry_T),
  16
};

#ifdef WITH_R123_AES
/* aes generator */
static const struct rng_desc rng_aes = {
  rng_cxx_init<aes_T>,
  rng_cxx_release<aes_T>,
  rng_cxx_set<aes_T>,
  rng_cxx_get<aes_T>,
  rng_cxx_discard<aes_T>,
  rng_cxx_read<aes_T>,
  rng_cxx_read_cstr<aes_T>,
  rng_cxx_write<aes_T>,
  rng_cxx_write_cstr<aes_T>,
  rng_cxx_entropy<aes_T>,
  aes_T::min(),
  aes_T::max(),
  sizeof(aes_T),
  16
};
#endif

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
rng_release(ref_T* ref)
{
  struct ssp_rng* rng;
  ASSERT(ref);
  rng = CONTAINER_OF(ref, struct ssp_rng, ref);
  if(rng->state) {
    if(rng->desc.release) {
      rng->desc.release(rng->state);
    }
    MEM_RM(rng->allocator, rng->state);
  }
  MEM_RM(rng->allocator, rng);
}

/*******************************************************************************
 * Exported function
 ******************************************************************************/
res_T
ssp_rng_create
  (struct mem_allocator* mem_allocator,
   const enum ssp_rng_type type,
   struct ssp_rng** out_rng)
{
  res_T res = RES_OK;

  switch(type) {
    case SSP_RNG_KISS:
      res = rng_create(mem_allocator, &rng_kiss, out_rng);
      break;
    case SSP_RNG_MT19937_64:
      res = rng_create(mem_allocator, &rng_mt19937_64, out_rng);
      break;
    case SSP_RNG_RANLUX48:
      res = rng_create(mem_allocator, &rng_ranlux48, out_rng);
      break;
    case SSP_RNG_RANDOM_DEVICE:
      res = rng_create(mem_allocator, &rng_random_device, out_rng);
      break;
    case SSP_RNG_THREEFRY:
      res = rng_create(mem_allocator, &rng_threefry, out_rng);
      break;
#ifdef WITH_R123_AES
    case SSP_RNG_AES:
      res = rng_create(mem_allocator, &rng_aes, out_rng);
      break;
#endif
    default: res = RES_BAD_ARG; break;
  }
  if(res != RES_OK) goto error;
  (*out_rng)->type = type;

exit:
  return res;
error:
  goto exit;
}

res_T
ssp_rng_ref_get(struct ssp_rng* rng)
{
  if(!rng) return RES_BAD_ARG;
  ref_get(&rng->ref);
  return RES_OK;
}

res_T
ssp_rng_ref_put(struct ssp_rng* rng)
{
  if(!rng) return RES_BAD_ARG;
  ref_put(&rng->ref, rng_release);
  return RES_OK;
}

res_T
ssp_rng_get_type(const struct ssp_rng* rng, enum ssp_rng_type* type)
{
  if(!rng || !type) return RES_BAD_ARG;
  *type = rng->type;
  return RES_OK;
}

uint64_t
ssp_rng_get(struct ssp_rng* rng)
{
  if(!rng) FATAL("The Random Number Generator is NULL\n");
  return rng->desc.get(rng->state);
}

uint64_t
ssp_rng_uniform_uint64
  (struct ssp_rng* rng, const uint64_t lower, const uint64_t upper)
{
  if(!rng) FATAL("The Random Number Generator is NULL\n");
  RAN_NAMESPACE::uniform_int_distribution<uint64_t> distrib(lower, upper);
  return wrap_ran(*rng, distrib);
}

double
ssp_rng_uniform_double
  (struct ssp_rng* rng, const double lower, const double upper)
{
  if(!rng) FATAL("The Random Number Generator is NULL\n");
  RAN_NAMESPACE::uniform_real_distribution<double> distrib(lower, upper);
  return wrap_ran(*rng, distrib);
}

float
ssp_rng_uniform_float
  (struct ssp_rng* rng, const float lower, const float upper)
{
  if(!rng) FATAL("The Random Number Generator is NULL\n");
  RAN_NAMESPACE::uniform_real_distribution<float> distrib(lower, upper);
  return wrap_ran(*rng, distrib);
}

double
ssp_rng_canonical(struct ssp_rng* rng)
{
  if(!rng) FATAL("The Random Number Generator is NULL\n");
#if 0
  rng_cxx rng_cxx(*rng);
  return RAN_NAMESPACE::generate_canonical
    <double, std::numeric_limits<double>::digits>(rng_cxx);
#else
  /* Optimized version */
  size_t k = rng->dbl_k;
  double sum = 0;
  double tmp = 1;
  for(; k !=0; --k) {
    sum += (double)(rng->desc.get(rng->state) - rng->desc.min) * tmp;
    tmp = (double)(tmp*rng->r);
  }
  return sum/tmp;
#endif
}

float
ssp_rng_canonical_float(struct ssp_rng* rng)
{
  if(!rng) FATAL("The Random Number Generator is NULL\n");
  /* The C++ standard library does not ensure that the generated single
   * precision floating point number is effectively canonical, i.e. it may be
   * equal to 1. Use a hand made workaround to handle this bug. */
#if 0
  rng_cxx rng_cxx(*rng);
  return RAN_NAMESPACE::generate_canonical
    <float, std::numeric_limits<float>::digits>(rng_cxx);
#else
  float r;
  do {
    /* Optimised version of the generate_canonical function */
    size_t k = rng->flt_k;
    float sum = 0;
    float tmp = 1;
    for(; k !=0; --k) {
      sum += (float)(rng->desc.get(rng->state) - rng->desc.min) * tmp;
      tmp = (float)(tmp*rng->r);
    }
    r = sum/tmp;
  } while(r >= 1);
  return r;
#endif
}

uint64_t
ssp_rng_min(struct ssp_rng* rng)
{
  if(!rng) FATAL("The Random Number Generator is NULL\n");
  return rng->desc.min;
}

uint64_t
ssp_rng_max(struct ssp_rng* rng)
{
  if(!rng) FATAL("The Random Number Generator is NULL\n");
  return rng->desc.max;
}

res_T
ssp_rng_set(struct ssp_rng* rng, const uint64_t seed)
{
  if(!rng) return RES_BAD_ARG;
  return rng->desc.set(rng->state, seed);
}

res_T
ssp_rng_read(struct ssp_rng* rng, FILE* stream)
{
  if(!rng || !stream) return RES_BAD_ARG;
  return rng->desc.read(rng->state, stream);
}

res_T
ssp_rng_read_cstr(struct ssp_rng* rng, const char* cstr)
{
  if(!rng || !cstr) return RES_BAD_ARG;
  return rng->desc.read_cstr(rng->state, cstr);
}

res_T
ssp_rng_write(const struct ssp_rng* rng, FILE* stream)
{
  if(!rng || !stream) return RES_BAD_ARG;
  return rng->desc.write(rng->state, stream);
}

res_T
ssp_rng_write_cstr
  (const struct ssp_rng* rng,
   char* buf,
   const size_t bufsz,
   size_t* len)
{
  if(!rng) return RES_BAD_ARG;
  return rng->desc.write_cstr(rng->state, buf, bufsz, len);
}

double
ssp_rng_entropy(const struct ssp_rng* rng)
{
  if (!rng) FATAL("The Random Number Generator is NULL\n");
  return rng->desc.entropy(rng->state);
}

res_T
ssp_rng_discard(struct ssp_rng* rng, uint64_t n)
{
  if (!rng) FATAL("The Random Number Generator is NULL\n");
  return rng->desc.discard(rng->state, n);
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
res_T
rng_create
  (struct mem_allocator* mem_allocator,
   const struct rng_desc* desc,
   struct ssp_rng** out_rng)
{
  struct mem_allocator* allocator;
  struct ssp_rng* rng = NULL;
  size_t log2r;
  res_T res = RES_OK;

  if(!desc || !out_rng) {
    res = RES_BAD_ARG;
    goto error;
  }

  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;

  /* Align the rng on 16 bytes since its 'r' attrib is a long double that on
   * Linux 64-bits systems must be aligned on 16 bytes. */
  rng = (struct ssp_rng*)MEM_ALLOC_ALIGNED(allocator, sizeof(*rng), 16);
  if(!rng) {
    res = RES_MEM_ERR;
    goto error;
  }
  memset(rng, 0, sizeof(struct ssp_rng));
  rng->allocator = allocator;
  ref_init(&rng->ref);
  rng->desc = *desc;

  rng->state = MEM_ALLOC_ALIGNED
    (rng->allocator, rng->desc.sizeof_state, rng->desc.alignof_state);
  if(!rng->state) {
    res = RES_MEM_ERR;
    goto error;
  }
  res = rng->desc.init(allocator, rng->state);
  if(res != RES_OK) goto error;

  /* Precompute some values for the canonical distribution */
  rng->r = (long double)rng->desc.max - (long double)rng->desc.min + 1.0L;
  log2r = (size_t)(std::log(rng->r) / std::log(2.0L));
  rng->dbl_k = std::max<size_t>(1UL, (53 + log2r - 1UL) / log2r);
  rng->flt_k = std::max<size_t>(1UL, (24 + log2r - 1UL) / log2r);

exit:
  if(out_rng) *out_rng = rng;
  return res;
error:
  if(rng) {
    SSP(rng_ref_put(rng));
    rng = NULL;
  }
  goto exit;
}
