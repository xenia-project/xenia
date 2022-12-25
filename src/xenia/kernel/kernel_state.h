/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_KERNEL_STATE_H_
#define XENIA_KERNEL_KERNEL_STATE_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <vector>

#include "xenia/base/bit_map.h"
#include "xenia/base/cvar.h"
#include "xenia/base/mutex.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/util/native_list.h"
#include "xenia/kernel/util/object_table.h"
#include "xenia/kernel/util/xdbf_utils.h"
#include "xenia/kernel/xam/app_manager.h"
#include "xenia/kernel/xam/content_manager.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/memory.h"
#include "xenia/vfs/virtual_file_system.h"
#include "xenia/xbox.h"

namespace xe {
class ByteStream;
class Emulator;
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace kernel {

constexpr fourcc_t kKernelSaveSignature = make_fourcc("KRNL");

class Dispatcher;
class XHostThread;
class KernelModule;
class XModule;
class XNotifyListener;
class XThread;
class UserModule;

// (?), used by KeGetCurrentProcessType
constexpr uint32_t X_PROCTYPE_IDLE = 0;
constexpr uint32_t X_PROCTYPE_USER = 1;
constexpr uint32_t X_PROCTYPE_SYSTEM = 2;

struct ProcessInfoBlock {
  xe::be<uint32_t> unk_00;
  xe::be<uint32_t> unk_04;  // blink
  xe::be<uint32_t> unk_08;  // flink
  xe::be<uint32_t> unk_0C;
  xe::be<uint32_t> unk_10;
  xe::be<uint32_t> thread_count;
  uint8_t unk_18;
  uint8_t unk_19;
  uint8_t unk_1A;
  uint8_t unk_1B;
  xe::be<uint32_t> kernel_stack_size;
  xe::be<uint32_t> unk_20;
  xe::be<uint32_t> tls_data_size;
  xe::be<uint32_t> tls_raw_data_size;
  xe::be<uint16_t> tls_slot_size;
  uint8_t unk_2E;
  uint8_t process_type;
  xe::be<uint32_t> bitmap[0x20 / 4];
  xe::be<uint32_t> unk_50;
  xe::be<uint32_t> unk_54;  // blink
  xe::be<uint32_t> unk_58;  // flink
  xe::be<uint32_t> unk_5C;
};

struct TerminateNotification {
  uint32_t guest_routine;
  uint32_t priority;
};

class KernelState {
 public:
  explicit KernelState(Emulator* emulator);
  ~KernelState();

  static KernelState* shared();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }
  vfs::VirtualFileSystem* file_system() const { return file_system_; }

  uint32_t title_id() const;
  util::XdbfGameData title_xdbf() const;
  util::XdbfGameData module_xdbf(object_ref<UserModule> exec_module) const;

  xam::AppManager* app_manager() const { return app_manager_.get(); }
  xam::ContentManager* content_manager() const {
    return content_manager_.get();
  }

  uint8_t GetConnectedUsers() const;
  void UpdateUsedUserProfiles();

  bool IsUserSignedIn(uint32_t index) const {
    return user_profiles_.find(index) != user_profiles_.cend();
  }

  bool IsUserSignedIn(uint64_t xuid) const {
    return user_profile(xuid) != nullptr;
  }

  xam::UserProfile* user_profile(uint32_t index) const {
    if (!IsUserSignedIn(index)) {
      return nullptr;
    }

    return user_profiles_.at(index).get();
  }

  xam::UserProfile* user_profile(uint64_t xuid) const {
    for (const auto& [key, value] : user_profiles_) {
      if (value->xuid() == xuid) {
        return user_profiles_.at(key).get();
      }
    }
    return nullptr;
  }

  // Access must be guarded by the global critical region.
  util::ObjectTable* object_table() { return &object_table_; }

  uint32_t process_type() const;
  void set_process_type(uint32_t value);
  uint32_t process_info_block_address() const {
    return process_info_block_address_;
  }

  uint32_t AllocateTLS();
  void FreeTLS(uint32_t slot);

  void RegisterTitleTerminateNotification(uint32_t routine, uint32_t priority);
  void RemoveTitleTerminateNotification(uint32_t routine);

  void RegisterModule(XModule* module);
  void UnregisterModule(XModule* module);
  bool RegisterUserModule(object_ref<UserModule> module);
  void UnregisterUserModule(UserModule* module);
  bool IsKernelModule(const std::string_view name);
  object_ref<XModule> GetModule(const std::string_view name,
                                bool user_only = false);

  object_ref<XThread> LaunchModule(object_ref<UserModule> module);
  object_ref<UserModule> GetExecutableModule();
  void SetExecutableModule(object_ref<UserModule> module);
  object_ref<UserModule> LoadUserModule(const std::string_view name,
                                        bool call_entry = true);
  X_RESULT FinishLoadingUserModule(const object_ref<UserModule> module,
                                   bool call_entry = true);
  void UnloadUserModule(const object_ref<UserModule>& module,
                        bool call_entry = true);

  object_ref<KernelModule> GetKernelModule(const std::string_view name);
  template <typename T>
  object_ref<KernelModule> LoadKernelModule() {
    auto kernel_module = object_ref<KernelModule>(new T(emulator_, this));
    LoadKernelModule(kernel_module);
    return kernel_module;
  }
  template <typename T>
  object_ref<T> GetKernelModule(const std::string_view name) {
    auto module = GetKernelModule(name);
    return object_ref<T>(reinterpret_cast<T*>(module.release()));
  }

  X_RESULT ApplyTitleUpdate(const object_ref<UserModule> module);
  // Terminates a title: Unloads all modules, and kills all guest threads.
  // This DOES NOT RETURN if called from a guest thread!
  void TerminateTitle();

  void RegisterThread(XThread* thread);
  void UnregisterThread(XThread* thread);
  void OnThreadExecute(XThread* thread);
  void OnThreadExit(XThread* thread);
  object_ref<XThread> GetThreadByID(uint32_t thread_id);

  void RegisterNotifyListener(XNotifyListener* listener);
  void UnregisterNotifyListener(XNotifyListener* listener);
  void BroadcastNotification(XNotificationID id, uint32_t data);

  util::NativeList* dpc_list() { return &dpc_list_; }

  void CompleteOverlapped(uint32_t overlapped_ptr, X_RESULT result);
  void CompleteOverlappedEx(uint32_t overlapped_ptr, X_RESULT result,
                            uint32_t extended_error, uint32_t length);

  void CompleteOverlappedImmediate(uint32_t overlapped_ptr, X_RESULT result);
  void CompleteOverlappedImmediateEx(uint32_t overlapped_ptr, X_RESULT result,
                                     uint32_t extended_error, uint32_t length);

  void CompleteOverlappedDeferred(
      std::function<void()> completion_callback, uint32_t overlapped_ptr,
      X_RESULT result, std::function<void()> pre_callback = nullptr,
      std::function<void()> post_callback = nullptr);
  void CompleteOverlappedDeferredEx(
      std::function<void()> completion_callback, uint32_t overlapped_ptr,
      X_RESULT result, uint32_t extended_error, uint32_t length,
      std::function<void()> pre_callback = nullptr,
      std::function<void()> post_callback = nullptr);

  void CompleteOverlappedDeferred(
      std::function<X_RESULT()> completion_callback, uint32_t overlapped_ptr,
      std::function<void()> pre_callback = nullptr,
      std::function<void()> post_callback = nullptr);
  void CompleteOverlappedDeferredEx(
      std::function<X_RESULT(uint32_t&, uint32_t&)> completion_callback,
      uint32_t overlapped_ptr, std::function<void()> pre_callback = nullptr,
      std::function<void()> post_callback = nullptr);

  bool Save(ByteStream* stream);
  bool Restore(ByteStream* stream);

 private:
  void LoadKernelModule(object_ref<KernelModule> kernel_module);

  Emulator* emulator_;
  Memory* memory_;
  cpu::Processor* processor_;
  vfs::VirtualFileSystem* file_system_;

  std::unique_ptr<xam::AppManager> app_manager_;
  std::unique_ptr<xam::ContentManager> content_manager_;
  std::map<uint8_t, std::unique_ptr<xam::UserProfile>> user_profiles_;

  xe::global_critical_region global_critical_region_;

  // Must be guarded by the global critical region.
  util::ObjectTable object_table_;
  std::unordered_map<uint32_t, XThread*> threads_by_id_;
  std::vector<object_ref<XNotifyListener>> notify_listeners_;
  bool has_notified_startup_ = false;

  uint32_t process_type_ = X_PROCTYPE_USER;
  object_ref<UserModule> executable_module_;
  std::vector<object_ref<KernelModule>> kernel_modules_;
  std::vector<object_ref<UserModule>> user_modules_;
  std::vector<TerminateNotification> terminate_notifications_;

  uint32_t process_info_block_address_ = 0;

  std::atomic<bool> dispatch_thread_running_;
  object_ref<XHostThread> dispatch_thread_;
  // Must be guarded by the global critical region.
  util::NativeList dpc_list_;
  std::condition_variable_any dispatch_cond_;
  std::list<std::function<void()>> dispatch_queue_;

  BitMap tls_bitmap_;

  friend class XObject;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_KERNEL_STATE_H_
