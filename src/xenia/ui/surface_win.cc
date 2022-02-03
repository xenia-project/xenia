/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/surface_win.h"

#include <algorithm>
#include <memory>

#include "xenia/base/logging.h"

namespace xe {
namespace ui {

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_GAMES)
bool Win32HwndSurface::GetSizeImpl(uint32_t& width_out,
                                   uint32_t& height_out) const {
  RECT client_rect;
  if (!GetClientRect(hwnd(), &client_rect)) {
    return false;
  }
  width_out = uint32_t(std::max(client_rect.right - client_rect.left, LONG(0)));
  height_out =
      uint32_t(std::max(client_rect.bottom - client_rect.top, LONG(0)));
  return true;
}
#endif

}  // namespace ui
}  // namespace xe
