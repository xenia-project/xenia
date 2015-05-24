/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_TEST_UTIL_H_
#define XENIA_TEST_UTIL_H_

#include "xenia/base/main.h"
#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/cpu.h"
#include "xenia/cpu/frontend/ppc_context.h"
#include "xenia/cpu/frontend/ppc_frontend.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/test_module.h"

#include "third_party/catch/single_include/catch.hpp"

#define XENIA_TEST_X64 1

namespace xe {
namespace cpu {
namespace test {

using xe::cpu::frontend::PPCContext;
using xe::cpu::Runtime;

class TestFunction {
 public:
  TestFunction(std::function<void(hir::HIRBuilder& b)> generator) {
    memory_size = 16 * 1024 * 1024;
    memory.reset(new Memory());
    memory->Initialize();

#if XENIA_TEST_X64
    {
      auto processor =
          std::make_unique<Processor>(memory.get(), nullptr, nullptr);
      processor->Setup();
      processors.emplace_back(std::move(processor));
    }
#endif  // XENIA_TEST_X64

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
      xe::cpu::Function* fn;
      processor->ResolveFunction(0x80000000, &fn);

      uint32_t stack_size = 64 * 1024;
      uint32_t stack_address = memory_size - stack_size;
      uint32_t thread_state_address = stack_address - 0x1000;
      auto thread_state = std::make_unique<ThreadState>(
          processor.get(), 0x100, ThreadStackType::kUserStack, stack_address,
          stack_size, thread_state_address);
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

}  // namespace test
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_TEST_UTIL_H_
