/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_REGISTER_FILE_H_
#define XENIA_GPU_REGISTER_FILE_H_

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/memory.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {

struct RegisterInfo {
  enum class Type {
    kDword,
    kFloat,
  };
  Type type;
  const char* name;
};

class RegisterFile {
 public:
  RegisterFile();

  static const RegisterInfo* GetRegisterInfo(uint32_t index);
  static bool IsValidRegister(uint32_t index);
  static constexpr size_t kRegisterCount = 0x5003;
  uint32_t values[kRegisterCount];

  const uint32_t& operator[](uint32_t reg) const { return values[reg]; }
  uint32_t& operator[](uint32_t reg) { return values[reg]; }

  template <typename T>
  T Get(uint32_t reg) const {
    return xe::memory::Reinterpret<T>(values[reg]);
  }
  template <typename T>
  T Get(Register reg) const {
    return Get<T>(static_cast<uint32_t>(reg));
  }
  template <typename T>
  T Get() const {
    return Get<T>(T::register_index);
  }

  xenos::xe_gpu_vertex_fetch_t GetVertexFetch(uint32_t index) const {
    assert_true(index < 96);
    xenos::xe_gpu_vertex_fetch_t fetch;
    std::memcpy(&fetch,
                &values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
                        (sizeof(fetch) / sizeof(uint32_t)) * index],
                sizeof(fetch));
    return fetch;
  }

  xenos::xe_gpu_texture_fetch_t GetTextureFetch(uint32_t index) const {
    assert_true(index < 32);
    xenos::xe_gpu_texture_fetch_t fetch;
    std::memcpy(&fetch,
                &values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
                        (sizeof(fetch) / sizeof(uint32_t)) * index],
                sizeof(fetch));
    return fetch;
  }

  xenos::xe_gpu_memexport_stream_t GetMemExportStream(
      uint32_t float_constant_index) const {
    assert_true(float_constant_index < 512);
    xenos::xe_gpu_memexport_stream_t stream;
    std::memcpy(
        &stream,
        &values[XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * float_constant_index],
        sizeof(stream));
    return stream;
  }
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_REGISTER_FILE_H_
