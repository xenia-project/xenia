/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_H_
#define XENIA_GPU_D3D11_D3D11_H_

#include <xenia/core.h>


namespace xe {
namespace gpu {

class CreationParams;
class GraphicsSystem;

namespace d3d11 {


GraphicsSystem* Create(const CreationParams* params);


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_H_
