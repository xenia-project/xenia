/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_PROTO_VARINT_H_
#define XENIA_DEBUG_PROTO_VARINT_H_

#include <cstdint>

namespace xe {
namespace debug {
namespace proto {

struct varint_t {
  varint_t() = default;
  varint_t(uint64_t value) : value_(value) {}
  varint_t(const varint_t& other) : value_(other.value_) {}

  operator uint64_t() const { return value_; }
  uint64_t value() const { return value_; }

  size_t size() const {
    uint64_t value = value_;
    size_t i = 0;
    do {
      value >>= 7;
      ++i;
    } while (value >= 0x80);
    return i + 1;
  }

  const uint8_t* Read(const uint8_t* src) {
    if (!(src[0] & 0x80)) {
      value_ = src[0];
      return src + 1;
    }
    uint64_t r = src[0] & 0x7F;
    size_t i;
    for (i = 1; i < 10; ++i) {
      r |= (src[i] & 0x7F) << (7 * i);
      if (!(src[i] & 0x80)) {
        break;
      }
    }
    value_ = r;
    return src + i + 1;
  }

  uint8_t* Write(uint8_t* dest) {
    uint64_t value = value_;
    size_t i = 0;
    do {
      dest[i] = uint8_t(value | 0x80);
      value >>= 7;
      ++i;
    } while (value >= 0x80);
    dest[i] = uint8_t(value);
    return dest + i + 1;
  }

 private:
  uint64_t value_ = 0;
};

}  // namespace proto
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_PROTO_VARINT_H_
