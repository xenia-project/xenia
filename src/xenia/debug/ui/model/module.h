/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_MODEL_MODULE_H_
#define XENIA_DEBUG_UI_MODEL_MODULE_H_

#include <cstdint>
#include <string>

#include "xenia/debug/proto/xdp_protocol.h"

namespace xe {
namespace debug {
namespace ui {
namespace model {

class System;

class Module {
 public:
  Module(System* system) : system_(system) {}

  bool is_dead() const { return is_dead_; }
  void set_dead(bool is_dead) { is_dead_ = is_dead; }

  uint32_t module_handle() const { return entry_.module_handle; }
  bool is_kernel_module() const { return entry_.is_kernel_module; }
  std::string name() const { return entry_.name; }
  const proto::ModuleListEntry* entry() const { return &entry_; }

  void Update(const proto::ModuleListEntry* entry);

 private:
  System* system_ = nullptr;
  bool is_dead_ = false;
  proto::ModuleListEntry entry_ = {0};
  proto::ModuleListEntry temp_entry_ = {0};
};

}  // namespace model
}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_MODEL_MODULE_H_
