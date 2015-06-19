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

RingBuffer::RingBuffer(uint8_t* raw_buffer, size_t size, size_t read_offset, size_t write_offset)
    : raw_buffer_(raw_buffer)
    , size_(size)
    , read_offset_(read_offset)
    , write_offset_(write_offset) {}

size_t RingBuffer::Skip(size_t num_bytes) {
  num_bytes = std::min(read_size(), num_bytes);
  if (read_offset_ + num_bytes < size_) {
    read_offset_ += num_bytes;
  } else {
    read_offset_ = num_bytes - (size_ - read_offset_);
  }
  return num_bytes;
}

size_t RingBuffer::Read(uint8_t* buffer, size_t num_bytes) {
  num_bytes = std::min(read_size(), num_bytes);
  if (!num_bytes) {
    return 0;
  }

  if (read_offset_ + num_bytes < size_) {
    std::memcpy(buffer, raw_buffer_ + read_offset_, num_bytes);
    read_offset_ += num_bytes;
  } else {
    size_t left_half = size_ - read_offset_;
    size_t right_half = size_ - left_half;
    std::memcpy(buffer, raw_buffer_ + read_offset_, left_half);
    std::memcpy(buffer + left_half, raw_buffer_, right_half);
    read_offset_ = right_half;
  }

  return num_bytes;
}

size_t RingBuffer::Write(uint8_t* buffer, size_t num_bytes) {
  num_bytes = std::min(num_bytes, write_size());
  if (!num_bytes) {
    return 0;
  }

  if (write_offset_ + num_bytes < size_) {
    std::memcpy(raw_buffer_ + write_offset_, buffer, num_bytes);
    write_offset_ += num_bytes;
  } else {
    size_t left_half = size_ - write_offset_;
    size_t right_half = size_ - left_half;
    std::memcpy(raw_buffer_ + write_offset_, buffer, left_half);
    std::memcpy(raw_buffer_, buffer + left_half, right_half);
    write_offset_ = right_half;
  }

  return num_bytes;
}

} // namespace xe
