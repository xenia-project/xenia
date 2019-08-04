/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_STRING_H_
#define XENIA_BASE_STRING_H_

#include <cstdarg>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "xenia/base/platform.h"

namespace xe {

std::string to_string(const std::wstring& source);
std::wstring to_wstring(const std::string& source);

std::string format_string(const char* format, va_list args);
inline std::string format_string(const char* format, ...) {
  va_list va;
  va_start(va, format);
  auto result = format_string(format, va);
  va_end(va);
  return result;
}
std::wstring format_string(const wchar_t* format, va_list args);
inline std::wstring format_string(const wchar_t* format, ...) {
  va_list va;
  va_start(va, format);
  auto result = format_string(format, va);
  va_end(va);
  return result;
}

// Splits the given string on any delimiters and returns all parts.
std::vector<std::string> split_string(const std::string& path,
                                      const std::string& delimiters);

std::vector<std::wstring> split_string(const std::wstring& path,
                                       const std::wstring& delimiters);

// find_first_of string, case insensitive.
std::string::size_type find_first_of_case(const std::string& target,
                                          const std::string& search);

// Converts the given path to an absolute path based on cwd.
std::wstring to_absolute_path(const std::wstring& path);

// Splits the given path on any valid path separator and returns all parts.
std::vector<std::string> split_path(const std::string& path);
std::vector<std::wstring> split_path(const std::wstring& path);

// Joins two path segments with the given separator.
std::string join_paths(const std::string& left, const std::string& right,
                       char sep = xe::kPathSeparator);
std::wstring join_paths(const std::wstring& left, const std::wstring& right,
                        wchar_t sep = xe::kPathSeparator);

// Replaces all path separators with the given value and removes redundant
// separators.
std::wstring fix_path_separators(const std::wstring& source,
                                 wchar_t new_sep = xe::kPathSeparator);
std::string fix_path_separators(const std::string& source,
                                char new_sep = xe::kPathSeparator);

// Find the top directory name or filename from a path.
std::string find_name_from_path(const std::string& path,
                                char sep = xe::kPathSeparator);
std::wstring find_name_from_path(const std::wstring& path,
                                 wchar_t sep = xe::kPathSeparator);

// Get parent path of the given directory or filename.
std::string find_base_path(const std::string& path,
                           char sep = xe::kPathSeparator);
std::wstring find_base_path(const std::wstring& path,
                            wchar_t sep = xe::kPathSeparator);

// Tests a match against a case-insensitive fuzzy filter.
// Returns the score of the match or 0 if none.
int fuzzy_match(const std::string& pattern, const char* value);

// Applies a case-insensitive fuzzy filter to the given entries and ranks
// results.
// Entries is a list of pointers to opaque structs, each of which contains a
// char* string at the given offset.
// Returns an unsorted list of {original index, score}.
std::vector<std::pair<size_t, int>> fuzzy_filter(const std::string& pattern,
                                                 const void* const* entries,
                                                 size_t entry_count,
                                                 size_t string_offset);
template <typename T>
std::vector<std::pair<size_t, int>> fuzzy_filter(const std::string& pattern,
                                                 const std::vector<T>& entries,
                                                 size_t string_offset) {
  return fuzzy_filter(pattern, reinterpret_cast<void* const*>(entries.data()),
                      entries.size(), string_offset);
}

}  // namespace xe

#endif  // XENIA_BASE_STRING_H_
