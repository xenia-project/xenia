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

#include <bitset>
#include <condition_variable>
#include <functional>
#include <list>
#include <vector>

#include "xenia/base/bit_map.h"
#include "xenia/cpu/backend/backend.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/kernel.h"
#include "xenia/kernel/smc.h"
#include "xenia/kernel/util/kernel_fwd.h"
#include "xenia/kernel/util/native_list.h"
#include "xenia/kernel/util/object_table.h"
#include "xenia/kernel/xam/achievement_manager.h"
#include "xenia/kernel/xam/app_manager.h"
#include "xenia/kernel/xam/content_manager.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/kernel/xam/xam_state.h"
#include "xenia/kernel/xam/xdbf/spa_info.h"
#include "xenia/kernel/xevent.h"
#include "xenia/vfs/virtual_file_system.h"

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

static constexpr const uint16_t kBaseKernelBuildVersion = 1888;

// (?), used by KeGetCurrentProcessType
constexpr uint32_t X_PROCTYPE_IDLE = 0;
constexpr uint32_t X_PROCTYPE_TITLE = 1;
constexpr uint32_t X_PROCTYPE_SYSTEM = 2;

struct X_KPROCESS {
  X_KSPINLOCK thread_list_spinlock;
  // list of threads in this process, guarded by the spinlock above
  X_LIST_ENTRY thread_list;

  // quantum value assigned to each thread of the process
  xe::be<int32_t> quantum;
  // kernel sets this to point to a section of size 0x2F700 called CLRDATAA,
  // except it clears bit 31 of the pointer. in 17559 the address is 0x801C0000,
  // so it sets this ptr to 0x1C0000
  xe::be<uint32_t> clrdataa_masked_ptr;
  xe::be<uint32_t> thread_count;
  uint8_t unk_18;
  uint8_t unk_19;
  uint8_t unk_1A;
  uint8_t unk_1B;
  xe::be<uint32_t> kernel_stack_size;
  xe::be<uint32_t> tls_static_data_address;
  xe::be<uint32_t> tls_data_size;
  xe::be<uint32_t> tls_raw_data_size;
  xe::be<uint16_t> tls_slot_size;
  // ExCreateThread calls a subfunc references this field, returns
  // X_STATUS_PROCESS_IS_TERMINATING if true
  uint8_t is_terminating;
  // one of X_PROCTYPE_
  uint8_t process_type;
  xe::be<uint32_t> bitmap[8];
  xe::be<uint32_t> unk_50;
  X_LIST_ENTRY unk_54;
  xe::be<uint32_t> unk_5C;
};
static_assert_size(X_KPROCESS, 0x60);

struct TerminateNotification {
  uint32_t guest_routine;
  uint32_t priority;
};

// structure for KeTimeStampBuindle
// a bit like the timers on KUSER_SHARED on normal win32
// https://www.geoffchappell.com/studies/windows/km/ntoskrnl/inc/api/ntexapi_x/kuser_shared_data/index.htm
struct X_TIME_STAMP_BUNDLE {
  uint64_t interrupt_time;
  // i assume system_time is in 100 ns intervals like on win32
  uint64_t system_time;
  uint32_t tick_count;
  uint32_t padding;
};
struct X_UNKNOWN_TYPE_REFED {
  xe::be<uint32_t> field0;
  xe::be<uint32_t> field4;
  // this is definitely a LIST_ENTRY?
  xe::be<uint32_t> points_to_self;  // this field points to itself
  xe::be<uint32_t>
      points_to_prior;  // points to the previous field, which points to itself
};
static_assert_size(X_UNKNOWN_TYPE_REFED, 16);

struct KernelGuestGlobals {
  X_OBJECT_TYPE ExThreadObjectType;
  X_OBJECT_TYPE ExEventObjectType;
  X_OBJECT_TYPE ExMutantObjectType;
  X_OBJECT_TYPE ExSemaphoreObjectType;
  X_OBJECT_TYPE ExTimerObjectType;
  X_OBJECT_TYPE IoCompletionObjectType;
  X_OBJECT_TYPE IoDeviceObjectType;
  X_OBJECT_TYPE IoFileObjectType;
  X_OBJECT_TYPE ObDirectoryObjectType;
  X_OBJECT_TYPE ObSymbolicLinkObjectType;
  // a constant buffer that some object types' "unknown_size_or_object" field
  // points to
  X_UNKNOWN_TYPE_REFED OddObj;
  X_KPROCESS idle_process;    // X_PROCTYPE_IDLE. runs in interrupt contexts. is
                              // also the context the kernel starts in?
  X_KPROCESS title_process;   // X_PROCTYPE_TITLE
  X_KPROCESS system_process;  // X_PROCTYPE_SYSTEM. no idea when this runs. can
                              // create threads in this process with
                              // ExCreateThread and the thread flag 0x2

  // locks.
  X_KSPINLOCK dispatcher_lock;  // called the "dispatcher lock" in nt 3.5 ppc
                                // .dbg file. Used basically everywhere that
                                // DISPATCHER_HEADER'd objects appear
  // this lock is only used in some Ob functions. It's odd that it is used at
  // all, as each table already has its own spinlock.
  X_KSPINLOCK ob_lock;

  // if LLE emulating Xam, this is needed or you get an immediate freeze
  X_KEVENT UsbdBootEnumerationDoneEvent;
};
struct DPCImpersonationScope {
  uint8_t previous_irql_;
};

struct KernelVersion {
  union {
    xe::be<uint64_t> value;

    struct {
      xe::be<uint16_t> major;
      xe::be<uint16_t> minor;
      xe::be<uint16_t> build;
      xe::be<uint16_t> qfe;
    };
  };

  KernelVersion(uint16_t build_ver = kBaseKernelBuildVersion) {
    major = 2;
    minor = 0;
    build = std::max(kBaseKernelBuildVersion, build_ver);
    qfe = 0;
  }
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
  const std::unique_ptr<xam::SpaInfo> title_xdbf() const;
  const std::unique_ptr<xam::SpaInfo> module_xdbf(
      object_ref<UserModule> exec_module) const;

  xam::XamState* xam_state() const { return xam_state_.get(); }

  SystemManagementController* smc() const { return smc_.get(); }

  xam::AchievementManager* achievement_manager() const {
    return xam_state()->achievement_manager();
  }
  xam::AppManager* app_manager() const { return xam_state()->app_manager(); }
  xam::ContentManager* content_manager() const {
    return xam_state()->content_manager();
  }

  std::bitset<4> GetConnectedUsers() const;

  // Access must be guarded by the global critical region.
  util::ObjectTable* object_table() { return &object_table_; }

  const KernelVersion* GetKernelVersion() const { return &kernel_version_; }

  uint32_t GetSystemProcess() const {
    return kernel_guest_globals_ + offsetof(KernelGuestGlobals, system_process);
  }

  uint32_t GetTitleProcess() const {
    return kernel_guest_globals_ + offsetof(KernelGuestGlobals, title_process);
  }
  // also the "interrupt" process
  uint32_t GetIdleProcess() const {
    return kernel_guest_globals_ + offsetof(KernelGuestGlobals, idle_process);
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
  object_ref<UserModule> LoadUserModuleFromMemory(const std::string_view name,
                                                  const void* addr,
                                                  const size_t length);
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

  X_RESULT ApplyTitleUpdate(const object_ref<UserModule> title_module);
  // Terminates a title: Unloads all modules, and kills all guest threads.
  // This DOES NOT RETURN if called from a guest thread!
  void TerminateTitle();

  void RegisterThread(XThread* thread);
  void UnregisterThread(XThread* thread);
  void OnThreadExecute(XThread* thread);
  void OnThreadExit(XThread* thread);
  object_ref<XThread> GetThreadByID(uint32_t thread_id);
  std::vector<uint32_t> GetAllThreadIDs();

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

  uint32_t notification_position_ = 2;
  XDeploymentType deployment_type_ = XDeploymentType::kOther;

  uint32_t GetKeTimestampBundle();

  XE_NOINLINE
  XE_COLD
  uint32_t CreateKeTimestampBundle();
  void UpdateKeTimestampBundle();

  void BeginDPCImpersonation(cpu::ppc::PPCContext* context,
                             DPCImpersonationScope& scope);
  void EndDPCImpersonation(cpu::ppc::PPCContext* context,
                           DPCImpersonationScope& end_scope);

  void EmulateCPInterruptDPC(uint32_t interrupt_callback,
                             uint32_t interrupt_callback_data, uint32_t source,
                             uint32_t cpu);

 private:
  void LoadKernelModule(object_ref<KernelModule> kernel_module);
  void InitializeProcess(X_KPROCESS* process, uint32_t type, char unk_18,
                         char unk_19, char unk_1A);
  void SetProcessTLSVars(X_KPROCESS* process, int num_slots, int tls_data_size,
                         int tls_static_data_address);
  void InitializeKernelGuestGlobals();

  std::vector<xam::XCONTENT_AGGREGATE_DATA> FindTitleUpdate(
      const uint32_t title_id) const;
  const object_ref<UserModule> LoadTitleUpdate(
      const xam::XCONTENT_AGGREGATE_DATA* title_update,
      const object_ref<UserModule> module);
  bool IsPatchSignatureProper(const object_ref<UserModule> title_module,
                              const object_ref<UserModule> patch_module) const;

  X_RESULT ApplyTitleUpdate(const object_ref<UserModule> title_module,
                            const object_ref<UserModule> patch_module);

  Emulator* emulator_;
  Memory* memory_;
  cpu::Processor* processor_;
  vfs::VirtualFileSystem* file_system_;
  std::unique_ptr<xam::XamState> xam_state_;
  std::unique_ptr<SystemManagementController> smc_;

  KernelVersion kernel_version_;

  xe::global_critical_region global_critical_region_;

  // Must be guarded by the global critical region.
  util::ObjectTable object_table_;
  std::unordered_map<uint32_t, XThread*> threads_by_id_;
  std::vector<object_ref<XNotifyListener>> notify_listeners_;
  bool has_notified_startup_ = false;

  object_ref<UserModule> executable_module_;
  std::vector<object_ref<KernelModule>> kernel_modules_;
  std::vector<object_ref<UserModule>> user_modules_;
  std::vector<TerminateNotification> terminate_notifications_;
  uint32_t kernel_guest_globals_ = 0;

  std::atomic<bool> dispatch_thread_running_;
  object_ref<XHostThread> dispatch_thread_;
  // Must be guarded by the global critical region.
  util::NativeList dpc_list_;
  std::condition_variable_any dispatch_cond_;
  std::list<std::function<void()>> dispatch_queue_;

  BitMap tls_bitmap_;
  uint32_t ke_timestamp_bundle_ptr_ = 0;
  std::unique_ptr<xe::threading::HighResolutionTimer> timestamp_timer_;
  cpu::backend::GuestTrampolineGroup kernel_trampoline_group_;
  // fixed address referenced by dashboards. Data is currently unknown
  uint32_t strange_hardcoded_page_ = 0x8E038634 & (~0xFFFF);
  uint32_t strange_hardcoded_location_ = 0x8E038634;

  friend class XObject;

 public:
  uint32_t dash_context_ = 0;
  std::unordered_map<XObject::Type, uint32_t>
      host_object_type_enum_to_guest_object_type_ptr_;
  uint32_t GetKernelGuestGlobals() const { return kernel_guest_globals_; }
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_KERNEL_STATE_H_
