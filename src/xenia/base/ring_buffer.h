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
    RingBuffer(uint8_t* raw_buffer, size_t size, size_t read_offset, size_t write_offset);

    size_t Read(uint8_t* buffer, size_t num_bytes);
    size_t Skip(size_t num_bytes);
    size_t Write(uint8_t* buffer, size_t num_bytes);

    size_t read_offset() { return read_offset_; }
    size_t write_offset() { return write_offset_; }

    size_t read_size() {
      if (read_offset_ == write_offset_) {
        return 0;
      }
      if (read_offset_ < write_offset_) {
        return write_offset_ - read_offset_;
      }
      return (size_ - read_offset_) + write_offset_;
    }

    size_t write_size() {
      if (write_offset_ == read_offset_) {
        return size_;
      }
      if (write_offset_ < read_offset_) {
        return read_offset_ - write_offset_;
      }
      return (size_ - write_offset_) + read_offset_;
    }

  private:
    uint8_t* raw_buffer_;
    size_t size_;
    size_t read_offset_;
    size_t write_offset_;
};

} // namespace xe

#endif // XENIA_BASE_RING_BUFFER_H_