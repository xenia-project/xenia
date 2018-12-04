/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include <algorithm>

#include "xenia/kernel/util/crypto_utils.h"
#include "xenia/xbox.h"

#include "third_party/crypto/TinySHA1.hpp"

namespace xe {
namespace kernel {
namespace util {

uint8_t xekey_0x19[] = {0xE1, 0xBC, 0x15, 0x9C, 0x73, 0xB1, 0xEA, 0xE9,
                        0xAB, 0x31, 0x70, 0xF3, 0xAD, 0x47, 0xEB, 0xF3};

uint8_t xekey_0x19_devkit[] = {0xDA, 0xB6, 0x9A, 0xD9, 0x8E, 0x28, 0x76, 0x4F,
                               0x97, 0x7E, 0xE2, 0x48, 0x7E, 0x4F, 0x3F, 0x68};

const uint8_t* GetXeKey(uint32_t idx, bool devkit) {
  if (idx != 0x19) {
    return nullptr;
  }

  return devkit ? xekey_0x19_devkit : xekey_0x19;
}

void HmacSha(const uint8_t* key, uint32_t key_size_in, const uint8_t* inp_1,
             uint32_t inp_1_size, const uint8_t* inp_2, uint32_t inp_2_size,
             const uint8_t* inp_3, uint32_t inp_3_size, uint8_t* out,
             uint32_t out_size) {
  uint32_t key_size = key_size_in;
  sha1::SHA1 sha;
  uint8_t kpad_i[0x40];
  uint8_t kpad_o[0x40];
  uint8_t tmp_key[0x40];
  std::memset(kpad_i, 0x36, 0x40);
  std::memset(kpad_o, 0x5C, 0x40);

  // Setup HMAC key
  // If > block size, use its hash
  if (key_size > 0x40) {
    sha1::SHA1 sha_key;
    sha_key.processBytes(key, key_size);
    sha_key.finalize((uint8_t*)tmp_key);

    key_size = 0x14u;
  } else {
    std::memcpy(tmp_key, key, key_size);
  }

  for (uint32_t i = 0; i < key_size; i++) {
    kpad_i[i] = tmp_key[i] ^ 0x36;
    kpad_o[i] = tmp_key[i] ^ 0x5C;
  }

  // Inner
  sha.processBytes(kpad_i, 0x40);

  if (inp_1_size) {
    sha.processBytes(inp_1, inp_1_size);
  }

  if (inp_2_size) {
    sha.processBytes(inp_2, inp_2_size);
  }

  if (inp_3_size) {
    sha.processBytes(inp_3, inp_3_size);
  }

  uint8_t digest[0x14];
  sha.finalize(digest);
  sha.reset();

  // Outer
  sha.processBytes(kpad_o, 0x40);
  sha.processBytes(digest, 0x14);
  sha.finalize(digest);

  std::memcpy(out, digest, std::min((uint32_t)out_size, 0x14u));
}

void RC4(const uint8_t* key, uint32_t key_size_in, const uint8_t* data,
         uint32_t data_size, uint8_t* out, uint32_t out_size) {
  uint8_t tmp_key[0x10];
  uint32_t sbox_size;
  uint8_t sbox[0x100];
  uint32_t i;
  uint32_t j;

  // Setup RC4 session...
  std::memcpy(tmp_key, key, 0x10);
  i = j = 0;
  sbox_size = 0x100;
  for (uint32_t x = 0; x < sbox_size; x++) {
    sbox[x] = (uint8_t)x;
  }

  uint32_t idx = 0;
  for (uint32_t x = 0; x < sbox_size; x++) {
    idx = (idx + sbox[x] + key[x % 0x10]) % sbox_size;
    uint8_t temp = sbox[idx];
    sbox[idx] = sbox[x];
    sbox[x] = temp;
  }

  // Crypt data
  for (uint32_t idx = 0; idx < data_size; idx++) {
    i = (i + 1) % sbox_size;
    j = (j + sbox[i]) % sbox_size;
    uint8_t temp = sbox[i];
    sbox[i] = sbox[j];
    sbox[j] = temp;

    uint8_t a = data[idx];
    uint8_t b = sbox[(sbox[i] + sbox[j]) % sbox_size];
    out[idx] = (uint8_t)(a ^ b);
  }
}

}  // namespace util
}  // namespace kernel
}  // namespace xe
