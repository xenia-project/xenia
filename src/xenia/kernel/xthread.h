/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XTHREAD_H_
#define XENIA_KERNEL_XTHREAD_H_

#include <atomic>
#include <string>

#include "xenia/base/mutex.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/thread.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/kernel/util/native_list.h"
#include "xenia/kernel/xmutant.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class XEvent;

constexpr uint32_t X_CREATE_SUSPENDED = 0x00000001;

constexpr uint32_t X_TLS_OUT_OF_INDEXES = UINT32_MAX;

struct XAPC {
  static const uint32_t kSize = 40;
  static const uint32_t kDummyKernelRoutine = 0xF00DFF00;
  static const uint32_t kDummyRundownRoutine = 0xF00DFF01;

  // KAPC is 0x28(40) bytes? (what's passed to ExAllocatePoolWithTag)
  // This is 4b shorter than NT - looks like the reserved dword at +4 is gone.
  // NOTE: stored in guest memory.
  uint8_t type;                      // +0
  uint8_t unk1;                      // +1
  uint8_t processor_mode;            // +2
  uint8_t enqueued;                  // +3
  xe::be<uint32_t> thread_ptr;       // +4
  xe::be<uint32_t> flink;            // +8
  xe::be<uint32_t> blink;            // +12
  xe::be<uint32_t> kernel_routine;   // +16
  xe::be<uint32_t> rundown_routine;  // +20
  xe::be<uint32_t> normal_routine;   // +24
  xe::be<uint32_t> normal_context;   // +28
  xe::be<uint32_t> arg1;             // +32
  xe::be<uint32_t> arg2;             // +36

  void Initialize() {
    type = 18;  // ApcObject
    unk1 = 0;
    processor_mode = 0;
    enqueued = 0;
    thread_ptr = 0;
    flink = blink = 0;
    kernel_routine = 0;
    normal_routine = 0;
    normal_context = 0;
    arg1 = arg2 = 0;
  }
};

// Processor Control Region
struct X_KPCR {
  xe::be<uint32_t> tls_ptr;         // 0x0
  char unk_04[0x2C];                // 0x4
  xe::be<uint32_t> pcr_ptr;         // 0x30
  char unk_34[0x3C];                // 0x34
  xe::be<uint32_t> stack_base_ptr;  // 0x70 Stack base address (high addr)
  xe::be<uint32_t> stack_end_ptr;   // 0x74 Stack end (low addr)
  char unk_78[0x88];                // 0x78
  xe::be<uint32_t> current_thread;  // 0x100
  char unk_104[0x8];                // 0x104
  xe::be<uint8_t> current_cpu;      // 0x10C
  char unk_10D[0x43];               // 0x10D
  xe::be<uint32_t> dpc_active;      // 0x150
};

struct X_KTHREAD {
  X_DISPATCH_HEADER header;      // 0x0
  char unk_10[0xAC];             // 0x10
  uint8_t suspend_count;         // 0xBC
  uint8_t unk_BD;                // 0xBD
  uint16_t unk_BE;               // 0xBE
  char unk_C0[0x70];             // 0xC0
  xe::be<uint64_t> create_time;  // 0x130
  xe::be<uint64_t> exit_time;    // 0x138
  xe::be<uint32_t> exit_status;  // 0x140
  char unk_144[0x8];             // 0x144
  xe::be<uint32_t> thread_id;    // 0x14C
  char unk_150[0x10];            // 0x150
  xe::be<uint32_t> last_error;   // 0x160
  char unk_164[0x94C];           // 0x164

  // This struct is actually quite long... so uh, not filling this out!
};
static_assert_size(X_KTHREAD, 0xAB0);

class XThread : public XObject, public cpu::Thread {
 public:
  static const Type kType = kTypeThread;

  enum StartupType { Normal, XapiThreadStartup, DllMain };

  struct CreationParams {
    uint32_t stack_size;
    StartupType startup_type;
    uint32_t xapi_thread_startup;
    uint32_t start_address;
    uint32_t start_context;
    uint32_t creation_flags;
  };

  XThread(KernelState* kernel_state);
  XThread(KernelState* kernel_state, uint32_t stack_size,
          StartupType startup_type, uint32_t xapi_thread_startup,
          uint32_t start_address, uint32_t start_context,
          uint32_t creation_flags, bool guest_thread, bool main_thread = false);
  ~XThread() override;

  static bool IsInThread(XThread* other);
  static bool IsInThread();
  static XThread* GetCurrentThread();
  static uint32_t GetCurrentThreadHandle();
  static uint32_t GetCurrentThreadId();

  static uint32_t GetLastError();
  static void SetLastError(uint32_t error_code);

  const CreationParams* creation_params() const { return &creation_params_; }
  uint32_t tls_ptr() const { return tls_static_address_; }
  uint32_t pcr_ptr() const { return pcr_address_; }
  // True if the thread is created by the guest app.
  bool is_guest_thread() const { return guest_thread_; }
  bool main_thread() const { return main_thread_; }
  bool is_running() const { return running_; }

  uint32_t thread_id() const { return thread_id_; }
  uint32_t last_error();
  void set_last_error(uint32_t error_code);
  void set_name(const std::string& name);

  X_STATUS Create();
  X_STATUS Exit(int exit_code);
  X_STATUS Terminate(int exit_code);

  virtual void Execute();

  static void EnterCriticalRegion();
  static void LeaveCriticalRegion();
  uint32_t RaiseIrql(uint32_t new_irql);
  void LowerIrql(uint32_t new_irql);

  void CheckApcs();
  void LockApc();
  void UnlockApc(bool queue_delivery);
  util::NativeList* apc_list() { return &apc_list_; }
  void EnqueueApc(uint32_t normal_routine, uint32_t normal_context,
                  uint32_t arg1, uint32_t arg2);

  int32_t priority() const { return priority_; }
  int32_t QueryPriority();
  void SetPriority(int32_t increment);
  uint32_t affinity() const { return affinity_; }
  void SetAffinity(uint32_t affinity);
  uint32_t active_cpu() const;
  void SetActiveCpu(uint32_t cpu_index);

  bool GetTLSValue(uint32_t slot, uint32_t* value_out);
  bool SetTLSValue(uint32_t slot, uint32_t value);

  uint32_t suspend_count();
  X_STATUS Resume(uint32_t* out_suspend_count = nullptr);
  X_STATUS Suspend(uint32_t* out_suspend_count = nullptr);
  X_STATUS Delay(uint32_t processor_mode, uint32_t alertable,
                 uint64_t interval);

  xe::threading::Thread* thread() { return thread_.get(); }

  virtual bool Save(ByteStream* stream) override;
  static object_ref<XThread> Restore(KernelState* kernel_state,
                                     ByteStream* stream);

  // Internal - do not use.
  void AcquireMutantOnStartup(object_ref<XMutant> mutant) {
    pending_mutant_acquires_.push_back(mutant);
  }

 protected:
  bool AllocateStack(uint32_t size);
  void FreeStack();
  void InitializeGuestObject();

  void DeliverAPCs();
  void RundownAPCs();

  xe::threading::WaitHandle* GetWaitHandle() override { return thread_.get(); }

  CreationParams creation_params_ = {0};

  std::vector<object_ref<XMutant>> pending_mutant_acquires_;

  uint32_t thread_id_ = 0;
  uint32_t scratch_address_ = 0;
  uint32_t scratch_size_ = 0;
  uint32_t tls_static_address_ = 0;
  uint32_t tls_dynamic_address_ = 0;
  uint32_t tls_total_size_ = 0;
  uint32_t pcr_address_ = 0;
  uint32_t stack_alloc_base_ = 0;  // Stack alloc base
  uint32_t stack_alloc_size_ = 0;  // Stack alloc size
  uint32_t stack_base_ = 0;        // High address
  uint32_t stack_limit_ = 0;       // Low address
  bool guest_thread_ = false;
  bool main_thread_ = false;  // Entry-point thread
  bool running_ = false;

  int32_t priority_ = 0;
  uint32_t affinity_ = 0;

  xe::global_critical_region global_critical_region_;
  std::atomic<uint32_t> irql_ = {0};
  util::NativeList apc_list_;
};

class XHostThread : public XThread {
 public:
  XHostThread(KernelState* kernel_state, uint32_t stack_size,
              uint32_t creation_flags, std::function<int()> host_fn);

  virtual void Execute();

 private:
  std::function<int()> host_fn_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XTHREAD_H_
