/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/graphics_context.h"

#include "xenia/ui/graphics_provider.h"

namespace xe {
namespace ui {

GraphicsContext::GraphicsContext(GraphicsProvider* provider,
                                 Window* target_window)
    : provider_(provider), target_window_(target_window) {}

GraphicsContext::~GraphicsContext() = default;

}  // namespace ui
}  // namespace xe
