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

#define _POSIX_C_SOURCE 200112L

#include "endianness.h"
#include "hash.h"
#include "rsys_math.h"
#include "mem_allocator.h"

#include <string.h>

struct buffer {
  const char* mem;
  size_t len;
};

/* Array of round constants: first 32 bits of the fractional parts of the cube
 * roots of the first 64 primes 2..311 */
static const uint32_t k[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
  0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
  0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
  0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
  0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
  0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* Most of this code comes from GnuPG's cipher/sha1.c */
static void
sha256_process_chunk(struct sha256_ctx* ctx, const char chunk[64])
{
  uint32_t w[16];
  uint32_t a, b, c, d, e, f, g, h;
  uint32_t i;

  uint32_t tm;
  uint32_t t0, t1;

  ASSERT(ctx);

  FOR_EACH(i, 0, 16) {
    w[i] = big_endian_32(((uint32_t*)chunk)[i]);
  }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];

  #define ROL(X, N) (((X) << (N)) | ((X) >> (32 - (N))))
  #define S0(X) (ROL(X,25)^ROL(X,14)^(X>>3))
  #define S1(X) (ROL(X,15)^ROL(X,13)^(X>>10))
  #define SS0(X) (ROL(X,30)^ROL(X,19)^ROL(X,10))
  #define SS1(X) (ROL(X,26)^ROL(X,21)^ROL(X,7))
  #define M(I) (tm = S1(w[(I- 2)&0x0f]) + w[(I-7)&0x0f]                        \
                   + S0(w[(I-15)&0x0f]) + w[I&0x0f], w[I&0x0f] = tm)
  #define F2(A, B, C) (( A & B ) | (C & (A | B)))
  #define F1(E, F, G) (G ^ (E & (F ^ G)))
  #define R(A, B, C, D, E, F, G, H, K, M) {                                    \
    t0 = SS0(A) + F2(A, B, C);                                                 \
    t1 = H + SS1(E) + F1(E, F, G) + K + M;                                     \
    D += t1;                                                                   \
    H = t0 + t1;                                                               \
  } (void)0

  R( a, b, c, d, e, f, g, h, k[ 0], w[ 0] );
  R( h, a, b, c, d, e, f, g, k[ 1], w[ 1] );
  R( g, h, a, b, c, d, e, f, k[ 2], w[ 2] );
  R( f, g, h, a, b, c, d, e, k[ 3], w[ 3] );
  R( e, f, g, h, a, b, c, d, k[ 4], w[ 4] );
  R( d, e, f, g, h, a, b, c, k[ 5], w[ 5] );
  R( c, d, e, f, g, h, a, b, k[ 6], w[ 6] );
  R( b, c, d, e, f, g, h, a, k[ 7], w[ 7] );
  R( a, b, c, d, e, f, g, h, k[ 8], w[ 8] );
  R( h, a, b, c, d, e, f, g, k[ 9], w[ 9] );
  R( g, h, a, b, c, d, e, f, k[10], w[10] );
  R( f, g, h, a, b, c, d, e, k[11], w[11] );
  R( e, f, g, h, a, b, c, d, k[12], w[12] );
  R( d, e, f, g, h, a, b, c, k[13], w[13] );
  R( c, d, e, f, g, h, a, b, k[14], w[14] );
  R( b, c, d, e, f, g, h, a, k[15], w[15] );
  R( a, b, c, d, e, f, g, h, k[16], M(16) );
  R( h, a, b, c, d, e, f, g, k[17], M(17) );
  R( g, h, a, b, c, d, e, f, k[18], M(18) );
  R( f, g, h, a, b, c, d, e, k[19], M(19) );
  R( e, f, g, h, a, b, c, d, k[20], M(20) );
  R( d, e, f, g, h, a, b, c, k[21], M(21) );
  R( c, d, e, f, g, h, a, b, k[22], M(22) );
  R( b, c, d, e, f, g, h, a, k[23], M(23) );
  R( a, b, c, d, e, f, g, h, k[24], M(24) );
  R( h, a, b, c, d, e, f, g, k[25], M(25) );
  R( g, h, a, b, c, d, e, f, k[26], M(26) );
  R( f, g, h, a, b, c, d, e, k[27], M(27) );
  R( e, f, g, h, a, b, c, d, k[28], M(28) );
  R( d, e, f, g, h, a, b, c, k[29], M(29) );
  R( c, d, e, f, g, h, a, b, k[30], M(30) );
  R( b, c, d, e, f, g, h, a, k[31], M(31) );
  R( a, b, c, d, e, f, g, h, k[32], M(32) );
  R( h, a, b, c, d, e, f, g, k[33], M(33) );
  R( g, h, a, b, c, d, e, f, k[34], M(34) );
  R( f, g, h, a, b, c, d, e, k[35], M(35) );
  R( e, f, g, h, a, b, c, d, k[36], M(36) );
  R( d, e, f, g, h, a, b, c, k[37], M(37) );
  R( c, d, e, f, g, h, a, b, k[38], M(38) );
  R( b, c, d, e, f, g, h, a, k[39], M(39) );
  R( a, b, c, d, e, f, g, h, k[40], M(40) );
  R( h, a, b, c, d, e, f, g, k[41], M(41) );
  R( g, h, a, b, c, d, e, f, k[42], M(42) );
  R( f, g, h, a, b, c, d, e, k[43], M(43) );
  R( e, f, g, h, a, b, c, d, k[44], M(44) );
  R( d, e, f, g, h, a, b, c, k[45], M(45) );
  R( c, d, e, f, g, h, a, b, k[46], M(46) );
  R( b, c, d, e, f, g, h, a, k[47], M(47) );
  R( a, b, c, d, e, f, g, h, k[48], M(48) );
  R( h, a, b, c, d, e, f, g, k[49], M(49) );
  R( g, h, a, b, c, d, e, f, k[50], M(50) );
  R( f, g, h, a, b, c, d, e, k[51], M(51) );
  R( e, f, g, h, a, b, c, d, k[52], M(52) );
  R( d, e, f, g, h, a, b, c, k[53], M(53) );
  R( c, d, e, f, g, h, a, b, k[54], M(54) );
  R( b, c, d, e, f, g, h, a, k[55], M(55) );
  R( a, b, c, d, e, f, g, h, k[56], M(56) );
  R( h, a, b, c, d, e, f, g, k[57], M(57) );
  R( g, h, a, b, c, d, e, f, k[58], M(58) );
  R( f, g, h, a, b, c, d, e, k[59], M(59) );
  R( e, f, g, h, a, b, c, d, k[60], M(60) );
  R( d, e, f, g, h, a, b, c, k[61], M(61) );
  R( c, d, e, f, g, h, a, b, k[62], M(62) );
  R( b, c, d, e, f, g, h, a, k[63], M(63) );

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
void
sha256_ctx_init(struct sha256_ctx* ctx)
{
  /* Initial hash values: first 32 bits of the fractional parts of the square
   * roots of the first 8 primes 2..19 */
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
  ctx->len = 0;
  ctx->nbits = 0;
}

void
sha256_ctx_update
  (struct sha256_ctx* ctx,
   const char* bytes,
   size_t len)
{
  size_t n;
  uint32_t i;
  ASSERT(ctx);
  ASSERT(bytes || !len);

  if(ctx->len) {
    n = MMIN(64 - ctx->len, len);
    memcpy(ctx->chunk + ctx->len, bytes, n);
    ctx->len += (uint32_t)n;
    bytes += n;
    len -= n;

    if(ctx->len == 64) {
      sha256_process_chunk(ctx,  ctx->chunk);
      ctx->nbits += 512;
      ctx->len = 0;
    }
  }

  if(len >= 64) {
    n = len / 64;
    FOR_EACH(i, 0, n) {
      sha256_process_chunk(ctx,  bytes);
      bytes += 64;
    }
    ctx->nbits += n * 512;
    len -= n * 64;
  }

  if(len) {
    memcpy(ctx->chunk, bytes, len);
    ctx->len = (uint32_t)len;
  }
}

void
sha256_ctx_finalize(struct sha256_ctx* ctx, hash256_T hash)
{
  uint32_t i;

  ASSERT(ctx && hash);

  ctx->nbits += ctx->len * 8; /* Update the message's length */
  i = ctx->len;

  /* Setup the '1' bit that marks the end of the data */
  ctx->chunk[i++] = (char)0x80;

  /* Clean up the bytes after the data up to the last 8 bytes */
  if(ctx->len < 56) {
    memset(ctx->chunk+i, 0, 56-i);
  } else {
    memset(ctx->chunk+i, 0, 64-i);
    sha256_process_chunk(ctx, ctx->chunk);
    memset(ctx->chunk, 0, 56);
  }

  /* Store the message's length in bits */
  *((uint64_t*)(ctx->chunk + 56)) = big_endian_64(ctx->nbits);
  sha256_process_chunk(ctx, ctx->chunk);

  /* Store result the result */
  ((uint32_t*)hash)[0] = big_endian_32(ctx->state[0]);
  ((uint32_t*)hash)[1] = big_endian_32(ctx->state[1]);
  ((uint32_t*)hash)[2] = big_endian_32(ctx->state[2]);
  ((uint32_t*)hash)[3] = big_endian_32(ctx->state[3]);
  ((uint32_t*)hash)[4] = big_endian_32(ctx->state[4]);
  ((uint32_t*)hash)[5] = big_endian_32(ctx->state[5]);
  ((uint32_t*)hash)[6] = big_endian_32(ctx->state[6]);
  ((uint32_t*)hash)[7] = big_endian_32(ctx->state[7]);
}

void
hash_sha256
  (const void* data,
   const size_t len,
   hash256_T hash)
{
  struct sha256_ctx ctx;
  ASSERT(hash);
  ASSERT(data || !len);

  sha256_ctx_init(&ctx);
  sha256_ctx_update(&ctx, data, len);
  sha256_ctx_finalize(&ctx, hash);
}

void
hash256_to_cstr(const hash256_T hash, char cstr[65])
{
  size_t i;
  ASSERT(hash && cstr);
  FOR_EACH(i, 0, sizeof(hash256_T)) {
    sprintf(cstr+i*2, "%02x", (uint8_t)hash[i]);
  }
}

int
hash256_eq(const hash256_T hash0, const hash256_T hash1)
{
  return memcmp(hash0, hash1, sizeof(hash256_T)) == 0;
}
