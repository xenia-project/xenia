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
#include "xenia/ui/window_listener.h"

struct ImDrawData;
struct ImGuiContext;
struct ImGuiIO;

namespace xe {
namespace ui {

class GraphicsContext;
class Window;

class ImGuiDrawer : public WindowListener {
 public:
  ImGuiDrawer(Window* window);
  ~ImGuiDrawer();

  void SetupDefaultInput() {}

  ImGuiIO& GetIO();
  void RenderDrawLists();

 protected:
  void Initialize();
  void SetupFont();

  void RenderDrawLists(ImDrawData* data);

  void OnKeyDown(KeyEvent* e) override;
  void OnKeyUp(KeyEvent* e) override;
  void OnKeyChar(KeyEvent* e) override;
  void OnMouseDown(MouseEvent* e) override;
  void OnMouseMove(MouseEvent* e) override;
  void OnMouseUp(MouseEvent* e) override;
  void OnMouseWheel(MouseEvent* e) override;

  static ImGuiDrawer* current_drawer_;

  Window* window_ = nullptr;
  GraphicsContext* graphics_context_ = nullptr;

  ImGuiContext* internal_state_ = nullptr;
  std::unique_ptr<ImmediateTexture> font_texture_;

 private:
  void OnKey(KeyEvent* e, bool is_down);
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_IMGUI_DRAWER_H_
