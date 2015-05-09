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

#include <memory>
#include <mutex>

#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/app.h"
#include "xenia/kernel/content_manager.h"
#include "xenia/kernel/fs/filesystem.h"
#include "xenia/kernel/object_table.h"
#include "xenia/kernel/user_profile.h"
#include "xenia/memory.h"
#include "xenia/xbox.h"

namespace xe {
class Emulator;
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace kernel {

class Dispatcher;
class XModule;
class XNotifyListener;
class XThread;
class XUserModule;

class KernelState {
 public:
  KernelState(Emulator* emulator);
  ~KernelState();

  static KernelState* shared();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }
  fs::FileSystem* file_system() const { return file_system_; }

  uint32_t title_id() const;

  Dispatcher* dispatcher() const { return dispatcher_; }

  XAppManager* app_manager() const { return app_manager_.get(); }
  UserProfile* user_profile() const { return user_profile_.get(); }
  ContentManager* content_manager() const { return content_manager_.get(); }

  ObjectTable* object_table() const { return object_table_; }
  std::mutex& object_mutex() { return object_mutex_; }

  uint32_t process_type() const { return process_type_; }
  void set_process_type(uint32_t value) { process_type_ = value; }

  void RegisterModule(XModule* module);
  void UnregisterModule(XModule* module);
  XModule* GetModule(const char* name);
  XUserModule* GetExecutableModule();
  void SetExecutableModule(XUserModule* module);
  XUserModule* LoadUserModule(const char *name);

  void RegisterThread(XThread* thread);
  void UnregisterThread(XThread* thread);
  void OnThreadExecute(XThread* thread);
  void OnThreadExit(XThread* thread);
  XThread* GetThreadByID(uint32_t thread_id);

  void RegisterNotifyListener(XNotifyListener* listener);
  void UnregisterNotifyListener(XNotifyListener* listener);
  void BroadcastNotification(XNotificationID id, uint32_t data);

  void CompleteOverlapped(uint32_t overlapped_ptr, X_RESULT result,
                          uint32_t length = 0);
  void CompleteOverlappedImmediate(uint32_t overlapped_ptr, X_RESULT result,
                                   uint32_t length = 0);

 private:
  Emulator* emulator_;
  Memory* memory_;
  cpu::Processor* processor_;
  fs::FileSystem* file_system_;

  Dispatcher* dispatcher_;

  std::unique_ptr<XAppManager> app_manager_;
  std::unique_ptr<UserProfile> user_profile_;
  std::unique_ptr<ContentManager> content_manager_;

  ObjectTable* object_table_;
  std::mutex object_mutex_;
  std::unordered_map<uint32_t, XThread*> threads_by_id_;
  std::vector<XNotifyListener*> notify_listeners_;
  bool has_notified_startup_;

  uint32_t process_type_;
  XUserModule* executable_module_;

  std::vector<XUserModule*> user_modules_;

  friend class XObject;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_KERNEL_STATE_H_
