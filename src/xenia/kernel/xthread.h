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
#include "xenia/cpu/thread_state.h"
#include "xenia/kernel/util/native_list.h"
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
  xe::be<uint8_t> type;              // +0
  xe::be<uint8_t> unk1;              // +1
  xe::be<uint8_t> processor_mode;    // +2
  xe::be<uint8_t> enqueued;          // +3
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

class XThread : public XObject {
 public:
  static const Type kType = kTypeThread;

  struct CreationParams {
    uint32_t stack_size;
    uint32_t xapi_thread_startup;
    uint32_t start_address;
    uint32_t start_context;
    uint32_t creation_flags;
  };

  XThread(KernelState* kernel_state, uint32_t stack_size,
          uint32_t xapi_thread_startup, uint32_t start_address,
          uint32_t start_context, uint32_t creation_flags, bool guest_thread);
  ~XThread() override;

  static bool IsInThread(XThread* other);
  static bool IsInThread();
  static XThread* GetCurrentThread();
  static uint32_t GetCurrentThreadHandle();
  static uint32_t GetCurrentThreadId();

  static uint32_t GetLastError();
  static void SetLastError(uint32_t error_code);

  const CreationParams* creation_params() const { return &creation_params_; }
  uint32_t tls_ptr() const { return tls_address_; }
  uint32_t pcr_ptr() const { return pcr_address_; }
  // True if the thread is created by the guest app.
  bool is_guest_thread() const { return guest_thread_; }
  // True if the thread should be paused by the debugger.
  // All threads that can run guest code must be stopped for the debugger to
  // work properly.
  bool can_debugger_suspend() const { return can_debugger_suspend_; }
  void set_can_debugger_suspend(bool value) { can_debugger_suspend_ = value; }
  bool is_running() const { return running_; }

  cpu::ThreadState* thread_state() const { return thread_state_; }
  uint32_t thread_id() const { return thread_id_; }
  uint32_t last_error();
  void set_last_error(uint32_t error_code);
  const std::string& name() const { return name_; }
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

  uint32_t suspend_count();
  X_STATUS Resume(uint32_t* out_suspend_count = nullptr);
  X_STATUS Suspend(uint32_t* out_suspend_count);
  X_STATUS Delay(uint32_t processor_mode, uint32_t alertable,
                 uint64_t interval);

  xe::threading::WaitHandle* GetWaitHandle() override { return thread_.get(); }

 protected:
  void DeliverAPCs();
  void RundownAPCs();

  CreationParams creation_params_ = {0};

  uint32_t thread_id_ = 0;
  std::unique_ptr<xe::threading::Thread> thread_;
  uint32_t scratch_address_ = 0;
  uint32_t scratch_size_ = 0;
  uint32_t tls_address_ = 0;
  uint32_t pcr_address_ = 0;
  cpu::ThreadState* thread_state_ = nullptr;
  bool guest_thread_ = false;
  bool can_debugger_suspend_ = true;
  bool running_ = false;

  std::string name_;

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
