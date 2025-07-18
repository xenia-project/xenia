/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/windowed_app.h"

namespace xe {
namespace ui {

#if XE_UI_WINDOWED_APPS_IN_LIBRARY
// A zero-initialized pointer to remove dependence on the initialization order
// of the map relatively to the app creator proxies.
std::unordered_map<std::string, WindowedApp::Creator>* WindowedApp::creators_;
#endif  // XE_UI_WINDOWED_APPS_IN_LIBRARY

}  // namespace ui
}  // namespace xe
