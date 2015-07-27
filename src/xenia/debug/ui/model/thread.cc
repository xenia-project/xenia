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
  if (!entry_.thread_handle) {
    std::memcpy(&entry_, entry, sizeof(entry_));
  } else {
    std::memcpy(&temp_entry_, entry, sizeof(temp_entry_));
    system_->loop()->Post(
        [this]() { std::memcpy(&entry_, &temp_entry_, sizeof(temp_entry_)); });
  }
}

}  // namespace model
}  // namespace ui
}  // namespace debug
}  // namespace xe
