/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PROCESSOR_H_
#define XENIA_CPU_PROCESSOR_H_

#include <xenia/core.h>
#include <xenia/debug/debug_target.h>

#include <vector>

XEDECLARECLASS2(alloy, runtime, Breakpoint);
XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS1(xe, ExportResolver);
XEDECLARECLASS2(xe, cpu, XenonMemory);
XEDECLARECLASS2(xe, cpu, XenonRuntime);
XEDECLARECLASS2(xe, cpu, XenonThreadState);
XEDECLARECLASS2(xe, cpu, XexModule);


namespace xe {
namespace cpu {


class Processor : public debug::DebugTarget {
public:
  Processor(Emulator* emulator);
  ~Processor();

  ExportResolver* export_resolver() const { return export_resolver_; }
  XenonRuntime* runtime() const { return runtime_; }
  Memory* memory() const { return memory_; }

  int Setup();

  int Execute(
      XenonThreadState* thread_state, uint64_t address);
  uint64_t Execute(
      XenonThreadState* thread_state, uint64_t address, uint64_t arg0);
  uint64_t Execute(
      XenonThreadState* thread_state, uint64_t address, uint64_t arg0,
      uint64_t arg1);

  uint64_t ExecuteInterrupt(
      uint32_t cpu, uint64_t address, uint64_t arg0, uint64_t arg1);

  virtual void OnDebugClientConnected(uint32_t client_id);
  virtual void OnDebugClientDisconnected(uint32_t client_id);
  virtual json_t* OnDebugRequest(
      uint32_t client_id, const char* command, json_t* request,
      bool& succeeded);

private:
  json_t* DumpModule(XexModule* module, bool& succeeded);
  json_t* DumpFunction(uint64_t address, bool& succeeded);
  json_t* DumpThreadState(XenonThreadState* thread_state);

private:
  Emulator*           emulator_;
  ExportResolver*     export_resolver_;

  XenonRuntime*       runtime_;
  Memory*             memory_;

  xe_mutex_t*         interrupt_thread_lock_;
  XenonThreadState*   interrupt_thread_state_;
  uint64_t            interrupt_thread_block_;

  class DebugClientState {
  public:
    DebugClientState(XenonRuntime* runtime);
    ~DebugClientState();

    int AddBreakpoint(const char* breakpoint_id,
                      alloy::runtime::Breakpoint* breakpoint);
    int RemoveBreakpoint(const char* breakpoint_id);
    int RemoveAllBreakpoints();

  private:
    XenonRuntime* runtime_;

    xe_mutex_t* breakpoints_lock_;
    typedef std::unordered_map<std::string, alloy::runtime::Breakpoint*> BreakpointMap;
    BreakpointMap breakpoints_;
  };
  xe_mutex_t* debug_client_states_lock_;
  typedef std::unordered_map<uint32_t, DebugClientState*> DebugClientStateMap;
  DebugClientStateMap debug_client_states_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_PROCESSOR_H_
