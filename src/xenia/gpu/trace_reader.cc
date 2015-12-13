/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/trace_reader.h"

#include "xenia/base/mapped_memory.h"
#include "xenia/gpu/packet_disassembler.h"
#include "xenia/gpu/trace_protocol.h"
#include "xenia/memory.h"

#include "third_party/zlib/zlib.h"

namespace xe {
namespace gpu {

bool TraceReader::Open(const std::wstring& path) {
  Close();

  mmap_ = MappedMemory::Open(path, MappedMemory::Mode::kRead);
  if (!mmap_) {
    return false;
  }

  trace_data_ = reinterpret_cast<const uint8_t*>(mmap_->data());
  trace_size_ = mmap_->size();

  ParseTrace();

  return true;
}

void TraceReader::Close() {
  mmap_.reset();
  trace_data_ = nullptr;
  trace_size_ = 0;
}

void TraceReader::ParseTrace() {
  auto trace_ptr = trace_data_;
  Frame current_frame;
  current_frame.start_ptr = trace_ptr;
  const PacketStartCommand* packet_start = nullptr;
  const uint8_t* packet_start_ptr = nullptr;
  const uint8_t* last_ptr = trace_ptr;
  bool pending_break = false;
  while (trace_ptr < trace_data_ + trace_size_) {
    ++current_frame.command_count;
    auto type = static_cast<TraceCommandType>(xe::load<uint32_t>(trace_ptr));
    switch (type) {
      case TraceCommandType::kPrimaryBufferStart: {
        auto cmd =
            reinterpret_cast<const PrimaryBufferStartCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        break;
      }
      case TraceCommandType::kPrimaryBufferEnd: {
        auto cmd = reinterpret_cast<const PrimaryBufferEndCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        break;
      }
      case TraceCommandType::kIndirectBufferStart: {
        auto cmd =
            reinterpret_cast<const IndirectBufferStartCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        break;
      }
      case TraceCommandType::kIndirectBufferEnd: {
        auto cmd = reinterpret_cast<const IndirectBufferEndCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        break;
      }
      case TraceCommandType::kPacketStart: {
        auto cmd = reinterpret_cast<const PacketStartCommand*>(trace_ptr);
        packet_start_ptr = trace_ptr;
        packet_start = cmd;
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        break;
      }
      case TraceCommandType::kPacketEnd: {
        auto cmd = reinterpret_cast<const PacketEndCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        if (!packet_start_ptr) {
          continue;
        }
        auto packet_category = PacketDisassembler::GetPacketCategory(
            packet_start_ptr + sizeof(*packet_start));
        switch (packet_category) {
          case PacketCategory::kDraw: {
            Frame::Command command;
            command.type = Frame::Command::Type::kDraw;
            command.head_ptr = packet_start_ptr;
            command.start_ptr = last_ptr;
            command.end_ptr = trace_ptr;
            current_frame.commands.push_back(std::move(command));
            last_ptr = trace_ptr;
            break;
          }
          case PacketCategory::kSwap: {
            //
            break;
          }
        }
        if (pending_break) {
          current_frame.end_ptr = trace_ptr;
          frames_.push_back(std::move(current_frame));
          current_frame.start_ptr = trace_ptr;
          current_frame.end_ptr = nullptr;
          current_frame.command_count = 0;
          pending_break = false;
        }
        break;
      }
      case TraceCommandType::kMemoryRead: {
        auto cmd = reinterpret_cast<const MemoryReadCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd) + cmd->length;
        break;
      }
      case TraceCommandType::kMemoryWrite: {
        auto cmd = reinterpret_cast<const MemoryWriteCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd) + cmd->length;
        break;
      }
      case TraceCommandType::kEvent: {
        auto cmd = reinterpret_cast<const EventCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        switch (cmd->event_type) {
          case EventType::kSwap: {
            pending_break = true;
            break;
          }
        }
        break;
      }
      default:
        // Broken trace file?
        assert_unhandled_case(type);
        break;
    }
  }
  if (pending_break || current_frame.command_count) {
    current_frame.end_ptr = trace_ptr;
    frames_.push_back(std::move(current_frame));
  }
}

bool TraceReader::DecompressMemory(const uint8_t* src, size_t src_size,
                                   uint8_t* dest, size_t dest_size) {
  uLongf dest_len = uint32_t(dest_size);
  int ret = uncompress(dest, &dest_len, src, uint32_t(src_size));
  assert_true(ret >= 0);
  return ret >= 0;
}

}  // namespace gpu
}  // namespace xe
