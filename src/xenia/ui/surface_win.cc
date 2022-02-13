/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/surface_win.h"

namespace xe {
namespace ui {

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_GAMES)
bool Win32HwndSurface::GetSizeImpl(uint32_t& width_out,
                                   uint32_t& height_out) const {
  RECT client_rect;
  if (!GetClientRect(hwnd(), &client_rect)) {
    return false;
  }
  // GetClientRect returns a rectangle with 0 origin.
  width_out = uint32_t(client_rect.right);
  height_out = uint32_t(client_rect.bottom);
  return true;
}
#endif

}  // namespace ui
}  // namespace xe
