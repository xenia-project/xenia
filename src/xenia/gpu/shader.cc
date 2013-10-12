/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/shader.h>

#include <xenia/gpu/xenos/ucode_disassembler.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


Shader::Shader(
    XE_GPU_SHADER_TYPE type,
    const uint8_t* src_ptr, size_t length,
    uint64_t hash) :
    type_(type), hash_(hash) {
  // Verify.
  dword_count_ = length / 4;
  XEASSERT(dword_count_ <= 512);

  // Copy bytes and swap.
  size_t byte_size = dword_count_ * sizeof(uint32_t);
  dwords_ = (uint32_t*)xe_malloc(byte_size);
  for (uint32_t n = 0; n < dword_count_; n++) {
    dwords_[n] = XEGETUINT32BE(src_ptr + n * 4);
  }
}

Shader::~Shader() {
  xe_free(dwords_);
}

char* Shader::Disassemble() {
  return DisassembleShader(type_, dwords_, dword_count_);
}

int Shader::Prepare() {
  return 0;
}
