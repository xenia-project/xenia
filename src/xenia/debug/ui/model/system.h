/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_MODEL_SYSTEM_H_
#define XENIA_DEBUG_UI_MODEL_SYSTEM_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "xenia/base/delegate.h"
#include "xenia/debug/client/xdp/xdp_client.h"
#include "xenia/debug/ui/model/function.h"
#include "xenia/debug/ui/model/module.h"
#include "xenia/debug/ui/model/thread.h"
#include "xenia/ui/loop.h"

namespace xe {
namespace debug {
namespace ui {
namespace model {

using xe::debug::client::xdp::ExecutionState;

class System : public client::xdp::ClientListener {
 public:
  System(xe::ui::Loop* loop, client::xdp::XdpClient* client);

  xe::ui::Loop* loop() const { return loop_; }
  client::xdp::XdpClient* client() const { return client_; }

  ExecutionState execution_state();

  std::vector<Module*> modules();
  std::vector<Thread*> threads();

  Module* GetModuleByHandle(uint32_t module_handle);
  Thread* GetThreadByHandle(uint32_t thread_handle);

  Delegate<void> on_execution_state_changed;
  Delegate<void> on_modules_updated;
  Delegate<void> on_threads_updated;

 private:
  void OnExecutionStateChanged(ExecutionState execution_state) override;
  void OnModulesUpdated(
      std::vector<const proto::ModuleListEntry*> entries) override;
  void OnThreadsUpdated(
      std::vector<const proto::ThreadListEntry*> entries) override;

  xe::ui::Loop* loop_ = nullptr;
  client::xdp::XdpClient* client_ = nullptr;

  std::recursive_mutex mutex_;
  std::vector<std::unique_ptr<Module>> modules_;
  std::unordered_map<uint32_t, Module*> modules_by_handle_;
  std::vector<std::unique_ptr<Thread>> threads_;
  std::unordered_map<uint32_t, Thread*> threads_by_handle_;
};

}  // namespace model
}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_MODEL_SYSTEM_H_
