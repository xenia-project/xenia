/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/alloy.h>
#include <alloy/backend/ivm/ivm_backend.h>
#include <alloy/backend/x64/x64_backend.h>
#include <alloy/frontend/ppc/ppc_context.h>
#include <alloy/frontend/ppc/ppc_frontend.h>
#include <alloy/runtime/raw_module.h>
#include <poly/poly.h>
#include <xenia/cpu/xenon_memory.h>

#include <gflags/gflags.h>

namespace alloy {
namespace sandbox {

using alloy::frontend::ppc::PPCContext;
using alloy::runtime::Runtime;

class ThreadState : public alloy::runtime::ThreadState {
 public:
  ThreadState(Runtime* runtime, uint32_t thread_id, size_t stack_size,
              uint64_t thread_state_address)
      : alloy::runtime::ThreadState(runtime, thread_id),
        stack_size_(stack_size),
        thread_state_address_(thread_state_address) {
    stack_address_ = memory_->HeapAlloc(0, stack_size, MEMORY_FLAG_ZERO);

    // Allocate with 64b alignment.
    context_ = (PPCContext*)xe_malloc_aligned(sizeof(PPCContext));
    assert_true((reinterpret_cast<uint64_t>(context_) & 0xF) == 0);
    memset(&context_, 0, sizeof(PPCContext));

    // Stash pointers to common structures that callbacks may need.
    context_->reserve_address = memory_->reserve_address();
    context_->membase = memory_->membase();
    context_->runtime = runtime;
    context_->thread_state = this;

    // Set initial registers.
    context_->r[1] = stack_address_ + stack_size;
    context_->r[13] = thread_state_address_;

    // Pad out stack a bit, as some games seem to overwrite the caller by about
    // 16 to 32b.
    context_->r[1] -= 64;

    raw_context_ = context_;

    runtime_->debugger()->OnThreadCreated(this);
  }
  ~ThreadState() override {
    runtime_->debugger()->OnThreadDestroyed(this);
    xe_free_aligned(context_);
    memory_->HeapFree(stack_address_, stack_size_);
  }

  PPCContext* context() const { return context_; }

 private:
  uint64_t stack_address_;
  size_t stack_size_;
  uint64_t thread_state_address_;

  // NOTE: must be 64b aligned for SSE ops.
  PPCContext* context_;
};

// TODO(benvanik): simple memory? move more into core?

int main(int argc, xechar_t** argv) {
  xe::Profiler::Initialize();
  xe::Profiler::ThreadEnter("main");

  auto memory = std::make_unique<xe::cpu::XenonMemory>();
  auto runtime = std::make_unique<Runtime>(memory.get());

  auto frontend =
      std::make_unique<alloy::frontend::ppc::PPCFrontend>(runtime.get());
  std::unique_ptr<alloy::backend::Backend> backend;
  //  auto backend =
  //      std::make_unique<alloy::backend::ivm::IVMBackend>(runtime.get());
  //  auto backend =
  //      std::make_unique<alloy::backend::x64::X64Backend>(runtime.get());
  runtime->Initialize(std::move(frontend), std::move(backend));

  auto module = std::make_unique<alloy::runtime::RawModule>(runtime.get());
  module->LoadFile(0x82000000, "test\\codegen\\instr_add.bin");
  runtime->AddModule(std::move(module));

  {
    auto thread_state =
        std::make_unique<ThreadState>(runtime.get(), 100, 64 * 1024, 0);

    alloy::runtime::Function* fn;
    runtime->ResolveFunction(0x82000000, &fn);
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

  xe::Profiler::Dump();
  xe::Profiler::ThreadExit();

  return 0;
}

}  // namespace sandbox
}  // namespace alloy

// TODO(benvanik): move main thunk into poly
// ehhh
#include <xenia/platform.cc>
XE_MAIN_THUNK(alloy::sandbox::main, "alloy-sandbox");
