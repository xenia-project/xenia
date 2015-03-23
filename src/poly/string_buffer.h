/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_STRING_BUFFER_H_
#define POLY_STRING_BUFFER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace poly {

class StringBuffer {
 public:
  StringBuffer(size_t initial_capacity = 0);
  ~StringBuffer();

  size_t length() const { return buffer_.size(); }

  void Reset();

  void Append(const std::string& value);
  void Append(const char* format, ...);
  void AppendVarargs(const char* format, va_list args);
  void AppendBytes(const uint8_t* buffer, size_t length);

  const char* GetString() const;
  std::string to_string();
  char* ToString();
  char* EncodeBase64();

 private:
  void Grow(size_t additional_length);

  std::vector<char> buffer_;
};

}  // namespace poly

#endif  // POLY_STRING_BUFFER_H_
