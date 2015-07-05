/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_TURBO_BADGER_RENDERER_H_
#define XENIA_DEBUG_UI_TURBO_BADGER_RENDERER_H_

#include <cstddef>
#include <memory>

#include "xenia/ui/gl/circular_buffer.h"
#include "xenia/ui/gl/gl_context.h"
#include "xenia/ui/gl/gl.h"

#include "third_party/turbobadger/src/tb/graphics/batching_renderer.h"

namespace xe {
namespace debug {
namespace ui {

class TBRendererGL4 : public tb::graphics::BatchingRenderer {
 public:
  TBRendererGL4(xe::ui::gl::GLContext* context);
  ~TBRendererGL4() override;

  static std::unique_ptr<TBRendererGL4> Create(xe::ui::gl::GLContext* context);

  void BeginPaint(int render_target_w, int render_target_h) override;
  void EndPaint() override;

  tb::graphics::Bitmap* CreateBitmap(int width, int height,
                                     uint32_t* data) override;

  void RenderBatch(Batch* batch) override;
  void SetClipRect(const tb::Rect& rect) override;

 private:
  class TBBitmapGL4;

  bool Initialize();
  void Flush();

  xe::ui::gl::GLContext* context_ = nullptr;

  GLuint program_ = 0;
  GLuint vao_ = 0;
  xe::ui::gl::CircularBuffer vertex_buffer_;

  static const size_t kMaxCommands = 512;
  struct {
    GLenum prim_type;
    size_t vertex_offset;
    size_t vertex_count;
  } draw_commands_[kMaxCommands] = {0};
  uint32_t draw_command_count_ = 0;
  TBBitmapGL4* current_bitmap_ = nullptr;
};

}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_TURBO_BADGER_RENDERER_H_
