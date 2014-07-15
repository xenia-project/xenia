/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_DEBUGGER_H_
#define ALLOY_RUNTIME_DEBUGGER_H_

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

#include <alloy/core.h>
#include <alloy/delegate.h>

namespace alloy {
namespace runtime {

class Debugger;
class Function;
class FunctionInfo;
class Runtime;
class ThreadState;

class Breakpoint {
 public:
  enum Type {
    TEMP_TYPE,
    CODE_TYPE,
  };

 public:
  Breakpoint(Type type, uint64_t address);
  ~Breakpoint();

  Type type() const { return type_; }
  uint64_t address() const { return address_; }

  const char* id() const { return id_.c_str(); }
  void set_id(const char* id) { id_ = std::string(id); }

 private:
  Type type_;
  uint64_t address_;

  std::string id_;
};

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
  BreakpointHitEvent(Debugger* debugger, ThreadState* thread_state,
                     Breakpoint* breakpoint)
      : DebugEvent(debugger),
        thread_state_(thread_state),
        breakpoint_(breakpoint) {}
  ~BreakpointHitEvent() override = default;
  ThreadState* thread_state() const { return thread_state_; }
  Breakpoint* breakpoint() const { return breakpoint_; }

 protected:
  ThreadState* thread_state_;
  Breakpoint* breakpoint_;
};

class Debugger {
 public:
  Debugger(Runtime* runtime);
  ~Debugger();

  Runtime* runtime() const { return runtime_; }

  int SuspendAllThreads(uint32_t timeout_ms = UINT_MAX);
  int ResumeThread(uint32_t thread_id);
  int ResumeAllThreads(bool force = false);

  void ForEachThread(std::function<void(ThreadState*)> callback);

  int AddBreakpoint(Breakpoint* breakpoint);
  int RemoveBreakpoint(Breakpoint* breakpoint);
  void FindBreakpoints(uint64_t address,
                       std::vector<Breakpoint*>& out_breakpoints);

  void OnThreadCreated(ThreadState* thread_state);
  void OnThreadDestroyed(ThreadState* thread_state);
  void OnFunctionDefined(FunctionInfo* symbol_info, Function* function);

  void OnBreakpointHit(ThreadState* thread_state, Breakpoint* breakpoint);

 public:
  Delegate<BreakpointHitEvent> breakpoint_hit;

 private:
  Runtime* runtime_;

  std::mutex threads_lock_;
  std::unordered_map<uint32_t, ThreadState*> threads_;

  std::mutex breakpoints_lock_;
  std::multimap<uint64_t, Breakpoint*> breakpoints_;
};

}  // namespace runtime
}  // namespace alloy

#endif  // ALLOY_RUNTIME_DEBUGGER_H_
