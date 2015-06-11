/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TRACING_H_
#define XENIA_GPU_TRACING_H_

#include <string>

#include "xenia/memory.h"

namespace xe {
namespace gpu {

enum class TraceCommandType : uint32_t {
  kPrimaryBufferStart,
  kPrimaryBufferEnd,
  kIndirectBufferStart,
  kIndirectBufferEnd,
  kPacketStart,
  kPacketEnd,
  kMemoryRead,
  kMemoryWrite,
  kEvent,
};

struct PrimaryBufferStartCommand {
  TraceCommandType type;
  uint32_t base_ptr;
  uint32_t count;
};

struct PrimaryBufferEndCommand {
  TraceCommandType type;
};

struct IndirectBufferStartCommand {
  TraceCommandType type;
  uint32_t base_ptr;
  uint32_t count;
};

struct IndirectBufferEndCommand {
  TraceCommandType type;
};

struct PacketStartCommand {
  TraceCommandType type;
  uint32_t base_ptr;
  uint32_t count;
};

struct PacketEndCommand {
  TraceCommandType type;
};

struct MemoryReadCommand {
  TraceCommandType type;
  uint32_t base_ptr;
  uint32_t length;
};

struct MemoryWriteCommand {
  TraceCommandType type;
  uint32_t base_ptr;
  uint32_t length;
};

enum class EventType {
  kSwap,
};

struct EventCommand {
  TraceCommandType type;
  EventType event_type;
};

class TraceWriter {
 public:
  TraceWriter(uint8_t* membase);
  ~TraceWriter();

  bool is_open() const { return file_ != nullptr; }

  bool Open(const std::wstring& path);
  void Flush();
  void Close();

  void WritePrimaryBufferStart(uint32_t base_ptr, uint32_t count) {
    if (!file_) {
      return;
    }
    auto cmd = PrimaryBufferStartCommand({
        TraceCommandType::kPrimaryBufferStart, base_ptr, 0,
    });
    fwrite(&cmd, 1, sizeof(cmd), file_);
  }

  void WritePrimaryBufferEnd() {
    if (!file_) {
      return;
    }
    auto cmd = PrimaryBufferEndCommand({
        TraceCommandType::kPrimaryBufferEnd,
    });
    fwrite(&cmd, 1, sizeof(cmd), file_);
  }

  void WriteIndirectBufferStart(uint32_t base_ptr, uint32_t count) {
    if (!file_) {
      return;
    }
    auto cmd = IndirectBufferStartCommand({
        TraceCommandType::kIndirectBufferStart, base_ptr, 0,
    });
    fwrite(&cmd, 1, sizeof(cmd), file_);
  }

  void WriteIndirectBufferEnd() {
    if (!file_) {
      return;
    }
    auto cmd = IndirectBufferEndCommand({
        TraceCommandType::kIndirectBufferEnd,
    });
    fwrite(&cmd, 1, sizeof(cmd), file_);
  }

  void WritePacketStart(uint32_t base_ptr, uint32_t count) {
    if (!file_) {
      return;
    }
    auto cmd = PacketStartCommand({
        TraceCommandType::kPacketStart, base_ptr, count,
    });
    fwrite(&cmd, 1, sizeof(cmd), file_);
    fwrite(membase_ + base_ptr, 4, count, file_);
  }

  void WritePacketEnd() {
    if (!file_) {
      return;
    }
    auto cmd = PacketEndCommand({
        TraceCommandType::kPacketEnd,
    });
    fwrite(&cmd, 1, sizeof(cmd), file_);
  }

  void WriteMemoryRead(uint32_t base_ptr, size_t length) {
    if (!file_) {
      return;
    }
    auto cmd = MemoryReadCommand({
        TraceCommandType::kMemoryRead, base_ptr, uint32_t(length),
    });
    fwrite(&cmd, 1, sizeof(cmd), file_);
    fwrite(membase_ + base_ptr, 1, length, file_);
  }

  void WriteMemoryWrite(uint32_t base_ptr, size_t length) {
    if (!file_) {
      return;
    }
    auto cmd = MemoryWriteCommand({
        TraceCommandType::kMemoryWrite, base_ptr, uint32_t(length),
    });
    fwrite(&cmd, 1, sizeof(cmd), file_);
    fwrite(membase_ + base_ptr, 1, length, file_);
  }

  void WriteEvent(EventType event_type) {
    if (!file_) {
      return;
    }
    auto cmd = EventCommand({
        TraceCommandType::kEvent, event_type,
    });
    fwrite(&cmd, 1, sizeof(cmd), file_);
  }

 private:
  uint8_t* membase_;
  FILE* file_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACING_H_
