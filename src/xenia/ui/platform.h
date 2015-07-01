#pragma once
/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2014 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#ifndef XENIA_UI_PLATFORM_H_
#define XENIA_UI_PLATFORM_H_

// TODO(benvanik): only on windows.
#include "xenia/ui/win32/win32_file_picker.h"
#include "xenia/ui/win32/win32_loop.h"
#include "xenia/ui/win32/win32_menu_item.h"
#include "xenia/ui/win32/win32_window.h"

namespace xe {
namespace ui {

using PlatformFilePicker = xe::ui::win32::Win32FilePicker;
using PlatformLoop = xe::ui::win32::Win32Loop;
using PlatformMenu = xe::ui::win32::Win32MenuItem;
using PlatformWindow = xe::ui::win32::Win32Window;

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_PLATFORM_H_
