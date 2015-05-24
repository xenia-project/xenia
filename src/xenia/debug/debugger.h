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

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "xenia/base/delegate.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/debug/breakpoint.h"

namespace xe {
class Emulator;
}  // namespace xe

namespace xe {
namespace cpu {
class Function;
class FunctionInfo;
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace debug {

class Debugger;

class DebugEvent {
 public:
  DebugEvent(Debugger* debugger) : debugger_(debugger) {}
  virtual ~DebugEvent() = default;
  Debugger* debugger() const { return debugger_; }

 protected:
  Debugger* debugger_;
};

class BreakpointHitEvent : public DebugEvent {
 public:
  BreakpointHitEvent(Debugger* debugger, cpu::ThreadState* thread_state,
                     Breakpoint* breakpoint)
      : DebugEvent(debugger),
        thread_state_(thread_state),
        breakpoint_(breakpoint) {}
  ~BreakpointHitEvent() override = default;
  cpu::ThreadState* thread_state() const { return thread_state_; }
  Breakpoint* breakpoint() const { return breakpoint_; }

 protected:
  cpu::ThreadState* thread_state_;
  Breakpoint* breakpoint_;
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

  int SuspendAllThreads(uint32_t timeout_ms = UINT_MAX);
  int ResumeThread(uint32_t thread_id);
  int ResumeAllThreads(bool force = false);

  void ForEachThread(std::function<void(cpu::ThreadState*)> callback);

  int AddBreakpoint(Breakpoint* breakpoint);
  int RemoveBreakpoint(Breakpoint* breakpoint);
  void FindBreakpoints(uint32_t address,
                       std::vector<Breakpoint*>& out_breakpoints);

  // TODO(benvanik): utility functions for modification (make function ignored,
  // etc).

  void OnThreadCreated(cpu::ThreadState* thread_state);
  void OnThreadDestroyed(cpu::ThreadState* thread_state);
  void OnFunctionDefined(cpu::FunctionInfo* symbol_info,
                         cpu::Function* function);

  void OnBreakpointHit(cpu::ThreadState* thread_state, Breakpoint* breakpoint);

 public:
  Delegate<BreakpointHitEvent> breakpoint_hit;

 private:
  void OnMessage(std::vector<uint8_t> buffer);

  Emulator* emulator_;

  UINT_PTR listen_socket_;
  std::thread accept_thread_;
  xe::threading::Fence accept_fence_;
  UINT_PTR client_socket_;
  std::thread receive_thread_;

  std::wstring functions_path_;
  std::unique_ptr<ChunkedMappedMemoryWriter> functions_file_;
  std::wstring functions_trace_path_;
  std::unique_ptr<ChunkedMappedMemoryWriter> functions_trace_file_;

  std::mutex threads_lock_;
  std::unordered_map<uint32_t, cpu::ThreadState*> threads_;

  std::mutex breakpoints_lock_;
  std::multimap<uint32_t, Breakpoint*> breakpoints_;
};

}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_DEBUGGER_H_
