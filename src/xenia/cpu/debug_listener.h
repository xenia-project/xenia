/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_DEBUG_LISTENER_H_
#define XENIA_CPU_DEBUG_LISTENER_H_

namespace xe {
namespace cpu {

class Breakpoint;
struct ThreadDebugInfo;

// Debug event listener interface.
// Implementations will receive calls from arbitrary threads and must marshal
// them as required.
class DebugListener {
 public:
  // Handles request for debugger focus (such as when the user requests the
  // debugger be brought to the front).
  virtual void OnFocus() = 0;

  // Handles the debugger detaching from the target.
  // This will be called on shutdown or when the user requests the debug session
  // end.
  virtual void OnDetached() = 0;

  // Handles execution being interrupted and transitioning to
  // ExceutionState::kPaused.
  virtual void OnExecutionPaused() = 0;
  // Handles execution continuing when transitioning to
  // ExecutionState::kRunning or ExecutionState::kStepping.
  virtual void OnExecutionContinued() = 0;
  // Handles execution ending when transitioning to ExecutionState::kEnded.
  virtual void OnExecutionEnded() = 0;

  // Handles step completion events when a requested step operation completes.
  // The thread is the one that hit its step target. Note that because multiple
  // threads could be stepping simultaneously (such as a run-to-cursor) use the
  // thread passed instead of keeping any other state.
  virtual void OnStepCompleted(ThreadDebugInfo* thread_info) = 0;

  // Handles breakpoint events as they are hit per-thread.
  // Breakpoints may be hit during stepping.
  virtual void OnBreakpointHit(Breakpoint* breakpoint,
                               ThreadDebugInfo* thread_info) = 0;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_DEBUG_LISTENER_H_
