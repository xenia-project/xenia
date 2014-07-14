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

#include <xenia/cpu/xenon_thread_state.h>

using namespace alloy;
using namespace alloy::frontend::ppc;
using namespace alloy::runtime;
using namespace xe;
using namespace xe::cpu;


XenonRuntime::XenonRuntime(
    alloy::Memory* memory, ExportResolver* export_resolver) :
    Runtime(memory),
    export_resolver_(export_resolver) {
}

XenonRuntime::~XenonRuntime() = default;

int XenonRuntime::Initialize(std::unique_ptr<backend::Backend> backend) {
  std::unique_ptr<PPCFrontend> frontend(new PPCFrontend(this));
  // TODO(benvanik): set options/etc.

  int result = Runtime::Initialize(std::move(frontend), std::move(backend));
  if (result) {
    return result;
  }

  return result;
}
