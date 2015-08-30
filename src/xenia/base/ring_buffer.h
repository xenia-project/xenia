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

  uint8_t* buffer() const { return buffer_; }
  size_t capacity() const { return capacity_; }
  bool empty() const { return read_offset_ == write_offset_; }

  size_t read_offset() const { return read_offset_; }
  void set_read_offset(size_t offset) { read_offset_ = offset % capacity_; }
  size_t read_count() const {
    if (read_offset_ == write_offset_) {
      return 0;
    } else if (read_offset_ < write_offset_) {
      return write_offset_ - read_offset_;
    } else {
      return (capacity_ - read_offset_) + write_offset_;
    }
  }

  size_t write_offset() const { return write_offset_; }
  void set_write_offset(size_t offset) { write_offset_ = offset % capacity_; }
  size_t write_count() const {
    if (read_offset_ == write_offset_) {
      return capacity_;
    } else if (write_offset_ < read_offset_) {
      return read_offset_ - write_offset_;
    } else {
      return (capacity_ - write_offset_) + read_offset_;
    }
  }

  size_t Read(uint8_t* buffer, size_t count);
  template <typename T>
  size_t Read(T* buffer, size_t count) {
    return Read(reinterpret_cast<uint8_t*>(buffer), count);
  }

  size_t Write(const uint8_t* buffer, size_t count);
  template <typename T>
  size_t Write(const T* buffer, size_t count) {
    return Write(reinterpret_cast<const uint8_t*>(buffer), count);
  }

 private:
  uint8_t* buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t read_offset_ = 0;
  size_t write_offset_ = 0;
};

}  // namespace xe

#endif  // XENIA_BASE_RING_BUFFER_H_
