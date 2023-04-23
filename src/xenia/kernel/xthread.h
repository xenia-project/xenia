/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
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

constexpr fourcc_t kThreadSaveSignature = make_fourcc("THRD");

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
  uint16_t type;                      // +0
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
  xe::be<uint32_t> msr_mask;		// 0x4
  xe::be<uint16_t> interrupt_related;  // 0x8
  uint8_t unk_08[0xE];                 // 0xA
  uint8_t current_irql;                // 0x18
  uint8_t unk_19[0x17];                // 0x19
  xe::be<uint32_t> pcr_ptr;         // 0x30
  uint8_t unk_34[0x38];             // 0x34
  xe::be<uint32_t> use_alternative_stack; //0x6C
  xe::be<uint32_t> stack_base_ptr;  // 0x70 Stack base address (high addr)
  xe::be<uint32_t> stack_end_ptr;   // 0x74 Stack end (low addr)

  //maybe these are the stacks used in apcs?
  //i know they're stacks, RtlGetStackLimits returns them if another var here is set

  xe::be<uint32_t> alt_stack_base_ptr;  // 0x78
  xe::be<uint32_t> alt_stack_end_ptr;   // 0x7C
  uint8_t unk_80[0x80];             // 0x80
  xe::be<uint32_t> current_thread;  // 0x100
  uint8_t unk_104[0x8];             // 0x104
  uint8_t current_cpu;              // 0x10C
  uint8_t unk_10D[0x43];            // 0x10D
  xe::be<uint32_t> dpc_active;      // 0x150
};

struct X_KTHREAD {
  X_DISPATCH_HEADER header;           // 0x0
  xe::be<uint32_t> unk_10;            // 0x10
  xe::be<uint32_t> unk_14;            // 0x14
  uint8_t unk_18[0x28];               // 0x10
  xe::be<uint32_t> unk_40;            // 0x40
  xe::be<uint32_t> unk_44;            // 0x44
  xe::be<uint32_t> unk_48;            // 0x48
  xe::be<uint32_t> unk_4C;            // 0x4C
  uint8_t unk_50[0x4];                // 0x50
  xe::be<uint16_t> unk_54;            // 0x54
  xe::be<uint16_t> unk_56;            // 0x56
  uint8_t unk_58[0x4];                // 0x58
  xe::be<uint32_t> stack_base;        // 0x5C
  xe::be<uint32_t> stack_limit;       // 0x60
  xe::be<uint32_t> stack_kernel;       // 0x64
  xe::be<uint32_t> tls_address;       // 0x68
  uint8_t unk_6C;                     // 0x6C
  //0x70 = priority?
  uint8_t unk_6D[0x3];                // 0x6D
  uint8_t priority;					  // 0x70
  uint8_t fpu_exceptions_on;		  // 0x71
  uint8_t unk_72;
  uint8_t unk_73;
  xe::be<uint32_t> unk_74;            // 0x74
  xe::be<uint32_t> unk_78;            // 0x78
  xe::be<uint32_t> unk_7C;            // 0x7C
  xe::be<uint32_t> unk_80;            // 0x80
  xe::be<uint32_t> unk_84;            // 0x84
  uint8_t unk_88[0x3];                // 0x88
  uint8_t unk_8B;                     // 0x8B
  uint8_t unk_8C[0x10];               // 0x8C
  xe::be<uint32_t> unk_9C;            // 0x9C
  uint8_t unk_A0[0x10];               // 0xA0
  int32_t apc_disable_count;          // 0xB0
  uint8_t unk_B4[0x8];                // 0xB4
  uint8_t suspend_count;              // 0xBC
  uint8_t unk_BD;                     // 0xBD
  uint8_t terminated;                     // 0xBE
  uint8_t current_cpu;                // 0xBF
  uint8_t unk_C0[0x10];               // 0xC0
  xe::be<uint32_t> stack_alloc_base;  // 0xD0
  uint8_t unk_D4[0x5C];               // 0xD4
  xe::be<uint64_t> create_time;       // 0x130
  xe::be<uint64_t> exit_time;         // 0x138
  xe::be<uint32_t> exit_status;       // 0x140
  xe::be<uint32_t> unk_144;           // 0x144
  xe::be<uint32_t> unk_148;           // 0x148
  xe::be<uint32_t> thread_id;         // 0x14C
  xe::be<uint32_t> start_address;     // 0x150
  xe::be<uint32_t> unk_154;           // 0x154
  xe::be<uint32_t> unk_158;           // 0x158
  uint8_t unk_15C[0x4];               // 0x15C
  xe::be<uint32_t> last_error;        // 0x160
  xe::be<uint32_t> fiber_ptr;         // 0x164
  uint8_t unk_168[0x4];               // 0x168
  xe::be<uint32_t> creation_flags;    // 0x16C
  uint8_t unk_170[0xC];               // 0x170
  xe::be<uint32_t> unk_17C;           // 0x17C
  uint8_t unk_180[0x930];             // 0x180

  // This struct is actually quite long... so uh, not filling this out!
};
static_assert_size(X_KTHREAD, 0xAB0);

class XThread : public XObject, public cpu::Thread {
 public:
  static const XObject::Type kObjectType = XObject::Type::Thread;

  static constexpr uint32_t kStackAddressRangeBegin = 0x70000000;
  static constexpr uint32_t kStackAddressRangeEnd = 0x7F000000;

  struct CreationParams {
    uint32_t stack_size;
    uint32_t xapi_thread_startup;
    uint32_t start_address;
    uint32_t start_context;
    uint32_t creation_flags;
  };

  XThread(KernelState* kernel_state);
  XThread(KernelState* kernel_state, uint32_t stack_size,
          uint32_t xapi_thread_startup, uint32_t start_address,
          uint32_t start_context, uint32_t creation_flags, bool guest_thread,
          bool main_thread = false);
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
  void set_name(const std::string_view name);

  X_STATUS Create();
  X_STATUS Exit(int exit_code);
  X_STATUS Terminate(int exit_code);

  virtual void Execute();

  virtual void Reenter(uint32_t address);

  void EnterCriticalRegion();
  void LeaveCriticalRegion();
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

  // Xbox thread IDs:
  // 0 - core 0, thread 0 - user
  // 1 - core 0, thread 1 - user
  // 2 - core 1, thread 0 - sometimes xcontent
  // 3 - core 1, thread 1 - user
  // 4 - core 2, thread 0 - xaudio
  // 5 - core 2, thread 1 - user
  void SetAffinity(uint32_t affinity);
  uint8_t active_cpu() const;
  void SetActiveCpu(uint8_t cpu_index);

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
