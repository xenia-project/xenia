/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_SURFACE_ANDROID_H_
#define XENIA_UI_SURFACE_ANDROID_H_

#include <android/native_window.h>

#include "xenia/ui/surface.h"

namespace xe {
namespace ui {

class AndroidNativeWindowSurface : public Surface {
 public:
  explicit AndroidNativeWindowSurface(ANativeWindow* window)
      : window_(window) {}
  TypeIndex GetType() const override { return kTypeIndex_AndroidNativeWindow; }
  ANativeWindow* window() const { return window_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override;

 private:
  ANativeWindow* window_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_SURFACE_ANDROID_H_
