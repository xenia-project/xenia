/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADER_CACHE_H_
#define XENIA_GPU_SHADER_CACHE_H_

#include <xenia/core.h>
#include <xenia/gpu/shader.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


class ShaderCache {
public:
  ShaderCache();
  virtual ~ShaderCache();

  Shader* Create(
      xenos::XE_GPU_SHADER_TYPE type,
      const uint8_t* src_ptr, size_t length);
  Shader* Find(
      xenos::XE_GPU_SHADER_TYPE type,
      const uint8_t* src_ptr, size_t length);
  Shader* FindOrCreate(
      xenos::XE_GPU_SHADER_TYPE type,
      const uint8_t* src_ptr, size_t length);

  void Clear();

private:
  uint64_t Hash(const uint8_t* src_ptr, size_t length);

  std::unordered_map<uint64_t, Shader*> map_;

protected:
  virtual Shader* CreateCore(
      xenos::XE_GPU_SHADER_TYPE type,
      const uint8_t* src_ptr, size_t length,
      uint64_t hash);
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_SHADER_CACHE_H_
