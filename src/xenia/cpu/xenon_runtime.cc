/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/xenon_runtime.h"

#include "xenia/cpu/frontend/ppc/ppc_frontend.h"
#include "xenia/cpu/xenon_thread_state.h"

namespace xe {
namespace cpu {

XenonRuntime::XenonRuntime(Memory* memory, ExportResolver* export_resolver,
                           uint32_t debug_info_flags, uint32_t trace_flags)
    : Runtime(memory, debug_info_flags, trace_flags),
      export_resolver_(export_resolver) {}

XenonRuntime::~XenonRuntime() = default;

int XenonRuntime::Initialize(
    std::unique_ptr<xe::cpu::backend::Backend> backend) {
  auto frontend = std::make_unique<xe::cpu::frontend::ppc::PPCFrontend>(this);
  // TODO(benvanik): set options/etc.

  int result = Runtime::Initialize(std::move(frontend), std::move(backend));
  if (result) {
    return result;
  }

  return result;
}

}  // namespace cpu
}  // namespace xe
