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

#include <xenia/core.h>


namespace xe {
namespace ui {

class App;
class Window;


class UIEvent {
public:
  UIEvent(Window* window = NULL) :
      window_(window) {}
  virtual ~UIEvent() {}

  Window* window() const { return window_; }

private:
  Window*   window_;
};


class MouseEvent : public UIEvent {
public:
  enum Button {
    MOUSE_BUTTON_NONE = 0,
    MOUSE_BUTTON_LEFT,
    MOUSE_BUTTON_RIGHT,
    MOUSE_BUTTON_MIDDLE,
    MOUSE_BUTTON_X1,
    MOUSE_BUTTON_X2,
  };

public:
  MouseEvent(Window* window,
             Button button, int32_t x, int32_t y,
             int32_t dx = 0, int32_t dy = 0) :
      button_(button), x_(x), y_(y), dx_(dx), dy_(dy),
      UIEvent(window) {}
  virtual ~MouseEvent() {}

  Button button() const { return button_; }
  int32_t x() const { return x_; }
  int32_t y() const { return y_; }
  int32_t dx() const { return dx_; }
  int32_t dy() const { return dy_; }

private:
  Button  button_;
  int32_t x_;
  int32_t y_;
  int32_t dx_;
  int32_t dy_;
};


}  // namespace ui
}  // namespace xe


#endif  // XENIA_UI_UI_EVENT_H_
