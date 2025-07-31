/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/function.h"

#include "xenia/base/logging.h"
#include "xenia/cpu/symbol.h"
#include "xenia/cpu/thread_state.h"

namespace xe {
namespace cpu {

Function::Function(Module* module, uint32_t address)
    : Symbol(Symbol::Type::kFunction, module, address) {}

Function::~Function() = default;

BuiltinFunction::BuiltinFunction(Module* module, uint32_t address)
    : Function(module, address) {
  behavior_ = Behavior::kBuiltin;
}

BuiltinFunction::~BuiltinFunction() = default;

void BuiltinFunction::SetupBuiltin(Handler handler, void* arg0, void* arg1) {
  behavior_ = Behavior::kBuiltin;
  handler_ = handler;
  arg0_ = arg0;
  arg1_ = arg1;
}

bool BuiltinFunction::Call(ThreadState* thread_state, uint32_t return_address) {
  // SCOPE_profile_cpu_f("cpu");

  ThreadState* original_thread_state = ThreadState::Get();
  if (original_thread_state != thread_state) {
    ThreadState::Bind(thread_state);
  }

  assert_not_null(handler_);
  handler_(thread_state->context(), arg0_, arg1_);

  if (original_thread_state != thread_state) {
    ThreadState::Bind(original_thread_state);
  }

  return true;
}

GuestFunction::GuestFunction(Module* module, uint32_t address)
    : Function(module, address) {
  behavior_ = Behavior::kDefault;
}

GuestFunction::~GuestFunction() = default;

void GuestFunction::SetupExtern(ExternHandler handler, Export* export_data) {
  behavior_ = Behavior::kExtern;
  extern_handler_ = handler;
  export_data_ = export_data;
}

const SourceMapEntry* GuestFunction::LookupGuestAddress(
    uint32_t guest_address) const {
  // TODO(benvanik): binary search? We know the list is sorted by code order.
  for (size_t i = 0; i < source_map_.size(); ++i) {
    const auto& entry = source_map_[i];
    if (entry.guest_address == guest_address) {
      return &entry;
    }
  }
  return nullptr;
}

const SourceMapEntry* GuestFunction::LookupHIROffset(uint32_t offset) const {
  // TODO(benvanik): binary search? We know the list is sorted by code order.
  for (size_t i = 0; i < source_map_.size(); ++i) {
    const auto& entry = source_map_[i];
    if (entry.hir_offset >= offset) {
      return &entry;
    }
  }
  return nullptr;
}

const SourceMapEntry* GuestFunction::LookupMachineCodeOffset(
    uint32_t offset) const {
  // TODO(benvanik): binary search? We know the list is sorted by code order.
  for (int64_t i = source_map_.size() - 1; i >= 0; --i) {
    const auto& entry = source_map_[i];
    if (entry.code_offset <= offset) {
      return &entry;
    }
  }
  return source_map_.empty() ? nullptr : &source_map_[0];
}

uint32_t GuestFunction::MapGuestAddressToMachineCodeOffset(
    uint32_t guest_address) const {
  auto entry = LookupGuestAddress(guest_address);
  return entry ? entry->code_offset : 0;
}

uintptr_t GuestFunction::MapGuestAddressToMachineCode(
    uint32_t guest_address) const {
  auto entry = LookupGuestAddress(guest_address);
  return reinterpret_cast<uintptr_t>(machine_code()) +
         (entry ? entry->code_offset : 0);
}

uint32_t GuestFunction::MapMachineCodeToGuestAddress(
    uintptr_t host_address) const {
  auto entry = LookupMachineCodeOffset(static_cast<uint32_t>(
      host_address - reinterpret_cast<uintptr_t>(machine_code())));
  return entry ? entry->guest_address : address();
}

bool GuestFunction::Call(ThreadState* thread_state, uint32_t return_address) {
  // SCOPE_profile_cpu_f("cpu");

  XELOGI("GuestFunction::Call: Starting call to function 0x{:08X}", address());
  fflush(stdout); fflush(stderr);
  XELOGI("GuestFunction::Call: this pointer = 0x{:016X}", (uintptr_t)this);
  fflush(stdout); fflush(stderr);
  
  // Check vtable - examine entire vtable structure
  void** vtable = *(void***)this;
  XELOGI("GuestFunction::Call: vtable pointer = 0x{:016X}", (uintptr_t)vtable);
  fflush(stdout); fflush(stderr);
  if (vtable) {
    // Print first 8 vtable entries to understand the layout
    for (int i = 0; i < 8; i++) {
      XELOGI("GuestFunction::Call: vtable[{}] = 0x{:016X}", i, (uintptr_t)vtable[i]);
      fflush(stdout); fflush(stderr);
    }
  }
  
  // Also check if 'this' pointer itself is corrupt
  XELOGI("GuestFunction::Call: Checking object integrity - address()=0x{:08X}", address());
  fflush(stdout); fflush(stderr);
  
  ThreadState* original_thread_state = ThreadState::Get();
  if (original_thread_state != thread_state) {
    ThreadState::Bind(thread_state);
  }

  XELOGI("GuestFunction::Call: About to call CallImpl");
  fflush(stdout); fflush(stderr);
  
  // Get the vtable and check where CallImpl will actually jump to  
  void** current_vtable = *(void***)this;
  if (current_vtable) {
    XELOGI("GuestFunction::Call: Current vtable pointer = 0x{:016X}", (uintptr_t)current_vtable);
    // Check if we can safely read vtable[6]
    try {
      void* callimpl_ptr = current_vtable[6];
      XELOGI("GuestFunction::Call: CallImpl will jump to vtable[6] = 0x{:016X}", (uintptr_t)callimpl_ptr);
    } catch (...) {
      XELOGE("GuestFunction::Call: Cannot read vtable[6] - memory access error!");
    }
    fflush(stdout); fflush(stderr);
  } else {
    XELOGE("GuestFunction::Call: vtable pointer is null!");
    fflush(stdout); fflush(stderr);
  }
  
  XELOGI("GuestFunction::Call: About to make virtual call to CallImpl");
  fflush(stdout); fflush(stderr);
  
  // Before making the virtual call, let's examine the actual vtable entry that will be used
  void** vtable_check = *(void***)this;
  if (vtable_check && vtable_check[6]) {
    XELOGI("GuestFunction::Call: Final vtable check - CallImpl pointer = 0x{:016X}", (uintptr_t)vtable_check[6]);
    
    // Check if this points to thunk address (which would be wrong)
    uintptr_t thunk_start = 0x134604000; // From the logs - adjust as needed
    uintptr_t thunk_end = thunk_start + 0x1000; // Assume 4KB thunk area
    uintptr_t callimpl_addr = (uintptr_t)vtable_check[6];
    
    if (callimpl_addr >= thunk_start && callimpl_addr < thunk_end) {
      XELOGE("GuestFunction::Call: ERROR - CallImpl vtable entry points to thunk area! This will crash!");
      XELOGE("GuestFunction::Call: CallImpl address 0x{:016X} is in thunk range [0x{:016X}, 0x{:016X})", 
             callimpl_addr, thunk_start, thunk_end);
      
      // Print the entire vtable for analysis
      XELOGE("GuestFunction::Call: Complete vtable dump:");
      for (int i = 0; i < 8; i++) {
        XELOGE("GuestFunction::Call: CORRUPTED vtable[{}] = 0x{:016X}", i, (uintptr_t)vtable_check[i]);
      }
      fflush(stdout); fflush(stderr);
      return false; // Avoid the crash
    } else {
      XELOGI("GuestFunction::Call: CallImpl pointer looks valid (not in thunk range)");
    }
  } else {
    XELOGE("GuestFunction::Call: vtable or vtable[6] is null!");
  }
  fflush(stdout); fflush(stderr);
  
  bool result = CallImpl(thread_state, return_address);
  XELOGI("GuestFunction::Call: CallImpl returned");

  if (original_thread_state != thread_state) {
    ThreadState::Bind(original_thread_state);
  }

  return result;
}

}  // namespace cpu
}  // namespace xe
