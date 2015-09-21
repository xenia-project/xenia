/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_DEBUGGER_H_
#define XENIA_DEBUG_DEBUGGER_H_

#include <gflags/gflags.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/exception_handler.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/base/mutex.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/debug/breakpoint.h"

DECLARE_bool(debug);

namespace xe {
class Emulator;
namespace kernel {
class XThread;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace debug {

// Describes the current state of the debug target as known to the debugger.
// This determines which state the debugger is in and what operations are
// allowed.
enum class ExecutionState {
  // Target is running; the debugger is not waiting for any events.
  kRunning,
  // Target is running in stepping mode with the debugger waiting for feedback.
  kStepping,
  // Target is paused for debugging.
  kPaused,
  // Target has been stopped and cannot be restarted (crash, etc).
  kEnded,
};

// Per-XThread structure holding debugger state and a cache of the sampled call
// stack.
//
// In most cases debug consumers should rely only on data in this structure as
// it is never removed (even when a thread is destroyed) and always available
// even when running.
struct ThreadExecutionInfo {
  ThreadExecutionInfo();
  ~ThreadExecutionInfo();

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

  // XThread::handle() of the thread.
  uint32_t thread_handle = 0;
  // Target XThread, if it has not been destroyed.
  // TODO(benvanik): hold a ref here to keep zombie threads around?
  kernel::XThread* thread = nullptr;
  // Current state of the thread.
  State state = State::kAlive;
  // Whether the debugger has forcefully suspended this thread.
  bool suspended = false;

  // A breakpoint managed by the stepping system, installed as required to
  // trigger a break at the next instruction.
  std::unique_ptr<StepBreakpoint> step_breakpoint;
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
  xe::cpu::frontend::PPCContext guest_context;
  // Last-sampled host x64 context.
  // This is updated whenever the debugger stops and must be used instead of any
  // value taken from the StackWalker as it properly respects exception stacks.
  X64Context host_context;

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
    cpu::Function* guest_function = nullptr;
    // Name of the function, if known.
    // TODO(benvanik): string table?
    char name[256] = {0};
  };

  // Last-sampled call stack.
  // This is updated whenever the debugger stops.
  std::vector<Frame> frames;
};

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
  virtual void OnStepCompleted(xe::kernel::XThread* thread) = 0;

  // Handles breakpoint events as they are hit per-thread.
  // Breakpoints may be hit during stepping.
  virtual void OnBreakpointHit(Breakpoint* breakpoint,
                               xe::kernel::XThread* thread) = 0;
};

class Debugger {
 public:
  explicit Debugger(Emulator* emulator);
  ~Debugger();

  Emulator* emulator() const { return emulator_; }

  // True if a debug listener is attached and the debugger is active.
  bool is_attached() const { return !!debug_listener_; }
  // Gets the active debug listener, if any.
  DebugListener* debug_listener() const { return debug_listener_; }
  // Sets the active debug listener, if any.
  // This can be used to detach the listener.
  void set_debug_listener(DebugListener* debug_listener);
  // Sets a handler that will be called from a random thread when a debugger
  // listener is required (such as on a breakpoint hit/etc).
  // Will only be called if the debug listener has not already been specified
  // with set_debug_listener.
  void set_debug_listener_request_handler(
      std::function<DebugListener*(Debugger*)> handler) {
    debug_listener_handler_ = std::move(handler);
  }

  // The current execution state of the target.
  ExecutionState execution_state() const { return execution_state_; }

  // TODO(benvanik): remove/cleanup.
  bool StartSession();
  void PreLaunch();
  void StopSession();
  void FlushSession();

  // TODO(benvanik): hide.
  uint8_t* AllocateFunctionData(size_t size);
  uint8_t* AllocateFunctionTraceData(size_t size);

  // Returns a list of debugger info for all threads that have ever existed.
  // This is the preferred way to sample thread state vs. attempting to ask
  // the kernel.
  std::vector<ThreadExecutionInfo*> QueryThreadExecutionInfos();
  // Returns the debugger info for the given thread.
  ThreadExecutionInfo* QueryThreadExecutionInfo(uint32_t thread_handle);

  // Adds a breakpoint to the debugger and activates it (if enabled).
  // The given breakpoint will not be owned by the debugger and must remain
  // allocated so long as it is added.
  void AddBreakpoint(Breakpoint* breakpoint);
  // Removes a breakpoint from the debugger and deactivates it.
  void RemoveBreakpoint(Breakpoint* breakpoint);
  // Returns all currently registered breakpoints.
  const std::vector<Breakpoint*>& breakpoints() const { return breakpoints_; }

  // TODO(benvanik): utility functions for modification (make function ignored,
  // etc).

  // Shows the debug listener, focusing it if it already exists.
  void Show();

  // Pauses target execution by suspending all threads.
  // The debug listener will be requested if it has not been attached.
  void Pause();

  // Continues target execution from wherever it is.
  // This will cancel any active step operations and resume all threads.
  void Continue();

  // Steps the given thread a single PPC guest instruction.
  // If the step is over a branch the branch will be followed.
  void StepGuestInstruction(uint32_t thread_handle);
  // Steps the given thread a single x64 host instruction.
  // If the step is over a branch the branch will be followed.
  void StepHostInstruction(uint32_t thread_handle);

 public:
  // TODO(benvanik): hide.
  void OnThreadCreated(xe::kernel::XThread* thread);
  void OnThreadExit(xe::kernel::XThread* thread);
  void OnThreadDestroyed(xe::kernel::XThread* thread);
  void OnThreadEnteringWait(xe::kernel::XThread* thread);
  void OnThreadLeavingWait(xe::kernel::XThread* thread);

  void OnFunctionDefined(xe::cpu::Function* function);

  bool OnUnhandledException(Exception* ex);

 private:
  // Synchronously demands a debug listener.
  void DemandDebugListener();

  // Suspends all known threads (except the caller).
  bool SuspendAllThreads();
  // Resumes the given thread.
  bool ResumeThread(uint32_t thread_handle);
  // Resumes all known threads (except the caller).
  bool ResumeAllThreads();
  // Updates all cached thread execution info (state, call stacks, etc).
  // The given override thread handle and context will be used in place of
  // sampled values for that thread.
  void UpdateThreadExecutionStates(uint32_t override_handle = 0,
                                   X64Context* override_context = nullptr);

  // Suspends all breakpoints, uninstalling them as required.
  // No breakpoints will be triggered until they are resumed.
  void SuspendAllBreakpoints();
  // Resumes all breakpoints, re-installing them if required.
  void ResumeAllBreakpoints();

  static bool ExceptionCallbackThunk(Exception* ex, void* data);
  bool ExceptionCallback(Exception* ex);
  void OnStepCompleted(ThreadExecutionInfo* thread_info);
  void OnBreakpointHit(ThreadExecutionInfo* thread_info,
                       Breakpoint* breakpoint);

  // Calculates the next guest instruction based on the current thread state and
  // current PC. This will look for branches and other control flow
  // instructions.
  uint32_t CalculateNextGuestInstruction(ThreadExecutionInfo* thread_info,
                                         uint32_t current_pc);
  // Calculates the next host instruction based on the current thread state and
  // current PC. This will look for branches and other control flow
  // instructions.
  uint64_t CalculateNextHostInstruction(ThreadExecutionInfo* thread_info,
                                        uint64_t current_pc);

  Emulator* emulator_ = nullptr;

  // TODO(benvanik): move this to the CPU backend and remove x64-specific stuff?
  uintptr_t capstone_handle_ = 0;

  std::function<DebugListener*(Debugger*)> debug_listener_handler_;
  DebugListener* debug_listener_ = nullptr;

  // TODO(benvanik): cleanup - maybe remove now that in-process?
  std::wstring functions_path_;
  std::unique_ptr<ChunkedMappedMemoryWriter> functions_file_;
  std::wstring functions_trace_path_;
  std::unique_ptr<ChunkedMappedMemoryWriter> functions_trace_file_;

  xe::global_critical_region global_critical_region_;

  ExecutionState execution_state_ = ExecutionState::kPaused;

  // Maps thread ID to state. Updated on thread create, and threads are never
  // removed.
  std::map<uint32_t, std::unique_ptr<ThreadExecutionInfo>>
      thread_execution_infos_;
  // TODO(benvanik): cleanup/change structures.
  std::vector<Breakpoint*> breakpoints_;
};

}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_DEBUGGER_H_
