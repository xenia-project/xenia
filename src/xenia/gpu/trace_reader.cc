/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/trace_reader.h"

#include <cinttypes>

#include "third_party/snappy/snappy.h"

#include "xenia/base/logging.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/base/math.h"
#include "xenia/gpu/packet_disassembler.h"
#include "xenia/gpu/trace_protocol.h"
#include "xenia/memory.h"

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

  // Verify version.
  auto header = reinterpret_cast<const TraceHeader*>(trace_data_);
  if (header->version != kTraceFormatVersion) {
    XELOGE("Trace format version mismatch, code has %u, file has %u",
           kTraceFormatVersion, header->version);
    if (header->version < kTraceFormatVersion) {
      XELOGE("You need to regenerate your trace for the latest version");
    }
    return false;
  }

  auto path_str = xe::to_string(path);
  XELOGI("Mapped %" PRId64 "b trace from %s", trace_size_, path_str.c_str());
  XELOGI("   Version: %u", header->version);
  auto commit_str = std::string(header->build_commit_sha,
                                xe::countof(header->build_commit_sha));
  XELOGI("    Commit: %s", commit_str.c_str());
  XELOGI("  Title ID: %u", header->title_id);

  ParseTrace();

  return true;
}

void TraceReader::Close() {
  mmap_.reset();
  trace_data_ = nullptr;
  trace_size_ = 0;
}

void TraceReader::ParseTrace() {
  // Skip file header.
  auto trace_ptr = trace_data_;
  trace_ptr += sizeof(TraceHeader);

  Frame current_frame;
  current_frame.start_ptr = trace_ptr;
  const PacketStartCommand* packet_start = nullptr;
  const uint8_t* packet_start_ptr = nullptr;
  const uint8_t* last_ptr = trace_ptr;
  bool pending_break = false;
  auto current_command_buffer = new CommandBuffer();
  current_frame.command_tree =
      std::unique_ptr<CommandBuffer>(current_command_buffer);

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

        // Traverse down a level.
        auto sub_command_buffer = new CommandBuffer();
        sub_command_buffer->parent = current_command_buffer;
        current_command_buffer->commands.push_back(
            CommandBuffer::Command(sub_command_buffer));
        current_command_buffer = sub_command_buffer;
        break;
      }
      case TraceCommandType::kIndirectBufferEnd: {
        auto cmd = reinterpret_cast<const IndirectBufferEndCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);

        // IB packet is wrapped in a kPacketStart/kPacketEnd. Skip the end.
        auto end_cmd = reinterpret_cast<const PacketEndCommand*>(trace_ptr);
        assert_true(end_cmd->type == TraceCommandType::kPacketEnd);
        trace_ptr += sizeof(*cmd);

        // Go back up a level. If parent is null, this frame started in an
        // indirect buffer.
        if (current_command_buffer->parent) {
          current_command_buffer = current_command_buffer->parent;
        }
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
            current_command_buffer->commands.push_back(CommandBuffer::Command(
                uint32_t(current_frame.commands.size() - 1)));
            break;
          }
          case PacketCategory::kSwap: {
            Frame::Command command;
            command.type = Frame::Command::Type::kSwap;
            command.head_ptr = packet_start_ptr;
            command.start_ptr = last_ptr;
            command.end_ptr = trace_ptr;
            current_frame.commands.push_back(std::move(command));
            last_ptr = trace_ptr;
            current_command_buffer->commands.push_back(CommandBuffer::Command(
                uint32_t(current_frame.commands.size() - 1)));
          } break;
          case PacketCategory::kGeneric: {
            // Ignored.
            break;
          }
        }
        if (pending_break) {
          current_frame.end_ptr = trace_ptr;
          frames_.push_back(std::move(current_frame));
          current_command_buffer = new CommandBuffer();
          current_frame.command_tree =
              std::unique_ptr<CommandBuffer>(current_command_buffer);
          current_frame.start_ptr = trace_ptr;
          current_frame.end_ptr = nullptr;
          current_frame.command_count = 0;
          pending_break = false;
        }
        break;
      }
      case TraceCommandType::kMemoryRead: {
        auto cmd = reinterpret_cast<const MemoryCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd) + cmd->encoded_length;
        break;
      }
      case TraceCommandType::kMemoryWrite: {
        auto cmd = reinterpret_cast<const MemoryCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd) + cmd->encoded_length;
        break;
      }
      case TraceCommandType::kEvent: {
        auto cmd = reinterpret_cast<const EventCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        switch (cmd->event_type) {
          case EventCommand::Type::kSwap: {
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

bool TraceReader::DecompressMemory(MemoryEncodingFormat encoding_format,
                                   const uint8_t* src, size_t src_size,
                                   uint8_t* dest, size_t dest_size) {
  switch (encoding_format) {
    case MemoryEncodingFormat::kNone:
      assert_true(src_size == dest_size);
      std::memcpy(dest, src, src_size);
      return true;
    case MemoryEncodingFormat::kSnappy:
      return snappy::RawUncompress(reinterpret_cast<const char*>(src), src_size,
                                   reinterpret_cast<char*>(dest));
    default:
      assert_unhandled_case(encoding_format);
      return false;
  }
}

}  // namespace gpu
}  // namespace xe
