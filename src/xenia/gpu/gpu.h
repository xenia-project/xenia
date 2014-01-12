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


XEDECLARECLASS1(xe, Emulator);


namespace xe {
namespace gpu {


GraphicsSystem* Create(Emulator* emulator);

GraphicsSystem* CreateNop(Emulator* emulator);

#if XE_PLATFORM_WIN32
GraphicsSystem* CreateD3D11(Emulator* emulator);
#endif  // WIN32


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_GPU_H_
