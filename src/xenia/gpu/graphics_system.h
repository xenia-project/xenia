/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GRAPHICS_SYSTEM_H_
#define XENIA_GPU_GRAPHICS_SYSTEM_H_

#include <xenia/core.h>


namespace xe {
namespace gpu {


class CreationParams {
public:
  xe_memory_ref memory;
};


class GraphicsSystem {
public:
  virtual ~GraphicsSystem();

  xe_memory_ref memory();

protected:
  GraphicsSystem(const CreationParams* params);

  xe_memory_ref   memory_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_GRAPHICS_SYSTEM_H_
