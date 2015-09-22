/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/breakpoint.h"

#include "xenia/base/memory.h"
#include "xenia/base/string_util.h"
#include "xenia/cpu/backend/code_cache.h"
#include "xenia/debug/debugger.h"
#include "xenia/emulator.h"

namespace xe {
namespace debug {

void CodeBreakpoint::ForEachHostAddress(
    std::function<void(uintptr_t)> callback) const {
  auto processor = debugger_->emulator()->processor();
  if (address_type_ == AddressType::kGuest) {
    auto guest_address = this->guest_address();

    // Lookup all functions that contain this guest address and patch them.
    auto functions = processor->FindFunctionsWithAddress(guest_address);

    if (functions.empty()) {
      // If function does not exist demand it, as we need someplace to put our
      // breakpoint. Note that this follows the same resolution rules as the
      // JIT, so what's returned is the function the JIT would have jumped to.
      auto fn = processor->ResolveFunction(guest_address);
      if (!fn) {
        // TODO(benvanik): error out better with 'invalid breakpoint'?
        assert_not_null(fn);
        return;
      }
      functions.push_back(fn);
    }
    assert_false(functions.empty());

    for (auto function : functions) {
      // TODO(benvanik): other function types.
      assert_true(function->is_guest());
      auto guest_function = reinterpret_cast<cpu::GuestFunction*>(function);
      uintptr_t host_address =
          guest_function->MapGuestAddressToMachineCode(guest_address);
      assert_not_zero(host_address);
      callback(host_address);
    }
  } else {
    // Direct host address patching.
    callback(host_address());
  }
}

xe::cpu::GuestFunction* CodeBreakpoint::guest_function() const {
  auto processor = debugger_->emulator()->processor();
  if (address_type_ == AddressType::kGuest) {
    auto functions = processor->FindFunctionsWithAddress(guest_address());
    if (functions.empty()) {
      return nullptr;
    } else if (functions[0]->is_guest()) {
      return static_cast<xe::cpu::GuestFunction*>(functions[0]);
    }
    return nullptr;
  } else {
    return processor->backend()->code_cache()->LookupFunction(host_address());
  }
}

bool CodeBreakpoint::ContainsHostAddress(uintptr_t search_address) const {
  bool contains = false;
  ForEachHostAddress([&contains, search_address](uintptr_t host_address) {
    if (host_address == search_address) {
      contains = true;
    }
  });
  return contains;
}

void CodeBreakpoint::Install() {
  Breakpoint::Install();

  assert_true(patches_.empty());
  ForEachHostAddress([this](uintptr_t host_address) {
    patches_.emplace_back(host_address, PatchAddress(host_address));
  });
}

void CodeBreakpoint::Uninstall() {
  // Simply unpatch all locations we patched when installing.
  for (auto& location : patches_) {
    UnpatchAddress(location.first, location.second);
  }
  patches_.clear();

  Breakpoint::Uninstall();
}

uint16_t CodeBreakpoint::PatchAddress(uintptr_t host_address) {
  auto ptr = reinterpret_cast<void*>(host_address);
  auto original_bytes = xe::load_and_swap<uint16_t>(ptr);
  assert_true(original_bytes != 0x0F0B);
  xe::store_and_swap<uint16_t>(ptr, 0x0F0B);
  return original_bytes;
}

void CodeBreakpoint::UnpatchAddress(uintptr_t host_address,
                                    uint16_t original_bytes) {
  auto ptr = reinterpret_cast<uint8_t*>(host_address);
  auto instruction_bytes = xe::load_and_swap<uint16_t>(ptr);
  assert_true(instruction_bytes == 0x0F0B);
  xe::store_and_swap<uint16_t>(ptr, original_bytes);
}

std::string CodeBreakpoint::to_string() const {
  if (address_type_ == AddressType::kGuest) {
    auto str =
        std::string("PPC ") + xe::string_util::to_hex_string(guest_address());
    auto processor = debugger_->emulator()->processor();
    auto functions = processor->FindFunctionsWithAddress(guest_address());
    if (functions.empty()) {
      return str;
    }
    str += " " + functions[0]->name();
    return str;
  } else {
    return std::string("x64 ") + xe::string_util::to_hex_string(host_address());
  }
}

}  // namespace debug
}  // namespace xe
