/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_UI_DRAWER_H_
#define XENIA_UI_UI_DRAWER_H_

#include <cstdint>

namespace xe {
namespace ui {

class UIDrawContext;

class UIDrawer {
 public:
  // No preparation or finishing callbacks because drawers may register or
  // unregister each other, so between the loops the list may be different.

  // The draw function may register or unregister drawers (and depending on the
  // Z order the changes may or may not effect immediately). However, they must
  // not perform any lifetime management of the presenter and of its connection
  // to the surface. Ideally drawing should not be changing any state at all,
  // however, in Dear ImGui, input is handled directly during drawing - any
  // quitting or resizing, if done in the UI, must be deferred via something
  // like WindowedAppContext::CallInUIThreadDeferred.
  virtual void Draw(UIDrawContext& context) = 0;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_UI_DRAWER_H_
