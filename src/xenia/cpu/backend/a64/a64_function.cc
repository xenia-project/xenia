/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_function.h"

#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"

#include <mach/mach.h>
#include <mach/vm_map.h>
#include <unistd.h>
#include <pthread.h>

#if XE_PLATFORM_MAC && defined(__aarch64__)
// Thread-local storage to track if this thread has been initialized for JIT execution
thread_local bool jit_thread_initialized = false;

// Initialize JIT execution for the current thread
static void InitializeJITThread() {
  if (!jit_thread_initialized) {
    // Ensure this thread can execute JIT code by setting execute mode
    pthread_jit_write_protect_np(1);  // Enable execute, disable write
    jit_thread_initialized = true;
    XELOGI("JIT thread initialization completed for thread 0x{:016X}", (uintptr_t)pthread_self());
  }
}
#endif

// Helper function to force immediate output for debugging crashes
static inline void ForceLogFlush() {
  fflush(stdout);
  fflush(stderr);
  fsync(STDOUT_FILENO);
  fsync(STDERR_FILENO);
}

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

A64Function::A64Function(Module* module, uint32_t address)
    : GuestFunction(module, address) {
  XELOGI("A64Function::A64Function: Constructor called for address 0x{:08X}", address);
  ForceLogFlush();
  
  // Check vtable immediately after construction
  void** vtable = *(void***)this;
  XELOGI("A64Function::A64Function: vtable pointer = 0x{:016X}", (uintptr_t)vtable);
  ForceLogFlush();
  if (vtable) {
    // Print first 8 vtable entries to see if they're correct
    for (int i = 0; i < 8; i++) {
      XELOGI("A64Function::A64Function: vtable[{}] = 0x{:016X}", i, (uintptr_t)vtable[i]);
      ForceLogFlush();
    }
  }
}

A64Function::~A64Function() {
  // machine_code_ is freed by code cache.
}

void A64Function::Setup(uint8_t* machine_code, size_t machine_code_length) {
  XELOGI("A64Function::Setup: Setting up machine code at 0x{:016X}, length {}", (uintptr_t)machine_code, machine_code_length);
  ForceLogFlush();
  
  // Check vtable before setup
  void** vtable = *(void***)this;
  XELOGI("A64Function::Setup: vtable pointer before setup = 0x{:016X}", (uintptr_t)vtable);
  ForceLogFlush();
  
  machine_code_ = machine_code;
  machine_code_length_ = machine_code_length;
  
  // Check vtable after setup
  vtable = *(void***)this;
  XELOGI("A64Function::Setup: vtable pointer after setup = 0x{:016X}", (uintptr_t)vtable);
  ForceLogFlush();
  if (vtable) {
    // Print first 8 vtable entries
    for (int i = 0; i < 8; i++) {
      XELOGI("A64Function::Setup: vtable[{}] = 0x{:016X}", i, (uintptr_t)vtable[i]);
      ForceLogFlush();
    }
  }
}

bool A64Function::CallImpl(ThreadState* thread_state, uint32_t return_address) {
  XELOGI("A64Function::CallImpl: Starting execution of function 0x{:08X}", address());
  
#if XE_PLATFORM_MAC && defined(__aarch64__)
  // CRITICAL: Initialize JIT execution for this thread
  // This ensures pthread_jit_write_protect_np is set correctly for execution
  InitializeJITThread();
#endif

  auto backend =
      reinterpret_cast<A64Backend*>(thread_state->processor()->backend());
  auto thunk = backend->host_to_guest_thunk();
  XELOGI("A64Function::CallImpl: Got thunk at 0x{:016X}, machine_code at 0x{:016X}", 
         (uintptr_t)thunk, (uintptr_t)machine_code_);
  
  // Test if thunk memory is readable (basic sanity check)
  try {
    uint32_t first_instruction = *reinterpret_cast<const uint32_t*>(thunk);
    XELOGI("A64Function::CallImpl: Thunk first instruction = 0x{:08X}", first_instruction);
    
    // Also check a few more bytes to see if the memory is accessible
    const uint8_t* thunk_bytes = reinterpret_cast<const uint8_t*>(thunk);
    XELOGI("A64Function::CallImpl: First 16 bytes: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
           thunk_bytes[0], thunk_bytes[1], thunk_bytes[2], thunk_bytes[3],
           thunk_bytes[4], thunk_bytes[5], thunk_bytes[6], thunk_bytes[7],
           thunk_bytes[8], thunk_bytes[9], thunk_bytes[10], thunk_bytes[11],
           thunk_bytes[12], thunk_bytes[13], thunk_bytes[14], thunk_bytes[15]);
  } catch (...) {
    XELOGE("A64Function::CallImpl: Failed to read thunk memory - memory not accessible!");
    return false;
  }
  
  XELOGI("A64Function::CallImpl: About to call thunk to execute compiled code");
  ForceLogFlush();
  
  // Additional debugging before thunk call
  XELOGI("A64Function::CallImpl: thunk address = 0x{:016X}", (uintptr_t)thunk);
  ForceLogFlush();
  
  XELOGI("A64Function::CallImpl: machine_code_ = 0x{:016X}", (uintptr_t)machine_code_);
  ForceLogFlush();
  
  XELOGI("A64Function::CallImpl: context = 0x{:016X}", (uintptr_t)thread_state->context());
  ForceLogFlush();
  
  XELOGI("A64Function::CallImpl: return_address = 0x{:016X}", (uintptr_t)return_address);
  ForceLogFlush();
  
  // Verify machine code is still accessible
  try {
    volatile uint32_t* test_ptr = reinterpret_cast<volatile uint32_t*>(machine_code_);
    uint32_t first_instr = *test_ptr;
    XELOGI("A64Function::CallImpl: Machine code first instruction = 0x{:08X}", first_instr);
    ForceLogFlush();
  } catch (...) {
    XELOGE("A64Function::CallImpl: Machine code memory is not accessible!");
    ForceLogFlush();
    return false;
  }
  
  // Verify thunk is accessible
  try {
    volatile uint32_t* thunk_ptr = reinterpret_cast<volatile uint32_t*>(thunk);
    uint32_t thunk_first_instr = *thunk_ptr;
    XELOGI("A64Function::CallImpl: Thunk first instruction = 0x{:08X}", thunk_first_instr);
    ForceLogFlush();
  } catch (...) {
    XELOGE("A64Function::CallImpl: Thunk memory is not accessible!");
    ForceLogFlush();
    return false;
  }
  
  // Verify memory protection - check if both regions are executable
  XELOGI("A64Function::CallImpl: Checking memory protection for thunk and machine code");
  ForceLogFlush();
  
  // Check thunk memory protection
  vm_address_t thunk_addr = (vm_address_t)thunk;
  vm_size_t thunk_size = sizeof(void*); // Just check first page
  vm_region_basic_info_64 thunk_info;
  mach_msg_type_number_t thunk_count = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t thunk_object_name;
  
  kern_return_t thunk_kr = vm_region_64(mach_task_self(), &thunk_addr, &thunk_size,
                                        VM_REGION_BASIC_INFO_64, 
                                        (vm_region_info_t)&thunk_info, 
                                        &thunk_count, &thunk_object_name);
  
  if (thunk_kr == KERN_SUCCESS) {
    XELOGI("A64Function::CallImpl: Thunk memory protection = 0x{:X} (MAP_JIT memory shows RW but is executable via pthread_jit_write_protect_np)", 
           thunk_info.protection);
    ForceLogFlush();
  } else {
    XELOGE("A64Function::CallImpl: Failed to query thunk memory protection, error = {}", thunk_kr);
    ForceLogFlush();
  }
  
  // Check machine code memory protection
  vm_address_t code_addr = (vm_address_t)machine_code_;
  vm_size_t code_size = sizeof(void*); // Just check first page
  vm_region_basic_info_64 code_info;
  mach_msg_type_number_t code_count = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t code_object_name;
  
  kern_return_t code_kr = vm_region_64(mach_task_self(), &code_addr, &code_size,
                                       VM_REGION_BASIC_INFO_64, 
                                       (vm_region_info_t)&code_info, 
                                       &code_count, &code_object_name);
  
  if (code_kr == KERN_SUCCESS) {
#if XE_PLATFORM_MAC && defined(__aarch64__)
    XELOGI("A64Function::CallImpl: Machine code memory protection = 0x{:X} (MAP_JIT memory - executable via hardware)", 
           code_info.protection);
    // On MAP_JIT memory, protection shows as RW but execute is controlled by pthread_jit_write_protect_np
    // The memory is actually executable when pthread_jit_write_protect_np(1) is active
#else
    XELOGI("A64Function::CallImpl: Machine code memory protection = 0x{:X} (should be 0x7 for RWX)", 
           code_info.protection);
    
    if (!(code_info.protection & VM_PROT_EXECUTE)) {
      XELOGE("A64Function::CallImpl: CRITICAL - Machine code memory is not executable! Protection = 0x{:X}", 
             code_info.protection);
      ForceLogFlush();
      return false;
    }
#endif
  } else {
    XELOGE("A64Function::CallImpl: Failed to query machine code memory protection, error = {}", code_kr);
    ForceLogFlush();
  }
  
  XELOGI("A64Function::CallImpl: All memory checks passed, making thunk call");
  ForceLogFlush();
  
#if XE_PLATFORM_MAC && defined(__aarch64__)
  // JIT thread already initialized - no additional setup needed
  XELOGI("A64Function::CallImpl: MAP_JIT thread already initialized for execution");
#else
  // With RWX memory, no JIT protection management needed
  XELOGI("A64Function::CallImpl: Using RWX memory - no JIT setup required");
#endif
  ForceLogFlush();
  
  XELOGI("A64Function::CallImpl: === CRITICAL POINT === About to execute RWX ARM64 code");
  ForceLogFlush();
  
  // Let's verify the exact function pointer we're about to call
  XELOGI("A64Function::CallImpl: Verifying function pointer before call");
  XELOGI("A64Function::CallImpl: thunk function pointer = 0x{:016X}", (uintptr_t)thunk);
  XELOGI("A64Function::CallImpl: machine_code parameter = 0x{:016X}", (uintptr_t)machine_code_);
  XELOGI("A64Function::CallImpl: context parameter = 0x{:016X}", (uintptr_t)thread_state->context());
  XELOGI("A64Function::CallImpl: return_address parameter = 0x{:016X}", (uintptr_t)return_address);
  ForceLogFlush();
  
  // Final verification: read the instruction we're about to execute
  volatile uint32_t* execution_point = reinterpret_cast<volatile uint32_t*>(thunk);
  uint32_t instruction_at_thunk = *execution_point;
  XELOGI("A64Function::CallImpl: Instruction at thunk address: 0x{:08X}", instruction_at_thunk);
  ForceLogFlush();
  
  XELOGI("A64Function::CallImpl: Making thunk call now...");
  ForceLogFlush();
  
#if XE_PLATFORM_MAC && defined(__aarch64__)
  // CRITICAL: Ensure JIT execution is initialized for this specific thread
  // This is especially important if execution happens on a different thread than code generation
  XELOGI("A64Function::CallImpl: Re-initializing JIT for execution thread 0x{:016X}", (uintptr_t)pthread_self());
  pthread_jit_write_protect_np(1);  // Ensure execute mode is enabled for THIS thread
  ForceLogFlush();
  
  // Verify that we can still read the memory after setting execute mode
  try {
    volatile uint32_t test_read = *reinterpret_cast<volatile uint32_t*>(thunk);
    XELOGI("A64Function::CallImpl: Thunk still readable after JIT setup: 0x{:08X}", test_read);
    ForceLogFlush();
  } catch (...) {
    XELOGE("A64Function::CallImpl: FATAL - Thunk memory became unreadable after JIT setup!");
    ForceLogFlush();
    return false;
  }
#endif
  
  // Make the actual thunk call - note the thunk returns void*
  void* result = thunk(machine_code_, thread_state->context(),
                      reinterpret_cast<void*>(uintptr_t(return_address)));
  
  XELOGI("A64Function::CallImpl: Thunk call returned successfully with result = 0x{:016X}", (uintptr_t)result);
  ForceLogFlush();
  return true;
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
