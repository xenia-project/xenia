/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/string_buffer.h>

using namespace alloy;


StringBuffer::StringBuffer(size_t initial_capacity) :
    buffer_(0), capacity_(0), offset_(0) {
  capacity_ = MAX(initial_capacity, 1024);
  buffer_ = (char*)xe_calloc(capacity_);
  buffer_[0] = 0;
}

StringBuffer::~StringBuffer() {
  xe_free(buffer_);
}

void StringBuffer::Reset() {
  offset_ = 0;
  buffer_[0] = 0;
}

void StringBuffer::Append(const char* format, ...) {
  while (true) {
    va_list args;
    va_start(args, format);
    int len = xevsnprintfa(
        buffer_ + offset_, capacity_ - offset_ - 1, format, args);
    va_end(args);
    if (len == -1) {
      size_t old_capacity = capacity_;
      capacity_ = capacity_ * 2;
      buffer_ = (char*)xe_realloc(buffer_, old_capacity, capacity_);
      continue;
    } else {
      offset_ += len;
      break;
    }
  }
  buffer_[offset_] = 0;
}

const char* StringBuffer::GetString() {
  return buffer_;
}

char* StringBuffer::ToString() {
  return xestrdupa(buffer_);
}
