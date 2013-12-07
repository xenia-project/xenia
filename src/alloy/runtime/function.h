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


namespace alloy {
namespace runtime {

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

  int Call(ThreadState* thread_state);

protected:
  virtual int CallImpl(ThreadState* thread_state) = 0;

protected:
  Type        type_;
  uint64_t    address_;
};


class ExternFunction : public Function {
public:
  typedef void(*Handler)(void* context, void* arg0, void* arg1);
public:
  ExternFunction(uint64_t address, Handler handler, void* arg0, void* arg1);
  virtual ~ExternFunction();

  Handler handler() const { return handler_; }
  void* arg0() const { return arg0_; }
  void* arg1() const { return arg1_; }

protected:
  virtual int CallImpl(ThreadState* thread_state);

protected:
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
