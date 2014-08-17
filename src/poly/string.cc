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

std::wstring to_absolute_path(const std::wstring& path) {
#if XE_LIKE_WIN32
  wchar_t buffer[poly::max_path];
  _wfullpath(buffer, path.c_str(), sizeof(buffer) / sizeof(wchar_t));
  return buffer;
#else
  char buffer[poly::max_path];
  realpath(poly::to_string(path).c_str(), buffer);
  return poly::to_wstring(buffer);
#endif  // XE_LIKE_WIN32
}

std::vector<std::string> split_path(const std::string& path) {
  std::vector<std::string> parts;
  size_t n = 0;
  size_t last = 0;
  while ((n = path.find_first_of("\\/", last)) != path.npos) {
    if (last != n) {
      parts.push_back(path.substr(last, n - last));
    }
    last = n + 1;
  }
  if (last != path.size()) {
    parts.push_back(path.substr(last));
  }
  return parts;
}

std::wstring join_paths(const std::wstring& left, const std::wstring& right,
                        wchar_t sep) {
  if (!left.size()) {
    return right;
  } else if (!right.size()) {
    return left;
  }
  if (left[left.size() - 1] == sep) {
    return left + right;
  } else {
    return left + sep + right;
  }
}

std::wstring fix_path_separators(const std::wstring& source, wchar_t new_sep) {
  // Swap all separators to new_sep.
  wchar_t old_sep = new_sep == '\\' ? '/' : '\\';
  std::wstring::size_type pos = 0;
  std::wstring dest = source;
  while ((pos = source.find_first_of(old_sep, pos)) != std::wstring::npos) {
    dest[pos] = new_sep;
    ++pos;
  }
  // Replace redundant separators.
  pos = 0;
  while ((pos = dest.find_first_of(new_sep, pos)) != std::wstring::npos) {
    if (pos < dest.size() - 1) {
      if (dest[pos + 1] == new_sep) {
        dest.erase(pos + 1, 1);
      }
    }
    ++pos;
  }
  return dest;
}

}  // namespace poly
