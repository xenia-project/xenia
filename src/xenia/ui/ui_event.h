/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_UI_EVENT_H_
#define XENIA_UI_UI_EVENT_H_

#include <cstdint>
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
  Window* target_;
};

class UISetupEvent : public UIEvent {
 public:
  explicit UISetupEvent(Window* target = nullptr, bool is_initial_setup = false)
      : UIEvent(target), is_initial_setup_(is_initial_setup) {}

  bool is_initial_setup() const { return is_initial_setup_; }

 private:
  bool is_initial_setup_;
};

class MonitorUpdateEvent : public UISetupEvent {
 public:
  explicit MonitorUpdateEvent(Window* target,
                              bool old_monitor_potentially_disconnected,
                              bool is_initial_setup = false)
      : UISetupEvent(target, is_initial_setup),
        old_monitor_potentially_disconnected_(
            old_monitor_potentially_disconnected) {}

  bool old_monitor_potentially_disconnected() const {
    return old_monitor_potentially_disconnected_;
  }

 private:
  bool old_monitor_potentially_disconnected_;
};

class FileDropEvent : public UIEvent {
 public:
  explicit FileDropEvent(Window* target, std::filesystem::path filename)
      : UIEvent(target), filename_(std::move(filename)) {}
  ~FileDropEvent() override = default;

  const std::filesystem::path& filename() const { return filename_; }

 private:
  const std::filesystem::path filename_;
};

class KeyEvent : public UIEvent {
 public:
  explicit KeyEvent(Window* target, VirtualKey virtual_key, int repeat_count,
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
  // Matching Windows WHEEL_DELTA.
  static constexpr uint32_t kScrollPerDetent = 120;

  explicit MouseEvent(Window* target, Button button, int32_t x, int32_t y,
                      int32_t scroll_x = 0, int32_t scroll_y = 0)
      : UIEvent(target),
        button_(button),
        x_(x),
        y_(y),
        scroll_x_(scroll_x),
        scroll_y_(scroll_y) {}
  ~MouseEvent() override = default;

  bool is_handled() const { return handled_; }
  void set_handled(bool value) { handled_ = value; }

  Button button() const { return button_; }
  int32_t x() const { return x_; }
  int32_t y() const { return y_; }
  int32_t scroll_x() const { return scroll_x_; }
  int32_t scroll_y() const { return scroll_y_; }

 private:
  bool handled_ = false;
  Button button_;
  int32_t x_ = 0;
  int32_t y_ = 0;
  int32_t scroll_x_ = 0;
  // Positive is up (away from the user), negative is down (towards the user).
  int32_t scroll_y_ = 0;
};

class TouchEvent : public UIEvent {
 public:
  enum class Action {
    kDown,
    kUp,
    // Should be treated as an up event, but without performing the usual action
    // for releasing.
    kCancel,
    kMove,
  };

  // Can be used by event listeners as the value for when there's no current
  // pointer, for example.
  static constexpr uint32_t kPointerIDNone = UINT32_MAX;

  explicit TouchEvent(Window* target, uint32_t pointer_id, Action action,
                      float x, float y)
      : UIEvent(target),
        pointer_id_(pointer_id),
        action_(action),
        x_(x),
        y_(y) {}

  bool is_handled() const { return handled_; }
  void set_handled(bool value) { handled_ = value; }

  uint32_t pointer_id() { return pointer_id_; }
  Action action() const { return action_; }
  // Can be outside the boundaries of the surface.
  float x() const { return x_; }
  float y() const { return y_; }

 private:
  bool handled_ = false;
  uint32_t pointer_id_;
  Action action_;
  float x_;
  float y_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_UI_EVENT_H_
