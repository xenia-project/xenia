/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_STRING_BUFFER_H_
#define XENIA_BASE_STRING_BUFFER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace xe {

class StringBuffer {
 public:
  explicit StringBuffer(size_t initial_capacity = 0);
  ~StringBuffer();

  size_t length() const { return buffer_offset_; }

  void Reset();

  void Append(char c);
  void Append(const char* value);
  void Append(const std::string& value);
  void AppendFormat(const char* format, ...);
  void AppendVarargs(const char* format, va_list args);
  void AppendBytes(const uint8_t* buffer, size_t length);

  const char* GetString() const;
  std::string to_string();
  char* ToString();
  std::vector<uint8_t> ToBytes() const;

 private:
  void Grow(size_t additional_length);

  char* buffer_ = nullptr;
  size_t buffer_offset_ = 0;
  size_t buffer_capacity_ = 0;
};

}  // namespace xe

#endif  // XENIA_BASE_STRING_BUFFER_H_
