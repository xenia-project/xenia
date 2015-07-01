/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_KERNEL_STATE_H_
#define XENIA_KERNEL_KERNEL_STATE_H_

#include <gflags/gflags.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <mutex>

#include "xenia/base/mutex.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/app.h"
#include "xenia/kernel/content_manager.h"
#include "xenia/kernel/object_table.h"
#include "xenia/kernel/user_profile.h"
#include "xenia/memory.h"
#include "xenia/vfs/virtual_file_system.h"
#include "xenia/xbox.h"

namespace xe {
class Emulator;
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

DECLARE_bool(headless);

namespace xe {
namespace kernel {

class Dispatcher;
class XHostThread;
class XKernelModule;
class XModule;
class XNotifyListener;
class XThread;
class XUserModule;

// (?), used by KeGetCurrentProcessType
constexpr uint32_t X_PROCTYPE_IDLE = 0;
constexpr uint32_t X_PROCTYPE_USER = 1;
constexpr uint32_t X_PROCTYPE_SYSTEM = 2;

struct X_KPROCESS {
  xe::be<uint32_t> thread_list_lock;               // 0x0
  X_LIST_ENTRY thread_list_head;                   // 0x4
  xe::be<uint32_t> thread_quantum;                 // 0xC
  xe::be<uint32_t> directory_table_base;           // 0x10
  xe::be<uint32_t> thread_count;                   // 0x14
  xe::be<uint8_t> idle_priority_class;             // 0x18
  xe::be<uint8_t> normal_priority_class;           // 0x19
  xe::be<uint8_t> time_critical_priority_class;    // 0x1A
  xe::be<uint8_t> disable_quantum;                 // 0x1B
  xe::be<uint32_t> default_kernel_stack_size;      // 0x1C
  xe::be<uint32_t> tls_static_data_image;          // 0x20
  xe::be<uint32_t> size_of_tls_static_data;        // 0x24
  xe::be<uint32_t> size_of_tls_static_data_image;  // 0x28
  xe::be<uint16_t> size_of_tls_slots;              // 0x2C
  xe::be<uint8_t> terminating;                     // 0x2E
  xe::be<uint8_t> process_type;                    // 0x2D
  xe::be<uint32_t> bitmap[8];                      // 0x2C
  xe::be<uint32_t> file_object_list_lock;          // 0x50
  X_LIST_ENTRY file_object_list_head;              // 0x54
  xe::be<uint32_t> win32_default_heap_handle;      // 0x5C
};
static_assert_size(X_KPROCESS, 0x60);

class KernelState {
 public:
  KernelState(Emulator* emulator);
  ~KernelState();

  static KernelState* shared();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }
  vfs::VirtualFileSystem* file_system() const { return file_system_; }

  uint32_t title_id() const;

  Dispatcher* dispatcher() const { return dispatcher_; }

  XAppManager* app_manager() const { return app_manager_.get(); }
  UserProfile* user_profile() const { return user_profile_.get(); }
  ContentManager* content_manager() const { return content_manager_.get(); }

  ObjectTable* object_table() const { return object_table_; }
  xe::recursive_mutex& object_mutex() { return object_mutex_; }

  uint32_t process_type() const;
  void set_process_type(uint32_t value);
  uint32_t process_info_block_address() const {
    return process_info_block_address_;
  }

  void RegisterModule(XModule* module);
  void UnregisterModule(XModule* module);
  bool IsKernelModule(const char* name);
  object_ref<XModule> GetModule(const char* name);
  object_ref<XUserModule> GetExecutableModule();
  void SetExecutableModule(object_ref<XUserModule> module);
  template <typename T>
  object_ref<XKernelModule> LoadKernelModule() {
    auto kernel_module = object_ref<XKernelModule>(new T(emulator_, this));
    LoadKernelModule(kernel_module);
    return kernel_module;
  }
  object_ref<XUserModule> LoadUserModule(const char* name);

  void RegisterThread(XThread* thread);
  void UnregisterThread(XThread* thread);
  void OnThreadExecute(XThread* thread);
  void OnThreadExit(XThread* thread);
  object_ref<XThread> GetThreadByID(uint32_t thread_id);

  void RegisterNotifyListener(XNotifyListener* listener);
  void UnregisterNotifyListener(XNotifyListener* listener);
  void BroadcastNotification(XNotificationID id, uint32_t data);

  void CompleteOverlapped(uint32_t overlapped_ptr, X_RESULT result);
  void CompleteOverlappedEx(uint32_t overlapped_ptr, X_RESULT result,
                            uint32_t extended_error, uint32_t length);
  void CompleteOverlappedImmediate(uint32_t overlapped_ptr, X_RESULT result);
  void CompleteOverlappedImmediateEx(uint32_t overlapped_ptr, X_RESULT result,
                                     uint32_t extended_error, uint32_t length);
  void CompleteOverlappedDeferred(std::function<void()> completion_callback,
                                  uint32_t overlapped_ptr, X_RESULT result);
  void CompleteOverlappedDeferredEx(std::function<void()> completion_callback,
                                    uint32_t overlapped_ptr, X_RESULT result,
                                    uint32_t extended_error, uint32_t length);

 private:
  void LoadKernelModule(object_ref<XKernelModule> kernel_module);

  Emulator* emulator_;
  Memory* memory_;
  cpu::Processor* processor_;
  vfs::VirtualFileSystem* file_system_;

  Dispatcher* dispatcher_;

  std::unique_ptr<XAppManager> app_manager_;
  std::unique_ptr<UserProfile> user_profile_;
  std::unique_ptr<ContentManager> content_manager_;

  ObjectTable* object_table_;
  xe::recursive_mutex object_mutex_;
  std::unordered_map<uint32_t, XThread*> threads_by_id_;
  std::vector<object_ref<XNotifyListener>> notify_listeners_;
  bool has_notified_startup_;

  uint32_t process_type_;
  object_ref<XUserModule> executable_module_;
  std::vector<object_ref<XKernelModule>> kernel_modules_;
  std::vector<object_ref<XUserModule>> user_modules_;

  uint32_t process_info_block_address_;

  std::atomic<bool> dispatch_thread_running_ = false;
  object_ref<XHostThread> dispatch_thread_;
  std::mutex dispatch_mutex_;
  std::condition_variable dispatch_cond_;
  std::list<std::function<void()>> dispatch_queue_;

  friend class XObject;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_KERNEL_STATE_H_
