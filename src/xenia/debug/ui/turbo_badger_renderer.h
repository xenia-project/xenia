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

#include <memory>

#include "xenia/ui/gl/circular_buffer.h"
#include "xenia/ui/gl/gl.h"

#include "third_party/turbobadger/src/tb/tb_renderer.h"
#include "third_party/turbobadger/src/tb/tb_renderer_batcher.h"

namespace xe {
namespace debug {
namespace ui {

class TBRendererGL4 : public tb::TBRendererBatcher {
 public:
  TBRendererGL4();
  ~TBRendererGL4() override;

  static std::unique_ptr<TBRendererGL4> Create();

  void BeginPaint(int render_target_w, int render_target_h) override;
  void EndPaint() override;

  tb::TBBitmap* CreateBitmap(int width, int height, uint32_t* data) override;

  void RenderBatch(Batch* batch) override;
  void SetClipRect(const tb::TBRect& rect) override;

 private:
  class TBBitmapGL4;

  bool Initialize();
  void Flush();

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
