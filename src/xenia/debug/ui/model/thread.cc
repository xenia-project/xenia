/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/ui/model/thread.h"

#include "xenia/debug/ui/model/system.h"

namespace xe {
namespace debug {
namespace ui {
namespace model {

std::string Thread::to_string() {
  std::string value = entry_.name;
  if (is_host_thread()) {
    value += " (host)";
  }
  return value;
}

void Thread::Update(const proto::ThreadListEntry* entry) {
  std::memcpy(&entry_, entry, sizeof(entry_));
}

void Thread::UpdateCallStack(
    std::vector<const proto::ThreadCallStackFrame*> frames) {
  call_stack_.resize(frames.size());
  for (size_t i = 0; i < frames.size(); ++i) {
    std::memcpy(call_stack_.data() + i, frames[i], sizeof(Frame));
  }
}

}  // namespace model
}  // namespace ui
}  // namespace debug
}  // namespace xe
