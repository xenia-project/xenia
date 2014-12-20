/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_UI_UI_EVENT_H_
#define POLY_UI_UI_EVENT_H_

namespace poly {
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
      : UIEvent(control), key_code_(key_code) {}
  ~KeyEvent() override = default;

  int key_code() const { return key_code_; }

 private:
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
      : UIEvent(control), button_(button), x_(x), y_(y), dx_(dx), dy_(dy) {}
  ~MouseEvent() override = default;

  Button button() const { return button_; }
  int32_t x() const { return x_; }
  int32_t y() const { return y_; }
  int32_t dx() const { return dx_; }
  int32_t dy() const { return dy_; }

 private:
  Button button_;
  int32_t x_;
  int32_t y_;
  int32_t dx_;
  int32_t dy_;
};

}  // namespace ui
}  // namespace poly

#endif  // POLY_UI_UI_EVENT_H_
