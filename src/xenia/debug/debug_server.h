/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_DEBUG_SERVER_H_
#define XENIA_DEBUG_DEBUG_SERVER_H_

#include <functional>

#include "xenia/cpu/function.h"
#include "xenia/cpu/processor.h"
#include "xenia/debug/breakpoint.h"

namespace xe {
namespace debug {

class Debugger;

class DebugServer {
 public:
  virtual ~DebugServer() = default;

  Debugger* debugger() const { return debugger_; }

  virtual bool Initialize() = 0;

  virtual void PostSynchronous(std::function<void()> fn) = 0;

  // TODO(benvanik): better thread type (XThread?)
  // virtual void OnThreadCreated(ThreadState* thread_state) = 0;
  // virtual void OnThreadDestroyed(ThreadState* thread_state) = 0;

  virtual void OnExecutionContinued() {}
  virtual void OnExecutionInterrupted() {}
  /*virtual void OnBreakpointHit(xe::cpu::ThreadState* thread_state,
                               Breakpoint* breakpoint) = 0;*/

 protected:
  DebugServer(Debugger* debugger) : debugger_(debugger) {}

  Debugger* debugger_ = nullptr;
};

}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_DEBUG_SERVER_H_
