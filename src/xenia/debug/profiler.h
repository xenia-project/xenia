/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_PROFILER_H_
#define XENIA_DEBUG_PROFILER_H_

#include <cstdint>
#include <map>

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/threading.h"

namespace xe {
namespace kernel {
class XThread;
}

namespace cpu {
class Function;
}

namespace debug {
class Debugger;

class Profiler {
 public:
  Profiler(Debugger* debugger);

  // Time elapsed between profiling start and stop (or now)
  uint64_t GetTicksElapsed() { return timer_.GetTicksElapsed(); }
  double GetSecondsElapsed() { return timer_.GetSecondsElapsed(); }

  void Start();
  void Stop();

  // Internal - do not use.
  void OnThreadsChanged();

  struct CallStack {
    uint64_t host_frames[128];
    size_t depth;

    bool operator<(const CallStack& other) const {
      if (depth != other.depth) {
        return depth < other.depth;
      }

      for (size_t i = 0; i < depth; i++) {
        if (host_frames[i] != other.host_frames[i]) {
          return host_frames[i] < other.host_frames[i];
        }
      }

      return false;
    }
  };

  bool is_profiling() const { return is_profiling_; }
  std::map<std::pair<uint64_t, CallStack>, uint64_t>& GetSamplesPerThread() {
    assert_false(is_profiling_);
    return samples_;
  }
  std::map<uint64_t, uint64_t>& GetTotalSamplesPerThread() {
    assert_false(is_profiling_);
    return total_samples_;
  }
  std::map<const char*, uint64_t>& GetSamplesPerInstruction() {
    assert_false(is_profiling_);
    return samples_per_instruction_;
  }
  uint64_t total_samples_collected() { return total_samples_collected_.load(); }

 private:
  void CalculateSamplesPerInstruction();
  void CalculateSamplesPerFunction();

  void ProfilerEntry();

  void SampleThread(threading::Thread* thread);

  Debugger* debugger_ = nullptr;

  // TODO: Callstack map
  // Map of {thread_id, stack} => # hits
  std::map<std::pair<uint64_t, CallStack>, uint64_t> samples_;
  std::map<uint64_t, uint64_t> total_samples_;  // thread_id => total samples
  std::atomic<uint64_t> total_samples_collected_;

  std::map<const char*, uint64_t> samples_per_instruction_;
  std::map<cpu::Function*, uint64_t> samples_per_function_;

  bool is_profiling_ = false;
  bool worker_should_exit_ = false;
  bool should_refresh_threads_ = false;
  threading::Fence worker_fence_;
  threading::Fence profiling_stop_fence_;
  std::unique_ptr<threading::Thread> worker_thread_;

  Timer timer_;
};

}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_PROFILER_H_