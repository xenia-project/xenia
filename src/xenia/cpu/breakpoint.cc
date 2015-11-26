/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/breakpoint.h"

#include "xenia/cpu/backend/backend.h"

namespace xe {
namespace cpu {

Breakpoint::Breakpoint(Processor* processor, uint32_t address,
                       std::function<void(uint32_t, uint64_t)> hit_callback)
    : processor_(processor), address_(address), hit_callback_(hit_callback) {}
Breakpoint::~Breakpoint() { assert_false(installed_); }

bool Breakpoint::Install() {
  assert_false(installed_);

  installed_ = processor_->InstallBreakpoint(this);
  return installed_;
}

bool Breakpoint::Uninstall() {
  assert_true(installed_);

  installed_ = !processor_->UninstallBreakpoint(this);
  return !installed_;
}

}  // namespace cpu
}  // namespace xe