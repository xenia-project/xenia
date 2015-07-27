/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_PROTO_PACKET_READER_H_
#define XENIA_DEBUG_PROTO_PACKET_READER_H_

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/debug/proto/varint.h"
#include "xenia/debug/proto/xdp_protocol.h"

namespace xe {
namespace debug {
namespace proto {

class PacketReader {
 public:
  PacketReader(size_t buffer_capacity) : buffer_(buffer_capacity) {}

  const uint8_t* buffer() { return buffer_.data(); }
  size_t buffer_capacity() const { return buffer_.size(); }
  size_t buffer_offset() const { return buffer_offset_; }

  void Reset() {
    buffer_offset_ = 0;
    buffer_size_ = 0;
  }

  void AppendBuffer(const uint8_t* buffer, size_t buffer_length) {
    if (buffer_size_ + buffer_length > buffer_.size()) {
      size_t new_size =
          std::max(buffer_.size() * 2,
                   xe::round_up(buffer_size_ + buffer_length, 16 * 1024));
      buffer_.resize(new_size);
    }
    std::memcpy(buffer_.data() + buffer_size_, buffer, buffer_length);
    buffer_size_ += buffer_length;
  }

  void Compact() {
    assert_null(current_packet_);
    if (!buffer_offset_) {
      return;
    }
    std::memmove(buffer_.data(), buffer_.data() + buffer_offset_,
                 buffer_size_ - buffer_offset_);
    buffer_size_ -= buffer_offset_;
    buffer_offset_ = 0;
  }

  const Packet* Begin() {
    assert_null(current_packet_);
    size_t bytes_remaining = buffer_size_ - buffer_offset_;
    if (bytes_remaining < sizeof(Packet)) {
      return nullptr;
    }
    auto packet =
        reinterpret_cast<const Packet*>(buffer_.data() + buffer_offset_);
    if (bytes_remaining < sizeof(Packet) + packet->body_length) {
      return nullptr;
    }
    packet_offset_ = buffer_offset_ + sizeof(Packet);
    buffer_offset_ += sizeof(Packet) + packet->body_length;
    current_packet_ = packet;
    return packet;
  }

  void End() {
    assert_not_null(current_packet_);
    current_packet_ = nullptr;
    packet_offset_ = 0;
  }

  const uint8_t* Read(size_t length) {
    // Can't read into next packet/off of end.
    assert_not_null(current_packet_);
    assert_true(packet_offset_ + length <= buffer_offset_);
    auto ptr = buffer_.data() + packet_offset_;
    packet_offset_ += length;
    return ptr;
  }

  template <typename T>
  const T* Read() {
    return reinterpret_cast<const T*>(Read(sizeof(T)));
  }

  template <typename T>
  std::vector<const T*> ReadArray(size_t count) {
    std::vector<const T*> entries;
    for (size_t i = 0; i < count; ++i) {
      entries.push_back(Read<T>());
    }
    return entries;
  }

  void Read(void* buffer, size_t buffer_length) {
    assert_not_null(current_packet_);
    auto src = Read(buffer_length);
    std::memcpy(buffer, src, buffer_length);
  }

  void ReadString(char* buffer, size_t buffer_length) {
    assert_not_null(current_packet_);
    varint_t length;
    auto src = length.Read(buffer_.data() + packet_offset_);
    assert_not_null(src);
    assert_true(length < buffer_length);
    assert_true(length.size() + length < (buffer_offset_ - packet_offset_));
    std::memcpy(buffer, src, length);
    packet_offset_ += length;
  }

  std::string ReadString() {
    assert_not_null(current_packet_);
    varint_t length;
    auto src = length.Read(buffer_.data() + packet_offset_);
    assert_not_null(src);
    assert_true(length.size() + length < (buffer_offset_ - packet_offset_));
    std::string value;
    value.resize(length.value());
    std::memcpy(const_cast<char*>(value.data()), src, length);
    packet_offset_ += length;
    return value;
  }

 private:
  std::vector<uint8_t> buffer_;
  size_t buffer_offset_ = 0;
  size_t buffer_size_ = 0;

  const Packet* current_packet_ = nullptr;
  size_t packet_offset_ = 0;
};

}  // namespace proto
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_PROTO_PACKET_READER_H_
