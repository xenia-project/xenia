/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_TEST_UTIL_H_
#define ALLOY_TEST_UTIL_H_

#include <alloy/alloy.h>
#include <alloy/backend/ivm/ivm_backend.h>
#include <alloy/backend/x64/x64_backend.h>
#include <alloy/frontend/ppc/ppc_context.h>
#include <alloy/frontend/ppc/ppc_frontend.h>
#include <alloy/hir/hir_builder.h>
#include <alloy/runtime/test_module.h>
#include <poly/main.h>
#include <poly/poly.h>

#include <third_party/catch/single_include/catch.hpp>

#define ALLOY_TEST_IVM 1
#define ALLOY_TEST_X64 1

namespace alloy {
namespace test {

using alloy::frontend::ppc::PPCContext;
using alloy::runtime::Runtime;

class ThreadState : public alloy::runtime::ThreadState {
 public:
  ThreadState(Runtime* runtime, uint32_t thread_id, uint64_t stack_address,
              size_t stack_size, uint64_t thread_state_address)
      : alloy::runtime::ThreadState(runtime, thread_id),
        stack_address_(stack_address),
        stack_size_(stack_size),
        thread_state_address_(thread_state_address) {
    memset(memory_->Translate(stack_address_), 0, stack_size_);

    // Allocate with 64b alignment.
    context_ = (PPCContext*)calloc(1, sizeof(PPCContext));
    assert_true((reinterpret_cast<uint64_t>(context_) & 0xF) == 0);

    // Stash pointers to common structures that callbacks may need.
    context_->reserve_address = memory_->reserve_address();
    context_->reserve_value = memory_->reserve_value();
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
    free(context_);
  }

  PPCContext* context() const { return context_; }

 private:
  uint64_t stack_address_;
  size_t stack_size_;
  uint64_t thread_state_address_;

  // NOTE: must be 64b aligned for SSE ops.
  PPCContext* context_;
};

class TestFunction {
 public:
  TestFunction(std::function<void(hir::HIRBuilder& b)> generator) {
    memory_size = 16 * 1024 * 1024;
    memory.reset(new SimpleMemory(memory_size));

#if ALLOY_TEST_IVM
    {
      auto runtime = std::make_unique<Runtime>(memory.get());
      auto frontend =
          std::make_unique<alloy::frontend::ppc::PPCFrontend>(runtime.get());
      auto backend =
          std::make_unique<alloy::backend::ivm::IVMBackend>(runtime.get());
      runtime->Initialize(std::move(frontend), std::move(backend));
      runtimes.emplace_back(std::move(runtime));
    }
#endif  // ALLOY_TEST_IVM
#if ALLOY_TEST_X64
    {
      auto runtime = std::make_unique<Runtime>(memory.get());
      auto frontend =
          std::make_unique<alloy::frontend::ppc::PPCFrontend>(runtime.get());
      auto backend =
          std::make_unique<alloy::backend::x64::X64Backend>(runtime.get());
      runtime->Initialize(std::move(frontend), std::move(backend));
      runtimes.emplace_back(std::move(runtime));
    }
#endif  // ALLOY_TEST_X64

    for (auto& runtime : runtimes) {
      auto module = std::make_unique<alloy::runtime::TestModule>(
          runtime.get(), "Test",
          [](uint64_t address) { return address == 0x1000; },
          [generator](hir::HIRBuilder& b) {
            generator(b);
            return true;
          });
      runtime->AddModule(std::move(module));
    }
  }

  ~TestFunction() {
    runtimes.clear();
    memory.reset();
  }

  void Run(std::function<void(PPCContext*)> pre_call,
           std::function<void(PPCContext*)> post_call) {
    for (auto& runtime : runtimes) {
      memory->Zero(0, memory_size);

      alloy::runtime::Function* fn;
      runtime->ResolveFunction(0x1000, &fn);

      uint64_t stack_size = 64 * 1024;
      uint64_t stack_address = memory_size - stack_size;
      uint64_t thread_state_address = stack_address - 0x1000;
      auto thread_state = std::make_unique<ThreadState>(
          runtime.get(), 0x100, stack_address, stack_size, thread_state_address);
      auto ctx = thread_state->context();
      ctx->lr = 0xBEBEBEBE;

      pre_call(ctx);

      fn->Call(thread_state.get(), ctx->lr);

      post_call(ctx);
    }
  }

  size_t memory_size;
  std::unique_ptr<Memory> memory;
  std::vector<std::unique_ptr<Runtime>> runtimes;
};

inline hir::Value* LoadGPR(hir::HIRBuilder& b, int reg) {
  return b.LoadContext(offsetof(PPCContext, r) + reg * 8, hir::INT64_TYPE);
}
inline void StoreGPR(hir::HIRBuilder& b, int reg, hir::Value* value) {
  b.StoreContext(offsetof(PPCContext, r) + reg * 8, value);
}

inline hir::Value* LoadFPR(hir::HIRBuilder& b, int reg) {
  return b.LoadContext(offsetof(PPCContext, f) + reg * 8, hir::FLOAT64_TYPE);
}
inline void StoreFPR(hir::HIRBuilder& b, int reg, hir::Value* value) {
  b.StoreContext(offsetof(PPCContext, f) + reg * 8, value);
}

inline hir::Value* LoadVR(hir::HIRBuilder& b, int reg) {
  return b.LoadContext(offsetof(PPCContext, v) + reg * 16, hir::VEC128_TYPE);
}
inline void StoreVR(hir::HIRBuilder& b, int reg, hir::Value* value) {
  b.StoreContext(offsetof(PPCContext, v) + reg * 16, value);
}

}  // namespace test
}  // namespace alloy

#endif  // ALLOY_TEST_UTIL_H_
