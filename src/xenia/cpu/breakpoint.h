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
  Breakpoint(Processor* processor,
             std::function<void(uint32_t, uint64_t)> hit_callback);
  Breakpoint(Processor* processor, uint32_t address,
             std::function<void(uint32_t, uint64_t)> hit_callback);
  ~Breakpoint();

  uint32_t address() const { return address_; }
  void set_address(uint32_t address) {
    assert_false(installed_);
    address_ = address;
  }
  bool installed() const { return installed_; }

  bool Install();
  bool Uninstall();
  void Hit(uint64_t host_pc) { hit_callback_(address_, host_pc); }

  // CPU backend data. Implementation specific - DO NOT TOUCH THIS!
  std::vector<std::pair<uint64_t, uint64_t>> backend_data() const {
    return backend_data_;
  }
  std::vector<std::pair<uint64_t, uint64_t>>& backend_data() {
    return backend_data_;
  }

 private:
  Processor* processor_ = nullptr;

  bool installed_ = false;
  uint32_t address_ = 0;
  std::function<void(uint32_t, uint64_t)> hit_callback_;

  // Opaque backend data. Don't touch this.
  std::vector<std::pair<uint64_t, uint64_t>> backend_data_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BREAKPOINT_H_