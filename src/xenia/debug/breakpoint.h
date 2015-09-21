/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_BREAKPOINT_H_
#define XENIA_DEBUG_BREAKPOINT_H_

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/cpu/function.h"

namespace xe {
namespace debug {

class Debugger;

class Breakpoint {
 public:
  enum class Type {
    kStepping,
    kCode,
    kData,
    kExport,
    kGpu,
  };

  Breakpoint(Debugger* debugger, Type type)
      : debugger_(debugger), type_(type) {}

  virtual ~Breakpoint() { assert_false(installed_); }

  Debugger* debugger() const { return debugger_; }
  Type type() const { return type_; }

  // Whether the breakpoint has been enabled by the user.
  bool is_enabled() const { return enabled_; }

  // Toggles the breakpoint state.
  // Assumes the caller holds the global lock.
  void set_enabled(bool is_enabled) {
    enabled_ = is_enabled;
    if (!enabled_ && installed_) {
      Uninstall();
    } else if (enabled_ && !installed_ && !suspend_count_) {
      Install();
    }
  }

  virtual std::string to_string() const { return "Breakpoint"; }

 private:
  friend class Debugger;
  // Suspends the breakpoint until it is resumed with Resume.
  // This preserves the user enabled state.
  // Assumes the caller holds the global lock.
  void Suspend() {
    ++suspend_count_;
    if (installed_) {
      Uninstall();
    }
  }

  // Resumes a previously-suspended breakpoint, reinstalling it if required.
  // Assumes the caller holds the global lock.
  void Resume() {
    --suspend_count_;
    if (!suspend_count_ && enabled_) {
      Install();
    }
  }

 protected:
  virtual void Install() { installed_ = true; }
  virtual void Uninstall() { installed_ = false; }

  Debugger* debugger_ = nullptr;
  Type type_;
  // True if patched into code.
  bool installed_ = false;
  // True if user enabled.
  bool enabled_ = true;
  // Depth of suspends (must be 0 to be installed). Defaults to suspended so
  // that it's never installed unless the debugger knows about it.
  int suspend_count_ = 1;
};

class CodeBreakpoint : public Breakpoint {
 public:
  enum class AddressType {
    kGuest,
    kHost,
  };

  CodeBreakpoint(Debugger* debugger, AddressType address_type, uint64_t address)
      : CodeBreakpoint(debugger, Type::kCode, address_type, address) {}

  AddressType address_type() const { return address_type_; }
  uint64_t address() const { return address_; }
  uint32_t guest_address() const {
    assert_true(address_type_ == AddressType::kGuest);
    return static_cast<uint32_t>(address_);
  }
  uintptr_t host_address() const {
    assert_true(address_type_ == AddressType::kHost);
    return static_cast<uintptr_t>(address_);
  }

  // TODO(benvanik): additional support:
  // - conditions (expressions? etc?)
  // - thread restrictions (set of thread ids)
  // - hit counters

  // Returns a guest function that contains the guest address, if any.
  // If there are multiple functions that contain the address a random one will
  // be returned. If this is a host-address code breakpoint this will attempt to
  // find a function with that address and otherwise return nullptr.
  xe::cpu::GuestFunction* guest_function() const;

  // Returns true if this breakpoint, when installed, contains the given host
  // address.
  bool ContainsHostAddress(uintptr_t search_address) const;

  std::string to_string() const override;

 protected:
  CodeBreakpoint(Debugger* debugger, Type type, AddressType address_type,
                 uint64_t address)
      : Breakpoint(debugger, type),
        address_type_(address_type),
        address_(address) {}

  void ForEachHostAddress(std::function<void(uintptr_t)> callback) const;

  void Install() override;
  void Uninstall() override;

  uint16_t PatchAddress(uintptr_t host_address);
  void UnpatchAddress(uintptr_t host_address, uint16_t original_bytes);

  AddressType address_type_;
  uint64_t address_ = 0;

  // Pairs of x64 address : original byte values when installed.
  // These locations have been patched and must be restored when the breakpoint
  // is uninstalled.
  std::vector<std::pair<uintptr_t, uint16_t>> patches_;
};

class StepBreakpoint : public CodeBreakpoint {
 public:
  StepBreakpoint(Debugger* debugger, AddressType address_type, uint64_t address)
      : CodeBreakpoint(debugger, Type::kStepping, address_type, address) {}
};

class DataBreakpoint : public Breakpoint {
 public:
  explicit DataBreakpoint(Debugger* debugger)
      : Breakpoint(debugger, Type::kData) {}
};

class ExportBreakpoint : public Breakpoint {
 public:
  explicit ExportBreakpoint(Debugger* debugger)
      : Breakpoint(debugger, Type::kExport) {}
};

class GpuBreakpoint : public Breakpoint {
 public:
  explicit GpuBreakpoint(Debugger* debugger)
      : Breakpoint(debugger, Type::kGpu) {}
};

}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_BREAKPOINT_H_
