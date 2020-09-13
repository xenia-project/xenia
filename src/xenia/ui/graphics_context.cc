/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/graphics_context.h"

#include <cstdlib>

#include "xenia/base/cvar.h"
#include "xenia/ui/graphics_provider.h"

DEFINE_bool(random_clear_color, false, "Randomize window clear color.", "UI");

namespace xe {
namespace ui {

GraphicsContext::GraphicsContext(GraphicsProvider* provider,
                                 Window* target_window)
    : provider_(provider), target_window_(target_window) {}

GraphicsContext::~GraphicsContext() = default;

bool GraphicsContext::is_current() { return true; }

bool GraphicsContext::MakeCurrent() { return true; }

void GraphicsContext::ClearCurrent() {}

void GraphicsContext::GetClearColor(float* rgba) {
  if (cvars::random_clear_color) {
    rgba[0] = rand() / float(RAND_MAX);  // NOLINT(runtime/threadsafe_fn)
    rgba[1] = 1.0f;
    rgba[2] = 0.0f;
  } else {
    rgba[0] = 0.0f;
    rgba[1] = 0.0f;
    rgba[2] = 0.0f;
  }
  rgba[3] = 1.0f;
}

}  // namespace ui
}  // namespace xe
