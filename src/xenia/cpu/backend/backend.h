/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_BACKEND_H_
#define XENIA_CPU_BACKEND_BACKEND_H_

#include <memory>

#include "xenia/cpu/backend/machine_info.h"
#include "xenia/cpu/thread_debug_info.h"

namespace xe {
namespace cpu {
class Breakpoint;
class Function;
class GuestFunction;
class Module;
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace backend {
static constexpr uint32_t MAX_GUEST_PSEUDO_STACKTRACE_ENTRIES = 32;

struct GuestPseudoStackTrace {
  uint32_t count;
  uint32_t truncated_flag;  // set to 1 if there were more than
                            // MAX_GUEST_PSEUDO_STACKTRACE_ENTRIES entries.
  uint32_t return_addrs[MAX_GUEST_PSEUDO_STACKTRACE_ENTRIES];
};
class Assembler;
class CodeCache;
using GuestTrampolineProc = void (*)(ppc::PPCContext* context, void* userarg1,
                                     void* userarg2);
using SimpleGuestTrampolineProc = void (*)(ppc::PPCContext*);
class Backend {
 public:
  explicit Backend();
  virtual ~Backend();

  Processor* processor() const { return processor_; }
  const MachineInfo* machine_info() const { return &machine_info_; }
  CodeCache* code_cache() const { return code_cache_; }

  virtual bool Initialize(Processor* processor);

  virtual void* AllocThreadData();
  virtual void FreeThreadData(void* thread_data);

  virtual void CommitExecutableRange(uint32_t guest_low,
                                     uint32_t guest_high) = 0;

  virtual std::unique_ptr<Assembler> CreateAssembler() = 0;

  virtual std::unique_ptr<GuestFunction> CreateGuestFunction(
      Module* module, uint32_t address) = 0;

  // Calculates the next host instruction based on the current thread state and
  // current PC. This will look for branches and other control flow
  // instructions.
  virtual uint64_t CalculateNextHostInstruction(ThreadDebugInfo* thread_info,
                                                uint64_t current_pc) = 0;

  virtual void InstallBreakpoint(Breakpoint* breakpoint) {}
  virtual void InstallBreakpoint(Breakpoint* breakpoint, Function* fn) {}
  virtual void UninstallBreakpoint(Breakpoint* breakpoint) {}
  // ctx points to the start of a ppccontext, ctx - page_allocation_granularity
  // up until the start of ctx may be used by the backend to store whatever data
  // they want
  virtual void InitializeBackendContext(void* ctx) {}

  /*
	Free any dynamically allocated data/resources that the backendcontext uses
  */
  virtual void DeinitializeBackendContext(void* ctx) {}
  virtual void SetGuestRoundingMode(void* ctx, unsigned int mode){};
  /*
        called by KeSetCurrentStackPointers in xboxkrnl_threading.cc just prior
  to calling XThread::Reenter this is an opportunity for a backend to clear any
  data related to the guest stack

        in the case of the X64 backend, it means we reset the stackpoint index
  to 0, since its a new stack and all of our old entries are invalid now

  * */
  virtual void PrepareForReentry(void* ctx) {}

  // returns true if populated st
  virtual bool PopulatePseudoStacktrace(GuestPseudoStackTrace* st) {
    return false;
  }

  virtual uint32_t CreateGuestTrampoline(GuestTrampolineProc proc,
                                         void* userdata1, void* userdata2,
                                         bool long_term = false) {
    return 0;
  }
  uint32_t CreateGuestTrampoline(void (*func)(ppc::PPCContext*),
                                 bool long_term = false) {
    return CreateGuestTrampoline(
        reinterpret_cast<GuestTrampolineProc>(reinterpret_cast<void*>(func)),
        nullptr, nullptr);
  }
  // if long-term, allocate towards the back of bitset to make allocating short
  // term ones faster
  uint32_t CreateLongTermGuestTrampoline(void (*func)(ppc::PPCContext*)) {
    return CreateGuestTrampoline(
        reinterpret_cast<GuestTrampolineProc>(reinterpret_cast<void*>(func)),
        nullptr, nullptr, true);
  }
  virtual void FreeGuestTrampoline(uint32_t trampoline_addr) {}

 protected:
  Processor* processor_ = nullptr;
  MachineInfo machine_info_;
  CodeCache* code_cache_ = nullptr;
};
/*
 * a set of guest trampolines that all have shared ownership.
 */
struct GuestTrampolineGroup
    : public std::map<SimpleGuestTrampolineProc, uint32_t> {
  Backend* const m_backend;
  xe_mutex m_mutex;

  uint32_t _NewTrampoline(SimpleGuestTrampolineProc proc, bool longterm) {
    uint32_t result;
    m_mutex.lock();
    auto iter = this->find(proc);
    if (iter == this->end()) {
      uint32_t new_entry = longterm
                               ? m_backend->CreateLongTermGuestTrampoline(proc)
                               : m_backend->CreateGuestTrampoline(proc);
      this->emplace_hint(iter, proc, new_entry);
      result = new_entry;
    } else {
      result = iter->second;
    }
    m_mutex.unlock();
    return result;
  }

 public:
  GuestTrampolineGroup(Backend* backend) : m_backend(backend) {}
  ~GuestTrampolineGroup() {
    m_mutex.lock();
    for (auto&& entry : *this) {
      m_backend->FreeGuestTrampoline(entry.second);
    }
    m_mutex.unlock();
  }

  uint32_t NewLongtermTrampoline(SimpleGuestTrampolineProc proc) {
    return _NewTrampoline(proc, true);
  }
  uint32_t NewTrampoline(SimpleGuestTrampolineProc proc) {
    return _NewTrampoline(proc, false);
  }
};

}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_BACKEND_H_
