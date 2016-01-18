/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BREAKPOINT_H_
#define XENIA_CPU_BREAKPOINT_H_

#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {

class Breakpoint {
 public:
  enum class AddressType {
    kGuest,
    kHost,
  };
  typedef std::function<void(Breakpoint*, ThreadDebugInfo*, uint64_t)>
      HitCallback;

  Breakpoint(Processor* processor, AddressType address_type, uint64_t address,
             HitCallback hit_callback);
  ~Breakpoint();

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

  // Whether the breakpoint is currently installed and active.
  bool is_installed() const { return installed_; }

  std::string to_string() const;

  // Returns a guest function that contains the guest address, if any.
  // If there are multiple functions that contain the address a random one will
  // be returned. If this is a host-address code breakpoint this will attempt to
  // find a function with that address and otherwise return nullptr.
  GuestFunction* guest_function() const;

  // Returns true if this breakpoint, when installed, contains the given host
  // address.
  bool ContainsHostAddress(uintptr_t search_address) const;

  // Enumerates all host addresses that correspond to this breakpoint.
  // If this is a host type it will return the single host address, if it is a
  // guest type it may return multiple host addresses.
  void ForEachHostAddress(std::function<void(uintptr_t)> callback) const;

  // CPU backend data. Implementation specific - DO NOT TOUCH THIS!
  std::vector<std::pair<uint64_t, uint64_t>> backend_data() const {
    return backend_data_;
  }
  std::vector<std::pair<uint64_t, uint64_t>>& backend_data() {
    return backend_data_;
  }

 private:
  friend class Processor;

  void OnHit(ThreadDebugInfo* thread_info, uint64_t host_pc) {
    hit_callback_(this, thread_info, host_pc);
  }

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

 private:
  Processor* processor_ = nullptr;

  void Install();
  void Uninstall();

  // True if patched into code.
  bool installed_ = false;
  // True if user enabled.
  bool enabled_ = true;
  // Depth of suspends (must be 0 to be installed). Defaults to suspended so
  // that it's never installed unless the debugger knows about it.
  int suspend_count_ = 1;

  AddressType address_type_;
  uint64_t address_ = 0;

  HitCallback hit_callback_;

  // Opaque backend data. Don't touch this.
  std::vector<std::pair<uint64_t, uint64_t>> backend_data_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BREAKPOINT_H_