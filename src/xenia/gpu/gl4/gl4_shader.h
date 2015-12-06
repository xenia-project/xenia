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

#include <string>

#include "xenia/gpu/shader.h"
#include "xenia/ui/gl/gl_context.h"

namespace xe {
namespace gpu {
namespace gl4 {

class GL4Shader : public Shader {
 public:
  GL4Shader(ShaderType shader_type, uint64_t data_hash,
            const uint32_t* dword_ptr, uint32_t dword_count);
  ~GL4Shader() override;

  GLuint program() const { return program_; }
  GLuint vao() const { return vao_; }

  bool Prepare() override;

 protected:
  bool PrepareVertexArrayObject();
  bool CompileProgram();

  GLuint program_;
  GLuint vao_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_GL4_SHADER_H_
