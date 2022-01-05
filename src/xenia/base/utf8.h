/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_UTF8_H_
#define XENIA_BASE_UTF8_H_

#include <string>
#include <vector>

#include "xenia/base/platform.h"

namespace xe::utf8 {

size_t count(const std::string_view view);

std::string lower_ascii(const std::string_view view);
std::string upper_ascii(const std::string_view view);

size_t hash_fnv1a(const std::string_view view);
size_t hash_fnv1a_case(const std::string_view view);

// Splits the given haystack on any delimiters (needles) and returns all parts.
std::vector<std::string_view> split(const std::string_view haystack,
                                    const std::string_view needles,
                                    bool remove_empty = false);

bool equal_z(const std::string_view left, const std::string_view right);

bool equal_case(const std::string_view left, const std::string_view right);

bool equal_case_z(const std::string_view left, const std::string_view right);

std::string_view::size_type find_any_of(const std::string_view haystack,
                                        const std::string_view needles);

std::string_view::size_type find_any_of_case(const std::string_view haystack,
                                             const std::string_view needles);

std::string_view::size_type find_first_of(const std::string_view haystack,
                                          const std::string_view needle);

// find_first_of string, case insensitive.
std::string_view::size_type find_first_of_case(const std::string_view haystack,
                                               const std::string_view needle);

bool starts_with(const std::string_view haystack,
                 const std::string_view needle);

bool starts_with_case(const std::string_view haystack,
                      const std::string_view needle);

bool ends_with(const std::string_view haystack, const std::string_view needle);

bool ends_with_case(const std::string_view haystack,
                    const std::string_view needle);

// Splits the given path on any valid path separator and returns all parts.
std::vector<std::string_view> split_path(const std::string_view path);

// Joins two path segments with the given separator.
std::string join_paths(const std::string_view left_path,
                       const std::string_view right_path,
                       char32_t separator = kPathSeparator);

std::string join_paths(const std::vector<std::string>& paths,
                       char32_t separator = kPathSeparator);

std::string join_paths(const std::vector<std::string_view>& paths,
                       char32_t separator = kPathSeparator);

inline std::string join_paths(
    std::initializer_list<const std::string_view> paths,
    char32_t separator = kPathSeparator) {
  std::string result;
  for (auto path : paths) {
    result = join_paths(result, path, separator);
  }
  return result;
}

inline std::string join_guest_paths(const std::string_view left_path,
                                    const std::string_view right_path) {
  return join_paths(left_path, right_path, kGuestPathSeparator);
}

inline std::string join_guest_paths(const std::vector<std::string>& paths) {
  return join_paths(paths, kGuestPathSeparator);
}

inline std::string join_guest_paths(
    const std::vector<std::string_view>& paths) {
  return join_paths(paths, kGuestPathSeparator);
}

inline std::string join_guest_paths(
    std::initializer_list<const std::string_view> paths) {
  return join_paths(paths, kGuestPathSeparator);
}

// Replaces all path separators with the given value and removes redundant
// separators.
std::string fix_path_separators(const std::string_view path,
                                char32_t new_separator = kPathSeparator);

inline std::string fix_guest_path_separators(const std::string_view path) {
  return fix_path_separators(path, kGuestPathSeparator);
}

// Find the top directory name or filename from a path.
std::string find_name_from_path(const std::string_view path,
                                char32_t separator = kPathSeparator);

inline std::string find_name_from_guest_path(const std::string_view path) {
  return find_name_from_path(path, kGuestPathSeparator);
}

std::string find_base_name_from_path(const std::string_view path,
                                     char32_t separator = kPathSeparator);

inline std::string find_base_name_from_guest_path(const std::string_view path) {
  return find_base_name_from_path(path, kGuestPathSeparator);
}

// Get parent path of the given directory or filename.
std::string find_base_path(const std::string_view path,
                           char32_t separator = kPathSeparator);

inline std::string find_base_guest_path(const std::string_view path) {
  return find_base_path(path, kGuestPathSeparator);
}

// Canonicalizes a path, removing ..'s.
std::string canonicalize_path(const std::string_view path,
                              char32_t separator = kPathSeparator);

inline std::string canonicalize_guest_path(const std::string_view path) {
  return canonicalize_path(path, kGuestPathSeparator);
}

}  // namespace xe::utf8

#endif  // XENIA_BASE_UTF8_H_
