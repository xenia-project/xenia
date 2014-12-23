/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2014 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#ifndef XENIA_GPU_GL4_GL4_SHADER_H_
#define XENIA_GPU_GL4_GL4_SHADER_H_

#include <xenia/common.h>
#include <xenia/gpu/shader.h>

namespace xe {
namespace gpu {
namespace gl4 {

class GL4Shader : public Shader {
 public:
  using Shader::Shader;

 protected:
  bool TranslateImpl() override;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_GL4_SHADER_H_
