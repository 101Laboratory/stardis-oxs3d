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

#include "endianness.h"
#include "hash.h"
#include "rsys_math.h"
#include "mem_allocator.h"
#include <string.h>

struct test_str {
  const char* str;
  const char* sha256sum;
};
struct test_data {
  const void* mem;
  const size_t len;
  const char* sha256sum;
};

struct buf {
  const char* mem;
  size_t len;
};

static void
chk_hash(const void* data, const size_t data_len, const char* sha256sum)
{
  hash256_T hash;
  char hash_str[65];
  ASSERT(sha256sum);

  hash_sha256(data, data_len, hash);
  hash256_to_cstr(hash, hash_str);
  CHK(!strcmp(hash_str, sha256sum));
}

int
main(int argc, char** argv)
{
  char* data = NULL;
  hash256_T hash0, hash1;
  (void)argc, (void)argv;

  data = mem_alloc(0x6000003e);
  CHK(data);

  data[0] = '\0';
  chk_hash(data, 0,
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  hash_sha256(data, 0, hash0);
  ((uint32_t*)hash1)[0] = big_endian_32(0xe3b0c442);
  ((uint32_t*)hash1)[1] = big_endian_32(0x98fc1c14);
  ((uint32_t*)hash1)[2] = big_endian_32(0x9afbf4c8);
  ((uint32_t*)hash1)[3] = big_endian_32(0x996fb924);
  ((uint32_t*)hash1)[4] = big_endian_32(0x27ae41e4);
  ((uint32_t*)hash1)[5] = big_endian_32(0x649b934c);
  ((uint32_t*)hash1)[6] = big_endian_32(0xa495991b);
  ((uint32_t*)hash1)[7] = big_endian_32(0x7852b855);
  CHK(hash256_eq(hash0, hash1));

  sprintf(data, "abc");
  chk_hash(data, strlen(data),
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  sprintf(data, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
  chk_hash(data, strlen(data),
    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

  data[0] = (char)0xbdu;
  chk_hash(data, 1,
    "68325720aabd7c82f30f554b313d0570c95accbb7dc4b5aae11204c08ffe732b");

  data[0] = (char)0xc9u;
  data[1] = (char)0x8cu;
  data[2] = (char)0x8eu;
  data[3] = (char)0x55u;
  chk_hash(data, 4,
    "7abc22c0ae5af26ce93dbb94433a0e0b2e119d014f8e7f65bd56c61ccccd9504");

  memset(data, 0, 55);
  chk_hash(data, 55,
    "02779466cdec163811d078815c633f21901413081449002f24aa3e80f0b88ef7");

  memset(data, 0, 56);
  chk_hash(data, 56,
    "d4817aa5497628e7c77e6b606107042bbba3130888c5f47a375e6179be789fbb");

  memset(data, 0, 57);
  chk_hash(data, 57,
    "65a16cb7861335d5ace3c60718b5052e44660726da4cd13bb745381b235a1785");

  memset(data, 0, 64);
  chk_hash(data, 64,
    "f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b");

  memset(data, 0, 1000);
  chk_hash(data, 1000,
    "541b3e9daa09b20bf85fa273e5cbd3e80185aa4ec298e765db87742b70138a53");

  memset(data, 'A', 1000);
  chk_hash(data, 1000,
    "c2e686823489ced2017f6059b8b239318b6364f6dcd835d0a519105a1eadd6e4");

  memset(data, 'U', 1005);
  chk_hash(data, 1005,
    "f4d62ddec0f3dd90ea1380fa16a5ff8dc4c54b21740650f24afc4120903552b0");

  memset(data, 0, 1000000);
  chk_hash(data, 1000000,
    "d29751f2649b32ff572b5e0a9f541ea660a50f94ff0beedfb0b692b924cc8025");

  memset(data, 'Z', 0x20000000);
  chk_hash(data, 0x20000000,
    "15a1868c12cc53951e182344277447cd0979536badcc512ad24c67e9b2d4f3dd");

  memset(data, 0, 0x41000000);
  chk_hash(data, 0x41000000,
    "461c19a93bd4344f9215f5ec64357090342bc66b15a148317d276e31cbc20b53");

  memset(data, 'B', 0x6000003e);
  chk_hash(data, 0x6000003e,
    "c23ce8a7895f4b21ec0daf37920ac0a262a220045a03eb2dfed48ef9b05aabea");

  mem_rm(data);
  CHK(mem_allocated_size() == 0);
  return 0;
}
