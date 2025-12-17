/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_function.h"

#ifdef XE_PLATFORM_MAC
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#include <pthread.h>
#endif

#include "xenia/base/logging.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"

#if XE_PLATFORM_MAC && defined(__aarch64__)
thread_local bool jit_thread_initialized = false;

// Initialize JIT execution for the current thread
static void InitializeJITThread() {
  if (!jit_thread_initialized) {
    // Ensure this thread can execute JIT code by setting execute mode
    pthread_jit_write_protect_np(1);  // Enable execute, disable write
    jit_thread_initialized = true;
  }
}
#endif

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

A64Function::A64Function(Module* module, uint32_t address)
    : GuestFunction(module, address) {}

A64Function::~A64Function() {
  // machine_code_ is freed by code cache.
}

void A64Function::Setup(uint8_t* machine_code, size_t machine_code_length) {
  machine_code_ = machine_code;
  machine_code_length_ = machine_code_length;
}

bool A64Function::CallImpl(ThreadState* thread_state, uint32_t return_address) {
#if XE_PLATFORM_MAC && defined(__aarch64__)
  // Initialize JIT execution for this thread
  // This ensures pthread_jit_write_protect_np is set correctly for execution
  InitializeJITThread();
#endif

  auto backend =
      reinterpret_cast<A64Backend*>(thread_state->processor()->backend());
  auto thunk = backend->host_to_guest_thunk();

  // Make the actual thunk call
  thunk(machine_code_, thread_state->context(),
        reinterpret_cast<void*>(uintptr_t(return_address)));

  return true;
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe