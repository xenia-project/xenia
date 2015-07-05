/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XTHREAD_H_
#define XENIA_KERNEL_XBOXKRNL_XTHREAD_H_

#include <atomic>
#include <mutex>
#include <string>

#include "xenia/cpu/thread_state.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class NativeList;
class XEvent;

constexpr uint32_t X_CREATE_SUSPENDED = 0x00000004;

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

// http://www.nirsoft.net/kernel_struct/vista/KTHREAD.html
struct X_THREAD {
  X_DISPATCH_HEADER header;
  xe::be<uint64_t> cycle_time;
  xe::be<uint32_t> high_cycle_time;  // FIXME: Needed?
  xe::be<uint64_t> quantum_target;
  xe::be<uint32_t> initial_stack_ptr;
  xe::be<uint32_t> stack_limit_ptr;
  xe::be<uint32_t> kernel_stack_ptr;

  // This struct is actually quite long... so uh, not filling this out!
};

class XThread : public XObject {
 public:
  XThread(KernelState* kernel_state, uint32_t stack_size,
          uint32_t xapi_thread_startup, uint32_t start_address,
          uint32_t start_context, uint32_t creation_flags, bool guest_thread);
  virtual ~XThread();

  static bool IsInThread(XThread* other);
  static XThread* GetCurrentThread();
  static uint32_t GetCurrentThreadHandle();
  static uint32_t GetCurrentThreadId(const uint8_t* pcr);

  uint32_t tls_ptr() const { return tls_address_; }
  uint32_t pcr_ptr() const { return pcr_address_; }
  uint32_t thread_state_ptr() const { return thread_state_address_; }
  bool guest_thread() const { return guest_thread_; }

  cpu::ThreadState* thread_state() const { return thread_state_; }
  uint32_t thread_id() const { return thread_id_; }
  uint32_t last_error();
  void set_last_error(uint32_t error_code);
  const std::string& name() const { return name_; }
  void set_name(const std::string& name);

  X_STATUS Create();
  X_STATUS Exit(int exit_code);

  virtual void Execute();

  static void EnterCriticalRegion();
  static void LeaveCriticalRegion();
  uint32_t RaiseIrql(uint32_t new_irql);
  void LowerIrql(uint32_t new_irql);

  void CheckApcs();
  void LockApc();
  void UnlockApc(bool queue_delivery);
  NativeList* apc_list() const { return apc_list_; }
  void EnqueueApc(uint32_t normal_routine, uint32_t normal_context,
                  uint32_t arg1, uint32_t arg2);

  int32_t priority() const { return priority_; }
  int32_t QueryPriority();
  void SetPriority(int32_t increment);
  uint32_t affinity() const { return affinity_; }
  void SetAffinity(uint32_t affinity);
  uint32_t active_cpu() const;
  void SetActiveCpu(uint32_t cpu_index);

  X_STATUS Resume(uint32_t* out_suspend_count = nullptr);
  X_STATUS Suspend(uint32_t* out_suspend_count);
  X_STATUS Delay(uint32_t processor_mode, uint32_t alertable,
                 uint64_t interval);

  virtual void* GetWaitHandle();

 protected:
  X_STATUS PlatformCreate();
  void PlatformDestroy();
  X_STATUS PlatformExit(int exit_code);

  static void DeliverAPCs(void* data);
  void RundownAPCs();

  struct {
    uint32_t stack_size;
    uint32_t xapi_thread_startup;
    uint32_t start_address;
    uint32_t start_context;
    uint32_t creation_flags;
  } creation_params_;

  uint32_t thread_id_;
  void* thread_handle_;
  uint32_t scratch_address_;
  uint32_t scratch_size_;
  uint32_t tls_address_;
  uint32_t pcr_address_;
  uint32_t thread_state_address_;
  cpu::ThreadState* thread_state_;
  bool guest_thread_; // Launched into guest code?

  std::string name_;

  int32_t priority_;
  uint32_t affinity_;

  std::atomic<uint32_t> irql_;
  xe::mutex apc_lock_;
  NativeList* apc_list_;

  object_ref<XEvent> event_;
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

#endif  // XENIA_KERNEL_XBOXKRNL_XTHREAD_H_
