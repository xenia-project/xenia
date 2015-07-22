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
#include <unordered_map>
#include <vector>

#include "xenia/base/mapped_memory.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/debug/breakpoint.h"

namespace xe {
class Emulator;
}  // namespace xe

namespace xe {
namespace debug {

class Transport;

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

  bool SuspendAllThreads();
  bool ResumeThread(uint32_t thread_id);
  bool ResumeAllThreads();

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

 private:
  void OnMessage(std::vector<uint8_t> buffer);

  Emulator* emulator_ = nullptr;

  std::vector<std::unique_ptr<Transport>> transports_;
  bool is_attached_ = false;
  xe::threading::Fence attach_fence_;

  std::wstring functions_path_;
  std::unique_ptr<ChunkedMappedMemoryWriter> functions_file_;
  std::wstring functions_trace_path_;
  std::unique_ptr<ChunkedMappedMemoryWriter> functions_trace_file_;

  std::mutex breakpoints_lock_;
  std::multimap<uint32_t, Breakpoint*> breakpoints_;
};

}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_DEBUGGER_H_
