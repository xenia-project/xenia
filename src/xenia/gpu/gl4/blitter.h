/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_BLITTER_H_
#define XENIA_GPU_GL4_BLITTER_H_

#include <memory>

#include "third_party/GL/glew.h"
#include "third_party/GL/wglew.h"

namespace xe {
namespace gpu {
namespace gl4 {

class Blitter {
 public:
  Blitter();
  ~Blitter();

  bool Initialize();
  void Shutdown();

  void BlitTexture2D(GLuint src_texture, uint32_t src_x, uint32_t src_y,
                     uint32_t src_width, uint32_t src_height, uint32_t dest_x,
                     uint32_t dest_y, uint32_t dest_width, uint32_t dest_height,
                     GLenum filter);

  void CopyColorTexture2D(GLuint src_texture, uint32_t src_x, uint32_t src_y,
                          uint32_t src_width, uint32_t src_height,
                          uint32_t dest_texture, uint32_t dest_x,
                          uint32_t dest_y, uint32_t dest_width,
                          uint32_t dest_height, GLenum filter);
  void CopyDepthTexture(GLuint src_texture, uint32_t src_x, uint32_t src_y,
                        uint32_t src_width, uint32_t src_height,
                        uint32_t dest_texture, uint32_t dest_x, uint32_t dest_y,
                        uint32_t dest_width, uint32_t dest_height);

 private:
  void Draw(GLuint src_texture, uint32_t src_x, uint32_t src_y,
            uint32_t src_width, uint32_t src_height, GLenum filter);

  GLuint vertex_program_;
  GLuint color_fragment_program_;
  GLuint depth_fragment_program_;
  GLuint color_pipeline_;
  GLuint depth_pipeline_;
  GLuint vbo_;
  GLuint vao_;
  GLuint nearest_sampler_;
  GLuint linear_sampler_;
  GLuint scratch_framebuffer_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_BLITTER_H_
