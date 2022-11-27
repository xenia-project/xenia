/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PROCESSOR_H_
#define XENIA_CPU_PROCESSOR_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/base/mutex.h"
#include "xenia/cpu/backend/backend.h"
#include "xenia/cpu/debug_listener.h"
#include "xenia/cpu/entry_table.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/module.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/thread_debug_info.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/memory.h"

DECLARE_bool(debug);

namespace xe {
namespace cpu {

constexpr fourcc_t kProcessorSaveSignature = make_fourcc("PROC");

class Breakpoint;
class StackWalker;
class XexModule;

enum class Irql : uint32_t {
  PASSIVE = 0,
  APC = 1,
  DISPATCH = 2,
  DPC = 3,
};

// Describes the current state of the emulator as known to the debugger.
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

class Processor {
 public:
  Processor(Memory* memory, ExportResolver* export_resolver);
  ~Processor();

  Memory* memory() const { return memory_; }
  StackWalker* stack_walker() const { return stack_walker_.get(); }
  ppc::PPCFrontend* frontend() const { return frontend_.get(); }
  backend::Backend* backend() const { return backend_.get(); }
  ExportResolver* export_resolver() const { return export_resolver_; }

  bool Setup(std::unique_ptr<backend::Backend> backend);

  // Runs any pre-launch logic once the module and thread have been setup.
  void PreLaunch();

  // The current execution state of the emulator.
  ExecutionState execution_state() const { return execution_state_; }

  // True if a debug listener is attached and the debugger is active.
  bool is_debugger_attached() const { return !!debug_listener_; }
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
      std::function<DebugListener*(Processor*)> handler) {
    debug_listener_handler_ = std::move(handler);
  }

  void set_debug_info_flags(uint32_t debug_info_flags) {
    debug_info_flags_ = debug_info_flags;
  }

  bool AddModule(std::unique_ptr<Module> module);
  void RemoveModule(const std::string_view name);
  Module* GetModule(const std::string_view name);
  std::vector<Module*> GetModules();

  Module* builtin_module() const { return builtin_module_; }
  Function* DefineBuiltin(const std::string_view name,
                          BuiltinFunction::Handler handler, void* arg0,
                          void* arg1);

  Function* QueryFunction(uint32_t address);
  std::vector<Function*> FindFunctionsWithAddress(uint32_t address);
  void RemoveFunctionByAddress(uint32_t address);

  Function* LookupFunction(uint32_t address);
  Module* LookupModule(uint32_t address);
  Function* LookupFunction(Module* module, uint32_t address);
  Function* ResolveFunction(uint32_t address);

  bool Execute(ThreadState* thread_state, uint32_t address);
  bool ExecuteRaw(ThreadState* thread_state, uint32_t address);
  uint64_t Execute(ThreadState* thread_state, uint32_t address, uint64_t args[],
                   size_t arg_count);
  uint64_t ExecuteInterrupt(ThreadState* thread_state, uint32_t address,
                            uint64_t args[], size_t arg_count);

  Irql RaiseIrql(Irql new_value);
  void LowerIrql(Irql old_value);

  bool Save(ByteStream* stream);
  bool Restore(ByteStream* stream);

  // Returns a list of debugger info for all threads that have ever existed.
  // This is the preferred way to sample thread state vs. attempting to ask
  // the kernel.
  std::vector<ThreadDebugInfo*> QueryThreadDebugInfos();

  // Returns the debugger info for the given thread.
  ThreadDebugInfo* QueryThreadDebugInfo(uint32_t thread_id);

  // Adds a breakpoint to the debugger and activates it (if enabled).
  // The given breakpoint will not be owned by the debugger and must remain
  // allocated so long as it is added.
  void AddBreakpoint(Breakpoint* breakpoint);

  // Removes a breakpoint from the debugger and deactivates it.
  void RemoveBreakpoint(Breakpoint* breakpoint);

  // Finds a breakpoint that may be registered at the given address.
  Breakpoint* FindBreakpoint(uint32_t address);

  // Returns all currently registered breakpoints.
  std::vector<Breakpoint*> breakpoints() const;

  // Shows the debug listener, focusing it if it already exists.
  void ShowDebugger();

  // Pauses target execution by suspending all threads.
  // The debug listener will be requested if it has not been attached.
  void Pause();

  // Continues target execution from wherever it is.
  // This will cancel any active step operations and resume all threads.
  void Continue();

  // Steps the given thread a single x64 host instruction.
  // If the step is over a branch the branch will be followed.
  void StepHostInstruction(uint32_t thread_id);

  // Steps the given thread a single PPC guest instruction.
  // If the step is over a branch the branch will be followed.
  void StepGuestInstruction(uint32_t thread_id);

  // Steps the given thread until the guest address is hit.
  // Returns false if the step could not be completed (invalid target address).
  bool StepToGuestAddress(uint32_t thread_id, uint32_t pc);

  // Steps the given thread to the target of the branch at the specified guest
  // address. The address must specify a branch instruction.
  // Returns the new PC guest address.
  uint32_t StepIntoGuestBranchTarget(uint32_t thread_id, uint32_t pc);

  // Steps the thread to a point where it's safe to terminate or read its
  // context. Returns the PC after we've finished stepping.
  // Pass true for ignore_host if you've stopped the thread yourself
  // in host code you want to ignore.
  // Returns the new PC guest address.
  uint32_t StepToGuestSafePoint(uint32_t thread_id, bool ignore_host = false);

 public:
  // TODO(benvanik): hide.
  void OnThreadCreated(uint32_t handle, ThreadState* thread_state,
                       Thread* thread);
  void OnThreadExit(uint32_t thread_id);
  void OnThreadDestroyed(uint32_t thread_id);
  void OnThreadEnteringWait(uint32_t thread_id);
  void OnThreadLeavingWait(uint32_t thread_id);

  bool OnUnhandledException(Exception* ex);
  bool OnThreadBreakpointHit(Exception* ex);

  uint8_t* AllocateFunctionTraceData(size_t size);

 private:
  // Synchronously demands a debug listener.
  void DemandDebugListener();

  // Suspends all known threads (except the caller).
  bool SuspendAllThreads();
  // Resumes the given thread.
  bool ResumeThread(uint32_t thread_id);
  // Resumes all known threads (except the caller).
  bool ResumeAllThreads();
  // Updates all cached thread execution info (state, call stacks, etc).
  // The given override thread handle and context will be used in place of
  // sampled values for that thread.
  void UpdateThreadExecutionStates(
      uint32_t override_handle = 0,
      HostThreadContext* override_context = nullptr);

  // Suspends all breakpoints, uninstalling them as required.
  // No breakpoints will be triggered until they are resumed.
  void SuspendAllBreakpoints();
  // Resumes all breakpoints, re-installing them if required.
  void ResumeAllBreakpoints();

  void OnFunctionDefined(Function* function);

  static bool ExceptionCallbackThunk(Exception* ex, void* data);
  bool ExceptionCallback(Exception* ex);
  void OnStepCompleted(ThreadDebugInfo* thread_info);
  void OnBreakpointHit(ThreadDebugInfo* thread_info, Breakpoint* breakpoint);

  // Calculates the next guest instruction based on the current thread state and
  // current PC. This will look for branches and other control flow
  // instructions.
  uint32_t CalculateNextGuestInstruction(ThreadDebugInfo* thread_info,
                                         uint32_t current_pc);

  bool DemandFunction(Function* function);

  Memory* memory_ = nullptr;
  std::unique_ptr<StackWalker> stack_walker_;

  std::function<DebugListener*(Processor*)> debug_listener_handler_;
  DebugListener* debug_listener_ = nullptr;

  // Which debug features are enabled in generated code.
  uint32_t debug_info_flags_ = 0;
  // If specified, the file trace data gets written to when running.
  std::filesystem::path functions_trace_path_;
  std::unique_ptr<ChunkedMappedMemoryWriter> functions_trace_file_;

  std::unique_ptr<ppc::PPCFrontend> frontend_;
  std::unique_ptr<backend::Backend> backend_;
  ExportResolver* export_resolver_ = nullptr;

  EntryTable entry_table_;
  xe::global_critical_region global_critical_region_;
  ExecutionState execution_state_ = ExecutionState::kPaused;
  std::vector<std::unique_ptr<Module>> modules_;
  Module* builtin_module_ = nullptr;
  uint32_t next_builtin_address_ = 0xFFFF0000u;

  // Maps thread ID to state. Updated on thread create, and threads are never
  // removed. Must be guarded with the global lock.
  std::map<uint32_t, std::unique_ptr<ThreadDebugInfo>> thread_debug_infos_;

  // TODO(benvanik): cleanup/change structures.
  std::vector<Breakpoint*> breakpoints_;

  Irql irql_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PROCESSOR_H_
