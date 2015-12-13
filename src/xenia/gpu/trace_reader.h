/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TRACE_READER_H_
#define XENIA_GPU_TRACE_READER_H_

#include <string>

#include "xenia/base/mapped_memory.h"
#include "xenia/gpu/trace_protocol.h"
#include "xenia/memory.h"

namespace xe {
namespace gpu {

// void Foo() {
//  auto trace_ptr = trace_data;
//  while (trace_ptr < trace_data + trace_size) {
//    auto cmd_type = *reinterpret_cast<const TraceCommandType*>(trace_ptr);
//    switch (cmd_type) {
//      case TraceCommandType::kPrimaryBufferStart:
//        break;
//      case TraceCommandType::kPrimaryBufferEnd:
//        break;
//      case TraceCommandType::kIndirectBufferStart:
//        break;
//      case TraceCommandType::kIndirectBufferEnd:
//        break;
//      case TraceCommandType::kPacketStart:
//        break;
//      case TraceCommandType::kPacketEnd:
//        break;
//      case TraceCommandType::kMemoryRead:
//        break;
//      case TraceCommandType::kMemoryWrite:
//        break;
//      case TraceCommandType::kEvent:
//        break;
//    }
//    /*trace_ptr = graphics_system->PlayTrace(
//    trace_ptr, trace_size - (trace_ptr - trace_data),
//    GraphicsSystem::TracePlaybackMode::kBreakOnSwap);*/
//  }
//}

class TraceReader {
 public:
  struct Frame {
    struct Command {
      enum class Type {
        kDraw,
        kSwap,
      };
      const uint8_t* head_ptr;
      const uint8_t* start_ptr;
      const uint8_t* end_ptr;
      Type type;
      union {
        struct {
          //
        } draw;
        struct {
          //
        } swap;
      };
    };

    const uint8_t* start_ptr = nullptr;
    const uint8_t* end_ptr = nullptr;
    int command_count = 0;
    std::vector<Command> commands;
  };

  TraceReader() = default;
  virtual ~TraceReader() = default;

  const Frame* frame(int n) const { return &frames_[n]; }
  int frame_count() const { return int(frames_.size()); }

  bool Open(const std::wstring& path);

  void Close();

 protected:
  void ParseTrace();
  bool DecompressMemory(const uint8_t* src, size_t src_size, uint8_t* dest,
                        size_t dest_size);

  std::unique_ptr<MappedMemory> mmap_;
  const uint8_t* trace_data_ = nullptr;
  size_t trace_size_ = 0;
  std::vector<Frame> frames_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACE_READER_H_
