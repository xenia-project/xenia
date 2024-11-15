/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/string.h"

#include <string.h>
#include <algorithm>
#include <locale>

#include "xenia/base/platform.h"

#if !XE_PLATFORM_WIN32
#include <strings.h>
#endif  // !XE_PLATFORM_WIN32

#define UTF_CPP_CPLUSPLUS 201703L
#include "third_party/utfcpp/source/utf8.h"

namespace utfcpp = utf8;

namespace xe {

int xe_strcasecmp(const char* string1, const char* string2) {
#if XE_PLATFORM_WIN32
  return _stricmp(string1, string2);
#else
  return strcasecmp(string1, string2);
#endif  // XE_PLATFORM_WIN32
}

int xe_strncasecmp(const char* string1, const char* string2, size_t count) {
#if XE_PLATFORM_WIN32
  return _strnicmp(string1, string2, count);
#else
  return strncasecmp(string1, string2, count);
#endif  // XE_PLATFORM_WIN32
}

#ifdef __APPLE__
char* strdup (const char* s)
{
  size_t slen = strlen(s);
    char* result = (char*) malloc(slen + 1);
  if(result == NULL)
  {
    return NULL;
  }

  memcpy(result, s, slen+1);
  return result;
}
#endif

char* xe_strdup(const char* source) {
#if XE_PLATFORM_WIN32
  return _strdup(source);
#else
  return strdup(source);
#endif  // XE_PLATFORM_WIN32
}

std::string to_utf8(const std::u16string_view source) {
  return utfcpp::utf16to8(source);
}

std::u16string to_utf16(const std::string_view source) {
  return utfcpp::utf8to16(source);
}

}  // namespace xe
