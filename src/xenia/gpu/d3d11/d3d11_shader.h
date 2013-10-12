/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_SHADER_H_
#define XENIA_GPU_D3D11_D3D11_SHADER_H_

#include <xenia/core.h>

#include <xenia/gpu/shader.h>


namespace xe {
namespace gpu {
namespace d3d11 {


class D3D11Shader : public Shader {
public:
  D3D11Shader();
  virtual ~D3D11Shader();
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_SHADER_H_
