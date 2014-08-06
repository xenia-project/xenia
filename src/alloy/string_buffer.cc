/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/string_buffer.h>

#include <algorithm>

namespace alloy {

StringBuffer::StringBuffer(size_t initial_capacity) {
  buffer_.reserve(MAX(initial_capacity, 1024));
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

void StringBuffer::Append(const std::string& value) {
  AppendBytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

void StringBuffer::Append(const char* format, ...) {
  va_list args;
  va_start(args, format);
  AppendVarargs(format, args);
  va_end(args);
}

void StringBuffer::AppendVarargs(const char* format, va_list args) {
  int length = vsnprintf(nullptr, 0, format, args);
  auto offset = buffer_.size();
  Grow(length + 1);
  buffer_.resize(buffer_.size() + length);
  vsnprintf(buffer_.data() + offset, buffer_.capacity() - 1, format, args);
  buffer_[buffer_.size()] = 0;
}

void StringBuffer::AppendBytes(const uint8_t* buffer, size_t length) {
  auto offset = buffer_.size();
  Grow(length + 1);
  buffer_.resize(buffer_.size() + length);
  memcpy(buffer_.data() + offset, buffer, length);
  buffer_[buffer_.size()] = 0;
}

const char* StringBuffer::GetString() const { return buffer_.data(); }

char* StringBuffer::ToString() { return strdup(buffer_.data()); }

}  // namespace alloy
