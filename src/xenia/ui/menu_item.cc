/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/ui/menu_item.h>


using namespace xe;
using namespace xe::ui;


MenuItem::MenuItem(Window* window, MenuItem* parent_item) :
    window_(window), parent_item_(parent_item) {
}

MenuItem::~MenuItem() {
}
