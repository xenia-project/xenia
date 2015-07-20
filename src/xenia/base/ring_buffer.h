/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_RING_BUFFER_H_
#define XENIA_BASE_RING_BUFFER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace xe {

class RingBuffer {
 public:
  RingBuffer(uint8_t* buffer, size_t capacity);

  size_t Read(uint8_t* buffer, size_t count);
  size_t Write(uint8_t* buffer, size_t count);

  uint8_t* buffer() { return buffer_; }
  size_t capacity() { return capacity_; }

  size_t read_offset() { return read_offset_; }
  size_t read_count() {
    if (read_offset_ == write_offset_) {
      return 0;
    } else if (read_offset_ < write_offset_) {
      return write_offset_ - read_offset_;
    } else {
      return (capacity_ - read_offset_) + write_offset_;
    }
  }

  size_t write_offset() { return write_offset_; }
  size_t write_count() {
    if (read_offset_ == write_offset_) {
      return capacity_;
    } else if (write_offset_ < read_offset_) {
      return read_offset_ - write_offset_;
    } else {
      return (capacity_ - write_offset_) + read_offset_;
    }
  }

  void set_read_offset(size_t offset) { read_offset_ = offset % capacity_; }

  void set_write_offset(size_t offset) { write_offset_ = offset % capacity_; }

 private:
  uint8_t* buffer_;
  size_t capacity_;
  size_t read_offset_;
  size_t write_offset_;
};

}  // namespace xe

#endif  // XENIA_BASE_RING_BUFFER_H_