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

struct X_KWAIT_BLOCK {
  X_LIST_ENTRY wait_list_entry;          // 0x0
  xe::be<uint32_t> thread;               // 0x8 PKTHREAD
  xe::be<uint32_t> object;               // 0xC
  xe::be<uint32_t> next_wait_block_ptr;  // 0x10 PKWAIT_BLOCK
  xe::be<uint16_t> wait_key;             // 0x14
  xe::be<uint16_t> wait_type;            // 0x16
};
static_assert_size(X_KWAIT_BLOCK, 0x18);

// https://github.com/G91/TitanOffLine/blob/master/xkelib/kernel/kePrivateStructs.h#L409
struct X_KTHREAD {
  X_DISPATCH_HEADER header;                // 0x0
  X_LIST_ENTRY mutant_list_head;           // 0x10
  char timer[0x28];                        // 0x18 KTIMER
  X_KWAIT_BLOCK timer_wait_block;          // 0x40 KWAIT_BLOCK
  xe::be<uint32_t> kernel_time;            // 0x58
  xe::be<uint32_t> stack_base;             // 0x5C
  xe::be<uint32_t> stack_limit;            // 0x60
  xe::be<uint32_t> kernel_stack;           // 0x64
  xe::be<uint32_t> tls_data;               // 0x68
  xe::be<uint8_t> state;                   // 0x6C
  xe::be<uint8_t> alerted[2];              // 0x6D
  xe::be<uint8_t> alertable;               // 0x6F
  xe::be<uint8_t> priority;                // 0x70
  xe::be<uint8_t> fpu_exception_enable;    // 0x71
  xe::be<uint8_t> create_process_type;     // 0x72
  xe::be<uint8_t> current_process_type;    // 0x73
  X_LIST_ENTRY apc_list_head[2];           // 0x74
  xe::be<uint32_t> process_ptr;            // 0x84 PKPROCESS
  xe::be<uint8_t> kernel_apc_in_progress;  // 0x88
  xe::be<uint8_t> kernel_apc_pending;      // 0x89
  xe::be<uint8_t> user_apc_pending;        // 0x8A
  xe::be<uint8_t> apc_queueable;           // 0x8B
  xe::be<uint32_t> apc_queue_lock;         // 0x8C
  xe::be<uint32_t> context_switches;       // 0x90
  X_LIST_ENTRY ready_list_entry;           // 0x94
  union {
    struct {
      xe::be<uint16_t> msr_enable_mask_high;  // 0x9C
      xe::be<uint16_t> msr_enable_mask_low;   // 0x9E
    };
    xe::be<uint32_t> msr_enable_mask;  // 0x9C
  } msr;
  xe::be<uint32_t> wait_status;                  // 0xA0
  xe::be<uint8_t> wait_irql;                     // 0xA4
  xe::be<uint8_t> wait_mode;                     // 0xA5
  xe::be<uint8_t> wait_next;                     // 0xA6
  xe::be<uint8_t> wait_reason;                   // 0xA7
  xe::be<uint32_t> wait_block_list;              // 0xA8
  xe::be<uint32_t> padding1;                     // 0xAC
  xe::be<uint32_t> kernel_apc_disable;           // 0xB0
  xe::be<uint32_t> quantum;                      // 0xB4
  xe::be<uint8_t> saturation;                    // 0xB8
  xe::be<uint8_t> base_priority;                 // 0xB9
  xe::be<uint8_t> priority_decrement;            // 0xBA
  xe::be<uint8_t> disable_boost;                 // 0xBB
  xe::be<uint8_t> suspend_count;                 // 0xBC
  xe::be<uint8_t> preempted;                     // 0xBD
  xe::be<uint8_t> has_terminated;                // 0xBE
  xe::be<uint8_t> current_processor;             // 0xBF
  xe::be<uint32_t> current_prcb;                 // 0xC0
  xe::be<uint32_t> affinity_prcb;                // 0xC4
  xe::be<uint8_t> idle_priority_class;           // 0xC8
  xe::be<uint8_t> normal_priority_class;         // 0xC9
  xe::be<uint8_t> time_critical_priority_class;  // 0xCA
  xe::be<uint8_t> has_async_terminated;          // 0xCB
  xe::be<uint32_t> active_timer_list_lock;       // 0xCC
  xe::be<uint32_t> stack_allocated_base;         // 0xD0
  XAPC suspend_apc;                              // 0xD4
  char suspend_semaphore[0x14];                  // 0xFC KSEMAPHORE
  X_LIST_ENTRY thread_list_entry;                // 0x110
  xe::be<uint32_t> queue_ptr;                    // 0x118 PKQUEUE
  X_LIST_ENTRY queue_list_entry;                 // 0x11C
  xe::be<uint32_t> user_mode_dispatcher;         // 0x124
  xe::be<uint32_t> user_mode_trap_frame_ptr;     // 0x128 PKTRAP_FRAME
  xe::be<uint64_t> create_time;                  // 0x130
  xe::be<uint64_t> exit_time;                    // 0x138
  xe::be<uint32_t> exit_status;                  // 0x140
  X_LIST_ENTRY active_timer_list_head;           // 0x144
  xe::be<uint32_t> thread_id_ptr;                // 0x14C
  xe::be<uint32_t> start_address;                // 0x150
  X_LIST_ENTRY irp_list;                         // 0x154
  xe::be<uint32_t> debug_monitor_data_ptr;       // 0x15C
  xe::be<uint32_t> last_win32_error_code;        // 0x160
  xe::be<uint32_t> win32_current_fiber;          // 0x164
  xe::be<uint32_t> padding2;                     // 0x168
  xe::be<uint32_t> create_options;               // 0x16C
  xe::be<float> vscr[4];                         // 0x170
  xe::be<float> vr[4][128];                      // 0x180
  xe::be<double> fpscr;                          // 0x980
  xe::be<double> fpr[32];                        // 0x988
  XAPC terminate_apc;                            // 0xA88
};

// Processor Region Control Block
// https://github.com/G91/TitanOffLine/blob/master/xkelib/kernel/kePrivateStructs.h#L378
struct X_KPRCB {
  xe::be<uint32_t> current_thread;               // 0x0 PKTHREAD
  xe::be<uint32_t> next_thread;                  // 0x4 PKTHREAD
  xe::be<uint32_t> idle_thread;                  // 0x8 PKTHREAD
  xe::be<uint8_t> number;                        // 0xC
  char dummy_0D[3];                              // 0xD
  xe::be<uint32_t> set_member;                   // 0x10
  xe::be<uint32_t> dpc_time;                     // 0x14
  xe::be<uint32_t> interrupt_time;               // 0x18
  xe::be<uint32_t> interrupt_count;              // 0x1C
  xe::be<uint32_t> lpi_frozen;                   // 0x20
  xe::be<uint32_t> current_packet[3];            // 0x24
  xe::be<uint32_t> target_set;                   // 0x30
  xe::be<uint32_t> worker_routine;               // 0x34
  xe::be<uint32_t> signal_done_ptr;              // 0x38 PKPRCB
  xe::be<uint32_t> request_summary;              // 0x3C
  xe::be<uint32_t> dpc_interrupt_requested;      // 0x40
  xe::be<uint32_t> dpc_lock;                     // 0x44
  X_LIST_ENTRY dpc_list_head;                    // 0x48
  xe::be<uint32_t> dpc_routine_active;           // 0x50
  xe::be<uint32_t> ready_list_lock;              // 0x54
  xe::be<uint32_t> idle_thread_active;           // 0x58
  X_SINGLE_LIST_ENTRY deferred_ready_list_head;  // 0x5C
  xe::be<uint32_t> ready_summary;                // 0x60
  xe::be<uint32_t> ready_summary_mask;           // 0x64
  X_LIST_ENTRY dispatcher_ready_list_head[32];   // 0x68
  char thread_reaper_dpc[0x1C];                  // 0x168
  X_LIST_ENTRY thread_reaper_list_head;          // 0x184
  char switch_processor_thread_dpc[0x1C];        // 0x18C
};
static_assert_size(X_KPRCB, 0x1A8);

// Processor Control Region
// https://github.com/G91/TitanOffLine/blob/master/xkelib/kernel/kePrivateStructs.h#L515
struct X_KPCR {
  xe::be<uint32_t> tls_ptr;  // 0x0
  union {
    struct {
      xe::be<uint16_t> msr_enable_mask_high;  // 0x4
      xe::be<uint16_t> msr_enable_mask_low;   // 0x6
    };
    xe::be<uint32_t> msr_enable_mask;  // 0x4
  } msr;
  union {
    struct {
      xe::be<uint8_t> dispatch_interrupt;  // 0x8
      xe::be<uint8_t> apc_interrupt;       // 0x9
    };
    xe::be<uint16_t> software_interrupt;  // 0x8
  } interrupt;
  union {
    struct {
      xe::be<uint8_t> dpc_fpu_state_saved;  // 0xA
      xe::be<uint8_t> dpc_vpu_state_saved;  // 0xB
    };
    xe::be<uint16_t> dpc_fpu_vpu_state_saved;  // 0xA
  } dpc;
  xe::be<uint8_t> dpc_current_process_type;      // 0xC
  xe::be<uint8_t> quantum_end;                   // 0xD
  xe::be<uint8_t> timer_request;                 // 0xE
  xe::be<uint8_t> hv_cr_0_save;                  // 0xF
  xe::be<uint32_t> fpu_owner_thread_ptr;         // 0x10 PKTHREAD
  xe::be<uint32_t> vpu_owner_thread_ptr;         // 0x14 PKTHREAD
  xe::be<uint8_t> current_irql;                  // 0x18
  xe::be<uint8_t> background_scheduling_active;  // 0x19
  union {
    struct {
      xe::be<uint8_t> start_background_scheduling;  // 0x1A
      xe::be<uint8_t> stop_background_scheduling;   // 0x1B
    };
    xe::be<uint16_t> start_stop_background_scheduling;  // 0x1A
  };
  xe::be<uint32_t> timer_hand;  // 0x1C
  union {
    struct {
      xe::be<uint64_t> lr_iar_save;  // 0x20
      xe::be<uint64_t> cr_msr_save;  // 0x28
      xe::be<uint64_t> gpr_13_save;  // 0x30
    } gp_save;
    struct {
      xe::be<uint32_t> gpr_1_restore;  // 0x20
      xe::be<uint32_t> iar_restore;    // 0x24
      xe::be<uint32_t> cr_restore;     // 0x28
      xe::be<uint32_t> msr_restore;    // 0x2C
    } gp_rest;
  };
  xe::be<uint64_t> hv_gpr_1_save;             // 0x38
  xe::be<uint64_t> hv_gpr_3_save;             // 0x40
  xe::be<uint64_t> hv_gpr_4_save;             // 0x48
  xe::be<uint64_t> hv_gpr_5_save;             // 0x50
  xe::be<uint32_t> user_mode_control;         // 0x58
  xe::be<uint32_t> panic_stack_ptr;           // 0x5C
  xe::be<uint32_t> dar_save;                  // 0x60
  xe::be<uint32_t> dsisr_save;                // 0x64
  xe::be<uint32_t> dbg_last_dpc_routine_ptr;  // 0x68
  xe::be<uint32_t> on_interrupt_stack;        // 0x6C
  xe::be<uint32_t> stack_base;                // 0x70
  xe::be<uint32_t> stack_limit;               // 0x74
  xe::be<uint32_t> interrupt_stack_base;      // 0x78
  xe::be<uint32_t> interrupt_stack_limit;     // 0x7C
  xe::be<uint32_t> interrupt_routine[0x20];   // 0x80
  X_KPRCB prcb_data;                          // 0x100
  xe::be<uint32_t> prcb_ptr;                  // 0x2A8
  xe::be<uint32_t> unused;                    // 0x2AC
  xe::be<uint32_t> pix_current;               // 0x2B0
  xe::be<uint32_t> pix_limit;                 // 0x2B4
  xe::be<uint32_t> profiler_current;          // 0x2B8
  xe::be<uint32_t> profiler_limit;            // 0x2BC
  xe::be<uint32_t> profiler_flags;            // 0x2C0
  xe::be<uint64_t> contention;                // 0x2C8
  xe::be<uint32_t> monitor_profile_data_ptr;  // 0x2D0
};
static_assert_size(X_KPCR, 0x2D8);

class XThread : public XObject {
 public:
  XThread(KernelState* kernel_state, uint32_t stack_size,
          uint32_t xapi_thread_startup, uint32_t start_address,
          uint32_t start_context, uint32_t creation_flags);
  virtual ~XThread();

  static bool IsInThread(XThread* other);
  static XThread* GetCurrentThread();
  static uint32_t GetCurrentThreadHandle();
  static uint32_t GetCurrentThreadId(const uint8_t* pcr);

  uint32_t tls_ptr() const { return tls_address_; }
  uint32_t pcr_ptr() const { return pcr_address_; }
  uint32_t thread_state_ptr() const { return thread_state_address_; }

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
