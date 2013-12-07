/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/xenon_runtime.h>

#include <alloy/frontend/ppc/ppc_frontend.h>
#include <alloy/runtime/tracing.h>

#include <xenia/cpu/xenon_thread_state.h>

using namespace alloy;
using namespace alloy::frontend::ppc;
using namespace alloy::runtime;
using namespace xe;
using namespace xe::cpu;


XenonRuntime::XenonRuntime(
    alloy::Memory* memory, ExportResolver* export_resolver) :
    export_resolver_(export_resolver),
    Runtime(memory) {
}

XenonRuntime::~XenonRuntime() {
  alloy::tracing::WriteEvent(EventType::Deinit({
  }));
}

int XenonRuntime::Initialize(backend::Backend* backend) {
  PPCFrontend* frontend = new PPCFrontend(this);
  // TODO(benvanik): set options/etc.

  int result = Runtime::Initialize(frontend, backend);
  if (result) {
    return result;
  }

  alloy::tracing::WriteEvent(EventType::Init({
  }));

  return result;
}
