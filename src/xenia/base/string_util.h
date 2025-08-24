/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_STRING_UTIL_H_
#define XENIA_BASE_STRING_UTIL_H_

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <string>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/memory.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"
#include "xenia/base/vec128.h"

// TODO(gibbed): Clang and GCC don't have std::from_chars for floating point(!)
// despite it being part of the C++17 standard. Check this in the future to see
// if it's been resolved.

#if XE_COMPILER_CLANG || XE_COMPILER_GNUC
#include <cstdlib>
#endif

namespace xe {
namespace string_util {

enum class Safety {
  IDontKnowWhatIAmDoing,
  IKnowWhatIAmDoing,
};

inline size_t copy_truncating(char* dest, const std::string_view source,
                              size_t dest_buffer_count) {
  if (!dest_buffer_count) {
    return 0;
  }
  size_t chars_copied = std::min(source.size(), dest_buffer_count - size_t(1));
  std::memcpy(dest, source.data(), chars_copied);
  dest[chars_copied] = '\0';
  return chars_copied;
}

inline size_t copy_truncating(char16_t* dest, const std::u16string_view source,
                              size_t dest_buffer_count) {
  if (!dest_buffer_count) {
    return 0;
  }
  size_t chars_copied = std::min(source.size(), dest_buffer_count - size_t(1));
  std::memcpy(dest, source.data(), chars_copied * sizeof(char16_t));
  dest[chars_copied] = u'\0';
  return chars_copied;
}

inline size_t copy_and_swap_truncating(char16_t* dest,
                                       const std::u16string_view source,
                                       size_t dest_buffer_count) {
  if (!dest_buffer_count) {
    return 0;
  }
  size_t chars_copied = std::min(source.size(), dest_buffer_count - size_t(1));
  xe::copy_and_swap(dest, source.data(), chars_copied);
  dest[chars_copied] = u'\0';
  return chars_copied;
}

template <Safety safety = Safety::IDontKnowWhatIAmDoing>
inline size_t copy_maybe_truncating(char* dest, const std::string_view source,
                                    size_t dest_buffer_count) {
  static_assert(safety == Safety::IKnowWhatIAmDoing);
  if (!dest_buffer_count) {
    return 0;
  }
  size_t chars_copied = std::min(source.size(), dest_buffer_count);
  std::memcpy(dest, source.data(), chars_copied);
  return chars_copied;
}

template <Safety safety = Safety::IDontKnowWhatIAmDoing>
inline size_t copy_maybe_truncating(char16_t* dest,
                                    const std::u16string_view source,
                                    size_t dest_buffer_count) {
  static_assert(safety == Safety::IKnowWhatIAmDoing);
  if (!dest_buffer_count) {
    return 0;
  }
  size_t chars_copied = std::min(source.size(), dest_buffer_count);
  std::memcpy(dest, source.data(), chars_copied * sizeof(char16_t));
  return chars_copied;
}

template <Safety safety = Safety::IDontKnowWhatIAmDoing>
inline size_t copy_and_swap_maybe_truncating(char16_t* dest,
                                             const std::u16string_view source,
                                             size_t dest_buffer_count) {
  static_assert(safety == Safety::IKnowWhatIAmDoing);
  if (!dest_buffer_count) {
    return 0;
  }
  size_t chars_copied = std::min(source.size(), dest_buffer_count);
  xe::copy_and_swap(dest, source.data(), chars_copied);
  return chars_copied;
}

inline bool hex_string_to_array(std::vector<uint8_t>& output_array,
                                const std::string_view value) {
  output_array.reserve((value.size() + 1) / 2);

  size_t remaining_length = value.size();
  while (remaining_length > 0) {
    uint8_t chars_to_read = remaining_length > 1 ? 2 : 1;
    const char* substring_pointer =
        value.data() + value.size() - remaining_length;

    uint8_t string_value = 0;
    std::from_chars_result result = std::from_chars(
        substring_pointer, substring_pointer + chars_to_read, string_value, 16);

    if (result.ec != std::errc() ||
        result.ptr != substring_pointer + chars_to_read) {
      output_array.clear();
      return false;
    }

    output_array.push_back(string_value);
    remaining_length -= chars_to_read;
  }
  return true;
}

inline std::string to_hex_string(uint32_t value) {
  return fmt::format("{:08X}", value);
}

inline std::string to_hex_string(uint64_t value) {
  return fmt::format("{:016X}", value);
}

inline std::string to_hex_string(float value) {
  static_assert(sizeof(uint32_t) == sizeof(value));
  uint32_t pun;
  std::memcpy(&pun, &value, sizeof(value));
  return to_hex_string(pun);
}

inline std::string to_hex_string(double value) {
  static_assert(sizeof(uint64_t) == sizeof(value));
  uint64_t pun;
  std::memcpy(&pun, &value, sizeof(value));
  return to_hex_string(pun);
}

inline std::string to_hex_string(const vec128_t& value) {
  return fmt::format("[{:08X} {:08X} {:08X} {:08X}]", value.u32[0],
                     value.u32[1], value.u32[2], value.u32[3]);
}

template <typename T>
inline T from_string(const std::string_view value, bool force_hex = false) {
  // Missing implementation for converting type T from string
  throw;
}

namespace internal {

template <typename T, typename V = std::make_signed_t<T>>
inline T make_negative(T value) {
  if constexpr (std::is_unsigned_v<T>) {
    value = static_cast<T>(-static_cast<V>(value));
  } else {
    value = -value;
  }
  return value;
}

// integral_from_string
template <typename T>
inline T ifs(const std::string_view value, bool force_hex) {
  int base = 10;
  std::string_view range = value;
  bool is_hex = force_hex;
  bool is_negative = false;
  if (utf8::starts_with(range, "-")) {
    is_negative = true;
    range = range.substr(1);
  }
  if (utf8::starts_with(range, "0x")) {
    is_hex = true;
    range = range.substr(2);
  }
  if (utf8::ends_with(range, "h")) {
    is_hex = true;
    range = range.substr(0, range.length() - 1);
  }
  T result;
  if (is_hex) {
    base = 16;
  }
  // TODO(gibbed): do something more with errors?
  auto [p, error] =
      std::from_chars(range.data(), range.data() + range.size(), result, base);
  if (error != std::errc()) {
    assert_always();
    return T();
  }
  if (is_negative) {
    result = make_negative(result);
  }
  return result;
}

// floating_point_from_string
template <typename T, typename PUN>
inline T fpfs(const std::string_view value, bool force_hex) {
  static_assert(sizeof(T) == sizeof(PUN));
  std::string_view range = value;
  bool is_hex = force_hex;
  bool is_negative = false;
  if (utf8::starts_with(range, "-")) {
    is_negative = true;
    range = range.substr(1);
  }
  if (utf8::starts_with(range, "0x")) {
    is_hex = true;
    range = range.substr(2);
  }
  if (utf8::ends_with(range, "h")) {
    is_hex = true;
    range = range.substr(0, range.length() - 1);
  }
  T result;
  if (is_hex) {
    PUN pun = from_string<PUN>(range, true);
    if (is_negative) {
      pun = make_negative(pun);
    }
    std::memcpy(&result, &pun, sizeof(PUN));
  } else {
#if XE_COMPILER_CLANG || XE_COMPILER_GNUC
    auto temp = std::string(range);
    result = std::strtod(temp.c_str(), nullptr);
#else
    auto [p, error] = std::from_chars(range.data(), range.data() + range.size(),
                                      result, std::chars_format::general);
    // TODO(gibbed): do something more with errors?
    if (error != std::errc()) {
      assert_always();
      return T();
    }
#endif
    if (is_negative) {
      result = -result;
    }
  }
  return result;
}

}  // namespace internal

template <>
inline bool from_string<bool>(const std::string_view value, bool force_hex) {
  return value == "true" || value == "1";
}

template <>
inline int8_t from_string<int8_t>(const std::string_view value,
                                  bool force_hex) {
  return internal::ifs<int8_t>(value, force_hex);
}

template <>
inline uint8_t from_string<uint8_t>(const std::string_view value,
                                    bool force_hex) {
  return internal::ifs<uint8_t>(value, force_hex);
}

template <>
inline int16_t from_string<int16_t>(const std::string_view value,
                                    bool force_hex) {
  return internal::ifs<int16_t>(value, force_hex);
}

template <>
inline uint16_t from_string<uint16_t>(const std::string_view value,
                                      bool force_hex) {
  return internal::ifs<uint16_t>(value, force_hex);
}

template <>
inline int32_t from_string<int32_t>(const std::string_view value,
                                    bool force_hex) {
  return internal::ifs<int32_t>(value, force_hex);
}

template <>
inline uint32_t from_string<uint32_t>(const std::string_view value,
                                      bool force_hex) {
  return internal::ifs<uint32_t>(value, force_hex);
}

template <>
inline int64_t from_string<int64_t>(const std::string_view value,
                                    bool force_hex) {
  return internal::ifs<int64_t>(value, force_hex);
}

template <>
inline uint64_t from_string<uint64_t>(const std::string_view value,
                                      bool force_hex) {
  return internal::ifs<uint64_t>(value, force_hex);
}

template <>
inline float from_string<float>(const std::string_view value, bool force_hex) {
  return internal::fpfs<float, uint32_t>(value, force_hex);
}

template <>
inline double from_string<double>(const std::string_view value,
                                  bool force_hex) {
  return internal::fpfs<double, uint64_t>(value, force_hex);
}

template <>
inline vec128_t from_string<vec128_t>(const std::string_view value,
                                      bool force_hex) {
  if (!value.size()) {
    return vec128_t();
  }
  vec128_t v;
#if XE_COMPILER_CLANG || XE_COMPILER_GNUC
  auto temp = std::string(value);
  auto p = temp.c_str();
  auto end = temp.c_str() + temp.size();
#else
  auto p = value.data();
  auto end = value.data() + value.size();
#endif
  bool is_hex = force_hex;
  if (p != end && *p == '[') {
    is_hex = true;
    ++p;
  } else if (p != end && *p == '(') {
    is_hex = false;
    ++p;
  } else {
    // Assume hex?
    is_hex = true;
  }
  if (p == end) {
    assert_always();
    return vec128_t();
  }
  if (is_hex) {
    for (size_t i = 0; i < 4; i++) {
      while (p != end && (*p == ' ' || *p == ',')) {
        ++p;
      }
      if (p == end) {
        assert_always();
        return vec128_t();
      }
      auto result = std::from_chars(p, end, v.u32[i], 16);
      if (result.ec != std::errc()) {
        assert_always();
        return vec128_t();
      }
      p = result.ptr;
    }
  } else {
    for (size_t i = 0; i < 4; i++) {
      while (p != end && (*p == ' ' || *p == ',')) {
        ++p;
      }
      if (p == end) {
        assert_always();
        return vec128_t();
      }
#if XE_COMPILER_CLANG || XE_COMPILER_GNUC
      char* next_p;
      v.f32[i] = std::strtof(p, &next_p);
      p = next_p;
#else
      auto result =
          std::from_chars(p, end, v.f32[i], std::chars_format::general);
      if (result.ec != std::errc()) {
        assert_always();
        return vec128_t();
      }
      p = result.ptr;
#endif
    }
  }
  return v;
}

}  // namespace string_util
}  // namespace xe

#endif  // XENIA_BASE_STRING_UTIL_H_
