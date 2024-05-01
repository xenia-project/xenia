/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_TESTING_UTIL_H_
#define XENIA_CPU_TESTING_UTIL_H_

#include <vector>

#include "xenia/base/platform.h"
#if XE_ARCH_AMD64
#include "xenia/cpu/backend/x64/x64_backend.h"
#elif XE_ARCH_ARM64
#include "xenia/cpu/backend/a64/a64_backend.h"
#endif
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/test_module.h"

#include "third_party/catch/include/catch.hpp"

namespace xe {
namespace cpu {
namespace testing {

using xe::cpu::ppc::PPCContext;

class TestFunction {
 public:
  TestFunction(std::function<void(hir::HIRBuilder& b)> generator) {
    memory_size = 16 * 1024 * 1024;
    memory.reset(new Memory());
    memory->Initialize();

    {
      std::unique_ptr<xe::cpu::backend::Backend> backend;
#if XE_ARCH_AMD64
      backend.reset(new xe::cpu::backend::x64::X64Backend());
#elif XE_ARCH_ARM64
      backend.reset(new xe::cpu::backend::a64::A64Backend());
#endif  // XE_ARCH
      if (backend) {
        auto processor = std::make_unique<Processor>(memory.get(), nullptr);
        processor->Setup(std::move(backend));
        processors.emplace_back(std::move(processor));
      }
    }

    for (auto& processor : processors) {
      auto module = std::make_unique<xe::cpu::TestModule>(
          processor.get(), "Test",
          [](uint64_t address) { return address == 0x80000000; },
          [generator](hir::HIRBuilder& b) {
            generator(b);
            return true;
          });
      processor->AddModule(std::move(module));
      processor->backend()->CommitExecutableRange(0x80000000, 0x80010000);
    }
  }

  ~TestFunction() {
    processors.clear();
    memory.reset();
  }

  void Run(std::function<void(PPCContext*)> pre_call,
           std::function<void(PPCContext*)> post_call) {
    for (auto& processor : processors) {
      auto fn = processor->ResolveFunction(0x80000000);

      uint32_t stack_size = 64 * 1024;
      uint32_t stack_address = memory_size - stack_size;
      uint32_t thread_state_address = stack_address - 0x1000;
      auto thread_state = std::make_unique<ThreadState>(processor.get(), 0x100);
      assert_always();  // TODO: Allocate a thread stack!!!
      auto ctx = thread_state->context();
      ctx->lr = 0xBCBCBCBC;

      pre_call(ctx);

      fn->Call(thread_state.get(), uint32_t(ctx->lr));

      post_call(ctx);
    }
  }

  uint32_t memory_size;
  std::unique_ptr<Memory> memory;
  std::vector<std::unique_ptr<Processor>> processors;
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

}  // namespace testing
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_TESTING_UTIL_H_
