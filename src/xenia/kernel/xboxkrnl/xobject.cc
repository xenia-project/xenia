/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/xobject.h>

#include <xenia/kernel/xboxkrnl/xboxkrnl_private.h>
#include <xenia/kernel/xboxkrnl/objects/xevent.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


XObject::XObject(KernelState* kernel_state, Type type) :
    kernel_state_(kernel_state),
    handle_ref_count_(0),
    pointer_ref_count_(1),
    type_(type), handle_(X_INVALID_HANDLE_VALUE) {
  kernel_state->object_table()->AddHandle(this, &handle_);
}

XObject::~XObject() {
  XEASSERTZERO(handle_ref_count_);
  XEASSERTZERO(pointer_ref_count_);
}

Memory* XObject::memory() const {
  return kernel_state_->memory();
}

XObject::Type XObject::type() {
  return type_;
}

X_HANDLE XObject::handle() const {
  return handle_;
}

void XObject::RetainHandle() {
  xe_atomic_inc_32(&handle_ref_count_);
}

bool XObject::ReleaseHandle() {
  if (!xe_atomic_dec_32(&handle_ref_count_)) {
    return true;
  }
  return false;
}

void XObject::Retain() {
  xe_atomic_inc_32(&pointer_ref_count_);
}

void XObject::Release() {
  if (!xe_atomic_dec_32(&pointer_ref_count_)) {
    XEASSERT(pointer_ref_count_ >= handle_ref_count_);
    delete this;
  }
}

X_STATUS XObject::Delete() {
  return shared_kernel_state_->object_table()->RemoveHandle(handle_);
}

X_STATUS XObject::Wait(uint32_t wait_reason, uint32_t processor_mode,
                       uint32_t alertable, uint64_t* opt_timeout) {
  return X_STATUS_SUCCESS;
}

void XObject::LockType() {
  xe_mutex_lock(shared_kernel_state_->object_mutex_);
}

void XObject::UnlockType() {
  xe_mutex_unlock(shared_kernel_state_->object_mutex_);
}

void XObject::SetNativePointer(uint32_t native_ptr) {
  XObject::LockType();

  DISPATCH_HEADER* header_be =
      (DISPATCH_HEADER*)kernel_state_->memory()->Translate(native_ptr);
  DISPATCH_HEADER header;
  header.type_flags = XESWAP32(header_be->type_flags);
  header.signal_state = XESWAP32(header_be->signal_state);
  header.wait_list_flink = XESWAP32(header_be->wait_list_flink);
  header.wait_list_blink = XESWAP32(header_be->wait_list_blink);

  XEASSERT(!(header.wait_list_blink & 0x1));

  // Stash pointer in struct.
  uint64_t object_ptr = reinterpret_cast<uint64_t>(this);
  object_ptr |= 0x1;
  header_be->wait_list_flink = XESWAP32((uint32_t)(object_ptr >> 32));
  header_be->wait_list_blink = XESWAP32((uint32_t)(object_ptr & 0xFFFFFFFF));

  XObject::UnlockType();
}

XObject* XObject::GetObject(KernelState* kernel_state, void* native_ptr) {
  // Unfortunately the XDK seems to inline some KeInitialize calls, meaning
  // we never see it and just randomly start getting passed events/timers/etc.
  // Luckily it seems like all other calls (Set/Reset/Wait/etc) are used and
  // we don't have to worry about PPC code poking the struct. Because of that,
  // we init on first use, store our pointer in the struct, and dereference it
  // each time.
  // We identify this by checking the low bit of wait_list_blink - if it's 1,
  // we have already put our pointer in there.

  XObject::LockType();

  DISPATCH_HEADER* header_be = (DISPATCH_HEADER*)native_ptr;
  DISPATCH_HEADER header;
  header.type_flags = XESWAP32(header_be->type_flags);
  header.signal_state = XESWAP32(header_be->signal_state);
  header.wait_list_flink = XESWAP32(header_be->wait_list_flink);
  header.wait_list_blink = XESWAP32(header_be->wait_list_blink);

  if (header.wait_list_blink & 0x1) {
    // Already initialized.
    uint64_t object_ptr =
        ((uint64_t)header.wait_list_flink << 32) |
        ((header.wait_list_blink) & ~0x1);
    XObject* object = reinterpret_cast<XObject*>(object_ptr);
    // TODO(benvanik): assert nothing has been changed in the struct.
    XObject::UnlockType();
    return object;
  } else {
    // First use, create new.
    // http://www.nirsoft.net/kernel_struct/vista/KOBJECTS.html
    XObject* object = NULL;
    switch (header.type_flags & 0xFF) {
    case 0: // EventNotificationObject
    case 1: // EventSynchronizationObject
      {
        XEvent* ev = new XEvent(kernel_state);
        ev->InitializeNative(native_ptr, header);
        object = ev;
      }
      break;
    case 2: // MutantObject
    case 3: // ProcessObject
    case 4: // QueueObject
    case 5: // SemaphoreObject
    case 6: // ThreadObject
    case 7: // GateObject
    case 8: // TimerNotificationObject
    case 9: // TimerSynchronizationObject
    case 18: // ApcObject
    case 19: // DpcObject
    case 20: // DeviceQueueObject
    case 21: // EventPairObject
    case 22: // InterruptObject
    case 23: // ProfileObject
    case 24: // ThreadedDpcObject
    default:
      XEASSERTALWAYS();
      XObject::UnlockType();
      return NULL;
    }

    // Stash pointer in struct.
    uint64_t object_ptr = reinterpret_cast<uint64_t>(object);
    object_ptr |= 0x1;
    header_be->wait_list_flink = XESWAP32((uint32_t)(object_ptr >> 32));
    header_be->wait_list_blink = XESWAP32((uint32_t)(object_ptr & 0xFFFFFFFF));

    XObject::UnlockType();
    return object;
  }
}
