/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IMGUI_DRAWER_H_
#define XENIA_UI_IMGUI_DRAWER_H_

#include <memory>
#include <vector>

#include "xenia/ui/immediate_drawer.h"

struct ImDrawData;

namespace xe {
namespace ui {

class GraphicsContext;
class Window;

class ImGuiDrawer {
 public:
  ImGuiDrawer(Window* window);
  ~ImGuiDrawer();

 protected:
  void Initialize();
  void SetupFont();

  void RenderDrawLists(ImDrawData* data);

  static ImGuiDrawer* global_drawer_;

  Window* window_ = nullptr;
  GraphicsContext* graphics_context_ = nullptr;

  std::vector<ImmediateVertex> vertices_;
  std::vector<uint16_t> indices_;

  std::unique_ptr<ImmediateTexture> font_texture_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_IMGUI_DRAWER_H_
