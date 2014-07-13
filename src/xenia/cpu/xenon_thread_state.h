/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_XENON_THREAD_STATE_H_
#define XENIA_CPU_XENON_THREAD_STATE_H_

#include <alloy/frontend/ppc/ppc_context.h>
#include <alloy/runtime/thread_state.h>
#include <xenia/core.h>


namespace xe {
namespace cpu {

class XenonRuntime;

using PPCContext = alloy::frontend::ppc::PPCContext;


class XenonThreadState : public alloy::runtime::ThreadState {
public:
  XenonThreadState(XenonRuntime* runtime, uint32_t thread_id,
                   size_t stack_size, uint64_t thread_state_address);
  virtual ~XenonThreadState();

  uint64_t stack_address() const { return stack_address_; }
  size_t stack_size() const { return stack_size_; }
  uint64_t thread_state_address() const { return thread_state_address_; }
  PPCContext* context() const { return context_; }

private:
  uint64_t  stack_address_;
  size_t    stack_size_;
  uint64_t  thread_state_address_;

  // NOTE: must be 64b aligned for SSE ops.
  PPCContext* context_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_XENON_THREAD_STATE_H_
