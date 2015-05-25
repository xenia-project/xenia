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

class XThread : public XObject {
 public:
  XThread(KernelState* kernel_state, uint32_t stack_size,
          uint32_t xapi_thread_startup, uint32_t start_address,
          uint32_t start_context, uint32_t creation_flags);
  virtual ~XThread();

  static XThread* GetCurrentThread();
  static uint32_t GetCurrentThreadHandle();
  static uint32_t GetCurrentThreadId(const uint8_t* pcr);

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

  void LockApc();
  void UnlockApc();
  NativeList* apc_list() const { return apc_list_; }

  int32_t QueryPriority();
  void SetPriority(int32_t increment);
  void SetAffinity(uint32_t affinity);

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

  std::atomic<uint32_t> irql_;
  std::mutex apc_lock_;
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
