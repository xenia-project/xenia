/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_DEMANGLE_H_
#define XENIA_BASE_DEMANGLE_H_

#include <string>

namespace xe {
std::string Demangle(const std::string& mangled_name);
}

#endif  // XENIA_BASE_DEMANGLE_H_
