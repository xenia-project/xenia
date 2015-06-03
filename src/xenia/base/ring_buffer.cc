/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "xenia/base/ring_buffer.h"

namespace xe {

RingBuffer::RingBuffer(uint8_t *raw_buffer, size_t size, size_t write_offset)
    : raw_buffer_(raw_buffer),
      size_(size),
      write_offset_(write_offset) {}

size_t RingBuffer::Write(uint8_t *buffer, size_t num_bytes) {
  size_t bytes_written = 0;
  size_t input_offset = 0;
  size_t bytes_to_write = 0;

  // write offset -> end
  bytes_to_write =
        num_bytes < size_ - write_offset_ ? num_bytes : size_ - write_offset_;

  std::memcpy(raw_buffer_ + write_offset_, buffer, bytes_to_write);
  input_offset = bytes_to_write;
  write_offset_ += bytes_to_write;

  bytes_written += bytes_to_write;

  // Wraparound (begin -> num_bytes)
  if (input_offset < num_bytes) {
    bytes_to_write = num_bytes - input_offset;

    std::memcpy(raw_buffer_, buffer + input_offset, bytes_to_write);
    write_offset_ = bytes_to_write;

    bytes_written += bytes_to_write;
  }

  return bytes_written;
}

size_t RingBuffer::DistanceToOffset(size_t offset) {
  // Make sure the offset is in range.
  if (offset > size_) {
    offset %= size_;
  }

  if (offset < size_ && offset >= write_offset_) {
    // Doesn't wraparound.
    return offset - write_offset_;
  } else {
    // Wraparound.
    size_t dist = size_ - write_offset_;
    dist += offset;

    return dist;
  }
}

} // namespace xe