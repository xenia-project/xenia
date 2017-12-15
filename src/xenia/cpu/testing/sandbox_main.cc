/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/main.h"
#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/cpu.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/raw_module.h"

#include <gflags/gflags.h>

namespace xe {
namespace cpu {
namespace sandbox {

using xe::cpu::Runtime;
using xe::cpu::ppc::PPCContext;

// TODO(benvanik): simple memory? move more into core?

int main(std::vector<std::wstring>& args) {
#if XE_OPTION_PROFILING
  xe::Profiler::Initialize();
  xe::Profiler::ThreadEnter("main");
#endif  // XE_OPTION_PROFILING

  size_t memory_size = 16 * 1024 * 1024;
  auto memory = std::make_unique<SimpleMemory>(memory_size);
  auto runtime = std::make_unique<Runtime>(memory.get());

  auto frontend =
      std::make_unique<xe::cpu::frontend::ppc::PPCFrontend>(runtime.get());
  std::unique_ptr<xe::cpu::backend::Backend> backend;
  // backend =
  //     std::make_unique<xe::cpu::backend::ivm::IVMBackend>(runtime.get());
  // backend =
  //     std::make_unique<xe::cpu::backend::x64::X64Backend>(runtime.get());
  runtime->Initialize(std::move(frontend), std::move(backend));

  auto module = std::make_unique<xe::cpu::RawModule>(runtime.get());
  module->LoadFile(0x00001000, L"test\\codegen\\instr_add.bin");
  runtime->AddModule(std::move(module));

  {
    uint64_t stack_size = 64 * 1024;
    uint64_t stack_address = memory_size - stack_size;
    uint64_t thread_state_address = stack_address - 0x1000;
    auto thread_state = std::make_unique<ThreadState>(
        runtime.get(), 100, stack_address, stack_size, thread_state_address);

    xe::cpu::Function* fn;
    runtime->ResolveFunction(0x1000, &fn);
    auto ctx = thread_state->context();
    ctx->lr = 0xBEBEBEBE;
    ctx->r[5] = 10;
    ctx->r[25] = 25;
    fn->Call(thread_state.get(), ctx->lr);
    auto result = ctx->r[11];
    printf("%llu", result);
  }

  runtime.reset();
  memory.reset();

#if XE_OPTION_PROFILING
  xe::Profiler::Dump();
  xe::Profiler::ThreadExit();
#endif  // XE_OPTION_PROFILING

  return 0;
}

}  // namespace sandbox
}  // namespace cpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-cpu-sandbox", L"?", xe::cpu::sandbox::main);
