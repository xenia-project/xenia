/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/ring_buffer.h"

#include <algorithm>
#include <cstring>

namespace xe {

RingBuffer::RingBuffer(uint8_t* buffer, size_t capacity)
    : buffer_(buffer), capacity_(capacity) {}

size_t RingBuffer::Read(uint8_t* buffer, size_t count) {
  count = std::min(count, capacity_);
  if (!count) {
    return 0;
  }

  if (read_offset_ + count < capacity_) {
    std::memcpy(buffer, buffer_ + read_offset_, count);
    read_offset_ += count;
  } else {
    size_t left_half = capacity_ - read_offset_;
    size_t right_half = count - left_half;
    std::memcpy(buffer, buffer_ + read_offset_, left_half);
    std::memcpy(buffer + left_half, buffer_, right_half);
    read_offset_ = right_half;
  }

  return count;
}

size_t RingBuffer::Write(const uint8_t* buffer, size_t count) {
  count = std::min(count, capacity_);
  if (!count) {
    return 0;
  }

  if (write_offset_ + count < capacity_) {
    std::memcpy(buffer_ + write_offset_, buffer, count);
    write_offset_ += count;
  } else {
    size_t left_half = capacity_ - write_offset_;
    size_t right_half = count - left_half;
    std::memcpy(buffer_ + write_offset_, buffer, left_half);
    std::memcpy(buffer_, buffer + left_half, right_half);
    write_offset_ = right_half;
  }

  return count;
}

}  // namespace xe
