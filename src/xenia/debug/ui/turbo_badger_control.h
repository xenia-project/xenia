/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_TURBO_BADGER_CONTROL_H_
#define XENIA_DEBUG_UI_TURBO_BADGER_CONTROL_H_

#include <memory>

#include "xenia/ui/gl/wgl_control.h"
#include "xenia/ui/loop.h"

#include "third_party/turbobadger/src/tb/tb_core.h"
#include "third_party/turbobadger/src/tb/tb_widgets.h"

namespace xe {
namespace debug {
namespace ui {

class TurboBadgerControl : public xe::ui::gl::WGLControl {
 public:
  TurboBadgerControl(xe::ui::Loop* loop);
  ~TurboBadgerControl() override;

  static bool InitializeTurboBadger(tb::Renderer* renderer);
  static void ShutdownTurboBadger();

  tb::Renderer* renderer() const { return renderer_.get(); }
  tb::Widget* root_widget() const { return root_widget_.get(); }

 protected:
  using super = xe::ui::gl::WGLControl;

  bool Create() override;
  void Destroy() override;

  void OnLayout(xe::ui::UIEvent& e) override;
  void OnPaint(xe::ui::UIEvent& e) override;

  void OnGotFocus(xe::ui::UIEvent& e) override;
  void OnLostFocus(xe::ui::UIEvent& e) override;

  tb::ModifierKeys GetModifierKeys();

  void OnKeyPress(xe::ui::KeyEvent& e, bool is_down);
  bool CheckShortcutKey(xe::ui::KeyEvent& e, tb::SpecialKey special_key,
                        bool is_down);
  void OnKeyDown(xe::ui::KeyEvent& e) override;
  void OnKeyUp(xe::ui::KeyEvent& e) override;

  void OnMouseDown(xe::ui::MouseEvent& e) override;
  void OnMouseMove(xe::ui::MouseEvent& e) override;
  void OnMouseUp(xe::ui::MouseEvent& e) override;
  void OnMouseWheel(xe::ui::MouseEvent& e) override;

  std::unique_ptr<tb::Renderer> renderer_;
  std::unique_ptr<tb::Widget> root_widget_;

  uint32_t frame_count_ = 0;
  uint32_t fps_ = 0;
  uint64_t fps_update_time_ = 0;
  uint64_t fps_frame_count_ = 0;

  bool modifier_shift_pressed_ = false;
  bool modifier_cntrl_pressed_ = false;
  bool modifier_alt_pressed_ = false;
  bool modifier_super_pressed_ = false;
  uint64_t last_click_time_ = 0;
  int last_click_x_ = 0;
  int last_click_y_ = 0;
  int last_click_counter_ = 0;
};

}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_TURBO_BADGER_CONTROL_H_
