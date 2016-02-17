/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GL_GL_IMMEDIATE_DRAWER_H_
#define XENIA_UI_GL_GL_IMMEDIATE_DRAWER_H_

#include <memory>

#include "xenia/ui/gl/gl.h"
#include "xenia/ui/immediate_drawer.h"

namespace xe {
namespace ui {
namespace gl {

class GLImmediateDrawer : public ImmediateDrawer {
 public:
  GLImmediateDrawer(GraphicsContext* graphics_context);
  ~GLImmediateDrawer() override;

  std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t width,
                                                  uint32_t height,
                                                  ImmediateTextureFilter filter,
                                                  bool repeat,
                                                  const uint8_t* data) override;
  void UpdateTexture(ImmediateTexture* texture, const uint8_t* data) override;

  void Begin(int render_target_width, int render_target_height) override;
  void BeginDrawBatch(const ImmediateDrawBatch& batch) override;
  void Draw(const ImmediateDraw& draw) override;
  void EndDrawBatch() override;
  void End() override;

 private:
  void InitializeShaders();

  GLuint program_ = 0;
  GLuint vao_ = 0;
  GLuint vertex_buffer_ = 0;
  GLuint index_buffer_ = 0;

  bool was_current_ = false;
  bool batch_has_index_buffer_ = false;
};

}  // namespace gl
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_GL_IMMEDIATE_DRAWER_H_
