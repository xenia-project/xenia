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
#include <alloy/runtime/register_access.h>

#include <vector>

XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS1(xe, ExportResolver);
XEDECLARECLASS2(xe, cpu, XenonMemory);
XEDECLARECLASS2(xe, cpu, XenonRuntime);
XEDECLARECLASS2(xe, cpu, XenonThreadState);


namespace xe {
namespace cpu {

using RegisterAccessCallbacks = alloy::runtime::RegisterAccessCallbacks;
using RegisterHandlesCallback = alloy::runtime::RegisterHandlesCallback;
using RegisterReadCallback = alloy::runtime::RegisterReadCallback;
using RegisterWriteCallback = alloy::runtime::RegisterWriteCallback;


class Processor {
public:
  Processor(Emulator* emulator);
  ~Processor();

  ExportResolver* export_resolver() const { return export_resolver_; }
  XenonRuntime* runtime() const { return runtime_; }
  Memory* memory() const { return memory_; }

  int Setup();

  void AddRegisterAccessCallbacks(RegisterAccessCallbacks callbacks);

  int Execute(
      XenonThreadState* thread_state, uint64_t address);
  uint64_t Execute(
      XenonThreadState* thread_state, uint64_t address, uint64_t arg0);
  uint64_t Execute(
      XenonThreadState* thread_state, uint64_t address, uint64_t arg0,
      uint64_t arg1);

  uint64_t ExecuteInterrupt(
      uint32_t cpu, uint64_t address, uint64_t arg0, uint64_t arg1);

private:
  Emulator*           emulator_;
  ExportResolver*     export_resolver_;

  XenonRuntime*       runtime_;
  Memory*             memory_;

  xe_mutex_t*         interrupt_thread_lock_;
  XenonThreadState*   interrupt_thread_state_;
  uint64_t            interrupt_thread_block_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_PROCESSOR_H_
