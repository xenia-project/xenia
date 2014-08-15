/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_STRING_H_
#define POLY_STRING_H_

#include <string>

#include <poly/config.h>

namespace poly {

std::string to_string(const std::wstring& source);
std::wstring to_wstring(const std::string& source);

}  // namespace poly

#endif  // POLY_STRING_H_
