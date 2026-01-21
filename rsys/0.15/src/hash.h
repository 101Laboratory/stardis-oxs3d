/* Copyright (C) 2013-2023, 2025 Vincent Forest (vaplv@free.fr)
 *
 * The RSys library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The RSys library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the RSys library. If not, see <http://www.gnu.org/licenses/>. */

#ifndef HASH_H
#define HASH_H

#include "rsys.h"

struct mem_allocator;

struct sha256_ctx {
  char chunk[64];
  uint32_t len; /* #bytes registered in chunk */
  uint32_t state[8]; /* Current hash state */
  uint64_t nbits; /* Overall size of the message in bits */
};

typedef char hash256_T[32];

struct chunked_data_desc {
  /* Get the data per chunk of 512 bits. The last chunk length can be lesser
   * than 512 bits */
  void (*get_chunk512)
    (char dst[64], /* Destination */
     const size_t ichunk512, /* Indices of the chunck of 512 bits to fetch */
     void* ctx); /* User defined variable */
  size_t size; /* Size in bytes of the data */
  void* context; /* User defined variable */
};
#define CHUNKED_DATA_DESC_NULL__ {NULL, 0, NULL}
static const struct chunked_data_desc CHUNKED_DATA_DESC_NULL =
  CHUNKED_DATA_DESC_NULL__;

/* 32-bits Fowler/Noll/Vo hash function */
static INLINE uint32_t
hash_fnv32(const void* data, const size_t len)
{
  const uint32_t FNV32_PRIME =
    (uint32_t)(((uint32_t)1<<24) + ((uint32_t)1<<8) + 0x93);
  const uint32_t OFFSET32_BASIS = 2166136261u;
  const char* octets = (const char*)data;
  uint32_t hash = OFFSET32_BASIS;
  size_t i;
  ASSERT(data);

  FOR_EACH(i, 0, len) {
    hash = hash ^ (uint32_t)((unsigned char)octets[i]);
    hash = hash * FNV32_PRIME;
  }
  return hash;
}

/* 64-bits Fowler/Noll/Vo hash function */
static INLINE uint64_t
hash_fnv64(const void* data, const size_t len)
{
  const uint64_t FNV64_PRIME =
    (uint64_t)(((uint64_t)1<<40) + ((uint64_t)1<<8) + 0xB3);
  const uint64_t OFFSET64_BASIS = (uint64_t)14695981039346656037u;
  const char* octets = (const char*)data;
  uint64_t hash = OFFSET64_BASIS;
  size_t i;
  ASSERT(data);

  FOR_EACH(i, 0, len) {
    hash = hash ^ (uint64_t)((unsigned char)octets[i]);
    hash = hash * FNV64_PRIME;
  }
  return hash;
}

BEGIN_DECLS

RSYS_API void
sha256_ctx_init
  (struct sha256_ctx* ctx);

RSYS_API void
sha256_ctx_update
  (struct sha256_ctx* ctx,
   const char* bytes,
   const size_t len); /* #bytes */

RSYS_API void
sha256_ctx_finalize
  (struct sha256_ctx* ctx,
   hash256_T hash);

RSYS_API void
hash_sha256
  (const void* data,
   const size_t len,
   hash256_T hash);

RSYS_API void
hash256_to_cstr
  (const hash256_T hash,
   char cstr[65]);

RSYS_API int
hash256_eq
  (const hash256_T hash0,
   const hash256_T hash1);

END_DECLS

#endif /* HASH_H */
