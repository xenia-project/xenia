/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WIN32_WIN32_LOOP_H_
#define XENIA_UI_WIN32_WIN32_LOOP_H_

#include <xenia/common.h>

#include <xenia/ui/menu_item.h>

namespace xe {
namespace ui {
namespace win32 {

class Win32Loop {
 public:
  Win32Loop();
  ~Win32Loop();

  bool Run();
  void Quit();

 private:
};

}  // namespace win32
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WIN32_WIN32_LOOP_H_
