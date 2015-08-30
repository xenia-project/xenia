/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/threading.h"

namespace xe {
namespace threading {

thread_local uint32_t current_thread_id_ = UINT_MAX;

uint32_t current_thread_id() {
  return current_thread_id_ == UINT_MAX ? current_thread_system_id()
                                        : current_thread_id_;
}

void set_current_thread_id(uint32_t id) { current_thread_id_ = id; }

}  // namespace threading
}  // namespace xe
