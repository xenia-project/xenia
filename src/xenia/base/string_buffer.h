/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_STRING_BUFFER_H_
#define XENIA_BASE_STRING_BUFFER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"

namespace xe {

class StringBuffer {
 public:
  explicit StringBuffer(size_t initial_capacity = 0);
  ~StringBuffer();

  char* buffer() const { return buffer_; }
  size_t length() const { return buffer_offset_; }

  void Reset();

  void Append(char c);
  void Append(char c, size_t count);
  void Append(const char* value);
  void Append(const std::string_view value);

  template <typename... Args>
  void AppendFormat(const char* format, const Args&... args) {
    auto s = fmt::format(fmt::runtime(format), args...);
    Append(s.c_str());
  }

  void AppendVarargs(const char* format, va_list args);
  void AppendBytes(const uint8_t* buffer, size_t length);

  std::string to_string();
  std::string_view to_string_view() const;
  std::vector<uint8_t> to_bytes() const;

 private:
  void Grow(size_t additional_length);

  char* buffer_ = nullptr;
  size_t buffer_offset_ = 0;
  size_t buffer_capacity_ = 0;
};

}  // namespace xe

#endif  // XENIA_BASE_STRING_BUFFER_H_
