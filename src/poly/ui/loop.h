/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_UI_LOOP_H_
#define POLY_UI_LOOP_H_

#include <functional>

namespace poly {
namespace ui {

class Loop {
 public:
  Loop() = default;
  virtual ~Loop() = default;

  virtual void Post(std::function<void()> fn) = 0;

  virtual void Quit() = 0;
  virtual void AwaitQuit() = 0;
};

}  // namespace ui
}  // namespace poly

#endif  // POLY_UI_LOOP_H_
