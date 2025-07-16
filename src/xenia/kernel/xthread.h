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

enum IRQL_FLAGS : uint8_t {
  IRQL_PASSIVE = 0,
  IRQL_APC = 1,
  IRQL_DISPATCH = 2,
  IRQL_DPC = 3,
  IRQL_AUDIO = 68,   // used a few times in the audio driver
  IRQL_CLOCK = 116,  // irql used by the clock interrupt
  IRQL_HIGHEST = 124
};

enum X_DISPATCHER_FLAGS {
  DISPATCHER_MANUAL_RESET_EVENT = 0,
  DISPATCHER_AUTO_RESET_EVENT = 1,
  DISPATCHER_MUTANT = 2,
  DISPATCHER_QUEUE = 4,
  DISPATCHER_SEMAPHORE = 5,
  DISPATCHER_THREAD = 6,
  DISPATCHER_MANUAL_RESET_TIMER = 8,
  DISPATCHER_AUTO_RESET_TIMER = 9,
};

// https://www.geoffchappell.com/studies/windows/km/ntoskrnl/inc/ntos/ke/kthread_state.htm
enum X_KTHREAD_STATE_FLAGS : uint8_t {
  KTHREAD_STATE_INITIALIZED = 0,
  KTHREAD_STATE_READY = 1,
  KTHREAD_STATE_RUNNING = 2,
  KTHREAD_STATE_STANDBY = 3,
  KTHREAD_STATE_TERMINATED = 4,
  KTHREAD_STATE_WAITING = 5,
  KTHREAD_STATE_UNKNOWN = 6,  //"Transition" except that makes no sense here, so
                              // 6 likely has a different meaning on xboxkrnl
};

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
  static constexpr uint32_t kSize = 40;
  static constexpr uint32_t kDummyKernelRoutine = 0xF00DFF00;
  static constexpr uint32_t kDummyRundownRoutine = 0xF00DFF01;

  // KAPC is 0x28(40) bytes? (what's passed to ExAllocatePoolWithTag)
  // This is 4b shorter than NT - looks like the reserved dword at +4 is gone.
  // NOTE: stored in guest memory.
  uint16_t type;                     // +0
  uint8_t apc_mode;                  // +2
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
  TypedGuestPointer<X_KTHREAD> next_thread;     // 0x4
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
  xe::be<uint32_t> dpc_lock;                         // 0x44
  X_LIST_ENTRY queued_dpcs_list_head;                // 0x48
  xe::be<uint32_t> dpc_active;                       // 0x50
  X_KSPINLOCK spin_lock;                             // 0x54
  TypedGuestPointer<X_KTHREAD> running_idle_thread;  // 0x58
  // definitely scheduler related
  X_SINGLE_LIST_ENTRY enqueued_threads_list;      // 0x5C
  xe::be<uint32_t> has_ready_thread_by_priority;  // 0x60
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
  XDPC switch_thread_processor_dpc;       // 0x18C
};
// Processor Control Region
struct X_KPCR {
  xe::be<uint32_t> tls_ptr;   // 0x0
  xe::be<uint32_t> msr_mask;  // 0x4
  union {
    xe::be<uint16_t> software_interrupt_state;  // 0x8
    struct {
      uint8_t generic_software_interrupt;    // 0x8
      uint8_t apc_software_interrupt_state;  // 0x9
    };
  };
  xe::be<uint16_t> unk_0A;           // 0xA
  uint8_t processtype_value_in_dpc;  // 0xC
  uint8_t timeslice_ended;           // 0xD
  uint8_t timer_pending;             // 0xE
  uint8_t unk_0F;                    // 0xF
  // used in KeSaveFloatingPointState / its vmx counterpart
  xe::be<uint32_t> thread_fpu_related;   // 0x10
  xe::be<uint32_t> thread_vmx_related;   // 0x14
  uint8_t current_irql;                  // 0x18
  uint8_t background_scheduling_active;  // 0x19
  uint8_t background_scheduling_1A;      // 0x1A
  uint8_t background_scheduling_1B;      // 0x1B
  xe::be<uint32_t> timer_related;        // 0x1C
  uint8_t unk_20[0x10];                  // 0x20
  xe::be<uint64_t> pcr_ptr;              // 0x30

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

enum : uint16_t {
  WAIT_ALL = 0,
  WAIT_ANY = 1,
};

// https://www.geoffchappell.com/studies/windows/km/ntoskrnl/inc/ntos/ke_x/kwait_block.htm
//  pretty much the vista KWAIT_BLOCK verbatim, except that sparebyte is gone
//  and WaitType is 2 bytes instead of 1
struct X_KWAIT_BLOCK {
  X_LIST_ENTRY wait_list_entry;  // 0x0
  TypedGuestPointer<X_KTHREAD> thread;
  TypedGuestPointer<X_DISPATCH_HEADER> object;
  TypedGuestPointer<X_KWAIT_BLOCK> next_wait_block;
  // this isnt the official vista name, but i think its better.
  // this value is what will be returned to the waiter if this particular wait
  // is satisfied
  xe::be<uint16_t> wait_result_xstatus;
  // WAIT_ALL or WAIT_ANY
  xe::be<uint16_t> wait_type;
};

static_assert_size(X_KWAIT_BLOCK, 0x18);

struct X_KTIMER {
  X_DISPATCH_HEADER header;         // 0x0
  xe::be<uint64_t> due_time;        // 0x10
  X_LIST_ENTRY table_bucket_entry;  // 0x18
  TypedGuestPointer<XDPC> dpc;      // 0x20
  xe::be<uint32_t> period;          // 0x24
};
static_assert_size(X_KTIMER, 0x28);

struct X_KTHREAD {
  X_DISPATCH_HEADER header;          // 0x0
  xe::be<uint32_t> unk_10;           // 0x10
  xe::be<uint32_t> unk_14;           // 0x14
  X_KTIMER wait_timeout_timer;       // 0x18
  X_KWAIT_BLOCK wait_timeout_block;  // 0x40
  uint8_t unk_58[0x4];               // 0x58
  xe::be<uint32_t> stack_base;       // 0x5C
  xe::be<uint32_t> stack_limit;      // 0x60
  xe::be<uint32_t> stack_kernel;     // 0x64
  xe::be<uint32_t> tls_address;      // 0x68
  // state = is thread running, suspended, etc
  uint8_t thread_state;  // 0x6C
  // 0x70 = priority?
  uint8_t alerted[2];         // 0x6D
  uint8_t alertable;          // 0x6F
  uint8_t priority;           // 0x70
  uint8_t fpu_exceptions_on;  // 0x71
  // these two process types both get set to the same thing, process_type is
  // referenced most frequently, however process_type_dup gets referenced a few
  // times while the process is being created
  uint8_t process_type_dup;
  uint8_t process_type;
  // apc_mode determines which list an apc goes into
  util::X_TYPED_LIST<XAPC, offsetof(XAPC, list_entry)> apc_lists[2];
  TypedGuestPointer<X_KPROCESS> process;  // 0x84
  uint8_t executing_kernel_apc;           // 0x88
  // when context switch happens, this is copied into
  // apc_software_interrupt_state for kpcr
  uint8_t deferred_apc_software_interrupt_state;  // 0x89
  uint8_t user_apc_pending;                       // 0x8A
  uint8_t may_queue_apcs;                         // 0x8B
  X_KSPINLOCK apc_lock;                           // 0x8C
  xe::be<uint32_t> num_context_switches_to;       // 0x90
  X_LIST_ENTRY ready_prcb_entry;                  // 0x94
  xe::be<uint32_t> msr_mask;                      // 0x9C
  xe::be<X_STATUS> wait_result;                   // 0xA0
  uint8_t wait_irql;                              // 0xA4
  uint8_t unk_A5[0xB];                            // 0xA5
  int32_t apc_disable_count;                      // 0xB0
  xe::be<int32_t> quantum;                        // 0xB4
  uint8_t unk_B8;                                 // 0xB8
  uint8_t unk_B9;                                 // 0xB9
  uint8_t unk_BA;                                 // 0xBA
  uint8_t boost_disabled;                         // 0xBB
  uint8_t suspend_count;                          // 0xBC
  uint8_t was_preempted;                          // 0xBD
  uint8_t terminated;                             // 0xBE
  uint8_t current_cpu;                            // 0xBF
  // these two pointers point to KPRCBs, but seem to be rarely referenced, if at
  // all
  TypedGuestPointer<X_KPRCB> a_prcb_ptr;        // 0xC0
  TypedGuestPointer<X_KPRCB> another_prcb_ptr;  // 0xC4
  uint8_t unk_C8;                               // 0xC8
  uint8_t unk_C9;                               // 0xC9
  uint8_t unk_CA;                               // 0xCA
  uint8_t unk_CB;                               // 0xCB
  X_KSPINLOCK timer_list_lock;                  // 0xCC
  xe::be<uint32_t> stack_alloc_base;            // 0xD0
  XAPC on_suspend;                              // 0xD4
  X_KSEMAPHORE suspend_sema;                    // 0xFC
  // this is an entry in
  X_LIST_ENTRY process_threads;  // 0x110
  xe::be<uint32_t> unk_118;      // 0x118
  X_LIST_ENTRY queue_related;    // 0x11C
  xe::be<uint32_t> unk_124;      // 0x124
  xe::be<uint32_t> unk_128;      // 0x128
  xe::be<uint32_t> unk_12C;      // 0x12C
  xe::be<uint64_t> create_time;  // 0x130
  xe::be<uint64_t> exit_time;    // 0x138
  xe::be<uint32_t> exit_status;  // 0x140
  // tracks all pending timers that have apcs which target this thread
  X_LIST_ENTRY timer_list;          // 0x144
  xe::be<uint32_t> thread_id;       // 0x14C
  xe::be<uint32_t> start_address;   // 0x150
  X_LIST_ENTRY unk_154;             // 0x154
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
    uint32_t guest_process;
  };

  XThread(KernelState* kernel_state);
  XThread(KernelState* kernel_state, uint32_t stack_size,
          uint32_t xapi_thread_startup, uint32_t start_address,
          uint32_t start_context, uint32_t creation_flags, bool guest_thread,
          bool main_thread = false, uint32_t guest_process = 0);
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
};

class XHostThread : public XThread {
 public:
  XHostThread(KernelState* kernel_state, uint32_t stack_size,
              uint32_t creation_flags, std::function<int()> host_fn,
              uint32_t guest_process = 0);

  virtual void Execute();

 private:
  std::function<int()> host_fn_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XTHREAD_H_
