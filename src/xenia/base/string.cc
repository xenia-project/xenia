/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/string.h"

// TODO(benvanik): when GCC finally gets codecvt, use that.
#if XE_PLATFORM_LINUX
#define NO_CODECVT 1
#else
#include <codecvt>
#endif  // XE_PLATFORM_LINUX

#include <cstring>
#include <locale>

namespace xe {

std::string to_string(const std::wstring& source) {
#if NO_CODECVT
  return std::string(source.begin(), source.end());
#else
  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
  return converter.to_bytes(source);
#endif  // XE_PLATFORM_LINUX
}

std::wstring to_wstring(const std::string& source) {
#if NO_CODECVT
  return std::wstring(source.begin(), source.end());
#else
  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
  return converter.from_bytes(source);
#endif  // XE_PLATFORM_LINUX
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
#if XE_PLATFORM_WIN32
  wchar_t buffer[kMaxPath];
  _wfullpath(buffer, path.c_str(), sizeof(buffer) / sizeof(wchar_t));
  return buffer;
#else
  char buffer[kMaxPath];
  realpath(xe::to_string(path).c_str(), buffer);
  return xe::to_wstring(buffer);
#endif  // XE_PLATFORM_WIN32
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

std::string join_paths(const std::string& left, const std::string& right,
                       char sep) {
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

std::string fix_path_separators(const std::string& source, char new_sep) {
  // Swap all separators to new_sep.
  char old_sep = new_sep == '\\' ? '/' : '\\';
  std::string::size_type pos = 0;
  std::string dest = source;
  while ((pos = source.find_first_of(old_sep, pos)) != std::string::npos) {
    dest[pos] = new_sep;
    ++pos;
  }
  // Replace redundant separators.
  pos = 0;
  while ((pos = dest.find_first_of(new_sep, pos)) != std::string::npos) {
    if (pos < dest.size() - 1) {
      if (dest[pos + 1] == new_sep) {
        dest.erase(pos + 1, 1);
      }
    }
    ++pos;
  }
  return dest;
}

std::string find_name_from_path(const std::string& path, char sep) {
  std::string name(path);
  if (!path.empty()) {
    std::string::size_type from(std::string::npos);
    if (path.back() == sep) {
      from = path.size() - 2;
    }
    auto pos(path.find_last_of(sep, from));
    if (pos != std::string::npos) {
      if (from == std::string::npos) {
        name = path.substr(pos + 1);
      } else {
        auto len(from - pos);
        name = path.substr(pos + 1, len);
      }
    }
  }
  return name;
}

std::wstring find_name_from_path(const std::wstring& path, wchar_t sep) {
  std::wstring name(path);
  if (!path.empty()) {
    std::wstring::size_type from(std::wstring::npos);
    if (path.back() == sep) {
      from = path.size() - 2;
    }
    auto pos(path.find_last_of(sep, from));
    if (pos != std::wstring::npos) {
      if (from == std::wstring::npos) {
        name = path.substr(pos + 1);
      } else {
        auto len(from - pos);
        name = path.substr(pos + 1, len);
      }
    }
  }
  return name;
}

std::string find_base_path(const std::string& path, char sep) {
  auto last_slash = path.find_last_of(sep);
  if (last_slash == std::string::npos) {
    return path;
  } else if (last_slash == path.length() - 1) {
    auto prev_slash = path.find_last_of(sep, last_slash - 1);
    if (prev_slash == std::string::npos) {
      return "";
    } else {
      return path.substr(0, prev_slash + 1);
    }
  } else {
    return path.substr(0, last_slash + 1);
  }
}

std::wstring find_base_path(const std::wstring& path, wchar_t sep) {
  auto last_slash = path.find_last_of(sep);
  if (last_slash == std::wstring::npos) {
    return path;
  } else if (last_slash == path.length() - 1) {
    auto prev_slash = path.find_last_of(sep, last_slash - 1);
    if (prev_slash == std::wstring::npos) {
      return L"";
    } else {
      return path.substr(0, prev_slash + 1);
    }
  } else {
    return path.substr(0, last_slash + 1);
  }
}

}  // namespace xe
