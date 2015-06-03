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
    RingBuffer(uint8_t *raw_buffer, size_t size, size_t write_offset = 0);

    size_t Write(uint8_t *buffer, size_t num_bytes);

    size_t DistanceToOffset(size_t offset);

    void set_write_offset(size_t write_offset) { write_offset_ = write_offset; }
    size_t write_offset() { return write_offset_; }

  private:
    uint8_t *raw_buffer_;
    size_t size_;
    size_t write_offset_;
};

} // namespace xe

#endif // XENIA_BASE_RING_BUFFER_H_