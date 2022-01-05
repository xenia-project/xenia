/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/byte_stream.h"

#include <cstring>

#include "xenia/base/assert.h"

namespace xe {

ByteStream::ByteStream(uint8_t* data, size_t data_length, size_t offset)
    : data_(data), data_length_(data_length), offset_(offset) {}

ByteStream::~ByteStream() = default;

void ByteStream::Advance(size_t num_bytes) {
  assert_true(offset_ + num_bytes <= data_length_);
  offset_ += num_bytes;
}

void ByteStream::Read(uint8_t* buf, size_t len) {
  assert_true(offset_ + len <= data_length_);
  std::memcpy(buf, data_ + offset_, len);
  Advance(len);
}

void ByteStream::Write(const uint8_t* buf, size_t len) {
  assert_true(offset_ + len <= data_length_);
  std::memcpy(data_ + offset_, buf, len);
  Advance(len);
}

template <>
std::string ByteStream::Read() {
  std::string str;
  uint32_t len = Read<uint32_t>();
  str.resize(len);
  Read(reinterpret_cast<uint8_t*>(&str[0]), len);
  return str;
}

template <>
std::u16string ByteStream::Read() {
  std::u16string str;
  size_t len = Read<uint32_t>();
  str.resize(len);
  Read(reinterpret_cast<uint8_t*>(&str[0]), len * 2);
  return str;
}

}  // namespace xe
