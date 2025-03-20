/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TRACE_PROTOCOL_H_
#define XENIA_GPU_TRACE_PROTOCOL_H_

#include <cstdint>

namespace xe {
namespace gpu {

// Trace file extension.
static constexpr char kTraceExtension[] = "xtr";

// Any byte changes to the files should bump this version.
// Only builds with matching versions will work.
// Other changes besides the file format may require bumps, such as
// anything that changes what is recorded into the files (new GPU
// command processor commands, etc).
constexpr uint32_t kTraceFormatVersion = 1;

// Trace file header identifying information about the trace.
// This must be positioned at the start of the file and must only occur once.
struct TraceHeader {
  // Must be the first 4 bytes of the file.
  // Set to kTraceFormatVersion.
  uint32_t version;

  // SHA1 of the commit used to record the trace.
  char build_commit_sha[40];

  // Title ID of game that was being recorded.
  // May be 0 if not generated from a game or the ID could not be retrieved.
  uint32_t title_id;
};

// Tags each command in the trace file stream as one of the *Command types.
// Each command has this value as its first dword.
enum class TraceCommandType : uint32_t {
  kPrimaryBufferStart,
  kPrimaryBufferEnd,
  kIndirectBufferStart,
  kIndirectBufferEnd,
  kPacketStart,
  kPacketEnd,
  kMemoryRead,
  kMemoryWrite,
  kEdramSnapshot,
  kEvent,
  kRegisters,
  kGammaRamp,
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

// The compression format used for memory read/write buffers.
// Note that not every memory read/write will have compressed data
// (as it's silly to compress 4 byte buffers).
enum class MemoryEncodingFormat {
  // Data is in its raw form. encoded_length == decoded_length.
  kNone,
  // Data is compressed with third_party/snappy.
  kSnappy,
};

// Represents the GPU reading or writing data from or to memory.
// Used for both TraceCommandType::kMemoryRead and kMemoryWrite.
struct MemoryCommand {
  TraceCommandType type;

  // Base physical memory pointer this read starts at.
  uint32_t base_ptr;
  // Encoding format of the data in the trace file.
  MemoryEncodingFormat encoding_format;
  // Number of bytes the data occupies in the trace file in its encoded form.
  uint32_t encoded_length;
  // Number of bytes the data occupies in memory after decoding.
  // Note that if no encoding is used this will equal encoded_length.
  uint32_t decoded_length;
};

// Represents a full 10 MB snapshot of EDRAM contents, for trace initialization
// (since replaying the trace will reconstruct its state at any point later) as
// a sequence of tiles with row-major samples (2x multisampling as 1x2 samples,
// 4x as 2x2 samples).
struct EdramSnapshotCommand {
  TraceCommandType type;
  // Encoding format of the data in the trace file.
  MemoryEncodingFormat encoding_format;
  // Number of bytes the data occupies in the trace file in its encoded form.
  uint32_t encoded_length;
};

// Represents a GPU event of EventCommand::Type.
struct EventCommand {
  TraceCommandType type;

  // Identifies the event that occurred.
  enum class Type {
    kSwap,
  };
  Type event_type;
};

// Represents a range of registers.
struct RegistersCommand {
  TraceCommandType type;

  uint32_t first_register;
  uint32_t register_count;
  // Whether to set the registers via WriteRegister, which may have side
  // effects, rather than by copying them directly to the register file.
  bool execute_callbacks;

  // Encoding format of the values in the trace file.
  MemoryEncodingFormat encoding_format;
  // Number of bytes the values occupy in the trace file in their encoded form.
  // If no encoding is used, this will be sizeof(uint32_t) * register_count.
  uint32_t encoded_length;
};

// Represents a gamma ramp - encoded 256 DC_LUT_30_COLOR values and 128
// interleaved RGB DC_LUT_PWL_DATA values.
// Assuming that all other gamma ramp state is saved as plain registers.
struct GammaRampCommand {
  TraceCommandType type;

  // The component index (0 = red, 1 = green, 2 = blue) for the next
  // DC_LUT_SEQ_COLOR or DC_LUT_PWL_DATA read or write.
  uint8_t rw_component;

  // Encoding format of the ramps in the trace file.
  MemoryEncodingFormat encoding_format;
  // Number of bytes the ramps occupy in the trace file in their encoded form.
  // If no encoding is used, this will be sizeof(uint32_t) * (256 + 3 * 128).
  uint32_t encoded_length;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACE_PROTOCOL_H_
