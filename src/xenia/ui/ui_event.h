/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_UI_EVENT_H_
#define XENIA_UI_UI_EVENT_H_

#include <filesystem>

#include "xenia/ui/virtual_key.h"

namespace xe {
namespace ui {

class Window;

class UIEvent {
 public:
  explicit UIEvent(Window* target = nullptr) : target_(target) {}
  virtual ~UIEvent() = default;

  Window* target() const { return target_; }

 private:
  Window* target_ = nullptr;
};

class FileDropEvent : public UIEvent {
 public:
  FileDropEvent(Window* target, std::filesystem::path filename)
      : UIEvent(target), filename_(std::move(filename)) {}
  ~FileDropEvent() override = default;

  const std::filesystem::path& filename() const { return filename_; }

 private:
  const std::filesystem::path filename_;
};

class KeyEvent : public UIEvent {
 public:
  KeyEvent(Window* target, VirtualKey virtual_key, int repeat_count,
           bool prev_state, bool modifier_shift_pressed,
           bool modifier_ctrl_pressed, bool modifier_alt_pressed,
           bool modifier_super_pressed)
      : UIEvent(target),
        virtual_key_(virtual_key),
        repeat_count_(repeat_count),
        prev_state_(prev_state),
        modifier_shift_pressed_(modifier_shift_pressed),
        modifier_ctrl_pressed_(modifier_ctrl_pressed),
        modifier_alt_pressed_(modifier_alt_pressed),
        modifier_super_pressed_(modifier_super_pressed) {}
  ~KeyEvent() override = default;

  bool is_handled() const { return handled_; }
  void set_handled(bool value) { handled_ = value; }

  VirtualKey virtual_key() const { return virtual_key_; }

  int repeat_count() const { return repeat_count_; }
  bool prev_state() const { return prev_state_; }

  bool is_shift_pressed() const { return modifier_shift_pressed_; }
  bool is_ctrl_pressed() const { return modifier_ctrl_pressed_; }
  bool is_alt_pressed() const { return modifier_alt_pressed_; }
  bool is_super_pressed() const { return modifier_super_pressed_; }

 private:
  bool handled_ = false;
  VirtualKey virtual_key_ = VirtualKey::kNone;

  int repeat_count_ = 0;
  bool prev_state_ = false;  // Key previously down(true) or up(false)

  bool modifier_shift_pressed_ = false;
  bool modifier_ctrl_pressed_ = false;
  bool modifier_alt_pressed_ = false;
  bool modifier_super_pressed_ = false;
};

class MouseEvent : public UIEvent {
 public:
  enum class Button {
    kNone = 0,
    kLeft,
    kRight,
    kMiddle,
    kX1,
    kX2,
  };

 public:
  MouseEvent(Window* target, Button button, int32_t x, int32_t y,
             int32_t dx = 0, int32_t dy = 0)
      : UIEvent(target), button_(button), x_(x), y_(y), dx_(dx), dy_(dy) {}
  ~MouseEvent() override = default;

  bool is_handled() const { return handled_; }
  void set_handled(bool value) { handled_ = value; }

  Button button() const { return button_; }
  int32_t x() const { return x_; }
  int32_t y() const { return y_; }
  int32_t dx() const { return dx_; }
  int32_t dy() const { return dy_; }

 private:
  bool handled_ = false;
  Button button_;
  int32_t x_ = 0;
  int32_t y_ = 0;
  int32_t dx_ = 0;
  int32_t dy_ = 0;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_UI_EVENT_H_
