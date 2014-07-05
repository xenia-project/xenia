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
namespace nop {


class NopGraphicsDriver : public GraphicsDriver {
public:
  NopGraphicsDriver(Memory* memory);
  virtual ~NopGraphicsDriver();

  ResourceCache* resource_cache() const override { return resource_cache_; }

  int Initialize() override;

  int Draw(const DrawCommand& command) override;

  int Resolve() override;

protected:
  ResourceCache* resource_cache_;
};


}  // namespace nop
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_NOP_NOP_GRAPHICS_DRIVER_H_
