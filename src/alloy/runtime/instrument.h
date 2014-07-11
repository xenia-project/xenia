/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_INSTRUMENT_H_
#define ALLOY_RUNTIME_INSTRUMENT_H_

#include <alloy/core.h>

XEDECLARECLASS1(alloy, Memory);

namespace alloy {
namespace runtime {

class Function;
class Runtime;
class ThreadState;

class Instrument {
 public:
  Instrument(Runtime* runtime);
  virtual ~Instrument();

  Runtime* runtime() const { return runtime_; }
  Memory* memory() const { return memory_; }
  bool is_attached() const { return is_attached_; }

  virtual bool Attach();
  virtual bool Detach();

 private:
  Runtime* runtime_;
  Memory* memory_;
  bool is_attached_;
};

class FunctionInstrument : public Instrument {
 public:
  FunctionInstrument(Runtime* runtime, Function* function);
  virtual ~FunctionInstrument() {}

  Function* target() const { return target_; }

  virtual bool Attach();
  virtual bool Detach();

 public:
  void Enter(ThreadState* thread_state);
  void Exit(ThreadState* thread_state);

 protected:
  class Instance {
   public:
    Instance(FunctionInstrument* instrument) : instrument_(instrument) {}
    virtual ~Instance() {}

    FunctionInstrument* instrument() const { return instrument_; }

    virtual void OnEnter(ThreadState* thread_state) = 0;
    virtual void OnExit(ThreadState* thread_state) = 0;

    // TODO(benvanik): utilities:
    //     Log(...)
    //     DebugBreak(type)
    //     GetArg(N)/GetReturn()/SetReturn(V) ?
    //     CallSelf()
    //     Call(target_fn/address)
    //     Return(opt_value)

   private:
    FunctionInstrument* instrument_;
  };

 private:
  Function* target_;
};

class MemoryInstrument : public Instrument {
 public:
  MemoryInstrument(Runtime* runtime, uint64_t address, uint64_t end_address);
  virtual ~MemoryInstrument() {}

  uint64_t address() const { return address_; }
  uint64_t end_address() const { return end_address_; }

  virtual bool Attach();
  virtual bool Detach();

 public:
  enum AccessType {
    ACCESS_READ = (1 << 1),
    ACCESS_WRITE = (1 << 2),
  };
  void Access(ThreadState* thread_state, uint64_t address, AccessType type);

 protected:
  class Instance {
   public:
    Instance(MemoryInstrument* instrument) : instrument_(instrument) {}
    virtual ~Instance() {}

    MemoryInstrument* instrument() const { return instrument_; }

    virtual void OnAccess(ThreadState* thread_state, uint64_t address,
                          AccessType type) = 0;

   private:
    MemoryInstrument* instrument_;
  };

 private:
  uint64_t address_;
  uint64_t end_address_;
};

// ThreadInstrument
//   (v Detach())
//   ThreadInstrumentInstance:
//     instrument()
//     v OnCreate(context, state_ptr)
//     v OnStart(context, state_ptr)
//     v OnResume(context, state_ptr)
//     v OnSuspend(context, state_ptr)
//     v OnExit(context, state_ptr)

// ModuleInstrument
//   (v Detach())
//   ModuleInstrumentInstance:
//     instrument()
//     v OnLoad(context)
//     v OnUnload(context)
//     // get proc address?

}  // namespace runtime
}  // namespace alloy

#endif  // ALLOY_RUNTIME_INSTRUMENT_H_
