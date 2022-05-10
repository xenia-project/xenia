/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TRACE_READER_H_
#define XENIA_GPU_TRACE_READER_H_

#include <string>
#include <vector>

#include "xenia/base/mapped_memory.h"
#include "xenia/gpu/trace_protocol.h"
#include "xenia/memory.h"

namespace xe {
namespace gpu {

class TraceReader {
 public:
  struct CommandBuffer {
    struct Command {
      enum class Type {
        kCommand,
        kBuffer,
      };

      Command() {}
      Command(Command&& other) {
        type = other.type;
        command_id = other.command_id;
        command_subtree = std::move(other.command_subtree);
      }
      Command(CommandBuffer* buf) {
        type = Type::kBuffer;
        command_subtree = std::unique_ptr<CommandBuffer>(buf);
      }
      Command(uint32_t id) {
        type = Type::kCommand;
        command_id = id;
      }
      ~Command() = default;

      Type type;
      uint32_t command_id = -1;
      std::unique_ptr<CommandBuffer> command_subtree = nullptr;
    };

    CommandBuffer() {}
    ~CommandBuffer() {}

    // Parent command buffer, if one exists.
    CommandBuffer* parent = nullptr;
    std::vector<Command> commands;
  };

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

    // Flat list of all commands in this frame.
    std::vector<Command> commands;

    // Tree of all command buffers
    std::unique_ptr<CommandBuffer> command_tree;
  };

  TraceReader() = default;
  virtual ~TraceReader() = default;

  const TraceHeader* header() const {
    return reinterpret_cast<const TraceHeader*>(trace_data_);
  }

  const Frame* frame(int n) const { return &frames_[n]; }
  int frame_count() const { return int(frames_.size()); }

  bool Open(const std::filesystem::path& path);

  void Close();

 protected:
  void ParseTrace();
  bool DecompressMemory(MemoryEncodingFormat encoding_format, const void* src,
                        size_t src_size, void* dest, size_t dest_size);

  std::unique_ptr<MappedMemory> mmap_;
  const uint8_t* trace_data_ = nullptr;
  size_t trace_size_ = 0;
  std::vector<Frame> frames_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACE_READER_H_
