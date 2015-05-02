/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_UI_EVENT_H_
#define XENIA_UI_UI_EVENT_H_

namespace xe {
namespace ui {

class Control;

class UIEvent {
 public:
  UIEvent(Control* control = nullptr) : control_(control) {}
  virtual ~UIEvent() = default;

  Control* control() const { return control_; }

 private:
  Control* control_;
};

class KeyEvent : public UIEvent {
 public:
  KeyEvent(Control* control, int key_code)
      : UIEvent(control), handled_(false), key_code_(key_code) {}
  ~KeyEvent() override = default;

  bool is_handled() const { return handled_; }
  void set_handled(bool value) { handled_ = value; }

  int key_code() const { return key_code_; }

 private:
  bool handled_;
  int key_code_;
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
  MouseEvent(Control* control, Button button, int32_t x, int32_t y,
             int32_t dx = 0, int32_t dy = 0)
      : UIEvent(control),
        handled_(false),
        button_(button),
        x_(x),
        y_(y),
        dx_(dx),
        dy_(dy) {}
  ~MouseEvent() override = default;

  bool is_handled() const { return handled_; }
  void set_handled(bool value) { handled_ = value; }

  Button button() const { return button_; }
  int32_t x() const { return x_; }
  int32_t y() const { return y_; }
  int32_t dx() const { return dx_; }
  int32_t dy() const { return dy_; }

 private:
  bool handled_;
  Button button_;
  int32_t x_;
  int32_t y_;
  int32_t dx_;
  int32_t dy_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_UI_EVENT_H_
