/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_STRING_H_
#define XENIA_BASE_STRING_H_

#include <string>

#include "xenia/base/utf8.h"

namespace xe {

int xe_strcasecmp(const char* string1, const char* string2);
int xe_strncasecmp(const char* string1, const char* string2, size_t count);
char* xe_strdup(const char* source);

std::string to_utf8(const std::u16string_view source);
std::u16string to_utf16(const std::string_view source);
std::string utf8_to_win1252(const std::string_view source);
std::string win1252_to_utf8(const std::string_view source);

}  // namespace xe

#endif  // XENIA_BASE_STRING_H_
