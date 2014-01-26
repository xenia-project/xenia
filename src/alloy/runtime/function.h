/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_FUNCTION_H_
#define ALLOY_RUNTIME_FUNCTION_H_

#include <alloy/core.h>
#include <alloy/runtime/debug_info.h>


namespace alloy {
namespace runtime {

class Breakpoint;
class FunctionInfo;
class ThreadState;


class Function {
public:
  enum Type {
    UNKNOWN_FUNCTION = 0,
    EXTERN_FUNCTION,
    USER_FUNCTION,
  };
public:
  Function(Type type, uint64_t address);
  virtual ~Function();

  Type type() const { return type_; }
  uint64_t address() const { return address_; }

  DebugInfo* debug_info() const { return debug_info_; }
  void set_debug_info(DebugInfo* debug_info) { debug_info_ = debug_info; }

  int AddBreakpoint(Breakpoint* breakpoint);
  int RemoveBreakpoint(Breakpoint* breakpoint);

  int Call(ThreadState* thread_state);

protected:
  Breakpoint* FindBreakpoint(uint64_t address);
  virtual int AddBreakpointImpl(Breakpoint* breakpoint) { return 0; }
  virtual int RemoveBreakpointImpl(Breakpoint* breakpoint) { return 0; }
  virtual int CallImpl(ThreadState* thread_state) = 0;

protected:
  Type        type_;
  uint64_t    address_;
  DebugInfo*  debug_info_;

  // TODO(benvanik): move elsewhere? DebugData?
  Mutex*      lock_;
  std::vector<Breakpoint*> breakpoints_;
};


class ExternFunction : public Function {
public:
  typedef void(*Handler)(void* context, void* arg0, void* arg1);
public:
  ExternFunction(uint64_t address, Handler handler, void* arg0, void* arg1);
  virtual ~ExternFunction();

  const char* name() const { return name_; }
  void set_name(const char* name);

  Handler handler() const { return handler_; }
  void* arg0() const { return arg0_; }
  void* arg1() const { return arg1_; }

protected:
  virtual int CallImpl(ThreadState* thread_state);

protected:
  char* name_;
  Handler handler_;
  void* arg0_;
  void* arg1_;
};


class GuestFunction : public Function {
public:
  GuestFunction(FunctionInfo* symbol_info);
  virtual ~GuestFunction();

  FunctionInfo* symbol_info() const { return symbol_info_; }

protected:
  FunctionInfo* symbol_info_;
};


}  // namespace runtime
}  // namespace alloy


#endif  // ALLOY_RUNTIME_FUNCTION_H_
