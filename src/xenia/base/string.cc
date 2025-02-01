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
#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

#if !XE_PLATFORM_WIN32
#include <strings.h>
#endif  // !XE_PLATFORM_WIN32

#define UTF_CPP_CPLUSPLUS 202002L
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

std::string utf8_to_win1252(const std::string_view source) {
#if XE_PLATFORM_WIN32
  std::string input_str(source);
  int srclen = static_cast<int>(input_str.size());

  int wlen =
      MultiByteToWideChar(CP_UTF8, 0, input_str.c_str(), srclen, NULL, 0);
  if (!wlen) {
    return "";
  }
  std::vector<WCHAR> wbuf(wlen);
  int result = MultiByteToWideChar(CP_UTF8, 0, input_str.c_str(), srclen,
                                   wbuf.data(), wlen);
  if (!result) {
    return "";
  }

  int len = WideCharToMultiByte(1252, 0, wbuf.data(), wlen, NULL, 0, "_", NULL);
  if (!len) {
    return "";
  }
  std::vector<CHAR> buf(len);
  result = WideCharToMultiByte(1252, 0, wbuf.data(), wlen, buf.data(), len, "_",
                               NULL);
  if (!result) {
    return "";
  }

  std::string output_str(buf.begin(), buf.end());
  return output_str;
#else
  // TODO: Use iconv on POSIX.
  std::string output_str(source);
  return output_str;
#endif
}

std::string win1252_to_utf8(const std::string_view source) {
#if XE_PLATFORM_WIN32
  std::string input_str(source);
  int srclen = static_cast<int>(input_str.size());

  int wlen = MultiByteToWideChar(1252, 0, input_str.c_str(), srclen, NULL, 0);
  if (!wlen) {
    return "";
  }
  std::vector<WCHAR> wbuf(wlen);
  int result = MultiByteToWideChar(1252, 0, input_str.c_str(), srclen,
                                   wbuf.data(), wlen);
  if (!result) {
    return "";
  }

  int len =
      WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, NULL, 0, "_", NULL);
  if (!len) {
    return "";
  }
  std::vector<CHAR> buf(len);
  result = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, buf.data(), len,
                               "_", NULL);
  if (!result) {
    return "";
  }

  std::string output_str(buf.begin(), buf.end());
  return output_str;
#else
  // TODO: Use iconv on POSIX.
  std::string output_str(source);
  return output_str;
#endif
}

}  // namespace xe
