/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xdb/postmortem_debug_target.h>

namespace xdb {

bool PostmortemDebugTarget::LoadTrace(const std::wstring& path) {
  // TODO(benvanik): memory map trace.
  return true;
}

bool PostmortemDebugTarget::LoadContent(const std::wstring& path) {
  // If no path is provided attempt to infer from the trace.
  if (path.empty()) {
    // TODO(benvanik): find process info block and read source path.
  }
  // TODO(benvanik): initialize filesystem and load iso/stfs/etc to get at xex.
  return true;
}

bool PostmortemDebugTarget::Prepare() {
  std::atomic<bool> cancelled(false);
  return Prepare(cancelled);
}

bool PostmortemDebugTarget::Prepare(std::atomic<bool>& cancelled) {
  // TODO(benvanik): scan file, build indicies, etc.
  return true;
}

}  // namespace xdb
