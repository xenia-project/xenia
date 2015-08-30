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

#include <cstdio>
#include <string>
#include <vector>

#include "xenia/base/platform.h"

namespace xe {

std::string to_string(const std::wstring& source);
std::wstring to_wstring(const std::string& source);

// find_first_of string, case insensitive.
std::string::size_type find_first_of_case(const std::string& target,
                                          const std::string& search);

// Converts the given path to an absolute path based on cwd.
std::wstring to_absolute_path(const std::wstring& path);

// Splits the given path on any valid path separator and returns all parts.
std::vector<std::string> split_path(const std::string& path);

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

}  // namespace xe

#endif  // XENIA_BASE_STRING_H_
