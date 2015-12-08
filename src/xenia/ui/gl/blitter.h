/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GL_BLITTER_H_
#define XENIA_UI_GL_BLITTER_H_

#include <memory>

#include "xenia/ui/gl/gl.h"

namespace xe {
namespace ui {
namespace gl {

struct Rect2D {
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
  Rect2D() : x(0), y(0), width(0), height(0) {}
  Rect2D(int32_t x_, int32_t y_, int32_t width_, int32_t height_)
      : x(x_), y(y_), width(width_), height(height_) {}
  int32_t right() const { return x + width; }
  int32_t bottom() const { return y + height; }
};

class Blitter {
 public:
  Blitter();
  ~Blitter();

  bool Initialize();
  void Shutdown();

  void BlitTexture2D(GLuint src_texture, Rect2D src_rect, Rect2D dest_rect,
                     GLenum filter, bool swap_channels);

  void CopyColorTexture2D(GLuint src_texture, Rect2D src_rect,
                          GLuint dest_texture, Rect2D dest_rect, GLenum filter,
                          bool swap_channels);
  void CopyDepthTexture(GLuint src_texture, Rect2D src_rect,
                        GLuint dest_texture, Rect2D dest_rect);

 private:
  void Draw(GLuint src_texture, Rect2D src_rect, Rect2D dest_rect,
            GLenum filter);

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

}  // namespace gl
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_BLITTER_H_
