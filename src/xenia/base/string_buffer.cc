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

#include "xenia/base/math.h"

namespace xe {

StringBuffer::StringBuffer(size_t initial_capacity) {
  buffer_capacity_ = std::max(initial_capacity, static_cast<size_t>(16 * 1024));
  buffer_ = reinterpret_cast<char*>(malloc(buffer_capacity_));
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
  buffer_ = reinterpret_cast<char*>(realloc(buffer_, new_capacity));
  buffer_capacity_ = new_capacity;
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
  Grow(length + 1);
  vsnprintf(buffer_ + buffer_offset_, buffer_capacity_, format, args);
  buffer_offset_ += length;
  buffer_[buffer_offset_] = 0;
}

void StringBuffer::AppendBytes(const uint8_t* buffer, size_t length) {
  Grow(length + 1);
  memcpy(buffer_ + buffer_offset_, buffer, length);
  buffer_offset_ += length;
  buffer_[buffer_offset_] = 0;
}

const char* StringBuffer::GetString() const { return buffer_; }

std::string StringBuffer::to_string() {
  return std::string(buffer_, buffer_offset_);
}

char* StringBuffer::ToString() { return strdup(buffer_); }

std::vector<uint8_t> StringBuffer::ToBytes() const {
  std::vector<uint8_t> bytes(buffer_offset_);
  std::memcpy(bytes.data(), buffer_, buffer_offset_);
  return bytes;
}

}  // namespace xe
