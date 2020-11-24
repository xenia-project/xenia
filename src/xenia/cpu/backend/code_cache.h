/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_CODE_CACHE_H_
#define XENIA_CPU_BACKEND_CODE_CACHE_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "xenia/cpu/function.h"

namespace xe {
namespace cpu {
namespace backend {

class CodeCache {
 public:
  CodeCache() = default;
  virtual ~CodeCache() = default;

  virtual const std::filesystem::path& file_name() const = 0;
  virtual uintptr_t execute_base_address() const = 0;
  virtual size_t total_size() const = 0;

  // Finds a function based on the given host PC (that may be within a
  // function).
  virtual GuestFunction* LookupFunction(uint64_t host_pc) = 0;

  // Finds platform-specific function unwind info for the given host PC.
  virtual void* LookupUnwindInfo(uint64_t host_pc) = 0;
};

}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_CODE_CACHE_H_
