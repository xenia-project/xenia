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
  
  // Detect corrupted builtin argument pointers before calling the handler.
  // A very low non-null address (< 0x10000) is almost certainly invalid and
  // indicates memory corruption, likely from guest code buffer overflow.
  // This check helps identify the problem before it causes a crash in the
  // mutex operations within builtin handlers.
  if (arg0_ && reinterpret_cast<uintptr_t>(arg0_) < 0x10000) {
    XELOGE(
        "BuiltinFunction '{}' detected corrupted arg0 pointer: {:p}. "
        "This likely indicates memory corruption from guest code. "
        "The emulation cannot continue safely.",
        name(), arg0_);
    assert_always("BuiltinFunction arg0 corrupted - guest code memory corruption detected");
    return false;
  }
  
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

  ThreadState* original_thread_state = ThreadState::Get();
  if (original_thread_state != thread_state) {
    ThreadState::Bind(thread_state);
  }

  // Validate PPCContext critical pointers before executing guest code.
  // This detects corruption that may have occurred from a previous function.
  auto ctx = thread_state->context();
  auto& expected_global_mutex = xe::global_critical_region::mutex();
  if (ctx->global_mutex != &expected_global_mutex) {
    uintptr_t corrupt_ptr = reinterpret_cast<uintptr_t>(ctx->global_mutex);
    XELOGE(
        "GuestFunction '{}' at 0x{:08X} called with corrupted PPCContext. "
        "global_mutex pointer is {:p} / 0x{:X} (expected {:p}). "
        "Corruption likely occurred in a previous function call.",
        name(), address(), ctx->global_mutex, corrupt_ptr,
        static_cast<void*>(&expected_global_mutex));
    assert_always(
        "PPCContext already corrupted before function execution. Previous "
        "guest function likely has buffer overflow.");
    return false;
  }

  bool result = CallImpl(thread_state, return_address);

  // Validate context after execution to catch corruption during this function.
  if (ctx->global_mutex != &expected_global_mutex) {
    uintptr_t corrupt_ptr = reinterpret_cast<uintptr_t>(ctx->global_mutex);
    XELOGE(
        "GuestFunction '{}' at 0x{:08X} CORRUPTED PPCContext during "
        "execution. global_mutex changed to {:p} / 0x{:X}. "
        "This function has a buffer overflow or invalid memory write.",
        name(), address(), ctx->global_mutex, corrupt_ptr);
    assert_always(
        "Memory corruption detected in guest function execution. "
        "The function has a buffer overflow bug.");
    return false;
  }

  if (original_thread_state != thread_state) {
    ThreadState::Bind(original_thread_state);
  }

  return result;
}

}  // namespace cpu
}  // namespace xe