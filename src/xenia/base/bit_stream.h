/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_BIT_STREAM_H_
#define XENIA_BASE_BIT_STREAM_H_

#include <cstddef>
#include <cstdint>

namespace xe {

class BitStream {
 public:
  BitStream(uint8_t* buffer, size_t size_in_bits);
  ~BitStream();

  const uint8_t* buffer() const { return buffer_; }
  uint8_t* buffer() { return buffer_; }
  size_t offset_bits() const { return offset_bits_; }
  size_t size_bits() const { return size_bits_; }

  void Advance(size_t num_bits);
  void SetOffset(size_t offset_bits);
  size_t BitsRemaining();
  bool IsOffsetValid(size_t num_bits);

  // Note: num_bits MUST be in the range 0-57 (inclusive)
  uint64_t Peek(size_t num_bits);
  uint64_t Read(size_t num_bits);
  bool Write(uint64_t val, size_t num_bits);  // TODO(DrChat): Not tested!

  size_t Copy(uint8_t* dest_buffer, size_t num_bits);

 private:
  uint8_t* buffer_ = nullptr;
  size_t offset_bits_ = 0;
  size_t size_bits_ = 0;
};

}  // namespace xe

#endif  // XENIA_BASE_BIT_STREAM_H_
