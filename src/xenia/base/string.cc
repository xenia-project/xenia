/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/string.h"
#include "xenia/base/logging.h"

#include <algorithm>
#include <locale>

#define UTF_CPP_CPLUSPLUS 201703L
#include "third_party/utfcpp/source/utf8.h"

namespace utfcpp = utf8;

namespace xe {

std::string to_utf8(const std::u16string_view source) {
  try {
    return utfcpp::utf16to8(source);
  } catch (const utfcpp::invalid_utf16& e) {
    XELOGW("Invalid to_utf8 conversion: {} - {:04X}", e.what(), e.utf16_word());
    return std::string();
  }
}

std::u16string to_utf16(const std::string_view source) {
  return utfcpp::utf8to16(utfcpp::replace_invalid(source));
}

}  // namespace xe
