/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/string_buffer.h"

#include <algorithm>
#include <cstdarg>

namespace xe {

StringBuffer::StringBuffer(size_t initial_capacity) {
  buffer_.reserve(std::max(initial_capacity, static_cast<size_t>(1024)));
}

StringBuffer::~StringBuffer() = default;

void StringBuffer::Reset() { buffer_.resize(0); }

void StringBuffer::Grow(size_t additional_length) {
  size_t old_capacity = buffer_.capacity();
  if (buffer_.size() + additional_length <= old_capacity) {
    return;
  }
  size_t new_capacity =
      std::max(buffer_.size() + additional_length, old_capacity * 2);
  buffer_.reserve(new_capacity);
}

void StringBuffer::Append(char c) {
  AppendBytes(reinterpret_cast<const uint8_t*>(&c), 1);
}

void StringBuffer::Append(const char* value) {
  AppendBytes(reinterpret_cast<const uint8_t*>(value), std::strlen(value));
}

void StringBuffer::Append(const std::string& value) {
  AppendBytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

void StringBuffer::AppendFormat(const char* format, ...) {
  va_list args;
  va_start(args, format);
  AppendVarargs(format, args);
  va_end(args);
}

void StringBuffer::AppendVarargs(const char* format, va_list args) {
  int length = vsnprintf(nullptr, 0, format, args);
  auto offset = buffer_.size();
  Grow(length + 1);
  buffer_.resize(buffer_.size() + length + 1);
  vsnprintf(buffer_.data() + offset, buffer_.capacity(), format, args);
  buffer_[buffer_.size() - 1] = 0;
  buffer_.resize(buffer_.size() - 1);
}

void StringBuffer::AppendBytes(const uint8_t* buffer, size_t length) {
  auto offset = buffer_.size();
  Grow(length + 1);
  buffer_.resize(buffer_.size() + length + 1);
  memcpy(buffer_.data() + offset, buffer, length);
  buffer_[buffer_.size() - 1] = 0;
  buffer_.resize(buffer_.size() - 1);
}

const char* StringBuffer::GetString() const { return buffer_.data(); }

std::string StringBuffer::to_string() {
  return std::string(buffer_.data(), buffer_.size());
}

char* StringBuffer::ToString() { return strdup(buffer_.data()); }

}  // namespace xe
