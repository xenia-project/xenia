/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_NOP_NOP_GRAPHICS_DRIVER_H_
#define XENIA_GPU_NOP_NOP_GRAPHICS_DRIVER_H_

#include <xenia/core.h>

#include <xenia/gpu/graphics_driver.h>
#include <xenia/gpu/nop/nop_gpu-private.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {

class ShaderCache;

namespace nop {


class NopGraphicsDriver : public GraphicsDriver {
public:
  NopGraphicsDriver(Memory* memory);
  virtual ~NopGraphicsDriver();

  virtual void Initialize();

  virtual void InvalidateState(
      uint32_t mask);
  virtual void SetShader(
      xenos::XE_GPU_SHADER_TYPE type,
      uint32_t address,
      uint32_t start,
      uint32_t length);
  virtual void DrawIndexBuffer(
      xenos::XE_GPU_PRIMITIVE_TYPE prim_type,
      bool index_32bit, uint32_t index_count,
      uint32_t index_base, uint32_t index_size, uint32_t endianness);
  virtual void DrawIndexAuto(
      xenos::XE_GPU_PRIMITIVE_TYPE prim_type,
      uint32_t index_count);

protected:
  ShaderCache*  shader_cache_;
};


}  // namespace nop
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_NOP_NOP_GRAPHICS_DRIVER_H_
