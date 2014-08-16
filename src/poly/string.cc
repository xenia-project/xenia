/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/string.h>

#include <codecvt>

namespace poly {

std::string to_string(const std::wstring& source) {
  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  return converter.to_bytes(source);
}

std::wstring to_wstring(const std::string& source) {
  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  return converter.from_bytes(source);
}

std::string::size_type find_first_of_case(const std::string& target,
                                          const std::string& search) {
  const char* str = target.c_str();
  while (*str) {
    if (!strncasecmp(str, search.c_str(), search.size())) {
      break;
    }
    str++;
  }
  if (*str) {
    return str - target.c_str();
  } else {
    return std::string::npos;
  }
}

}  // namespace poly
