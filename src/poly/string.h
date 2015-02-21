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

#include <cstdio>
#include <string>
#include <vector>

#include "poly/platform.h"

#if XE_LIKE_WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define snprintf _snprintf
#endif  // XE_LIKE_WIN32

namespace poly {

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
std::wstring join_paths(const std::wstring& left, const std::wstring& right,
                        wchar_t sep = poly::path_separator);

// Replaces all path separators with the given value and removes redundant
// separators.
std::wstring fix_path_separators(const std::wstring& source,
                                 wchar_t new_sep = poly::path_separator);
std::string fix_path_separators(const std::string& source,
                                char new_sep = poly::path_separator);

// Find the top directory name or filename from a path
std::string find_name_from_path(const std::string& path);
std::wstring find_name_from_path(const std::wstring& path);

}  // namespace poly

#endif  // POLY_STRING_H_
