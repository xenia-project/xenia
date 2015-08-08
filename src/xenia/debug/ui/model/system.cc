/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/ui/model/system.h"

#include <unordered_set>

namespace xe {
namespace debug {
namespace ui {
namespace model {

using namespace xe::debug::proto;  // NOLINT(build/namespaces)

System::System(xe::ui::Loop* loop, DebugClient* client)
    : loop_(loop), client_(client) {
  client_->set_listener(this);
  client_->set_loop(loop);
}

System::~System() { client_->set_listener(nullptr); }

ExecutionState System::execution_state() { return client_->execution_state(); }

std::vector<Module*> System::modules() {
  std::vector<Module*> result;
  for (auto& module : modules_) {
    result.push_back(module.get());
  }
  return result;
}

std::vector<Thread*> System::threads() {
  std::vector<Thread*> result;
  for (auto& thread : threads_) {
    result.push_back(thread.get());
  }
  return result;
}

Module* System::GetModuleByHandle(uint32_t module_handle) {
  auto it = modules_by_handle_.find(module_handle);
  return it != modules_by_handle_.end() ? it->second : nullptr;
}

Thread* System::GetThreadByHandle(uint32_t thread_handle) {
  auto it = threads_by_handle_.find(thread_handle);
  return it != threads_by_handle_.end() ? it->second : nullptr;
}

void System::OnExecutionStateChanged(ExecutionState execution_state) {
  on_execution_state_changed();
}

void System::OnModulesUpdated(std::vector<const ModuleListEntry*> entries) {
  std::unordered_set<uint32_t> extra_modules;
  for (size_t i = 0; i < modules_.size(); ++i) {
    extra_modules.emplace(modules_[i]->module_handle());
  }
  for (auto entry : entries) {
    auto existing_module = modules_by_handle_.find(entry->module_handle);
    if (existing_module == modules_by_handle_.end()) {
      auto module = std::make_unique<Module>(this);
      module->Update(entry);
      modules_by_handle_.emplace(entry->module_handle, module.get());
      modules_.emplace_back(std::move(module));
    } else {
      existing_module->second->Update(entry);
      extra_modules.erase(existing_module->first);
    }
  }
  for (auto module_handle : extra_modules) {
    auto module = modules_by_handle_.find(module_handle);
    if (module != modules_by_handle_.end()) {
      module->second->set_dead(true);
    }
  }

  on_modules_updated();
}

void System::OnThreadsUpdated(std::vector<const ThreadListEntry*> entries) {
  std::unordered_set<uint32_t> extra_threads;
  for (size_t i = 0; i < threads_.size(); ++i) {
    extra_threads.emplace(threads_[i]->thread_handle());
  }
  for (auto entry : entries) {
    auto existing_thread = threads_by_handle_.find(entry->thread_handle);
    if (existing_thread == threads_by_handle_.end()) {
      auto thread = std::make_unique<Thread>(this);
      thread->Update(entry);
      threads_by_handle_.emplace(entry->thread_handle, thread.get());
      threads_.emplace_back(std::move(thread));
    } else {
      existing_thread->second->Update(entry);
      extra_threads.erase(existing_thread->first);
    }
  }
  for (auto thread_handle : extra_threads) {
    auto thread = threads_by_handle_.find(thread_handle);
    if (thread != threads_by_handle_.end()) {
      thread->second->set_dead(true);
    }
  }

  on_threads_updated();
}

void System::OnThreadCallStackUpdated(
    uint32_t thread_handle, std::vector<const ThreadCallStackFrame*> frames) {
  auto thread = threads_by_handle_[thread_handle];
  if (thread != nullptr) {
    thread->UpdateCallStack(std::move(frames));
    on_thread_call_stack_updated(thread);
  }
}

}  // namespace model
}  // namespace ui
}  // namespace debug
}  // namespace xe
