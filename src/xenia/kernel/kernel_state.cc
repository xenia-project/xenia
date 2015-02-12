/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/kernel_state.h"

#include <gflags/gflags.h>

#include "xenia/emulator.h"
#include "xenia/kernel/dispatcher.h"
#include "xenia/kernel/xam_module.h"
#include "xenia/kernel/xboxkrnl_module.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/kernel/xobject.h"
#include "xenia/kernel/apps/apps.h"
#include "xenia/kernel/objects/xevent.h"
#include "xenia/kernel/objects/xmodule.h"
#include "xenia/kernel/objects/xnotify_listener.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/kernel/objects/xuser_module.h"

DEFINE_string(content_root, "content",
              "Root path for content (save/etc) storage.");

namespace xe {
namespace kernel {

// This is a global object initialized with the XboxkrnlModule.
// It references the current kernel state object that all kernel methods should
// be using to stash their variables.
KernelState* shared_kernel_state_ = nullptr;

KernelState::KernelState(Emulator* emulator)
    : emulator_(emulator),
      memory_(emulator->memory()),
      object_table_(nullptr),
      has_notified_startup_(false),
      process_type_(X_PROCTYPE_USER),
      executable_module_(nullptr) {
  processor_ = emulator->processor();
  file_system_ = emulator->file_system();

  dispatcher_ = new Dispatcher(this);

  app_manager_ = std::make_unique<XAppManager>();
  user_profile_ = std::make_unique<UserProfile>();

  auto content_root = poly::to_wstring(FLAGS_content_root);
  content_root = poly::to_absolute_path(content_root);
  content_manager_ = std::make_unique<ContentManager>(this, content_root);

  object_table_ = new ObjectTable();

  assert_null(shared_kernel_state_);
  shared_kernel_state_ = this;

  apps::RegisterApps(this, app_manager_.get());
}

KernelState::~KernelState() {
  SetExecutableModule(nullptr);

  // Delete all objects.
  delete object_table_;

  // Shutdown apps.
  app_manager_.reset();

  delete dispatcher_;

  assert_true(shared_kernel_state_ == this);
  shared_kernel_state_ = nullptr;
}

KernelState* KernelState::shared() { return shared_kernel_state_; }

uint32_t KernelState::title_id() const {
  assert_not_null(executable_module_);
  return executable_module_->xex_header()->execution_info.title_id;
}

void KernelState::RegisterModule(XModule* module) {}

void KernelState::UnregisterModule(XModule* module) {}

XModule* KernelState::GetModule(const char* name) {
  if (!name) {
    // NULL name = self.
    // TODO(benvanik): lookup module from caller address.
    return GetExecutableModule();
  } else if (strcasecmp(name, "xam.xex") == 0) {
    auto module = emulator_->xam();
    module->Retain();
    return module;
  } else if (strcasecmp(name, "xboxkrnl.exe") == 0) {
    auto module = emulator_->xboxkrnl();
    module->Retain();
    return module;
  } else if (strcasecmp(name, "kernel32.dll") == 0) {
    // Some games request this, for some reason. wtf.
    return nullptr;
  } else {
    // TODO(benvanik): support user modules/loading/etc.
    assert_always();
    return nullptr;
  }
}

XUserModule* KernelState::GetExecutableModule() {
  if (!executable_module_) {
    return nullptr;
  }

  executable_module_->Retain();
  return executable_module_;
}

void KernelState::SetExecutableModule(XUserModule* module) {
  if (module == executable_module_) {
    return;
  }

  if (executable_module_) {
    executable_module_->Release();
  }
  executable_module_ = module;
  if (executable_module_) {
    executable_module_->Retain();
  }
}

void KernelState::RegisterThread(XThread* thread) {
  std::lock_guard<std::mutex> lock(object_mutex_);
  threads_by_id_[thread->thread_id()] = thread;
}

void KernelState::UnregisterThread(XThread* thread) {
  std::lock_guard<std::mutex> lock(object_mutex_);
  auto it = threads_by_id_.find(thread->thread_id());
  if (it != threads_by_id_.end()) {
    threads_by_id_.erase(it);
  }
}

XThread* KernelState::GetThreadByID(uint32_t thread_id) {
  std::lock_guard<std::mutex> lock(object_mutex_);
  XThread* thread = nullptr;
  auto it = threads_by_id_.find(thread_id);
  if (it != threads_by_id_.end()) {
    thread = it->second;
    // Caller must release.
    thread->Retain();
  }
  return thread;
}

void KernelState::RegisterNotifyListener(XNotifyListener* listener) {
  std::lock_guard<std::mutex> lock(object_mutex_);
  notify_listeners_.push_back(listener);

  // Games seem to expect a few notifications on startup, only for the first
  // listener.
  // http://cs.rin.ru/forum/viewtopic.php?f=38&t=60668&hilit=resident+evil+5&start=375
  if (!has_notified_startup_ && listener->mask() & 0x00000001) {
    has_notified_startup_ = true;
    // XN_SYS_UI (on, off)
    listener->EnqueueNotification(0x00000009, 1);
    listener->EnqueueNotification(0x00000009, 0);
    // XN_SYS_SIGNINCHANGED x2
    listener->EnqueueNotification(0x0000000A, 1);
    listener->EnqueueNotification(0x0000000A, 1);
    // XN_SYS_INPUTDEVICESCHANGED x2
    listener->EnqueueNotification(0x00000012, 0);
    listener->EnqueueNotification(0x00000012, 0);
    // XN_SYS_INPUTDEVICECONFIGCHANGED x2
    listener->EnqueueNotification(0x00000013, 0);
    listener->EnqueueNotification(0x00000013, 0);
  }
}

void KernelState::UnregisterNotifyListener(XNotifyListener* listener) {
  std::lock_guard<std::mutex> lock(object_mutex_);
  for (auto it = notify_listeners_.begin(); it != notify_listeners_.end();
       ++it) {
    if (*it == listener) {
      notify_listeners_.erase(it);
      break;
    }
  }
}

void KernelState::BroadcastNotification(XNotificationID id, uint32_t data) {
  std::lock_guard<std::mutex> lock(object_mutex_);
  for (auto it = notify_listeners_.begin(); it != notify_listeners_.end();
       ++it) {
    (*it)->EnqueueNotification(id, data);
  }
}

void KernelState::CompleteOverlapped(uint32_t overlapped_ptr, X_RESULT result,
                                     uint32_t length) {
  auto ptr = memory()->membase() + overlapped_ptr;
  XOverlappedSetResult(ptr, result);
  XOverlappedSetLength(ptr, length);
  XOverlappedSetExtendedError(ptr, result);
  X_HANDLE event_handle = XOverlappedGetEvent(ptr);
  if (event_handle) {
    XEvent* ev = nullptr;
    if (XSUCCEEDED(object_table()->GetObject(
            event_handle, reinterpret_cast<XObject**>(&ev)))) {
      ev->Set(0, false);
      ev->Release();
    }
  }
  if (XOverlappedGetCompletionRoutine(ptr)) {
    assert_always();
    X_HANDLE thread_handle = XOverlappedGetContext(ptr);
    XThread* thread = nullptr;
    if (XSUCCEEDED(object_table()->GetObject(
            thread_handle, reinterpret_cast<XObject**>(&thread)))) {
      // TODO(benvanik): queue APC on the thread that requested the overlapped
      // operation.
      thread->Release();
    }
  }
}

void KernelState::CompleteOverlappedImmediate(uint32_t overlapped_ptr,
                                              X_RESULT result,
                                              uint32_t length) {
  auto ptr = memory()->membase() + overlapped_ptr;
  XOverlappedSetContext(ptr, XThread::GetCurrentThreadHandle());
  CompleteOverlapped(overlapped_ptr, result, length);
}

}  // namespace kernel
}  // namespace xe
