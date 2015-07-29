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

#include "xenia/base/mapped_memory.h"
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

class DebugServer;

enum class ExecutionState {
  kRunning,
  kStopped,
};

class Debugger {
 public:
  Debugger(Emulator* emulator);
  ~Debugger();

  Emulator* emulator() const { return emulator_; }

  bool StartSession();
  void PreLaunch();
  void StopSession();
  void FlushSession();

  uint8_t* AllocateFunctionData(size_t size);
  uint8_t* AllocateFunctionTraceData(size_t size);

  bool is_attached() const { return is_attached_; }
  void set_attached(bool attached);

  ExecutionState execution_state() const { return execution_state_; }

  void DumpThreadStacks();

  int AddBreakpoint(Breakpoint* breakpoint);
  int RemoveBreakpoint(Breakpoint* breakpoint);
  void FindBreakpoints(uint32_t address,
                       std::vector<Breakpoint*>& out_breakpoints);

  // TODO(benvanik): utility functions for modification (make function ignored,
  // etc).

  void Interrupt();
  void Continue();
  void StepOne(uint32_t thread_id);
  void StepTo(uint32_t thread_id, uint32_t target_pc);

  void OnThreadCreated(xe::kernel::XThread* thread);
  void OnThreadExit(xe::kernel::XThread* thread);
  void OnThreadDestroyed(xe::kernel::XThread* thread);

  void OnFunctionDefined(cpu::FunctionInfo* symbol_info,
                         cpu::Function* function);

  void OnBreakpointHit(xe::kernel::XThread* thread, Breakpoint* breakpoint);

 private:
  bool SuspendAllThreads();
  bool ResumeThread(uint32_t thread_id);
  bool ResumeAllThreads();

  Emulator* emulator_ = nullptr;

  std::unique_ptr<DebugServer> server_;
  bool is_attached_ = false;
  xe::threading::Fence attach_fence_;

  std::wstring functions_path_;
  std::unique_ptr<ChunkedMappedMemoryWriter> functions_file_;
  std::wstring functions_trace_path_;
  std::unique_ptr<ChunkedMappedMemoryWriter> functions_trace_file_;

  std::recursive_mutex mutex_;
  ExecutionState execution_state_ = ExecutionState::kStopped;

  std::multimap<uint32_t, Breakpoint*> breakpoints_;
};

}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_DEBUGGER_H_
