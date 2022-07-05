/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_THREAD_DEBUG_INFO_H_
#define XENIA_CPU_THREAD_DEBUG_INFO_H_

#include <vector>

#include "xenia/base/host_thread_context.h"
#include "xenia/cpu/thread.h"
#include "xenia/cpu/thread_state.h"

namespace xe {
namespace cpu {

class Breakpoint;
class Function;

// Per-thread structure holding debugger state and a cache of the sampled call
// stack.
//
// In most cases debug consumers should rely only on data in this structure as
// it is never removed (even when a thread is destroyed) and always available
// even when running.
struct ThreadDebugInfo {
  ThreadDebugInfo() = default;
  ~ThreadDebugInfo() = default;

  enum class State {
    // Thread is alive and running.
    kAlive,
    // Thread is in a wait state.
    kWaiting,
    // Thread has exited but not yet been killed.
    kExited,
    // Thread has been killed.
    kZombie,
  };

  // XThread::thread_id(), unique to the thread for the run of the emulator.
  uint32_t thread_id = 0;
  // XThread::handle() of the thread.
  // This will be invalidated when the thread dies.
  uint32_t thread_handle = 0;
  // Thread object. Only valid when the thread is alive.
  Thread* thread = nullptr;
  // Current state of the thread.
  State state = State::kAlive;
  // Whether the debugger has forcefully suspended this thread.
  bool suspended = false;

  // A breakpoint managed by the stepping system, installed as required to
  // trigger a break at the next instruction.
  std::unique_ptr<Breakpoint> step_breakpoint;
  // A breakpoint managed by the stepping system, installed as required to
  // trigger after a step over a disabled breakpoint.
  // When this breakpoint is hit the breakpoint referenced in
  // restore_original_breakpoint will be reinstalled.
  // Breakpoint restore_step_breakpoint;
  // If the thread is stepping over a disabled breakpoint this will point to
  // that breakpoint so it can be restored.
  // Breakpoint* restore_original_breakpoint = nullptr;

  // Last-sampled PPC context.
  // This is updated whenever the debugger stops.
  ppc::PPCContext guest_context;
  // Last-sampled host context.
  // This is updated whenever the debugger stops and must be used instead of any
  // value taken from the StackWalker as it properly respects exception stacks.
  HostThreadContext host_context;

  // A single frame in a call stack.
  struct Frame {
    // PC of the current instruction in host code.
    uint64_t host_pc = 0;
    // Base of the function the current host_pc is located within.
    uint64_t host_function_address = 0;
    // PC of the current instruction in guest code.
    // 0 if not a guest address or not known.
    uint32_t guest_pc = 0;
    // Base of the function the current guest_pc is located within.
    uint32_t guest_function_address = 0;
    // Function the current guest_pc is located within.
    Function* guest_function = nullptr;
    // Name of the function, if known.
    // TODO(benvanik): string table?
    char name[256] = {0};
  };

  // Last-sampled call stack.
  // This is updated whenever the debugger stops.
  std::vector<Frame> frames;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_THREAD_DEBUG_INFO_H_
