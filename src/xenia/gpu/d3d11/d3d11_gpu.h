/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_GPU_H_
#define XENIA_GPU_D3D11_D3D11_GPU_H_

#include <xenia/core.h>


XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS2(xe, gpu, GraphicsSystem);


namespace xe {
namespace gpu {
namespace d3d11 {


GraphicsSystem* Create(Emulator* emulator);


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_GPU_H_
