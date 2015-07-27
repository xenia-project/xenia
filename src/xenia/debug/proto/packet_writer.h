/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_PROTO_PACKET_WRITER_H_
#define XENIA_DEBUG_PROTO_PACKET_WRITER_H_

#include <algorithm>
#include <string>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/debug/proto/varint.h"
#include "xenia/debug/proto/xdp_protocol.h"

namespace xe {
namespace debug {
namespace proto {

class PacketWriter {
 public:
  PacketWriter(size_t buffer_capacity) : buffer_(buffer_capacity) {}

  uint8_t* buffer() { return buffer_.data(); }
  size_t buffer_capacity() const { return buffer_.size(); }
  size_t buffer_offset() const { return buffer_offset_; }

  void Reset() { buffer_offset_ = 0; }

  void Begin(PacketType packet_type, request_id_t request_id = 0) {
    assert_null(current_packet_);
    current_packet_ = Append<Packet>();
    current_packet_->packet_type = packet_type;
    current_packet_->request_id = request_id;
    current_packet_->body_length = 0;
  }

  void End() {
    assert_not_null(current_packet_);
    current_packet_->body_length =
        uint32_t((buffer_.data() + buffer_offset_) -
                 reinterpret_cast<uint8_t*>(current_packet_) - sizeof(Packet));
    current_packet_ = nullptr;
  }

  uint8_t* Append(size_t length) {
    if (buffer_offset_ + length > buffer_.size()) {
      size_t new_size = std::max(
          xe::round_up(buffer_offset_ + length, 16 * 1024), buffer_.size() * 2);
      buffer_.resize(new_size);
    }
    auto ptr = buffer_.data() + buffer_offset_;
    buffer_offset_ += length;
    return ptr;
  }

  template <typename T>
  T* Append() {
    return reinterpret_cast<T*>(Append(sizeof(T)));
  }

  void Append(const void* buffer, size_t buffer_length) {
    auto dest = Append(buffer_length);
    std::memcpy(dest, buffer, buffer_length);
  }

  void AppendString(const char* value) {
    size_t value_length = std::strlen(value);
    varint_t length(value_length);
    auto dest = Append(length.size() + value_length);
    dest = length.Write(dest);
    std::memcpy(dest, value, value_length);
  }
  void AppendString(const std::string& value) {
    varint_t length(value.size());
    auto dest = Append(length.size() + value.size());
    dest = length.Write(dest);
    std::memcpy(dest, value.data(), value.size());
  }

 private:
  std::vector<uint8_t> buffer_;
  size_t buffer_offset_ = 0;

  Packet* current_packet_ = nullptr;
};

}  // namespace proto
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_PROTO_PACKET_WRITER_H_
