/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TRACE_PLAYER_H_
#define XENIA_GPU_TRACE_PLAYER_H_

#include <atomic>
#include <string>

#include "xenia/base/threading.h"
#include "xenia/gpu/trace_protocol.h"
#include "xenia/gpu/trace_reader.h"

namespace xe {
namespace gpu {

class GraphicsSystem;

enum class TracePlaybackMode {
  kUntilEnd,
  kBreakOnSwap,
};

class TracePlayer : public TraceReader {
 public:
  TracePlayer(GraphicsSystem* graphics_system);

  GraphicsSystem* graphics_system() const { return graphics_system_; }
  void SetPresentLastCopy(bool present_last_copy) {
    present_last_copy_ = present_last_copy;
  }
  int current_frame_index() const { return current_frame_index_; }
  int current_command_index() const { return current_command_index_; }
  bool is_playing_trace() const { return playing_trace_; }
  const Frame* current_frame() const;

  // Only valid if playing_trace is true.
  // Scalar from 0-10000
  uint32_t playback_percent() const { return playback_percent_; }

  void SeekFrame(int target_frame);
  void SeekCommand(int target_command);

  void WaitOnPlayback();

 private:
  void PlayTrace(const uint8_t* trace_data, size_t trace_size,
                 TracePlaybackMode playback_mode, bool clear_caches);
  void PlayTraceOnThread(const uint8_t* trace_data, size_t trace_size,
                         TracePlaybackMode playback_mode, bool clear_caches,
                         bool present_last_copy);

  GraphicsSystem* graphics_system_;
  // Whether to present the results of the latest resolve instead of displaying
  // the front buffer from the trace.
  bool present_last_copy_ = false;
  int current_frame_index_;
  int current_command_index_;
  bool playing_trace_ = false;
  std::atomic<uint32_t> playback_percent_ = {0};
  std::unique_ptr<xe::threading::Event> playback_event_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACE_PLAYER_H_
