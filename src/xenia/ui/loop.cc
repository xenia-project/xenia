/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/loop.h"

#include "xenia/base/assert.h"
#include "xenia/base/threading.h"

namespace xe {
namespace ui {

Loop::Loop() = default;

Loop::~Loop() = default;

void Loop::PostSynchronous(std::function<void()> fn) {
  if (is_on_loop_thread()) {
    // Prevent deadlock if we are executing on ourselves.
    fn();
    return;
  }
  xe::threading::Fence fence;
  Post([&fn, &fence]() {
    fn();
    fence.Signal();
  });
  fence.Wait();
}

}  // namespace ui
}  // namespace xe
