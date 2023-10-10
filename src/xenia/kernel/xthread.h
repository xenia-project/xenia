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
struct XDPC {
  xe::be<uint16_t> type;
  uint8_t selected_cpu_number;
  uint8_t desired_cpu_number;
  X_LIST_ENTRY list_entry;
  xe::be<uint32_t> routine;
  xe::be<uint32_t> context;
  xe::be<uint32_t> arg1;
  xe::be<uint32_t> arg2;

  void Initialize(uint32_t guest_func, uint32_t guest_context) {
    type = 19;
    selected_cpu_number = 0;
    desired_cpu_number = 0;
    routine = guest_func;
    context = guest_context;
  }
};

struct XAPC {
  static const uint32_t kSize = 40;
  static const uint32_t kDummyKernelRoutine = 0xF00DFF00;
  static const uint32_t kDummyRundownRoutine = 0xF00DFF01;

  // KAPC is 0x28(40) bytes? (what's passed to ExAllocatePoolWithTag)
  // This is 4b shorter than NT - looks like the reserved dword at +4 is gone.
  // NOTE: stored in guest memory.
  uint16_t type;                      // +0
  uint8_t apc_mode;            // +2
  uint8_t enqueued;                  // +3
  xe::be<uint32_t> thread_ptr;       // +4
  X_LIST_ENTRY list_entry;           // +8
  xe::be<uint32_t> kernel_routine;   // +16
  xe::be<uint32_t> rundown_routine;  // +20
  xe::be<uint32_t> normal_routine;   // +24
  xe::be<uint32_t> normal_context;   // +28
  xe::be<uint32_t> arg1;             // +32
  xe::be<uint32_t> arg2;             // +36
};

struct X_KSEMAPHORE {
  X_DISPATCH_HEADER header;
  xe::be<uint32_t> limit;
};
static_assert_size(X_KSEMAPHORE, 0x14);

struct X_KTHREAD;
struct X_KPROCESS;
struct X_KPRCB {
  TypedGuestPointer<X_KTHREAD> current_thread;  // 0x0
  TypedGuestPointer<X_KTHREAD> unk_4;           // 0x4
  TypedGuestPointer<X_KTHREAD> idle_thread;     // 0x8
  uint8_t current_cpu;                          // 0xC
  uint8_t unk_D[3];                             // 0xD
  // should only have 1 bit set, used for ipis
  xe::be<uint32_t> processor_mask;  // 0x10
  // incremented in clock interrupt
  xe::be<uint32_t> dpc_clock;        // 0x14
  xe::be<uint32_t> interrupt_clock;  // 0x18
  xe::be<uint32_t> unk_1C;           // 0x1C
  xe::be<uint32_t> unk_20;           // 0x20
  // various fields used by KeIpiGenericCall
  xe::be<uint32_t> ipi_args[3];  // 0x24
  // looks like the target cpus clear their corresponding bit
  // in this mask to signal completion to the initiator
  xe::be<uint32_t> targeted_ipi_cpus_mask;  // 0x30
  xe::be<uint32_t> ipi_function;            // 0x34
  // used to synchronize?
  TypedGuestPointer<X_KPRCB> ipi_initiator_prcb;  // 0x38
  xe::be<uint32_t> unk_3C;                        // 0x3C
  xe::be<uint32_t> dpc_related_40;                // 0x40
  // must be held to modify any dpc-related fields in the kprcb
  xe::be<uint32_t> dpc_lock;           // 0x44
  X_LIST_ENTRY queued_dpcs_list_head;  // 0x48
  xe::be<uint32_t> dpc_active;         // 0x50
  xe::be<uint32_t> unk_54;             // 0x54
  xe::be<uint32_t> unk_58;             // 0x58
  // definitely scheduler related
  X_SINGLE_LIST_ENTRY unk_5C;  // 0x5C
  xe::be<uint32_t> unk_60;     // 0x60
  // i think the following mask has something to do with the array that comes
  // after
  xe::be<uint32_t> unk_mask_64;  // 0x64

  X_LIST_ENTRY unk_68[32];  // 0x68
  // ExTerminateThread tail calls a function that does KeInsertQueueDpc of this
  // dpc
  XDPC thread_exit_dpc;  // 0x168
  // thread_exit_dpc's routine drains this list and frees each threads threadid,
  // kernel stack and dereferences the thread
  X_LIST_ENTRY terminating_threads_list;  // 0x184
  XDPC unk_18C;                           // 0x18C
};
// Processor Control Region
struct X_KPCR {
  xe::be<uint32_t> tls_ptr;   // 0x0
  xe::be<uint32_t> msr_mask;  // 0x4
  union {
    xe::be<uint16_t> software_interrupt_state;  // 0x8
    struct {
      uint8_t unknown_8;                     // 0x8
      uint8_t apc_software_interrupt_state;  // 0x9
    };
  };
  uint8_t unk_0A[2];                 // 0xA
  uint8_t processtype_value_in_dpc;  // 0xC
  uint8_t unk_0D[3];                 // 0xD
  // used in KeSaveFloatingPointState / its vmx counterpart
  xe::be<uint32_t> thread_fpu_related;  // 0x10
  xe::be<uint32_t> thread_vmx_related;  // 0x14
  uint8_t current_irql;                 // 0x18
  uint8_t unk_19[0x17];                 // 0x19
  xe::be<uint64_t> pcr_ptr;             // 0x30

  // this seems to be just garbage data? we can stash a pointer to context here
  // as a hack for now
  union {
    uint8_t unk_38[8];    // 0x38
    uint64_t host_stash;  // 0x38
  };
  uint8_t unk_40[28];                      // 0x40
  xe::be<uint32_t> unk_stack_5c;           // 0x5C
  uint8_t unk_60[12];                      // 0x60
  xe::be<uint32_t> use_alternative_stack;  // 0x6C
  xe::be<uint32_t> stack_base_ptr;  // 0x70 Stack base address (high addr)
  xe::be<uint32_t> stack_end_ptr;   // 0x74 Stack end (low addr)

  // maybe these are the stacks used in apcs?
  // i know they're stacks, RtlGetStackLimits returns them if another var here
  // is set

  xe::be<uint32_t> alt_stack_base_ptr;  // 0x78
  xe::be<uint32_t> alt_stack_end_ptr;   // 0x7C
  // if bit 1 is set in a handler pointer, it actually points to a KINTERRUPT
  // otherwise, it points to a function to execute
  xe::be<uint32_t> interrupt_handlers[32];  // 0x80
  X_KPRCB prcb_data;                        // 0x100
  // pointer to KPCRB?
  TypedGuestPointer<X_KPRCB> prcb;  // 0x2A8
  uint8_t unk_2AC[0x2C];            // 0x2AC
};

struct X_KTHREAD {
  X_DISPATCH_HEADER header;       // 0x0
  xe::be<uint32_t> unk_10;        // 0x10
  xe::be<uint32_t> unk_14;        // 0x14
  uint8_t unk_18[0x28];           // 0x10
  xe::be<uint32_t> unk_40;        // 0x40
  xe::be<uint32_t> unk_44;        // 0x44
  xe::be<uint32_t> unk_48;        // 0x48
  xe::be<uint32_t> unk_4C;        // 0x4C
  uint8_t unk_50[0x4];            // 0x50
  xe::be<uint16_t> unk_54;        // 0x54
  xe::be<uint16_t> unk_56;        // 0x56
  uint8_t unk_58[0x4];            // 0x58
  xe::be<uint32_t> stack_base;    // 0x5C
  xe::be<uint32_t> stack_limit;   // 0x60
  xe::be<uint32_t> stack_kernel;  // 0x64
  xe::be<uint32_t> tls_address;   // 0x68
  // state = is thread running, suspended, etc
  uint8_t thread_state;  // 0x6C
  // 0x70 = priority?
  uint8_t unk_6D[0x3];        // 0x6D
  uint8_t priority;           // 0x70
  uint8_t fpu_exceptions_on;  // 0x71
  // these two process types both get set to the same thing, process_type is
  // referenced most frequently, however process_type_dup gets referenced a few
  // times while the process is being created
  uint8_t process_type_dup;
  uint8_t process_type;
  //apc_mode determines which list an apc goes into
  util::X_TYPED_LIST<XAPC, offsetof(XAPC, list_entry)> apc_lists[2]; 
  TypedGuestPointer<X_KPROCESS> process;  // 0x84
  uint8_t unk_88[0x3];                    // 0x88
  uint8_t apc_related;                    // 0x8B
  uint8_t unk_8C[0x10];                   // 0x8C
  xe::be<uint32_t> msr_mask;              // 0x9C
  uint8_t unk_A0[4];                      // 0xA0
  uint8_t unk_A4;                         // 0xA4
  uint8_t unk_A5[0xB];                    // 0xA5
  int32_t apc_disable_count;              // 0xB0
  uint8_t unk_B4[0x8];                    // 0xB4
  uint8_t suspend_count;                  // 0xBC
  uint8_t unk_BD;                         // 0xBD
  uint8_t terminated;                     // 0xBE
  uint8_t current_cpu;                    // 0xBF
  // these two pointers point to KPRCBs, but seem to be rarely referenced, if at
  // all
  TypedGuestPointer<X_KPRCB> a_prcb_ptr;        // 0xC0
  TypedGuestPointer<X_KPRCB> another_prcb_ptr;  // 0xC4
  uint8_t unk_C8[8];                            // 0xC8
  xe::be<uint32_t> stack_alloc_base;            // 0xD0
  // uint8_t unk_D4[0x5C];               // 0xD4
  XAPC on_suspend;      // 0xD4
  X_KSEMAPHORE unk_FC;  // 0xFC
  // this is an entry in
  X_LIST_ENTRY process_threads;     // 0x110
  xe::be<uint32_t> unk_118;         // 0x118
  xe::be<uint32_t> unk_11C;         // 0x11C
  xe::be<uint32_t> unk_120;         // 0x120
  xe::be<uint32_t> unk_124;         // 0x124
  xe::be<uint32_t> unk_128;         // 0x128
  xe::be<uint32_t> unk_12C;         // 0x12C
  xe::be<uint64_t> create_time;     // 0x130
  xe::be<uint64_t> exit_time;       // 0x138
  xe::be<uint32_t> exit_status;     // 0x140
  xe::be<uint32_t> unk_144;         // 0x144
  xe::be<uint32_t> unk_148;         // 0x148
  xe::be<uint32_t> thread_id;       // 0x14C
  xe::be<uint32_t> start_address;   // 0x150
  xe::be<uint32_t> unk_154;         // 0x154
  xe::be<uint32_t> unk_158;         // 0x158
  uint8_t unk_15C[0x4];             // 0x15C
  xe::be<uint32_t> last_error;      // 0x160
  xe::be<uint32_t> fiber_ptr;       // 0x164
  uint8_t unk_168[0x4];             // 0x168
  xe::be<uint32_t> creation_flags;  // 0x16C
  uint8_t unk_170[0xC];             // 0x170
  xe::be<uint32_t> unk_17C;         // 0x17C
  uint8_t unk_180[0x930];           // 0x180

  // This struct is actually quite long... so uh, not filling this out!
};
static_assert_size(X_KTHREAD, 0xAB0);

class XThread : public XObject, public cpu::Thread {
 public:
  static const XObject::Type kObjectType = XObject::Type::Thread;

  static constexpr uint32_t kStackAddressRangeBegin = 0x70000000;
  static constexpr uint32_t kStackAddressRangeEnd = 0x7F000000;

  static constexpr uint32_t kThreadKernelStackSize = 0xF0;

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
  void SetCurrentThread();
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
