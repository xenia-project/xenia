/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_IMGUI_RENDERER_H_
#define XENIA_DEBUG_UI_IMGUI_RENDERER_H_

#include <memory>

#include "third_party/imgui/imgui.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/gl/circular_buffer.h"
#include "xenia/ui/gl/gl_context.h"
#include "xenia/ui/window.h"

namespace xe {
namespace debug {
namespace ui {

class ImGuiRenderer {
 public:
  ImGuiRenderer(xe::ui::Window* window, xe::ui::GraphicsContext* context);
  ~ImGuiRenderer();

 private:
  static ImGuiRenderer* global_renderer_;

  void Initialize();
  void InitializeShaders();
  void InitializeFontTextures();
  void Shutdown();

  void RenderDrawLists(ImDrawData* data);

  xe::ui::Window* window_ = nullptr;
  xe::ui::GraphicsContext* context_ = nullptr;

  GLuint program_ = 0;
  GLuint vao_ = 0;
  xe::ui::gl::CircularBuffer vertex_buffer_;
  xe::ui::gl::CircularBuffer index_buffer_;
};

}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_IMGUI_RENDERER_H_
