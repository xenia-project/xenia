/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/breakpoint.h"

#include "xenia/base/string_util.h"
#include "xenia/cpu/backend/backend.h"
#include "xenia/cpu/backend/code_cache.h"

namespace xe {
namespace cpu {

Breakpoint::Breakpoint(Processor* processor, AddressType address_type,
                       uint64_t address, HitCallback hit_callback)
    : processor_(processor),
      address_type_(address_type),
      address_(address),
      hit_callback_(hit_callback) {}

Breakpoint::~Breakpoint() { assert_false(installed_); }

void Breakpoint::Install() {
  assert_false(installed_);
  processor_->backend()->InstallBreakpoint(this);
  installed_ = true;
}

void Breakpoint::Uninstall() {
  assert_true(installed_);
  processor_->backend()->UninstallBreakpoint(this);
  installed_ = false;
}

std::string Breakpoint::to_string() const {
  if (address_type_ == AddressType::kGuest) {
    auto str =
        std::string("PPC ") + xe::string_util::to_hex_string(guest_address());
    auto functions = processor_->FindFunctionsWithAddress(guest_address());
    if (functions.empty()) {
      return str;
    }
    str += " " + functions[0]->name();
    return str;
  } else {
    return std::string(XE_HOST_ARCH_NAME " ") +
           xe::string_util::to_hex_string(host_address());
  }
}

GuestFunction* Breakpoint::guest_function() const {
  if (address_type_ == AddressType::kGuest) {
    auto functions = processor_->FindFunctionsWithAddress(guest_address());
    if (functions.empty()) {
      return nullptr;
    } else if (functions[0]->is_guest()) {
      return static_cast<xe::cpu::GuestFunction*>(functions[0]);
    }
    return nullptr;
  } else {
    return processor_->backend()->code_cache()->LookupFunction(host_address());
  }
}

bool Breakpoint::ContainsHostAddress(uintptr_t search_address) const {
  bool contains = false;
  ForEachHostAddress([&contains, search_address](uintptr_t host_address) {
    if (host_address == search_address) {
      contains = true;
    }
  });
  return contains;
}

void Breakpoint::ForEachHostAddress(
    std::function<void(uintptr_t)> callback) const {
  if (address_type_ == AddressType::kGuest) {
    auto guest_address = this->guest_address();

    // Lookup all functions that contain this guest address and patch them.
    auto functions = processor_->FindFunctionsWithAddress(guest_address);

    if (functions.empty()) {
      // If function does not exist demand it, as we need someplace to put our
      // breakpoint. Note that this follows the same resolution rules as the
      // JIT, so what's returned is the function the JIT would have jumped to.
      auto fn = processor_->ResolveFunction(guest_address);
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
      auto guest_function = reinterpret_cast<GuestFunction*>(function);
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

}  // namespace cpu
}  // namespace xe