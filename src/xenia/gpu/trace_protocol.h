/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TRACE_PROTOCOL_H_
#define XENIA_GPU_TRACE_PROTOCOL_H_

#include <cstdint>

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
  uint32_t full_length;  // Length after inflation. 0 if not deflated.
};

struct MemoryWriteCommand {
  TraceCommandType type;
  uint32_t base_ptr;
  uint32_t length;
  uint32_t full_length;  // Length after inflation. 0 if not deflated.
};

enum class EventType {
  kSwap,
};

struct EventCommand {
  TraceCommandType type;
  EventType event_type;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACE_PROTOCOL_H_
