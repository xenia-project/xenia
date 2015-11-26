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
  Breakpoint(Processor* processor, uint32_t address,
             std::function<void(uint32_t, uint64_t)> hit_callback);
  ~Breakpoint();

  uint32_t address() const { return address_; }
  bool installed() const { return installed_; }

  bool Install();
  bool Uninstall();
  void Hit(uint64_t host_pc) { hit_callback_(address_, host_pc); }

  // CPU backend data. Implementation specific - DO NOT TOUCH THIS!
  uint64_t backend_data() const { return backend_data_; }
  void set_backend_data(uint64_t backend_data) { backend_data_ = backend_data; }

 private:
  Processor* processor_ = nullptr;

  bool installed_ = false;
  uint32_t address_ = 0;
  std::function<void(uint32_t, uint64_t)> hit_callback_;

  // Opaque backend data. Don't touch this.
  uint64_t backend_data_ = 0;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BREAKPOINT_H_