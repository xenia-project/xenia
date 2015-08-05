/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_MODEL_THREAD_H_
#define XENIA_DEBUG_UI_MODEL_THREAD_H_

#include <cstdint>
#include <string>
#include <vector>

#include "xenia/debug/proto/xdp_protocol.h"

namespace xe {
namespace debug {
namespace ui {
namespace model {

class System;

class Thread {
 public:
  using Frame = proto::ThreadCallStackFrame;

  Thread(System* system) : system_(system) {}

  bool is_dead() const { return is_dead_; }
  void set_dead(bool is_dead) { is_dead_ = is_dead; }

  uint32_t thread_handle() const { return entry_.thread_handle; }
  uint32_t thread_id() const { return entry_.thread_id; }
  bool is_host_thread() const { return entry_.is_host_thread; }
  std::string name() const { return entry_.name; }
  const proto::ThreadListEntry* entry() const { return &entry_; }
  const std::vector<Frame>& call_stack() const { return call_stack_; }

  std::string to_string();

  void Update(const proto::ThreadListEntry* entry);
  void UpdateCallStack(std::vector<const proto::ThreadCallStackFrame*> frames);

 private:
  System* system_ = nullptr;
  bool is_dead_ = false;
  proto::ThreadListEntry entry_ = {0};
  std::vector<Frame> call_stack_;
};

}  // namespace model
}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_MODEL_THREAD_H_
