/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/utf8.h"

#include <algorithm>
#include <cstdint>
#include <locale>
#include <numeric>
#include <tuple>

#define UTF_CPP_CPLUSPLUS 202002L
#include "third_party/utfcpp/source/utf8.h"

namespace utfcpp = utf8;

using citer = std::string_view::const_iterator;
using utf8_citer = utfcpp::iterator<std::string_view::const_iterator>;

namespace xe::utf8 {

std::string to_string(char32_t c) {
  std::string result;
  utfcpp::append(c, result);
  return result;
}

uint32_t lower_ascii(const uint32_t c) {
  return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

uint32_t upper_ascii(const uint32_t c) {
  return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

bool equal_ascii_case(const uint32_t l, const uint32_t r) {
  return l == r || lower_ascii(l) == lower_ascii(r);
}

std::pair<utf8_citer, utf8_citer> make_citer(const std::string_view view) {
  return {utf8_citer(view.cbegin(), view.cbegin(), view.cend()),
          utf8_citer(view.cend(), view.cbegin(), view.cend())};
}

std::pair<utf8_citer, utf8_citer> make_citer(const utf8_citer begin,
                                             const utf8_citer end) {
  return {utf8_citer(begin.base(), begin.base(), end.base()),
          utf8_citer(end.base(), begin.base(), end.base())};
}

size_t byte_length(utf8_citer begin, utf8_citer end) {
  return size_t(std::distance(begin.base(), end.base()));
}

size_t count(const std::string_view view) {
  return size_t(utfcpp::distance(view.cbegin(), view.cend()));
}

std::string lower_ascii(const std::string_view view) {
  auto [begin, end] = make_citer(view);
  std::string result;
  for (auto it = begin; it != end; ++it) {
    utfcpp::append(char32_t(lower_ascii(*it)), result);
  }
  return result;
}

std::string upper_ascii(const std::string_view view) {
  auto [begin, end] = make_citer(view);
  std::string result;
  for (auto it = begin; it != end; ++it) {
    utfcpp::append(char32_t(upper_ascii(*it)), result);
  }
  return result;
}

template <bool LOWER>
inline size_t hash_fnv1a(const std::string_view view) {
  const size_t offset_basis = 0xCBF29CE484222325ull;
  // chrispy: constant capture errors on clang
  auto work = [](size_t hash, uint8_t byte_of_data) {
    const size_t prime = 0x00000100000001B3ull;
    hash ^= byte_of_data;
    hash *= prime;
    return hash;
  };
  auto hash = offset_basis;
  auto [begin, end] = make_citer(view);
  for (auto it = begin; it != end; ++it) {
    uint32_t c;
    if constexpr (LOWER) {
      c = lower_ascii(*it);
    } else {
      c = *it;
    }
    hash = work(hash, uint8_t((c >> 0) & 0xFF));
    hash = work(hash, uint8_t((c >> 8) & 0xFF));
    hash = work(hash, uint8_t((c >> 16) & 0xFF));
    hash = work(hash, uint8_t((c >> 24) & 0xFF));
  }
  return hash;
}

size_t hash_fnv1a(const std::string_view view) {
  return hash_fnv1a<false>(view);
}

size_t hash_fnv1a_case(const std::string_view view) {
  return hash_fnv1a<true>(view);
}

// TODO(gibbed): this is a separate inline function instead of inline within
// split due to a Clang bug: reference to local binding 'needle_begin' declared
// in enclosing function 'split'.
inline utf8_citer find_needle(utf8_citer haystack_it, utf8_citer haystack_end,
                              utf8_citer needle_begin, utf8_citer needle_end) {
  return std::find_if(haystack_it, haystack_end, [&](const auto& c) {
    for (auto needle = needle_begin; needle != needle_end; ++needle) {
      if (c == *needle) {
        return true;
      }
    }
    return false;
  });
}

inline utf8_citer find_needle_case(utf8_citer haystack_it,
                                   utf8_citer haystack_end,
                                   utf8_citer needle_begin,
                                   utf8_citer needle_end) {
  return std::find_if(haystack_it, haystack_end, [&](const auto& c) {
    for (auto needle = needle_begin; needle != needle_end; ++needle) {
      if (equal_ascii_case(c, *needle)) {
        return true;
      }
    }
    return false;
  });
}

std::vector<std::string_view> split(const std::string_view haystack,
                                    const std::string_view needles,
                                    bool remove_empty) {
  std::vector<std::string_view> result;

  auto [haystack_begin, haystack_end] = make_citer(haystack);
  auto [needle_begin, needle_end] = make_citer(needles);

  auto it = haystack_begin;
  auto last = it;
  for (;;) {
    it = find_needle(it, haystack_end, needle_begin, needle_end);
    if (it == haystack_end) {
      break;
    }

    if (it != last) {
      auto offset = byte_length(haystack_begin, last);
      auto length = byte_length(haystack_begin, it) - offset;
      result.push_back(haystack.substr(offset, length));
    } else if (!remove_empty) {
      result.push_back("");
    }

    ++it;
    last = it;
  }

  if (last != haystack_end) {
    auto offset = byte_length(haystack_begin, last);
    result.push_back(haystack.substr(offset));
  }

  return result;
}

bool equal_z(const std::string_view left, const std::string_view right) {
  if (!left.size()) {
    return !right.size();
  } else if (!right.size()) {
    return false;
  }
  auto [left_begin, left_end] = make_citer(left);
  auto [right_begin, right_end] = make_citer(right);
  auto left_it = left_begin;
  auto right_it = right_begin;
  for (; left_it != left_end && *left_it != 0 && right_it != right_end &&
         *right_it != 0;
       ++left_it, ++right_it) {
    if (*left_it != *right_it) {
      return false;
    }
  }
  return (left_it == left_end || *left_it == 0) &&
         (right_it == right_end || *right_it == 0);
}

bool equal_case(const std::string_view left, const std::string_view right) {
  if (!left.size()) {
    return !right.size();
  } else if (!right.size()) {
    return false;
  }
  auto [left_begin, left_end] = make_citer(left);
  auto [right_begin, right_end] = make_citer(right);
  return std::equal(left_begin, left_end, right_begin, right_end,
                    equal_ascii_case);
}

bool equal_case_z(const std::string_view left, const std::string_view right) {
  if (!left.size()) {
    return !right.size();
  } else if (!right.size()) {
    return false;
  }
  auto [left_begin, left_end] = make_citer(left);
  auto [right_begin, right_end] = make_citer(right);
  auto left_it = left_begin;
  auto right_it = right_begin;
  for (; left_it != left_end && *left_it != 0 && right_it != right_end &&
         *right_it != 0;
       ++left_it, ++right_it) {
    if (!equal_ascii_case(*left_it, *right_it)) {
      return false;
    }
  }
  return (left_it == left_end || *left_it == 0) &&
         (right_it == right_end || *right_it == 0);
}

std::string_view::size_type find_any_of(const std::string_view haystack,
                                        const std::string_view needles) {
  if (needles.empty()) {
    return std::string_view::size_type(0);
  } else if (haystack.empty()) {
    return std::string_view::npos;
  }

  auto [haystack_begin, haystack_end] = make_citer(haystack);
  auto [needle_begin, needle_end] = make_citer(needles);

  auto it = find_needle(haystack_begin, haystack_end, needle_begin, needle_end);
  if (it == haystack_end) {
    return std::string_view::npos;
  }

  return std::string_view::size_type(byte_length(haystack_begin, it));
}

std::string_view::size_type find_any_of_case(const std::string_view haystack,
                                             const std::string_view needles) {
  if (needles.empty()) {
    return std::string_view::size_type(0);
  } else if (haystack.empty()) {
    return std::string_view::npos;
  }

  auto [haystack_begin, haystack_end] = make_citer(haystack);
  auto [needle_begin, needle_end] = make_citer(needles);

  auto it =
      find_needle_case(haystack_begin, haystack_end, needle_begin, needle_end);
  if (it == haystack_end) {
    return std::string_view::npos;
  }

  return std::string_view::size_type(byte_length(haystack_begin, it));
}

std::string_view::size_type find_first_of(const std::string_view haystack,
                                          const std::string_view needle) {
  if (needle.empty()) {
    return std::string_view::size_type(0);
  } else if (haystack.empty()) {
    return std::string_view::npos;
  }

  auto [haystack_begin, haystack_end] = make_citer(haystack);
  auto [needle_begin, needle_end] = make_citer(needle);
  auto needle_count = count(needle);

  auto it = haystack_begin;
  for (; it != haystack_end; ++it) {
    it = std::find(it, haystack_end, *needle_begin);
    if (it == haystack_end) {
      return std::string_view::npos;
    }

    auto end = it;
    for (size_t i = 0; i < needle_count; ++i) {
      if (end == haystack_end) {
        // not enough room in target for search
        return std::string_view::npos;
      }
      ++end;
    }

    auto [sub_start, sub_end] = make_citer(it, end);
    if (std::equal(needle_begin, needle_end, sub_start, sub_end)) {
      return std::string_view::size_type(byte_length(haystack_begin, it));
    }
  }

  return std::string_view::npos;
}

std::string_view::size_type find_first_of_case(const std::string_view haystack,
                                               const std::string_view needle) {
  if (needle.empty()) {
    return std::string_view::size_type(0);
  } else if (haystack.empty()) {
    return std::string_view::npos;
  }

  auto [haystack_begin, haystack_end] = make_citer(haystack);
  auto [needle_begin, needle_end] = make_citer(needle);
  auto needle_count = count(needle);
  auto nc = *needle_begin;

  auto it = haystack_begin;
  for (; it != haystack_end; ++it) {
    it = std::find_if(it, haystack_end, [&nc](const uint32_t& c) {
      return equal_ascii_case(nc, c);
    });
    if (it == haystack_end) {
      return std::string_view::npos;
    }

    auto end = it;
    for (size_t i = 0; i < needle_count; ++i) {
      if (end == haystack_end) {
        // not enough room in target for search
        return std::string_view::npos;
      }
      ++end;
    }

    auto [sub_start, sub_end] = make_citer(it, end);
    if (std::equal(needle_begin, needle_end, sub_start, sub_end,
                   equal_ascii_case)) {
      return std::string_view::size_type(byte_length(haystack_begin, it));
    }
  }

  return std::string_view::npos;
}

bool starts_with(const std::string_view haystack, char32_t needle) {
  if (haystack.empty()) {
    return false;
  }
  auto [it, end] = make_citer(haystack);
  return *it == uint32_t(needle);
}

bool starts_with(const std::string_view haystack,
                 const std::string_view needle) {
  if (needle.empty()) {
    return true;
  } else if (haystack.empty()) {
    return false;
  }

  auto [haystack_begin, haystack_end] = make_citer(haystack);
  auto [needle_begin, needle_end] = make_citer(needle);
  auto needle_count = count(needle);

  auto it = haystack_begin;
  auto end = it;
  for (size_t i = 0; i < needle_count; ++i) {
    if (end == haystack_end) {
      // not enough room in target for search
      return false;
    }
    ++end;
  }

  auto [sub_start, sub_end] = make_citer(it, end);
  return std::equal(needle_begin, needle_end, sub_start, sub_end);
}

bool starts_with_case(const std::string_view haystack,
                      const std::string_view needle) {
  if (needle.empty()) {
    return true;
  } else if (haystack.empty()) {
    return false;
  }

  auto [haystack_begin, haystack_end] = make_citer(haystack);
  auto [needle_begin, needle_end] = make_citer(needle);
  auto needle_count = count(needle);

  auto it = haystack_begin;
  auto end = it;
  for (size_t i = 0; i < needle_count; ++i) {
    if (end == haystack_end) {
      // not enough room in target for search
      return false;
    }
    ++end;
  }

  auto [sub_start, sub_end] = make_citer(it, end);
  return std::equal(needle_begin, needle_end, sub_start, sub_end,
                    equal_ascii_case);
}

bool ends_with(const std::string_view haystack, const std::string_view needle) {
  if (needle.empty()) {
    return true;
  } else if (haystack.empty()) {
    return false;
  }

  auto [haystack_begin, haystack_end] = make_citer(haystack);
  auto [needle_begin, needle_end] = make_citer(needle);
  auto needle_count = count(needle);

  auto it = haystack_end;
  auto end = it;
  --it;

  for (size_t i = 1; i < needle_count; ++i) {
    if (it == haystack_begin) {
      // not enough room in target for search
      return false;
    }
    --it;
  }

  auto [sub_start, sub_end] = make_citer(it, end);
  return std::equal(needle_begin, needle_end, sub_start, sub_end);
}

bool ends_with_case(const std::string_view haystack,
                    const std::string_view needle) {
  if (needle.empty()) {
    return true;
  } else if (haystack.empty()) {
    return false;
  }

  auto [haystack_begin, haystack_end] = make_citer(haystack);
  auto [needle_begin, needle_end] = make_citer(needle);
  auto needle_count = count(needle);

  auto it = haystack_end;
  auto end = it;
  --it;

  for (size_t i = 0; i < needle_count; ++i) {
    if (it == haystack_begin) {
      // not enough room in target for search
      return false;
    }
    --it;
  }

  auto [sub_start, sub_end] = make_citer(it, end);
  return std::equal(needle_begin, needle_end, sub_start, sub_end,
                    equal_ascii_case);
}

std::vector<std::string_view> split_path(const std::string_view path) {
  return split(path, "\\/", true);
}

std::string join_paths(const std::string_view left_path,
                       const std::string_view right_path, char32_t separator) {
  if (!left_path.length()) {
    return std::string(right_path);
  } else if (!right_path.length()) {
    return std::string(left_path);
  }

  utf8_citer it;
  std::tie(std::ignore, it) = make_citer(left_path);
  --it;

  std::string result = std::string(left_path);
  if (*it != static_cast<uint32_t>(separator)) {
    utfcpp::append(separator, result);
  }
  return result + std::string(right_path);
}

std::string join_paths(const std::vector<std::string>& paths,
                       char32_t separator) {
  std::string result;
  auto it = paths.cbegin();
  if (it != paths.cend()) {
    result = *it++;
    for (; it != paths.cend(); ++it) {
      result = join_paths(result, *it, separator);
    }
  }
  return result;
}

std::string join_paths(const std::vector<std::string_view>& paths,
                       char32_t separator) {
  std::string result;
  auto it = paths.cbegin();
  if (it != paths.cend()) {
    result = *it++;
    for (; it != paths.cend(); ++it) {
      result = join_paths(result, *it, separator);
    }
  }
  return result;
}

std::string fix_path_separators(const std::string_view path,
                                char32_t new_separator) {
  if (path.empty()) {
    return std::string();
  }

  // Swap all separators to new_sep.
  const char32_t old_separator = new_separator == U'\\' ? U'/' : U'\\';

  auto [path_begin, path_end] = make_citer(path);

  std::string result;
  auto it = path_begin;
  auto last = it;

  auto is_separator = [old_separator, new_separator](char32_t c) {
    return c == uint32_t(old_separator) || c == uint32_t(new_separator);
  };

  // Begins with a separator
  if (is_separator(*it)) {
    utfcpp::append(new_separator, result);
    ++it;
    last = it;
  }

  for (;;) {
    it = std::find_if(it, path_end, is_separator);
    if (it == path_end) {
      break;
    }

    if (it != last) {
      auto offset = byte_length(path_begin, last);
      auto length = byte_length(path_begin, it) - offset;
      result += path.substr(offset, length);
      utfcpp::append(new_separator, result);
    }

    ++it;
    last = it;
  }

  if (last == path_begin) {
    return std::string(path);
  }

  if (last != path_end) {
    auto offset = byte_length(path_begin, last);
    result += path.substr(offset);
  }

  return result;
}

std::string find_name_from_path(const std::string_view path,
                                char32_t separator) {
  if (path.empty()) {
    return std::string();
  }

  auto [begin, end] = make_citer(path);

  auto it = end;
  --it;

  // skip trailing separators at the end of the path
  while (*it == uint32_t(separator)) {
    if (it == begin) {
      // path is all separators, name is empty
      return std::string();
    }
    --it;
  }

  // update end so it is before any trailing separators
  end = std::next(it);

  // skip non-separators
  while (*it != uint32_t(separator)) {
    if (it == begin) {
      break;
    }
    --it;
  }

  // if the iterator is on a separator, advance
  if (*it == uint32_t(separator)) {
    ++it;
  }

  auto offset = byte_length(begin, it);
  auto length = byte_length(it, end);
  return std::string(path.substr(offset, length));
}

std::string find_base_name_from_path(const std::string_view path,
                                     char32_t separator) {
  auto name = find_name_from_path(path, separator);
  if (!name.length()) {
    return std::string();
  }

  auto [begin, end] = make_citer(name);

  auto it = end;
  --it;

  while (it != begin) {
    if (*it == uint32_t('.')) {
      break;
    }
    --it;
  }

  if (it == begin) {
    return name;
  }

  auto length = byte_length(it, end);
  auto offset = name.length() - length;
  return std::string(name.substr(0, offset));
}

std::string find_base_path(const std::string_view path, char32_t separator) {
  if (path.empty()) {
    return std::string();
  }

  auto [begin, end] = make_citer(path);

  auto it = end;
  --it;

  // skip trailing separators at the end of the path
  while (*it == uint32_t(separator)) {
    if (it == begin) {
      return std::string();
    }
    --it;
  }

  // skip non-separators
  while (*it != uint32_t(separator)) {
    if (it == begin) {
      // there are no separators, base path is empty
      return std::string();
    }
    --it;
  }

  // skip trailing separators at the end of the base path
  while (*it == uint32_t(separator)) {
    if (it == begin) {
      // base path is all separators, base path is empty
      return std::string();
    }
    --it;
  }

  auto length = byte_length(begin, std::next(it));
  return std::string(path.substr(0, length));
}

std::string canonicalize_path(const std::string_view path, char32_t separator) {
  if (path.empty()) {
    return std::string();
  }

  auto is_rooted = starts_with(path, separator);

  auto parts = split_path(path);
  for (auto it = parts.begin(); it != parts.end();) {
    const auto& part = *it;
    if (part == ".") {
      // Potential marker for current directory.
      it = parts.erase(it);
    } else if (part == "..") {
      // Ensure we don't override the device name.
      if (it != parts.begin()) {
        auto prev = std::prev(it);
        if (!ends_with(*prev, ":")) {
          it = parts.erase(prev);
        }
      }
      it = parts.erase(it);
    } else {
      ++it;
    }
  }

  return !is_rooted ? join_paths(parts, separator)
                    : to_string(separator) + join_paths(parts, separator);
}

}  // namespace xe::utf8
