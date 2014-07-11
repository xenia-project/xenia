/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/context_info.h>

namespace alloy {
namespace frontend {

ContextInfo::ContextInfo(size_t size, uintptr_t thread_state_offset)
    : size_(size), thread_state_offset_(thread_state_offset) {}

ContextInfo::~ContextInfo() {}

}  // namespace frontend
}  // namespace alloy
