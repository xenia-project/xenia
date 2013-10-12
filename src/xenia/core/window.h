/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_WINDOW_H_
#define XENIA_CORE_WINDOW_H_

#include <xenia/common.h>
#include <xenia/core/pal.h>
#include <xenia/core/ref.h>
#include <xenia/core/run_loop.h>


namespace xe {
namespace core {


class Window {
public:
  Window(xe_run_loop_ref run_loop);
  virtual ~Window();

  virtual void set_title(const xechar_t* title) = 0;

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  virtual void Resize(uint32_t width, uint32_t height) = 0;

  virtual void ShowError(const xechar_t* message) = 0;

protected:
  virtual void OnResize(uint32_t width, uint32_t height) {}
  virtual void OnClose() {}

  xe_run_loop_ref run_loop_;

  uint32_t  width_;
  uint32_t  height_;
};


}  // namespace core
}  // namespace xe


#endif  // XENIA_CORE_WINDOW_H_
