/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/graphics_context.h"

namespace xe {
namespace ui {

GraphicsContext::GraphicsContext(Window* target_window)
    : target_window_(target_window) {}

GraphicsContext::~GraphicsContext() = default;

}  // namespace ui
}  // namespace xe
