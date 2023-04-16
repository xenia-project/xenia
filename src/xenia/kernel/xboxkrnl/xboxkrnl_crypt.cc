/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2022 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

#ifdef XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"  // for bcrypt.h
#endif

#include "third_party/crypto/TinySHA1.hpp"
#include "third_party/crypto/des/des.cpp"
#include "third_party/crypto/des/des.h"
#include "third_party/crypto/des/des3.h"
#include "third_party/crypto/des/descbc.h"
#include "third_party/crypto/sha256.cpp"
#include "third_party/crypto/sha256.h"

extern "C" {
#include "third_party/aes_128/aes.h"
}

namespace xe {
namespace kernel {
namespace xboxkrnl {

typedef struct {
  uint8_t S[256];  // 0x0
  uint8_t i;       // 0x100
  uint8_t j;       // 0x101
} XECRYPT_RC4_STATE;
static_assert_size(XECRYPT_RC4_STATE, 0x102);

void XeCryptRc4Key_entry(pointer_t<XECRYPT_RC4_STATE> rc4_ctx, lpvoid_t key,
                         dword_t key_size) {
  // Setup RC4 state
  rc4_ctx->i = rc4_ctx->j = 0;
  for (uint32_t x = 0; x < 0x100; x++) {
    rc4_ctx->S[x] = (uint8_t)x;
  }

  uint32_t idx = 0;
  for (uint32_t x = 0; x < 0x100; x++) {
    idx = (idx + rc4_ctx->S[x] + key[x % 0x10]) % 0x100;
    uint8_t temp = rc4_ctx->S[idx];
    rc4_ctx->S[idx] = rc4_ctx->S[x];
    rc4_ctx->S[x] = temp;
  }
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptRc4Key, kNone, kImplemented);

void XeCryptRc4Ecb_entry(pointer_t<XECRYPT_RC4_STATE> rc4_ctx, lpvoid_t data,
                         dword_t size) {
  // Crypt data
  for (uint32_t idx = 0; idx < size; idx++) {
    rc4_ctx->i = (rc4_ctx->i + 1) % 0x100;
    rc4_ctx->j = (rc4_ctx->j + rc4_ctx->S[rc4_ctx->i]) % 0x100;
    uint8_t temp = rc4_ctx->S[rc4_ctx->i];
    rc4_ctx->S[rc4_ctx->i] = rc4_ctx->S[rc4_ctx->j];
    rc4_ctx->S[rc4_ctx->j] = temp;

    uint8_t a = data[idx];
    uint8_t b =
        rc4_ctx->S[(rc4_ctx->S[rc4_ctx->i] + rc4_ctx->S[rc4_ctx->j]) % 0x100];
    data[idx] = (uint8_t)(a ^ b);
  }
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptRc4Ecb, kNone, kImplemented);

void XeCryptRc4_entry(lpvoid_t key, dword_t key_size, lpvoid_t data,
                      dword_t size) {
  XECRYPT_RC4_STATE rc4_ctx;
  XeCryptRc4Key_entry(&rc4_ctx, key, key_size);
  XeCryptRc4Ecb_entry(&rc4_ctx, data, size);
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptRc4, kNone, kImplemented);

typedef struct {
  xe::be<uint32_t> count;     // 0x0
  xe::be<uint32_t> state[5];  // 0x4
  uint8_t buffer[64];         // 0x18
} XECRYPT_SHA_STATE;
static_assert_size(XECRYPT_SHA_STATE, 0x58);

void InitSha1(sha1::SHA1* sha, const XECRYPT_SHA_STATE* state) {
  uint32_t digest[5];
  std::copy(std::begin(state->state), std::end(state->state), digest);

  sha->init(digest, state->buffer, state->count);
}

void StoreSha1(const sha1::SHA1* sha, XECRYPT_SHA_STATE* state) {
  std::copy_n(sha->getDigest(), xe::countof(state->state), state->state);

  state->count = static_cast<uint32_t>(sha->getByteCount());
  std::copy_n(sha->getBlock(), sha->getBlockByteIndex(), state->buffer);
}

void XeCryptShaInit_entry(pointer_t<XECRYPT_SHA_STATE> sha_state) {
  sha_state.Zero();

  sha_state->state[0] = 0x67452301;
  sha_state->state[1] = 0xEFCDAB89;
  sha_state->state[2] = 0x98BADCFE;
  sha_state->state[3] = 0x10325476;
  sha_state->state[4] = 0xC3D2E1F0;
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptShaInit, kNone, kImplemented);

void XeCryptShaUpdate_entry(pointer_t<XECRYPT_SHA_STATE> sha_state,
                            lpvoid_t input, dword_t input_size) {
  sha1::SHA1 sha;
  InitSha1(&sha, sha_state);

  sha.processBytes(input, input_size);

  StoreSha1(&sha, sha_state);
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptShaUpdate, kNone, kImplemented);

void XeCryptShaFinal_entry(pointer_t<XECRYPT_SHA_STATE> sha_state,
                           pointer_t<uint8_t> out, dword_t out_size) {
  sha1::SHA1 sha;
  InitSha1(&sha, sha_state);

  uint8_t digest[0x14];
  sha.finalize(digest);

  std::copy_n(digest, std::min<size_t>(xe::countof(digest), out_size),
              static_cast<uint8_t*>(out));
  std::copy_n(sha.getDigest(), xe::countof(sha_state->state), sha_state->state);
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptShaFinal, kNone, kImplemented);

void XeCryptSha_entry(lpvoid_t input_1, dword_t input_1_size, lpvoid_t input_2,
                      dword_t input_2_size, lpvoid_t input_3,
                      dword_t input_3_size, lpvoid_t output,
                      dword_t output_size) {
  sha1::SHA1 sha;

  if (input_1 && input_1_size) {
    sha.processBytes(input_1, input_1_size);
  }
  if (input_2 && input_2_size) {
    sha.processBytes(input_2, input_2_size);
  }
  if (input_3 && input_3_size) {
    sha.processBytes(input_3, input_3_size);
  }

  uint8_t digest[0x14];
  sha.finalize(digest);
  std::copy_n(digest, std::min<size_t>(xe::countof(digest), output_size),
              output.as<uint8_t*>());
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptSha, kNone, kImplemented);

// TODO: Size of this struct hasn't been confirmed yet.
typedef struct {
  xe::be<uint32_t> count;     // 0x0
  xe::be<uint32_t> state[8];  // 0x4
  uint8_t buffer[64];         // 0x24
} XECRYPT_SHA256_STATE;

void XeCryptSha256Init_entry(pointer_t<XECRYPT_SHA256_STATE> sha_state) {
  sha_state.Zero();

  sha_state->state[0] = 0x6a09e667;
  sha_state->state[1] = 0xbb67ae85;
  sha_state->state[2] = 0x3c6ef372;
  sha_state->state[3] = 0xa54ff53a;
  sha_state->state[4] = 0x510e527f;
  sha_state->state[5] = 0x9b05688c;
  sha_state->state[6] = 0x1f83d9ab;
  sha_state->state[7] = 0x5be0cd19;
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptSha256Init, kNone, kImplemented);

void XeCryptSha256Update_entry(pointer_t<XECRYPT_SHA256_STATE> sha_state,
                               lpvoid_t input, dword_t input_size) {
  sha256::SHA256 sha;
  std::copy(std::begin(sha_state->state), std::end(sha_state->state),
            sha.getHashValues());
  std::copy(std::begin(sha_state->buffer), std::end(sha_state->buffer),
            sha.getBuffer());
  sha.setTotalSize(sha_state->count);

  sha.add(input, input_size);

  std::copy_n(sha.getHashValues(), xe::countof(sha_state->state),
              sha_state->state);
  std::copy_n(sha.getBuffer(), xe::countof(sha_state->buffer),
              sha_state->buffer);
  sha_state->count = static_cast<uint32_t>(sha.getTotalSize());
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptSha256Update, kNone, kImplemented);

void XeCryptSha256Final_entry(pointer_t<XECRYPT_SHA256_STATE> sha_state,
                              pointer_t<uint8_t> out, dword_t out_size) {
  sha256::SHA256 sha;
  std::copy(std::begin(sha_state->state), std::end(sha_state->state),
            sha.getHashValues());
  std::copy(std::begin(sha_state->buffer), std::end(sha_state->buffer),
            sha.getBuffer());
  sha.setTotalSize(sha_state->count);

  uint8_t hash[32];
  sha.getHash(hash);

  std::copy_n(hash, std::min<size_t>(xe::countof(hash), out_size),
              static_cast<uint8_t*>(out));
  std::copy(std::begin(hash), std::end(hash), sha_state->buffer);
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptSha256Final, kNone, kImplemented);

// Byteswaps each 8 bytes
void XeCryptBnQw_SwapDwQwLeBe_entry(pointer_t<uint64_t> qw_inp,
                                    pointer_t<uint64_t> qw_out, dword_t size) {
  xe::copy_and_swap<uint64_t>(qw_out, qw_inp, size);
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptBnQw_SwapDwQwLeBe, kNone, kImplemented);

typedef struct {
  xe::be<uint32_t> size;  // size of modulus in 8 byte units
  xe::be<uint32_t> public_exponent;
  xe::be<uint64_t> pad_8;

  // followed by modulus, followed by any private-key data
} XECRYPT_RSA;
static_assert_size(XECRYPT_RSA, 0x10);

dword_result_t XeCryptBnQwNeRsaPubCrypt_entry(pointer_t<uint64_t> qw_a,
                                              pointer_t<uint64_t> qw_b,
                                              pointer_t<XECRYPT_RSA> rsa) {
  // 0 indicates failure (but not a BOOL return value)
#ifndef XE_PLATFORM_WIN32
  XELOGE(
      "XeCryptBnQwNeRsaPubCrypt called but no implementation available for "
      "this platform!");
  assert_always();
  return 1;
#else
  uint32_t modulus_size = rsa->size * 8;

  // Convert XECRYPT blob into BCrypt format
  ULONG key_size = sizeof(BCRYPT_RSAKEY_BLOB) + sizeof(uint32_t) + modulus_size;
  auto key_buf = std::make_unique<uint8_t[]>(key_size);
  auto* key_header = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(key_buf.get());

  key_header->Magic = BCRYPT_RSAPUBLIC_MAGIC;
  key_header->BitLength = modulus_size * 8;
  key_header->cbPublicExp = sizeof(uint32_t);
  key_header->cbModulus = modulus_size;
  key_header->cbPrime1 = key_header->cbPrime2 = 0;

  // Copy in exponent/modulus, luckily these are BE inside BCrypt blob
  uint32_t* key_exponent = reinterpret_cast<uint32_t*>(&key_header[1]);
  *key_exponent = rsa->public_exponent.value;

  // ...except modulus needs to be reversed in 64-bit chunks for BCrypt to make
  // use of it properly for some reason
  uint64_t* key_modulus = reinterpret_cast<uint64_t*>(&key_exponent[1]);
  uint64_t* xecrypt_modulus = reinterpret_cast<uint64_t*>(&rsa[1]);
  std::reverse_copy(xecrypt_modulus, xecrypt_modulus + rsa->size, key_modulus);

  BCRYPT_ALG_HANDLE hAlgorithm = NULL;
  NTSTATUS status = BCryptOpenAlgorithmProvider(
      &hAlgorithm, BCRYPT_RSA_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0);

  if (!BCRYPT_SUCCESS(status)) {
    XELOGE(
        "XeCryptBnQwNeRsaPubCrypt: BCryptOpenAlgorithmProvider failed with "
        "status {:#X}!",
        status);
    return 0;
  }

  BCRYPT_KEY_HANDLE hKey = NULL;
  status = BCryptImportKeyPair(hAlgorithm, NULL, BCRYPT_RSAPUBLIC_BLOB, &hKey,
                               key_buf.get(), key_size, 0);

  if (!BCRYPT_SUCCESS(status)) {
    XELOGE(
        "XeCryptBnQwNeRsaPubCrypt: BCryptImportKeyPair failed with status "
        "{:#X}!",
        status);

    if (hAlgorithm) {
      BCryptCloseAlgorithmProvider(hAlgorithm, 0);
    }

    return 0;
  }

  // Byteswap & reverse the input into output, as BCrypt wants MSB first
  uint64_t* output = qw_b;
  uint8_t* output_bytes = reinterpret_cast<uint8_t*>(output);
  xe::copy_and_swap<uint64_t>(output, qw_a, rsa->size);
  std::reverse(output_bytes, output_bytes + modulus_size);

  // BCryptDecrypt only works with private keys, fortunately BCryptEncrypt
  // performs the right actions needed for us to decrypt the input
  ULONG result_size = 0;
  status =
      BCryptEncrypt(hKey, output_bytes, modulus_size, nullptr, nullptr, 0,
                    output_bytes, modulus_size, &result_size, BCRYPT_PAD_NONE);

  assert(result_size == modulus_size);

  if (!BCRYPT_SUCCESS(status)) {
    XELOGE("XeCryptBnQwNeRsaPubCrypt: BCryptEncrypt failed with status {:#X}!",
           status);
  } else {
    // Reverse data & byteswap again so data is as game expects
    std::reverse(output_bytes, output_bytes + modulus_size);
    xe::copy_and_swap(output, output, rsa->size);
  }

  if (hKey) {
    BCryptDestroyKey(hKey);
  }
  if (hAlgorithm) {
    BCryptCloseAlgorithmProvider(hAlgorithm, 0);
  }

  return BCRYPT_SUCCESS(status) ? 1 : 0;
#endif
}
#ifdef XE_PLATFORM_WIN32
DECLARE_XBOXKRNL_EXPORT1(XeCryptBnQwNeRsaPubCrypt, kNone, kImplemented);
#else
DECLARE_XBOXKRNL_EXPORT1(XeCryptBnQwNeRsaPubCrypt, kNone, kStub);
#endif

dword_result_t XeCryptBnDwLePkcs1Verify_entry(lpvoid_t hash, lpvoid_t sig,
                                              dword_t size) {
  // BOOL return value
  return 1;
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptBnDwLePkcs1Verify, kNone, kStub);

void XeCryptRandom_entry(lpvoid_t buf, dword_t buf_size) {
  std::memset(buf, 0xFD, buf_size);
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptRandom, kNone, kStub);

struct XECRYPT_DES_STATE {
  uint32_t keytab[16][2];
};

// Sets bit 0 to make the parity odd
void XeCryptDesParity_entry(lpvoid_t inp, dword_t inp_size, lpvoid_t out_ptr) {
  DES::set_parity(inp, inp_size, out_ptr);
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptDesParity, kNone, kImplemented);

struct XECRYPT_DES3_STATE {
  XECRYPT_DES_STATE des_state[3];
};

void XeCryptDes3Key_entry(pointer_t<XECRYPT_DES3_STATE> state_ptr,
                          lpqword_t key) {
  DES3 des3(key[0], key[1], key[2]);
  DES* des = des3.getDES();

  // Store our DES state into the state.
  for (int i = 0; i < 3; i++) {
    std::memcpy(state_ptr->des_state[i].keytab, des[i].get_sub_key(), 128);
  }
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptDes3Key, kNone, kImplemented);

void XeCryptDes3Ecb_entry(pointer_t<XECRYPT_DES3_STATE> state_ptr,
                          lpqword_t inp, lpqword_t out, dword_t encrypt) {
  DES3 des3((ui64*)state_ptr->des_state[0].keytab,
            (ui64*)state_ptr->des_state[1].keytab,
            (ui64*)state_ptr->des_state[2].keytab);

  if (encrypt) {
    *out = des3.encrypt(*inp);
  } else {
    *out = des3.decrypt(*inp);
  }
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptDes3Ecb, kNone, kImplemented);

void XeCryptDes3Cbc_entry(pointer_t<XECRYPT_DES3_STATE> state_ptr,
                          lpqword_t inp, dword_t inp_size, lpqword_t out,
                          lpqword_t feed, dword_t encrypt) {
  DES3 des3((ui64*)state_ptr->des_state[0].keytab,
            (ui64*)state_ptr->des_state[1].keytab,
            (ui64*)state_ptr->des_state[2].keytab);

  // DES can only do 8-byte chunks at a time!
  assert_true(inp_size % 8 == 0);

  uint64_t last_block = *feed;
  for (uint32_t i = 0; i < inp_size / 8; i++) {
    uint64_t block = inp[i];
    if (encrypt) {
      last_block = des3.encrypt(block ^ last_block);
      out[i] = last_block;
    } else {
      out[i] = des3.decrypt(block) ^ last_block;
      last_block = block;
    }
  }

  *feed = last_block;
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptDes3Cbc, kNone, kImplemented);

struct XECRYPT_AES_STATE {
  uint8_t keytabenc[11][4][4];  // 0x0
  uint8_t keytabdec[11][4][4];  // 0xB0
};
static_assert_size(XECRYPT_AES_STATE, 0x160);

static inline uint8_t xeXeCryptAesMul2(uint8_t a) {
  return (a & 0x80) ? ((a << 1) ^ 0x1B) : (a << 1);
}

void XeCryptAesKey_entry(pointer_t<XECRYPT_AES_STATE> state_ptr, lpvoid_t key) {
  aes_key_schedule_128(key, reinterpret_cast<uint8_t*>(state_ptr->keytabenc));
  // Decryption key schedule not needed by openluopworld/aes_128, but generated
  // to fill the context structure properly.
  std::memcpy(state_ptr->keytabdec[0], state_ptr->keytabenc[10], 16);
  // Inverse MixColumns.
  for (uint32_t i = 1; i < 10; ++i) {
    const uint8_t* enc =
        reinterpret_cast<const uint8_t*>(state_ptr->keytabenc[10 - i]);
    uint8_t* dec = reinterpret_cast<uint8_t*>(state_ptr->keytabdec[i]);
    uint8_t t, u, v;
    t = enc[0] ^ enc[1] ^ enc[2] ^ enc[3];
    dec[0] = t ^ enc[0] ^ xeXeCryptAesMul2(enc[0] ^ enc[1]);
    dec[1] = t ^ enc[1] ^ xeXeCryptAesMul2(enc[1] ^ enc[2]);
    dec[2] = t ^ enc[2] ^ xeXeCryptAesMul2(enc[2] ^ enc[3]);
    dec[3] = t ^ enc[3] ^ xeXeCryptAesMul2(enc[3] ^ enc[0]);
    u = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[0] ^ enc[2]));
    v = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[1] ^ enc[3]));
    t = xeXeCryptAesMul2(u ^ v);
    dec[0] ^= t ^ u;
    dec[1] ^= t ^ v;
    dec[2] ^= t ^ u;
    dec[3] ^= t ^ v;
    t = enc[4] ^ enc[5] ^ enc[6] ^ enc[7];
    dec[4] = t ^ enc[4] ^ xeXeCryptAesMul2(enc[4] ^ enc[5]);
    dec[5] = t ^ enc[5] ^ xeXeCryptAesMul2(enc[5] ^ enc[6]);
    dec[6] = t ^ enc[6] ^ xeXeCryptAesMul2(enc[6] ^ enc[7]);
    dec[7] = t ^ enc[7] ^ xeXeCryptAesMul2(enc[7] ^ enc[4]);
    u = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[4] ^ enc[6]));
    v = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[5] ^ enc[7]));
    t = xeXeCryptAesMul2(u ^ v);
    dec[4] ^= t ^ u;
    dec[5] ^= t ^ v;
    dec[6] ^= t ^ u;
    dec[7] ^= t ^ v;
    t = enc[8] ^ enc[9] ^ enc[10] ^ enc[11];
    dec[8] = t ^ enc[8] ^ xeXeCryptAesMul2(enc[8] ^ enc[9]);
    dec[9] = t ^ enc[9] ^ xeXeCryptAesMul2(enc[9] ^ enc[10]);
    dec[10] = t ^ enc[10] ^ xeXeCryptAesMul2(enc[10] ^ enc[11]);
    dec[11] = t ^ enc[11] ^ xeXeCryptAesMul2(enc[11] ^ enc[8]);
    u = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[8] ^ enc[10]));
    v = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[9] ^ enc[11]));
    t = xeXeCryptAesMul2(u ^ v);
    dec[8] ^= t ^ u;
    dec[9] ^= t ^ v;
    dec[10] ^= t ^ u;
    dec[11] ^= t ^ v;
    t = enc[12] ^ enc[13] ^ enc[14] ^ enc[15];
    dec[12] = t ^ enc[12] ^ xeXeCryptAesMul2(enc[12] ^ enc[13]);
    dec[13] = t ^ enc[13] ^ xeXeCryptAesMul2(enc[13] ^ enc[14]);
    dec[14] = t ^ enc[14] ^ xeXeCryptAesMul2(enc[14] ^ enc[15]);
    dec[15] = t ^ enc[15] ^ xeXeCryptAesMul2(enc[15] ^ enc[12]);
    u = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[12] ^ enc[14]));
    v = xeXeCryptAesMul2(xeXeCryptAesMul2(enc[13] ^ enc[15]));
    t = xeXeCryptAesMul2(u ^ v);
    dec[12] ^= t ^ u;
    dec[13] ^= t ^ v;
    dec[14] ^= t ^ u;
    dec[15] ^= t ^ v;
  }
  std::memcpy(state_ptr->keytabdec[10], state_ptr->keytabenc[0], 16);
  // TODO(Triang3l): Verify the order in keytabenc and everything in keytabdec.
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptAesKey, kNone, kImplemented);

void XeCryptAesEcb_entry(pointer_t<XECRYPT_AES_STATE> state_ptr,
                         lpvoid_t inp_ptr, lpvoid_t out_ptr, dword_t encrypt) {
  const uint8_t* keytab =
      reinterpret_cast<const uint8_t*>(state_ptr->keytabenc);
  if (encrypt) {
    aes_encrypt_128(keytab, inp_ptr, out_ptr);
  } else {
    aes_decrypt_128(keytab, inp_ptr, out_ptr);
  }
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptAesEcb, kNone, kImplemented);

void XeCryptAesCbc_entry(pointer_t<XECRYPT_AES_STATE> state_ptr,
                         lpvoid_t inp_ptr, dword_t inp_size, lpvoid_t out_ptr,
                         lpvoid_t feed_ptr, dword_t encrypt) {
  const uint8_t* keytab =
      reinterpret_cast<const uint8_t*>(state_ptr->keytabenc);
  const uint8_t* inp = inp_ptr.as<const uint8_t*>();
  uint8_t* out = out_ptr.as<uint8_t*>();
  uint8_t* feed = feed_ptr.as<uint8_t*>();
  if (encrypt) {
    for (uint32_t i = 0; i < inp_size; i += 16) {
      for (uint32_t j = 0; j < 16; ++j) {
        feed[j] ^= inp[j];
      }
      aes_encrypt_128(keytab, feed, feed);
      std::memcpy(out, feed, 16);
      inp += 16;
      out += 16;
    }
  } else {
    for (uint32_t i = 0; i < inp_size; i += 16) {
      // In case inp == out.
      uint8_t tmp[16];
      std::memcpy(tmp, inp, 16);
      aes_decrypt_128(keytab, inp, out);
      for (uint32_t j = 0; j < 16; ++j) {
        out[j] ^= feed[j];
      }
      std::memcpy(feed, tmp, 16);
      inp += 16;
      out += 16;
    }
  }
}
DECLARE_XBOXKRNL_EXPORT1(XeCryptAesCbc, kNone, kImplemented);

void XeCryptHmacSha_entry(lpvoid_t key, dword_t key_size_in, lpvoid_t inp_1,
                          dword_t inp_1_size, lpvoid_t inp_2,
                          dword_t inp_2_size, lpvoid_t inp_3,
                          dword_t inp_3_size, lpvoid_t out, dword_t out_size) {
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
DECLARE_XBOXKRNL_EXPORT1(XeCryptHmacSha, kNone, kImplemented);

// Keys
// TODO: Array of keys we need

// Retail key 0x19
static const uint8_t key19[] = {0xE1, 0xBC, 0x15, 0x9C, 0x73, 0xB1, 0xEA, 0xE9,
                                0xAB, 0x31, 0x70, 0xF3, 0xAD, 0x47, 0xEB, 0xF3};

dword_result_t XeKeysHmacSha_entry(dword_t key_num, lpvoid_t inp_1,
                                   dword_t inp_1_size, lpvoid_t inp_2,
                                   dword_t inp_2_size, lpvoid_t inp_3,
                                   dword_t inp_3_size, lpvoid_t out,
                                   dword_t out_size) {
  const uint8_t* key = nullptr;
  if (key_num == 0x19) {
    key = key19;
  }

  if (key) {
    XeCryptHmacSha_entry((void*)key, 0x10, inp_1, inp_1_size, inp_2, inp_2_size,
                         inp_3, inp_3_size, out, out_size);

    return X_STATUS_SUCCESS;
  }

  return X_STATUS_UNSUCCESSFUL;
}
DECLARE_XBOXKRNL_EXPORT1(XeKeysHmacSha, kNone, kImplemented);

static const uint8_t xe_key_obfuscation_key[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

dword_result_t XeKeysAesCbcUsingKey_entry(lpvoid_t obscured_key,
                                          lpvoid_t inp_ptr, dword_t inp_size,
                                          lpvoid_t out_ptr, lpvoid_t feed_ptr,
                                          dword_t encrypt) {
  uint8_t key[16];

  // Deobscure key
  XECRYPT_AES_STATE aes;
  XeCryptAesKey_entry(&aes, (uint8_t*)xe_key_obfuscation_key);
  XeCryptAesEcb_entry(&aes, obscured_key, key, 0);

  // Run CBC using deobscured key
  XeCryptAesKey_entry(&aes, key);
  XeCryptAesCbc_entry(&aes, inp_ptr, inp_size, out_ptr, feed_ptr, encrypt);

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XeKeysAesCbcUsingKey, kNone, kImplemented);

dword_result_t XeKeysObscureKey_entry(lpvoid_t input, lpvoid_t output) {
  // Based on HvxKeysObscureKey
  // Seems to encrypt input with per-console KEY_OBFUSCATION_KEY (key 0x18)

  XECRYPT_AES_STATE aes;
  XeCryptAesKey_entry(&aes, (uint8_t*)xe_key_obfuscation_key);
  XeCryptAesEcb_entry(&aes, input, output, 1);

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XeKeysObscureKey, kNone, kImplemented);

dword_result_t XeKeysHmacShaUsingKey_entry(lpvoid_t obscured_key,
                                           lpvoid_t inp_1, dword_t inp_1_size,
                                           lpvoid_t inp_2, dword_t inp_2_size,
                                           lpvoid_t inp_3, dword_t inp_3_size,
                                           lpvoid_t out, dword_t out_size) {
  if (!obscured_key) {
    return X_STATUS_INVALID_PARAMETER;
  }

  uint8_t key[16];

  // Deobscure key
  XECRYPT_AES_STATE aes;
  XeCryptAesKey_entry(&aes, (uint8_t*)xe_key_obfuscation_key);
  XeCryptAesEcb_entry(&aes, obscured_key, key, 0);

  XeCryptHmacSha_entry(key, 0x10, inp_1, inp_1_size, inp_2, inp_2_size, inp_3,
                       inp_3_size, out, out_size);
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XeKeysHmacShaUsingKey, kNone, kImplemented);

//going off of usage in some hbrew xex
//0 and 1 appear to be devkit, 2 is retail
//we default to saying we're retail
dword_result_t XeKeysGetConsoleType_entry(lpdword_t type_out) { 
	*type_out = 2; 
	return 0;
}

DECLARE_XBOXKRNL_EXPORT1(XeKeysGetConsoleType, kNone, kImplemented);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Crypt);
