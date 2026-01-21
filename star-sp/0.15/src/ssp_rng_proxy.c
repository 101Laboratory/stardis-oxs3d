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

#define _POSIX_C_SOURCE 200809L /* fmemopen, getpid */

#include "ssp_rng_c.h"

#include <rsys/dynamic_array_char.h>
#include <rsys/mutex.h>
#include <rsys/ref_count.h>
#include <rsys/signal.h>
#include <rsys/stretchy_array.h>

#include <limits.h>
#ifdef _WIN32
#include <process.h>   // _getpid
#define getpid _getpid
#else
#include <unistd.h>
#endif

#ifdef _WIN32
typedef int pid_t;
#else
#include <sys/types.h>
#endif

#ifdef _WIN32
FILE* fmemopen(void* buf, size_t size, const char* mode)
{
    FILE* fp = tmpfile();
    if (!fp) return NULL;

    if (mode[0] == 'r' || mode[0] == 'w' || mode[0] == 'a') {
        fwrite(buf, 1, size, fp);
        rewind(fp);
    }

    return fp;
}
#endif

#define BUCKET_SIZE_DEFAULT 1000000 /* #RNs per bucket */

/* Cache capacity. Maximum size in bytes that the cache can store */
#if 1
  #define STATE_CACHE_CAPACITY (4*1024*1024) /* 4 MB */
#else
  #define STATE_CACHE_CAPACITY 0 /* Disable the cache */
#endif

/* Cache of RNG states */
struct rng_state_cache {
  struct darray_char state; /* Save the next RNG state with 'no_wstream' */
  struct darray_char state_scratch; /* Scracth state buffer */

  struct mem_allocator* allocator;
  void* buffer; /* Memory in which RNG states are stored */
  FILE* stream; /* Stream to buffer storing RNG states */

  size_t state_pitch; /* #RNs between 2 cached states */
  size_t nstates; /* #cached states */

  long read, write; /* Offset into the stream where to read/write RNG states */
  long end_of_cache; /* Stream offset that marks the end of cache */

  int no_wstream; /* Define if the RNG states are no more written to a stream */
  int no_rstream; /* Define if the RNG states are no more read from a stream */

  int is_init; /* Cache initialisation state */
};

CLBK(rng_proxy_cb_T, ARG1(const struct ssp_rng_proxy*));

enum rng_proxy_sig {
  RNG_PROXY_SIG_SET_STATE,
  RNG_PROXY_SIGS_COUNT__
};

/* A proxy manages a list of N independent RNGs of the same type named buckets.
 * One ensure that each bucket have independent `infinite' random numbers by
 * partitioning a unique random sequence in N random pools, each containing
 * `bucket_size' random numbers. When a bucket has no more random number in its
 * affected pool, it silently retrieves a new pool of `bucket_size' random
 * numbers from the proxy.
 *
 * Mapping of the partitions of the unique random sequence to N buckets
 *
 * bucket_size
 * /         \
 * +---------+---------+-   -+---------+---------+---------+-
 * | Bucket0 | Bucket1 | ... |BucketN-1| Bucket0 | Bucket1 | ...
 * +---------+---------+-   -+---------+---------+---------+-
 *                  Unique random sequence */
struct ssp_rng_proxy {
  enum ssp_rng_type type; /* Type of the RNGs managed by the proxy */

  struct ssp_rng* rng; /* Main `type' RNG */

  /* The following arrays have the same size */
  ATOMIC* buckets; /* Flag that defines which bucket RNGs are created */
  size_t sequence_size; /* #RNs in a sequence */
  size_t sequence_bias; /* #RNs to discard between 2 consecutive sequence */
  size_t bucket_size; /* #random numbers per bucket */
  struct ssp_rng** pools; /* `type' RNGs wrapped by bucket RNGs */
  struct rng_state_cache* states; /* Cache of `type' RNG states */

  /* Index of the last queried sequence. This index is independent of the
   * original seed used by the proxy and is designed to identify the status of
   * the proxy relative to that original seed. When the proxy is created, the
   * sequence index is SSP_SEQUENCE_ID_NONE, i.e. no sequence was queried. At
   * the first request for a random number, the first sequence is consumed and
   * this sequence index is then 0. It is then incremented by one each time a
   * new sequence is required.
   *
   * Each bucket stores the sequence ID of its local RNG. The proxy sequence ID
   * is the maximum between these local sequence indices. Note that we also
   * keep track of the RNG proxy's sequence index (main_sequence_id); it is
   * equal to the proxy sequence identifier only when the caching mechanism is
   * still in use. */
  size_t* per_bucket_sequence_id;
  size_t main_sequence_id;

  signal_T signals[RNG_PROXY_SIGS_COUNT__];

  struct mutex* mutex;
  struct mem_allocator* allocator;
  ref_T ref;
};

/* Return a RNG with a pool of `bucket_size' indenpendant random numbers. Each
 * pool are ensured to be independant per `bucket_id' in [0, N) and per function
 * call, i.e. calling this function X times with the same bucket_id will
 * provide X different random pools.
 *
 *         bucket_size                 sequence_bias
 *         /          \                    /  \
 * +------+------------+-   -+------------+----+------------+-
 * |######|  Bucket 0  | ... | Bucket N-1 |####|  Bucket 0  | ...
 * |######|  1st pool  |     |  1st pool  |####|  2nd pool  |
 * +------+------------+-   -+------------+----+------------+-
 *  \    / \_________sequence_size_______/    /
 * sequence \________sequence_pitch__________/
 *  offset
 */
static struct ssp_rng*
rng_proxy_next_ran_pool
  (struct ssp_rng_proxy* proxy,
   const size_t bucket_id);

/* Write the RNG state into buf. State data are terminated by a null char */
static res_T
rng_write_cstr
  (const struct ssp_rng* rng,
   struct darray_char* buf,
   size_t* out_len) /* May be NULL. String length without the null char */
{
  size_t len;
  res_T res = RES_OK;
  ASSERT(rng && buf);

  /* Write the RNG state into a temporary buffer */
  res = ssp_rng_write_cstr
    (rng, darray_char_data_get(buf), darray_char_size_get(buf), &len);
  if(res != RES_OK) goto error;

  /* Not sufficient space to store the state */
  if(len >= darray_char_size_get(buf)) {
    res = darray_char_resize(buf, len + 1/*null char*/);
    if(res != RES_OK) goto error;

    res = ssp_rng_write_cstr
      (rng, darray_char_data_get(buf), darray_char_size_get(buf), &len);
    if(res != RES_OK) goto error;
    ASSERT(len + 1/*null char*/ == darray_char_size_get(buf));
  }

  if(out_len) *out_len = len;

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Cache of RNG states
 ******************************************************************************/
static void
rng_state_cache_release(struct rng_state_cache* cache)
{
  ASSERT(cache);
  if(!cache->is_init) return; /* Nothing to release */

  if(cache->stream) fclose(cache->stream);
  if(cache->buffer) MEM_RM(cache->allocator, cache->buffer);
  darray_char_release(&cache->state);
  darray_char_release(&cache->state_scratch);
}

static res_T
rng_state_cache_init
  (struct mem_allocator* allocator,
   const size_t state_pitch, /* #RNs between cached states */
   struct rng_state_cache* cache)
{
  res_T res = RES_OK;
  ASSERT(cache);

  memset(cache, 0, sizeof(*cache));
  darray_char_init(allocator, &cache->state);
  darray_char_init(allocator, &cache->state_scratch);
  cache->allocator = allocator;
  cache->end_of_cache = STATE_CACHE_CAPACITY;
  cache->state_pitch = state_pitch;

  if(STATE_CACHE_CAPACITY != 0) {
    cache->buffer = MEM_CALLOC(allocator, 1, STATE_CACHE_CAPACITY);
    if(!cache->buffer) { res = RES_MEM_ERR; goto error; }
    cache->stream = fmemopen(cache->buffer, STATE_CACHE_CAPACITY, "w+");
    if(!cache->stream) { res = RES_IO_ERR; goto error; }
    cache->read = cache->write = ftell(cache->stream);
  }

  cache->is_init = 1;

exit:
  return res;
error:
  rng_state_cache_release(cache);
  goto exit;
}

static res_T
rng_state_cache_clear(struct rng_state_cache* cache)
{
  ASSERT(cache);
  if(cache->stream) {
    rewind(cache->stream);
    cache->read = cache->write = ftell(cache->stream);
  } else {
    ASSERT(STATE_CACHE_CAPACITY == 0);
    cache->read = cache->write = 0;
  }
  cache->nstates = 0;
  cache->no_wstream = 0;
  cache->no_rstream = 0;
  return RES_OK;
}

static char
rng_state_cache_is_empty(struct rng_state_cache* cache)
{
  ASSERT(cache);
  return cache->nstates == 0;
}

static res_T
rng_state_cache_dump(struct rng_state_cache* cache)
{
  /* Output file into which cache stream is dumped */
  char name[128] = {0};
  pid_t process = 0; /* Process identifier */
  FILE* fp = NULL;

  /* Temporary memory to copy cache stream */
  void* mem = NULL;

  /* Miscellaneous */
  long offset = 0;
  int n = 0;
  res_T res = RES_OK;

  ASSERT(cache);

  /* No cache to dump */
  if(!cache->stream) goto exit;

  process = getpid();

  #define TRY(Cond, Err) { \
    if(!(Cond)) { \
      fprintf(stderr, "%s:%d: %s\n", FUNC_NAME, __LINE__, strerror(Err)); \
      switch(Err) { \
        case EIO: res = RES_IO_ERR; break; \
        case ENOMEM: res = RES_MEM_ERR; break; \
        default: res = RES_UNKNOWN_ERR; break; \
      } \
      goto error; \
    } \
  } (void)0

  /* Requests the offset at the end of the file,
   * i.e. the size of the cache stream */
  TRY(fseek(cache->stream, 0, SEEK_END) == 0, errno);
  TRY((offset = ftell(cache->stream)) >= 0, errno);

  /* Define the state cache filename */
  n = snprintf(name, sizeof(name), "rng_cache_r%lu_w%lu_s%ld_%d",
    cache->read, cache->write, offset, process);
  TRY(n >= 0, errno);
  TRY((unsigned long)n < sizeof(name), ENOMEM);

  /* Create the output file */
  TRY((fp = fopen(name, "w")) != NULL, errno);

  rewind(cache->stream);

  /* Load cache stream in mem, and then dump it to the state cache */
  TRY((mem = mem_alloc(offset)) != NULL, ENOMEM);
  TRY((fread(mem, offset, 1, cache->stream)) == 1, EIO);
  TRY((fwrite(mem, offset, 1, fp)) == 1, EIO);

  #undef TRY

exit:
  if(mem) mem_rm(mem);
  if(fp) CHK(fclose(fp) == 0);
  return res;
error:
  goto exit;
}

static res_T
rng_state_cache_read
  (struct rng_state_cache* cache,
   struct ssp_rng* rng,
   struct mutex* mutex) /* Proxy mutex */
{
  res_T res = RES_OK;
  ASSERT(cache && rng && mutex);

  mutex_lock(mutex);
  ASSERT(!rng_state_cache_is_empty(cache));

  /* The read file pointer has reached the end of the cache.
   * Rewind it to the beginning of the cache */
  if(cache->read == cache->end_of_cache) {
    cache->read = 0;
    cache->end_of_cache = STATE_CACHE_CAPACITY;
  }

  if(!cache->no_rstream
  && cache->no_wstream
  && cache->read == cache->write
  && cache->nstates == 1/* A state is saved in 'cache->state' */) {
    /* There is no more data cached into the stream. Do not rely anymore on the
     * proxy RNG to generate the RNG states */
    cache->no_rstream = 1;
  }

  /* Read the cached RNG state from the stream */
  if(!cache->no_rstream) {

    fseek(cache->stream, cache->read, SEEK_SET);
    res = ssp_rng_read(rng, cache->stream);
    if(res != RES_OK) {
      mutex_unlock(mutex);
      goto error;
    }
    cache->read = ftell(cache->stream);

    /* Remove one cached states */
    cache->nstates -= 1;

    mutex_unlock(mutex);

  /* Generate the next RNG state and load the cached one */
  } else {
    /* All is done locally to the RNG. We can thus unlock the proxy mutex */
    mutex_unlock(mutex);

    /* Copy the cached RNG state */
    res = darray_char_copy(&cache->state_scratch, &cache->state);
    if(res != RES_OK) goto error;

    /* Load the cached RNG state */
    res = ssp_rng_read_cstr(rng, darray_char_cdata_get(&cache->state));
    if(res != RES_OK) goto error;

    /* Setup the next RNG state */
    res = ssp_rng_discard(rng, cache->state_pitch);
    if(res != RES_OK) goto error;

    /* Save the next RNG state */
    res = rng_write_cstr(rng, &cache->state, NULL);
    if(res != RES_OK) goto error;

    /* Setup the current RNG state */
    res = ssp_rng_read_cstr(rng, darray_char_cdata_get(&cache->state_scratch));
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
rng_state_cache_write(struct rng_state_cache* cache, struct ssp_rng* rng)
{
  long remaining_space = 0; /* Remaining cache space */
  size_t len = 0; /* Length of the cache state in bytes */
  res_T res = RES_OK;

  ASSERT(cache && rng);

  if(cache->no_wstream) goto exit; /* Do not cache the submitted state */

  /* Store in memory the state to be cached */
  res = rng_write_cstr(rng, &cache->state, &len);
  if(res != RES_OK) goto error;

  /* There are no spaces left at the end of the stream: rewind the writing */
  if(len > (size_t)(STATE_CACHE_CAPACITY - cache->write)) {
    cache->end_of_cache = cache->write; /* Mark the end of cache */
    cache->write = 0;
  }

  /* Calculate remaining cache space */
  if(rng_state_cache_is_empty(cache) || cache->write > cache->read) {
    remaining_space = STATE_CACHE_CAPACITY - cache->write;
  } else {
    remaining_space = cache->read - cache->write;
  }

  /* There is no sufficient space. Cache cannot be used */
  if(remaining_space < 0 || len > (size_t)remaining_space) {
    cache->no_wstream = 1;

  /* There is sufficient space. Write the RNG state into the cached stream */
  } else {
    size_t sz = 0;

    fseek(cache->stream, cache->write, SEEK_SET);
    sz = fwrite(darray_char_cdata_get(&cache->state), 1, len, cache->stream);
    if(sz != len) { res = RES_IO_ERR; goto error; }
    cache->write = ftell(cache->stream);

    /* Flush write state to detect a write error */
    if(fflush(cache->stream) != 0) {
      res = RES_IO_ERR;
      goto error;
    }
  }

  /* Update the number of cached states */
  cache->nstates += 1;

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * RNG that control the scheduling of random number pools for a given bucket
 ******************************************************************************/
struct rng_bucket {
  struct ssp_rng* pool; /* Wrapped RNG providing a pool of `bucket_size' RNs */
  struct ssp_rng_proxy* proxy; /* The RNG proxy */
  size_t name; /* Unique bucket identifier in [0, #buckets) */
  size_t count; /* Remaining unique random numbers in `pool' */
  rng_proxy_cb_T cb_on_proxy_set_state;
};

static void
rng_bucket_on_proxy_set_state(const struct ssp_rng_proxy* proxy, void* ctx)
{
  struct rng_bucket* rng = (struct rng_bucket*)ctx;
  ASSERT(proxy && ctx && rng->proxy == proxy);
  (void)proxy;
  /* Reset bucket */
  rng->count = 0;
  rng->pool = NULL;
}

static INLINE void
rng_bucket_next_ran_pool(struct rng_bucket* rng)
{
  ASSERT(rng);
  rng->pool = rng_proxy_next_ran_pool(rng->proxy, rng->name);
  rng->count = rng->proxy->bucket_size;
}

static res_T
rng_bucket_set(void* data, const uint64_t seed)
{
  (void)data, (void)seed;
  return RES_BAD_OP;
}

static uint64_t
rng_bucket_get(void* data)
{
  struct rng_bucket* rng = (struct rng_bucket*)data;
  ASSERT(data);
  if(!rng->count) rng_bucket_next_ran_pool(rng);
  --rng->count;
  return ssp_rng_get(rng->pool);
}

static res_T
rng_bucket_read(void* data, FILE* file)
{
  (void)data, (void)file;
  return RES_BAD_OP;
}

static res_T
rng_bucket_read_cstr(void* data, const char* cstr)
{
  (void)data, (void)cstr;
  return RES_BAD_OP;
}

static res_T
rng_bucket_write(const void* data, FILE* file)
{
  (void)data, (void)file;
  return RES_BAD_OP;
}

static res_T
rng_bucket_write_cstr
  (const void* data,
   char* buf,
   const size_t bufsz,
   size_t* len)
{
  (void)data, (void)buf, (void)bufsz, (void)len;
  return RES_BAD_OP;
}

static res_T
rng_bucket_init(struct mem_allocator* allocator, void* data)
{
  struct rng_bucket* rng = (struct rng_bucket*)data;
  ASSERT(data);
  (void)allocator;
  rng->proxy = NULL;
  rng->pool = NULL;
  rng->name = SIZE_MAX;
  rng->count = 0;
  return RES_OK;
}

static void
rng_bucket_release(void* data)
{
  struct rng_bucket* rng = (struct rng_bucket*)data;
  ASSERT(data && rng->proxy);
  ATOMIC_SET(&rng->proxy->buckets[rng->name], 0);
  CLBK_DISCONNECT(&rng->cb_on_proxy_set_state);
  SSP(rng_proxy_ref_put(rng->proxy));
}

static double
rng_bucket_entropy(const void* data)
{
  (void)data;
  return 0;
}

static res_T
rng_bucket_discard(void* data, uint64_t n)
{
  struct rng_bucket* rng = (struct rng_bucket*)data;
  ASSERT(data);
  while (rng->count < n) {
    n -= rng->count;
    rng_bucket_next_ran_pool(rng);
  }
  rng->count -= n;
  return ssp_rng_discard(rng->pool, n);
}

static const struct rng_desc RNG_BUCKET_NULL = {
  rng_bucket_init,
  rng_bucket_release,
  rng_bucket_set,
  rng_bucket_get,
  rng_bucket_discard,
  rng_bucket_read,
  rng_bucket_read_cstr,
  rng_bucket_write,
  rng_bucket_write_cstr,
  rng_bucket_entropy,
  INT_MAX, /* Min dummy value */
  0, /* Max dummy value */
  sizeof(struct rng_bucket),
  16
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
/* Scheduler of random number pools */
struct ssp_rng*
rng_proxy_next_ran_pool
  (struct ssp_rng_proxy* proxy,
   const size_t bucket_name)
{
  res_T res = RES_OK;
  ASSERT(proxy);
  ASSERT(bucket_name <= sa_size(proxy->buckets));
  ASSERT(bucket_name <= sa_size(proxy->per_bucket_sequence_id));

  mutex_lock(proxy->mutex);

  if(rng_state_cache_is_empty(proxy->states + bucket_name)) {
    size_t ibucket;
    /* Register a new state for *all* buckets */
    FOR_EACH(ibucket, 0, sa_size(proxy->states)) {
      res = rng_state_cache_write(proxy->states + ibucket, proxy->rng);
      if(res != RES_OK) {
        rng_state_cache_dump(proxy->states + ibucket);
        FATAL("RNG proxy: cannot write to state cache\n");
      }
      ssp_rng_discard(proxy->rng, proxy->bucket_size);
    }
    /* Discard RNs to reach the next sequence */
    ssp_rng_discard(proxy->rng, proxy->sequence_bias);

    /* Increment the sequence id of the main RNG */
    proxy->main_sequence_id += 1;
  }
  mutex_unlock(proxy->mutex);

  /* Read the RNG pool state of `bucket_name' */
  res = rng_state_cache_read
    (proxy->states + bucket_name,
     proxy->pools[bucket_name],
     proxy->mutex);
  if(res != RES_OK) {
    mutex_lock(proxy->mutex);
    rng_state_cache_dump(proxy->states + bucket_name);
    mutex_unlock(proxy->mutex);
    FATAL("RNG proxy: cannot read from state cache\n");
  }

  /* Update the sequence of the bucket RNG */
  proxy->per_bucket_sequence_id[bucket_name] += 1;

  return proxy->pools[bucket_name];
}

static void
rng_proxy_clear(struct ssp_rng_proxy* proxy)
{
  size_t ibucket;
  ASSERT(proxy);
  ASSERT(sa_size(proxy->pools) == sa_size(proxy->buckets));
  ASSERT(sa_size(proxy->pools) == sa_size(proxy->states));

  FOR_EACH(ibucket, 0, sa_size(proxy->pools)) {
    ASSERT(proxy->buckets[ibucket] == 0); /* No bucket RNG should be created */
    if(proxy->pools[ibucket]) SSP(rng_ref_put(proxy->pools[ibucket]));
    rng_state_cache_release(proxy->states + ibucket);
  }
  sa_clear(proxy->buckets);
  sa_clear(proxy->pools);
  sa_clear(proxy->states);
}

static res_T
rng_proxy_setup
  (struct ssp_rng_proxy* proxy,
   const size_t sequence_pitch, /* #RNs between 2 consecutive sequences */
   const size_t nbuckets)
{
  size_t ibucket;
  res_T res = RES_OK;

  ASSERT(proxy && sequence_pitch && nbuckets);
  rng_proxy_clear(proxy);

  sa_add(proxy->states, nbuckets);
  sa_add(proxy->pools, nbuckets);
  sa_add(proxy->buckets, nbuckets);
  sa_add(proxy->per_bucket_sequence_id, nbuckets);

  /* Clearing allocated memory. This operation is necessary to manage errors and
   * identify which data has been initialised and which has not */
  memset(proxy->states, 0, sizeof(*proxy->states)*nbuckets);
  memset(proxy->pools, 0, sizeof(*proxy->pools)*nbuckets);
  memset(proxy->buckets, 0, sizeof(*proxy->buckets)*nbuckets);
  memset(proxy->per_bucket_sequence_id, 0,
    sizeof(*proxy->per_bucket_sequence_id)*nbuckets);

  FOR_EACH(ibucket, 0, nbuckets) {
    res = rng_state_cache_init
      (proxy->allocator, sequence_pitch, proxy->states+ibucket);
    if(res != RES_OK) goto error;
    res = ssp_rng_create(proxy->allocator, proxy->type, proxy->pools+ibucket);
    if(res != RES_OK) goto error;
    proxy->buckets[ibucket] = 0;

    /* Set the sequence index to SIZE_MAX because no sequence is active until a
     * random number query is made. On the first query, the index will be
     * incremented to 0 */
    proxy->per_bucket_sequence_id[ibucket] = SSP_SEQUENCE_ID_NONE/*<=> SIZE_MAX*/;
  }

exit:
  return res;
error:
  if(proxy) rng_proxy_clear(proxy);
  goto exit;
}

static res_T
rng_proxy_clear_caches(struct ssp_rng_proxy* proxy)
{
  size_t ibucket;
  res_T res = RES_OK;
  ASSERT(proxy);

  mutex_lock(proxy->mutex);
  FOR_EACH(ibucket, 0, sa_size(proxy->pools)) {
    res = rng_state_cache_clear(proxy->states+ibucket);
    if(res != RES_OK) break;
  }
  mutex_unlock(proxy->mutex);
  return res;
}

static void
rng_proxy_release(ref_T* ref)
{
  struct ssp_rng_proxy* proxy;
  ASSERT(ref);
  proxy = CONTAINER_OF(ref, struct ssp_rng_proxy, ref);
  rng_proxy_clear(proxy);
  sa_release(proxy->states);
  sa_release(proxy->pools);
  sa_release(proxy->buckets);
  sa_release(proxy->per_bucket_sequence_id);
  if(proxy->rng) SSP(rng_ref_put(proxy->rng));
  if(proxy->mutex) mutex_destroy(proxy->mutex);
  MEM_RM(proxy->allocator, proxy);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
ssp_rng_proxy_create
  (struct mem_allocator* mem_allocator,
   const enum ssp_rng_type type,
   const size_t nbuckets,
   struct ssp_rng_proxy** out_proxy)
{
  struct ssp_rng_proxy_create2_args args = SSP_RNG_PROXY_CREATE2_ARGS_NULL;
  const size_t sz = BUCKET_SIZE_DEFAULT * nbuckets;
  args.type = type;
  args.sequence_offset = 0;
  args.sequence_size = sz;
  args.sequence_pitch = sz;
  args.nbuckets = nbuckets;
  return ssp_rng_proxy_create2(mem_allocator, &args, out_proxy);
}

res_T
ssp_rng_proxy_create_from_rng
  (struct mem_allocator* mem_allocator,
   const struct ssp_rng* rng,
   const size_t nbuckets,
   struct ssp_rng_proxy** out_proxy)
{
  struct ssp_rng_proxy_create2_args args = SSP_RNG_PROXY_CREATE2_ARGS_NULL;
  const size_t sz = BUCKET_SIZE_DEFAULT * nbuckets;
  args.rng = rng;
  args.sequence_offset = 0;
  args.sequence_size = sz;
  args.sequence_pitch = sz;
  args.nbuckets = nbuckets;
  return ssp_rng_proxy_create2(mem_allocator, &args, out_proxy);
}

res_T
ssp_rng_proxy_create2
  (struct mem_allocator* mem_allocator,
   const struct ssp_rng_proxy_create2_args* args,
   struct ssp_rng_proxy** out_proxy)
{
  struct darray_char buf;
  struct mem_allocator* allocator = NULL;
  struct ssp_rng_proxy* proxy = NULL;
  size_t i;
  res_T res = RES_OK;

  darray_char_init(mem_allocator, &buf);

  if(!args
  || !out_proxy
  || !args->sequence_size
  || !args->nbuckets
  || (args->type == SSP_RNG_TYPE_NULL && !args->rng)
  || args->sequence_pitch < args->sequence_size
  || args->sequence_size < args->nbuckets) {
    res = RES_BAD_ARG;
    goto error;
  }

  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  proxy = (struct ssp_rng_proxy*)MEM_CALLOC(allocator, 1, sizeof(*proxy));
  if(!proxy) {
    res = RES_MEM_ERR;
    goto error;
  }
  proxy->allocator = allocator;
  ref_init(&proxy->ref);
  proxy->bucket_size = args->sequence_size / args->nbuckets;
  proxy->sequence_size = args->sequence_size;
  proxy->sequence_bias =
    args->sequence_pitch - (proxy->bucket_size * args->nbuckets);

  proxy->main_sequence_id = SSP_SEQUENCE_ID_NONE;

  /* Create the proxy RNG in its default state */
  if(!args->rng) {
    res = ssp_rng_create(allocator, args->type, &proxy->rng);
    if(res != RES_OK) goto error;
    proxy->type = args->type;

  /* Create the proxy RNG from a submitted RNG state */
  } else {
    size_t len;

    /* Create the RNG proxy of the type of the submitted RNG. Simply Ignore the
     * submitted RNG type if any */
    res = ssp_rng_get_type(args->rng, &proxy->type);
    if(res != RES_OK) goto error;

    /* Bucket RNG is not allowed to be a proxy RNG */
    if(args->rng->desc.init == rng_bucket_init) {
      res = RES_BAD_ARG;
      goto error;
    }

    res = ssp_rng_create(allocator, proxy->type, &proxy->rng);
    if(res != RES_OK) goto error;

    /* Initialise the RNG proxy state from the state of the submitted RNG */
    res = ssp_rng_write_cstr(args->rng, NULL, 0, &len);
    if(res != RES_OK) goto error;
    res = darray_char_resize(&buf, len+1/*Null char*/);
    if(res != RES_OK) goto error;
    res = ssp_rng_write_cstr(args->rng, darray_char_data_get(&buf), len+1, &len);
    if(res != RES_OK) goto error;
    res = ssp_rng_read_cstr(proxy->rng, darray_char_cdata_get(&buf));
    if(res != RES_OK) goto error;
  }

  res = ssp_rng_discard(proxy->rng, args->sequence_offset);
  if(res != RES_OK) goto error;

  proxy->mutex = mutex_create();
  if(!proxy->mutex) {
    res = RES_MEM_ERR;
    goto error;
  }

  FOR_EACH(i, 0, RNG_PROXY_SIGS_COUNT__) {
    SIG_INIT(proxy->signals + i);
  }

  res = rng_proxy_setup(proxy, args->sequence_pitch, args->nbuckets);
  if(res != RES_OK) goto error;

exit:
  darray_char_release(&buf);
  if(out_proxy) *out_proxy = proxy;
  return res;
error:
  if(proxy) {
    SSP(rng_proxy_ref_put(proxy));
    proxy = NULL;
  }
  goto exit;
}

res_T
ssp_rng_proxy_read(struct ssp_rng_proxy* proxy, FILE* stream)
{
  res_T res = RES_OK;
  if(!proxy || !stream) return RES_BAD_ARG;

  mutex_lock(proxy->mutex);
  res = ssp_rng_read(proxy->rng, stream);
  mutex_unlock(proxy->mutex);
  if(res != RES_OK) return res;

  /* Discard the cached RNG states */
  res = rng_proxy_clear_caches(proxy);
  if(res != RES_OK) return res;

  /* Notify to bucket RNGs that the proxy RNG state was updated */
  SIG_BROADCAST
    (proxy->signals+RNG_PROXY_SIG_SET_STATE, rng_proxy_cb_T, ARG1(proxy));
  return RES_OK;
}

res_T
ssp_rng_proxy_write(const struct ssp_rng_proxy* proxy, FILE* stream)
{
  res_T res = RES_OK;

  if(!proxy || !stream) return RES_BAD_ARG;

  mutex_lock(proxy->mutex);
  res = ssp_rng_write(proxy->rng, stream);
  mutex_unlock(proxy->mutex);
  return res;
}

res_T
ssp_rng_proxy_ref_get(struct ssp_rng_proxy* proxy)
{
  if(!proxy) return RES_BAD_ARG;
  ref_get(&proxy->ref);
  return RES_OK;
}

res_T
ssp_rng_proxy_ref_put(struct ssp_rng_proxy* proxy)
{
  if(!proxy) return RES_BAD_ARG;
  ref_put(&proxy->ref, rng_proxy_release);
  return RES_OK;
}

res_T
ssp_rng_proxy_create_rng
  (struct ssp_rng_proxy* proxy,
   const size_t ibucket,
   struct ssp_rng** out_rng)
{
  struct ssp_rng* rng = NULL;
  struct rng_desc desc = RNG_BUCKET_NULL;
  struct rng_bucket* bucket = NULL;
  res_T res = RES_OK;

  if(!proxy || ibucket >= sa_size(proxy->buckets) || !out_rng) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(ATOMIC_CAS(&proxy->buckets[ibucket], 1, 0) == 1) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Update the dummy rng bucket min/max value with the min/max values of the
   * RNG desc on which the proxy relies */
  desc.min = proxy->rng->desc.min;
  desc.max = proxy->rng->desc.max;

  res = rng_create(proxy->allocator, &desc, &rng);
  if(res != RES_OK) goto error;
  rng->type = proxy->type;
  bucket = (struct rng_bucket*)rng->state;
  bucket->name = ibucket;
  bucket->proxy = proxy;
  SSP(rng_proxy_ref_get(proxy));

  /* The bucket RNG listens the "write" signal of the proxy to reset its
   * internal RNs counter on "write" invocation. */
  CLBK_INIT(&bucket->cb_on_proxy_set_state);
  CLBK_SETUP
    (&bucket->cb_on_proxy_set_state, rng_bucket_on_proxy_set_state, bucket);
  SIG_CONNECT_CLBK
    (proxy->signals+RNG_PROXY_SIG_SET_STATE, &bucket->cb_on_proxy_set_state);

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

res_T
ssp_rng_proxy_get_type
  (const struct ssp_rng_proxy* proxy,
   enum ssp_rng_type* type)
{
  if(!proxy || !type) return RES_BAD_ARG;
  *type = proxy->type;
  return RES_OK;
}

res_T
ssp_rng_proxy_get_sequence_id(const struct ssp_rng_proxy* proxy, size_t* out_id)
{
  size_t id = SSP_SEQUENCE_ID_NONE;
  size_t i;

  if(!proxy || !out_id) return RES_BAD_ARG;

  mutex_lock(proxy->mutex);
  FOR_EACH(i, 0, sa_size(proxy->per_bucket_sequence_id)) {
    if(proxy->per_bucket_sequence_id[i] == SSP_SEQUENCE_ID_NONE) continue;
    id = id == SSP_SEQUENCE_ID_NONE
      ? proxy->per_bucket_sequence_id[i]
      : MMAX(id, proxy->per_bucket_sequence_id[i]);
  }
  mutex_unlock(proxy->mutex);

  *out_id = id;
  return RES_OK;
}

res_T
ssp_rng_proxy_flush_sequences
  (struct ssp_rng_proxy* proxy,
   const size_t nseqs)
{
  size_t nseqs_proxy = 0;
  size_t iseq;
  size_t i;
  res_T res = RES_OK;

  if(!proxy) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Nothing to discard */
  if(nseqs == 0) goto exit;

  res = ssp_rng_proxy_get_sequence_id(proxy, &iseq);
  if(res != RES_OK) goto error;

  /* Calculate the number of sequences to flush for the main RNG, i.e. the one
   * used when the cache is in use. We want to dump the 'nseqs' sequences. That
   * said, since the status of the RNG is set to the status of the 1st random
   * number of the next sequence, it is enough to 'nseqs-1' sequences and clear
   * the cache. Anyway, note that the sequence identifier of the main RNG may
   * be behind the global sequence identifier. This happens when the cache
   * mechanism is no longer used. In this case, we need to dump the extra
   * sequences from the main RNG to match the current sequence index of the
   * proxy. */
  nseqs_proxy = (nseqs - 1) + (iseq - proxy->main_sequence_id);

  mutex_lock(proxy->mutex);
  res = ssp_rng_discard(proxy->rng, proxy->sequence_size * nseqs_proxy);
  mutex_unlock(proxy->mutex);
  if(res != RES_OK) goto error;

  proxy->main_sequence_id += nseqs_proxy;

  /* Discard the cached RNG states */
  rng_proxy_clear_caches(proxy);

  /* Reset the RNGs sequence id */
  FOR_EACH(i, 0, sa_size(proxy->per_bucket_sequence_id)) {
    proxy->per_bucket_sequence_id[i] = proxy->main_sequence_id;
  }

  /* Notify to bucket RNGs that the proxy RNG state was updated */
  SIG_BROADCAST
    (proxy->signals+RNG_PROXY_SIG_SET_STATE, rng_proxy_cb_T, ARG1(proxy));

exit:
  return res;
error:
  goto exit;
}
