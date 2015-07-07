/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_ELEMENTAL_CONTROL_H_
#define XENIA_UI_ELEMENTAL_CONTROL_H_

#include <memory>

#include "el/element.h"
#include "el/graphics/renderer.h"
#include "xenia/ui/control.h"
#include "xenia/ui/loop.h"
#include "xenia/ui/platform.h"

namespace xe {
namespace ui {

class ElementalControl : public PlatformControl {
 public:
  ElementalControl(Loop* loop, uint32_t flags);
  ~ElementalControl() override;

  el::graphics::Renderer* renderer() const { return renderer_.get(); }
  el::Element* root_element() const { return root_element_.get(); }

 protected:
  using super = PlatformControl;

  bool InitializeElemental(el::graphics::Renderer* renderer);
  virtual std::unique_ptr<el::graphics::Renderer> CreateRenderer() = 0;

  bool Create() override;
  void Destroy() override;

  void OnLayout(UIEvent& e) override;
  void OnPaint(UIEvent& e) override;

  void OnGotFocus(UIEvent& e) override;
  void OnLostFocus(UIEvent& e) override;

  el::ModifierKeys GetModifierKeys();

  void OnKeyPress(KeyEvent& e, bool is_down);
  bool CheckShortcutKey(KeyEvent& e, el::SpecialKey special_key, bool is_down);
  void OnKeyDown(KeyEvent& e) override;
  void OnKeyUp(KeyEvent& e) override;

  void OnMouseDown(MouseEvent& e) override;
  void OnMouseMove(MouseEvent& e) override;
  void OnMouseUp(MouseEvent& e) override;
  void OnMouseWheel(MouseEvent& e) override;

  std::unique_ptr<el::graphics::Renderer> renderer_;
  std::unique_ptr<el::Element> root_element_;

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
}  // namespace xe

#endif  // XENIA_UI_ELEMENTAL_CONTROL_H_
