/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_CONTEXT_INFO_H_
#define ALLOY_FRONTEND_CONTEXT_INFO_H_

#include <alloy/core.h>

namespace alloy {
namespace frontend {

class ContextInfo {
 public:
  ContextInfo(size_t size, uintptr_t thread_state_offset);
  ~ContextInfo();

  size_t size() const { return size_; }

  uintptr_t thread_state_offset() const { return thread_state_offset_; }

 private:
  size_t size_;
  uintptr_t thread_state_offset_;
};

}  // namespace frontend
}  // namespace alloy

#endif  // ALLOY_FRONTEND_CONTEXT_INFO_H_
