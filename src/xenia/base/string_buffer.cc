/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/string_buffer.h"

#include <algorithm>
#include <cstdarg>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"

namespace xe {

StringBuffer::StringBuffer(size_t initial_capacity) {
  buffer_capacity_ = std::max(initial_capacity, static_cast<size_t>(16 * 1024));
  buffer_ = reinterpret_cast<char*>(std::malloc(buffer_capacity_));
  assert_not_null(buffer_);
  buffer_[0] = 0;
}

StringBuffer::~StringBuffer() {
  free(buffer_);
  buffer_ = nullptr;
}

void StringBuffer::Reset() {
  buffer_offset_ = 0;
  buffer_[0] = 0;
}

void StringBuffer::Grow(size_t additional_length) {
  if (buffer_offset_ + additional_length <= buffer_capacity_) {
    return;
  }
  size_t old_capacity = buffer_capacity_;
  size_t new_capacity =
      std::max(xe::round_up(buffer_offset_ + additional_length, 16 * 1024),
               old_capacity * 2);
  auto new_buffer = std::realloc(buffer_, new_capacity);
  assert_not_null(new_buffer);
  buffer_ = reinterpret_cast<char*>(new_buffer);
  buffer_capacity_ = new_capacity;
}

void StringBuffer::Append(char c) {
  AppendBytes(reinterpret_cast<const uint8_t*>(&c), 1);
}

void StringBuffer::Append(char c, size_t count) {
  Grow(count + 1);
  std::memset(buffer_ + buffer_offset_, c, count);
  buffer_offset_ += count;
  buffer_[buffer_offset_] = 0;
}

void StringBuffer::Append(const char* value) {
  AppendBytes(reinterpret_cast<const uint8_t*>(value), std::strlen(value));
}

void StringBuffer::Append(const std::string_view value) {
  AppendBytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

void StringBuffer::AppendVarargs(const char* format, va_list args) {
  va_list size_args;
  va_copy(size_args, args);  // arg is indeterminate after the return so copy it
  int length = vsnprintf(nullptr, 0, format, size_args);
  assert_true(length >= 0);
  va_end(size_args);
  Grow(length + 1);
  int result = vsnprintf(buffer_ + buffer_offset_,
                         buffer_capacity_ - buffer_offset_, format, args);
  assert_true(result == length);
  buffer_offset_ += length;
  buffer_[buffer_offset_] = 0;
}

void StringBuffer::AppendBytes(const uint8_t* buffer, size_t length) {
  Grow(length + 1);
  memcpy(buffer_ + buffer_offset_, buffer, length);
  buffer_offset_ += length;
  buffer_[buffer_offset_] = 0;
}

std::string StringBuffer::to_string() {
  return std::string(buffer_, buffer_offset_);
}

std::string_view StringBuffer::to_string_view() const {
  return std::string_view(buffer_, buffer_offset_);
}

std::vector<uint8_t> StringBuffer::to_bytes() const {
  std::vector<uint8_t> bytes(buffer_offset_);
  std::memcpy(bytes.data(), buffer_, buffer_offset_);
  return bytes;
}

}  // namespace xe
