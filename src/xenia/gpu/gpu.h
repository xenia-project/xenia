/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GPU_H_
#define XENIA_GPU_GPU_H_

#include <xenia/gpu/graphics_system.h>


namespace xe {
namespace gpu {


GraphicsSystem* Create(const CreationParams* params);

GraphicsSystem* CreateNop(const CreationParams* params);

#if XE_PLATFORM(WIN32)
GraphicsSystem* CreateD3D11(const CreationParams* params);
#endif  // WIN32


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_GPU_H_
