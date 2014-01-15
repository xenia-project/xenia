/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/ui/win32/win32_menu_item.h>


using namespace xe;
using namespace xe::ui;
using namespace xe::ui::win32;


Win32MenuItem::Win32MenuItem(Window* window, MenuItem* parent_item) :
    MenuItem(window, parent_item) {
}

Win32MenuItem::~Win32MenuItem() {
}
