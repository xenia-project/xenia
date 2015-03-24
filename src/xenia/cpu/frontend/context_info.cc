/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/frontend/context_info.h"

namespace xe {
namespace cpu {
namespace frontend {

ContextInfo::ContextInfo(size_t size, uintptr_t thread_state_offset,
                         uintptr_t thread_id_offset)
    : size_(size),
      thread_state_offset_(thread_state_offset),
      thread_id_offset_(thread_id_offset) {}

ContextInfo::~ContextInfo() {}

}  // namespace frontend
}  // namespace cpu
}  // namespace xe
