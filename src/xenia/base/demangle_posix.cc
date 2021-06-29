/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/demangle.h"

#include <cxxabi.h>
#include <memory>

namespace xe {

std::string Demangle(const std::string& mangled_name) {
  std::size_t len = 0;
  int status = 0;
  std::unique_ptr<char, decltype(&std::free)> ptr(
      __cxxabiv1::__cxa_demangle(mangled_name.c_str(), nullptr, &len, &status),
      &std::free);
  return ptr ? std::string("class ") + ptr.get() : "";
}

}  // namespace xe
