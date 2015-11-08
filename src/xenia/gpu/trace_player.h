/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TRACE_PLAYER_H_
#define XENIA_GPU_TRACE_PLAYER_H_

#include <string>

#include "xenia/gpu/trace_protocol.h"
#include "xenia/gpu/trace_reader.h"
#include "xenia/ui/loop.h"

namespace xe {
namespace gpu {

class GraphicsSystem;

enum class TracePlaybackMode {
  kUntilEnd,
  kBreakOnSwap,
};

class TracePlayer : public TraceReader {
 public:
  TracePlayer(xe::ui::Loop* loop, GraphicsSystem* graphics_system);
  ~TracePlayer() override;

  GraphicsSystem* graphics_system() const { return graphics_system_; }
  int current_frame_index() const { return current_frame_index_; }
  int current_command_index() const { return current_command_index_; }
  const Frame* current_frame() const;

  void SeekFrame(int target_frame);
  void SeekCommand(int target_command);

 private:
  void PlayTrace(const uint8_t* trace_data, size_t trace_size,
                 TracePlaybackMode playback_mode);
  void PlayTraceOnThread(const uint8_t* trace_data, size_t trace_size,
                         TracePlaybackMode playback_mode);

  xe::ui::Loop* loop_;
  GraphicsSystem* graphics_system_;
  int current_frame_index_;
  int current_command_index_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACE_PLAYER_H_
