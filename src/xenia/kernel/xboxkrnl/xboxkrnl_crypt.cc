/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

#include "third_party/crypto/TinySHA1.hpp"

namespace xe {
namespace kernel {
namespace xboxkrnl {

typedef struct {
  xe::be<uint32_t> count;     // 0x0
  xe::be<uint32_t> state[5];  // 0x4
  uint8_t buffer[64];         // 0x18
} XECRYPT_SHA_STATE;

void InitSha1(sha1::SHA1* sha, const XECRYPT_SHA_STATE* state) {
  uint32_t digest[5];
  for (int i = 0; i < 5; i++) {
    digest[i] = state->state[i];
  }

  sha->init(digest, state->buffer, state->count);
}

void StoreSha1(sha1::SHA1* sha, XECRYPT_SHA_STATE* state) {
  for (int i = 0; i < 5; i++) {
    state->state[i] = sha->getDigest()[i];
  }

  state->count = static_cast<uint32_t>(sha->getByteCount());
  std::memcpy(state->buffer, sha->getBlock(), sha->getBlockByteIndex());
}

void XeCryptShaInit(pointer_t<XECRYPT_SHA_STATE> sha_state) {
  sha_state.Zero();

  sha_state->state[0] = 0x67452301;
  sha_state->state[1] = 0xEFCDAB89;
  sha_state->state[2] = 0x98BADCFE;
  sha_state->state[3] = 0x10325476;
  sha_state->state[4] = 0xC3D2E1F0;
}
DECLARE_XBOXKRNL_EXPORT(XeCryptShaInit, ExportTag::kImplemented);

void XeCryptShaUpdate(pointer_t<XECRYPT_SHA_STATE> sha_state, lpvoid_t input,
                      dword_t input_size) {
  sha1::SHA1 sha;
  InitSha1(&sha, sha_state);

  sha.processBytes(input, input_size);

  StoreSha1(&sha, sha_state);
}
DECLARE_XBOXKRNL_EXPORT(XeCryptShaUpdate, ExportTag::kImplemented);

void XeCryptShaFinal(pointer_t<XECRYPT_SHA_STATE> sha_state,
                     pointer_t<xe::be<uint32_t>> out, dword_t out_size) {
  sha1::SHA1 sha;
  InitSha1(&sha, sha_state);

  uint32_t digest[5];
  sha.finalize(digest);

  for (int i = 0; i < 5; i++) {
    sha_state->state[i] = digest[i];
  }

  for (uint32_t i = 0; i < out_size / 4; i++) {
    out[i] = digest[i];
  }
}
DECLARE_XBOXKRNL_EXPORT(XeCryptShaFinal, ExportTag::kImplemented);

void XeCryptSha(lpvoid_t input_1, dword_t input_1_size, lpvoid_t input_2,
                dword_t input_2_size, lpvoid_t input_3, dword_t input_3_size,
                pointer_t<xe::be<uint32_t>> output, dword_t output_size) {
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

  uint32_t digest[5];
  sha.finalize(digest);

  for (uint32_t i = 0; i < output_size / 4; i++) {
    output[i] = digest[i];
  }
}
DECLARE_XBOXKRNL_EXPORT(XeCryptSha, ExportTag::kImplemented);

// Byteswap?
dword_result_t XeCryptBnQw_SwapDwQwLeBe(lpqword_t qw_inp, lpqword_t qw_out,
                                        dword_t size) {
  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XeCryptBnQw_SwapDwQwLeBe, ExportTag::kStub);

dword_result_t XeCryptBnQwNeRsaPubCrypt(lpqword_t qw_a, lpqword_t qw_b,
                                        lpvoid_t rsa) {
  // 0 indicates failure (but not a BOOL return value)
  return 1;
}
DECLARE_XBOXKRNL_EXPORT(XeCryptBnQwNeRsaPubCrypt, ExportTag::kStub);

dword_result_t XeCryptBnDwLePkcs1Verify(lpvoid_t hash, lpvoid_t sig,
                                        dword_t size) {
  // BOOL return value
  return 1;
}
DECLARE_XBOXKRNL_EXPORT(XeCryptBnDwLePkcs1Verify, ExportTag::kStub);

void RegisterCryptExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state) {}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
