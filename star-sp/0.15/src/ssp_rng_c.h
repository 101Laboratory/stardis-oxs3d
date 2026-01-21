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

#ifndef SSP_RNG_C_H
#define SSP_RNG_C_H

/* Prevent Windows min/max macro pollution before including any headers */
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <rsys/rsys.h>

/* disable some warnings in Random123 includes */
#ifdef COMPILER_GCC
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wconversion" /* unsafe conversion */
  #pragma GCC diagnostic ignored "-Wshadow"
  #pragma GCC diagnostic ignored "-Wunused-parameter" /* in Random123/aes.h */
#elif defined(COMPILER_CL) || defined(_MSC_VER)
  #pragma warning(push)
  #pragma warning(disable:4100) /* unreferenced formal parameter */
  #pragma warning(disable:4127) /* conditional expression is constant */
  #pragma warning(disable:4512) /* assignment operator could not be generated */
  #pragma warning(disable:4521) /* multiple copy constructors specified */
#endif

#include <Random123/conventional/Engine.hpp>
#include <Random123/threefry.h>
#ifdef WITH_R123_AES
  #include <Random123/aes.h>
#endif

#ifdef COMPILER_GCC
  #pragma GCC diagnostic pop
#elif defined(COMPILER_CL) || defined(_MSC_VER)
  #pragma warning(pop)
#endif

#include "ssp.h"
#include <rsys/ref_count.h>

#define SQRT_2_PI 2.50662827463100050240

#ifdef USE_BOOST_RANDOM
  /* Disable some warnings in boost includes */
  #pragma warning(push)
  #pragma warning(disable:4244) /* possible loss of data due to data conversion */
  #pragma warning(disable:4458) /* declaration of a variable hides class member */

  /* The random C++11 library is bugged on MSVC 12 & 14. Use the boost version */
  #include <boost/random.hpp>
  #include <boost/random/random_device.hpp>
  #define RAN_NAMESPACE boost::random

  #pragma warning(pop)
  #pragma warning(push)
  #pragma warning(disable:4706) /* Assignment in conditional expression */
#else
  #include <random>
  #define RAN_NAMESPACE std
#endif

/* Generic Random Number Generator type descriptor */
struct rng_desc {
  res_T (*init)(struct mem_allocator* allocator, void* state);
  void (*release)(void* state);
  res_T (*set)(void* state, const uint64_t seed);

  uint64_t (*get)(void* state);
  res_T (*discard)(void* state, uint64_t n);
  res_T (*read)(void* state, FILE* file);
  res_T (*read_cstr)(void* state, const char* cstr);
  res_T (*write)(const void* state, FILE* file);
  res_T (*write_cstr)(const void* state, char* buf, const size_t sz, size_t* out_sz);
  double (*entropy)(const void* state);

  uint64_t min;
  uint64_t max;

  size_t sizeof_state;
  size_t alignof_state;
};

struct ssp_rng {
  struct rng_desc desc;
  enum ssp_rng_type type;
  void* state;
  struct mem_allocator* allocator;

  /* Precomputed RNG constants used to speed up the canonical generations */
  long double r; /* max - min + 1 */
  size_t dbl_k; /* max(1, (#bits_mantisse_double + log2(r)) / log2(r)) */
  size_t flt_k; /* max(1, (#bits_mantisse_float  + log2(r)) / log2(r)) */

  ref_T ref;
};

/*******************************************************************************
 * Local API
 ******************************************************************************/
extern LOCAL_SYM res_T
rng_create
  (struct mem_allocator* allocator,
   const struct rng_desc* desc,
   struct ssp_rng** out_rng);

/*******************************************************************************
 * C++ Wrappers 
 ******************************************************************************/
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

template<typename ResultType, uint64_t MinVal, uint64_t MaxVal>
class rng_cxx
{
public:
  FINLINE rng_cxx(struct ssp_rng& _rng) : rng(_rng) {}
  FINLINE uint64_t operator()() { return rng.desc.get(rng.state); }
  
  static FINLINE uint64_t min() { return MinVal; }
  static FINLINE uint64_t max() { return MaxVal; }
  
  typedef ResultType result_type;

private:
  struct ssp_rng& rng;
};

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

template<typename Ran> typename Ran::result_type
wrap_ran(struct ssp_rng& rng, Ran& ran)
{
  switch(rng.type) {
    case SSP_RNG_KISS: {
      rng_cxx<uint64_t, 0, UINT32_MAX> rng_cxx_inst(rng);
      return ran(rng_cxx_inst);
    }
    case SSP_RNG_MT19937_64: {
      rng_cxx<uint64_t, 0, UINT64_MAX> rng_cxx_inst(rng);
      return ran(rng_cxx_inst);
    }
    case SSP_RNG_RANLUX48: {
      rng_cxx<uint64_t, 0, UINT64_MAX> rng_cxx_inst(rng);
      return ran(rng_cxx_inst);
    }
    case SSP_RNG_RANDOM_DEVICE: {
      rng_cxx<unsigned int, 0, UINT32_MAX> rng_cxx_inst(rng);
      return ran(rng_cxx_inst);
    }
    case SSP_RNG_THREEFRY: {
      rng_cxx<uint64_t, 0, UINT64_MAX> rng_cxx_inst(rng);
      return ran(rng_cxx_inst);
    }
#ifdef WITH_R123_AES
    case SSP_RNG_AES: {
      rng_cxx<uint32_t, 0, UINT32_MAX> rng_cxx_inst(rng);
      return ran(rng_cxx_inst);
    }
#endif
    default: FATAL("Unreachable code\n");
  }
}

#endif /* SSP_RNG_C_H */

