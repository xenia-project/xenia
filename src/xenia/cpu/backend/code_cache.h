/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BACKEND_CODE_CACHE_H_
#define XENIA_BACKEND_CODE_CACHE_H_

#include <string>

#include "xenia/cpu/symbol_info.h"

namespace xe {
namespace cpu {
namespace backend {

class CodeCache {
 public:
  CodeCache() = default;
  virtual ~CodeCache() = default;

  virtual std::wstring file_name() const = 0;
  virtual uint32_t base_address() const = 0;
  virtual uint32_t total_size() const = 0;

  // Finds a function based on the given host PC (that may be within a
  // function).
  virtual FunctionInfo* LookupFunction(uint64_t host_pc) = 0;

  // Finds platform-specific function unwind info for the given host PC.
  virtual void* LookupUnwindInfo(uint64_t host_pc) = 0;
};

}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_BACKEND_CODE_CACHE_H_
