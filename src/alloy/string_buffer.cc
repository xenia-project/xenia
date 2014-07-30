/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/string_buffer.h>

namespace alloy {

StringBuffer::StringBuffer(size_t initial_capacity) : offset_(0) {
  buffer_.resize(MAX(initial_capacity, 1024));
}

StringBuffer::~StringBuffer() = default;

void StringBuffer::Reset() {
  offset_ = 0;
  buffer_[0] = 0;
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
  while (true) {
    int len = vsnprintf(buffer_.data() + offset_, buffer_.size() - offset_ - 1,
                        format, args);
    if (len == -1) {
      buffer_.resize(buffer_.size() * 2);
      continue;
    } else {
      offset_ += len;
      break;
    }
  }
  buffer_[offset_] = 0;
}

void StringBuffer::AppendBytes(const uint8_t* buffer, size_t length) {
  if (offset_ + length > buffer_.size()) {
    buffer_.resize(MAX(buffer_.size() * 2, buffer_.size() + length));
  }
  memcpy(buffer_.data() + offset_, buffer, length);
  offset_ += length;
  buffer_[offset_] = 0;
}

const char* StringBuffer::GetString() const { return buffer_.data(); }

char* StringBuffer::ToString() { return strdup(buffer_.data()); }

}  // namespace alloy
