/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/debugger.h>

using namespace alloy;
using namespace alloy::runtime;


Breakpoint::Breakpoint(Type type, uint64_t address) :
    type_(type), address_(address) {
}

Breakpoint::~Breakpoint() {
}

Debugger::Debugger(Runtime* runtime) :
    runtime_(runtime) {
}

Debugger::~Debugger() {
}

int Debugger::AddBreakpoint(Breakpoint* breakpoint) {
  return 0;
}

int Debugger::RemoveBreakpoint(Breakpoint* breakpoint) {
  return 0;
}
