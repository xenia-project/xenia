/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XOBJECT_H_
#define XENIA_KERNEL_XBOXKRNL_XOBJECT_H_

#include <atomic>

#include <xenia/kernel/kernel_state.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {


// http://www.nirsoft.net/kernel_struct/vista/DISPATCHER_HEADER.html
typedef struct {
  uint32_t type_flags;
  uint32_t signal_state;
  uint32_t wait_list_flink;
  uint32_t wait_list_blink;
} DISPATCH_HEADER;


class XObject {
public:
  enum Type {
    kTypeModule,
    kTypeThread,
    kTypeEvent,
    kTypeFile,
    kTypeSemaphore,
    kTypeNotifyListener,
    kTypeMutant,
    kTypeTimer,
    kTypeEnumerator,
  };

  XObject(KernelState* kernel_state, Type type);
  virtual ~XObject();

  Emulator* emulator() const { return kernel_state_->emulator_; }
  KernelState* kernel_state() const { return kernel_state_; }

  Type type();
  X_HANDLE handle() const;

  void RetainHandle();
  bool ReleaseHandle();
  void Retain();
  void Release();
  X_STATUS Delete();

  // Reference()
  // Dereference()

  X_STATUS Wait(
      uint32_t wait_reason, uint32_t processor_mode, uint32_t alertable,
      uint64_t* opt_timeout);
  static X_STATUS SignalAndWait(
      XObject* signal_object, XObject* wait_object,
      uint32_t wait_reason, uint32_t processor_mode, uint32_t alertable,
      uint64_t* opt_timeout);
  static X_STATUS WaitMultiple(
      uint32_t count, XObject** objects,
      uint32_t wait_type, uint32_t wait_reason, uint32_t processor_mode,
      uint32_t alertable, uint64_t* opt_timeout);

  static void LockType();
  static void UnlockType();
  static XObject* GetObject(KernelState* kernel_state, void* native_ptr,
                            int32_t as_type = -1);

  virtual void* GetWaitHandle() { return 0; }

protected:
  Memory* memory() const;
  void SetNativePointer(uint32_t native_ptr);

  static uint32_t TimeoutTicksToMs(int64_t timeout_ticks);

  KernelState*  kernel_state_;

private:
  std::atomic<int32_t> handle_ref_count_;
  std::atomic<int32_t> pointer_ref_count_;

  Type          type_;
  X_HANDLE      handle_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_XOBJECT_H_
