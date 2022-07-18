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

class Assembler;
class CodeCache;

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

 protected:
  Processor* processor_ = nullptr;
  MachineInfo machine_info_;
  CodeCache* code_cache_ = nullptr;
};

}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_BACKEND_H_
