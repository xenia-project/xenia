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

#include "xenia/xbox.h"

namespace xe {
class Emulator;
class Memory;
}  // namespace xe

namespace xe {
namespace kernel {

class KernelState;

template <typename T>
class object_ref;

// http://www.nirsoft.net/kernel_struct/vista/DISPATCHER_HEADER.html
typedef struct {
  xe::be<uint32_t> type_flags;
  xe::be<uint32_t> signal_state;
  xe::be<uint32_t> wait_list_flink;
  xe::be<uint32_t> wait_list_blink;
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

  Emulator* emulator() const;
  KernelState* kernel_state() const;
  Memory* memory() const;

  Type type();
  X_HANDLE handle() const;
  const std::string& name() const { return name_; }

  void RetainHandle();
  bool ReleaseHandle();
  void Retain();
  void Release();
  X_STATUS Delete();

  // Reference()
  // Dereference()

  void SetAttributes(const uint8_t* obj_attrs_ptr);

  X_STATUS Wait(uint32_t wait_reason, uint32_t processor_mode,
                uint32_t alertable, uint64_t* opt_timeout);
  static X_STATUS SignalAndWait(XObject* signal_object, XObject* wait_object,
                                uint32_t wait_reason, uint32_t processor_mode,
                                uint32_t alertable, uint64_t* opt_timeout);
  static X_STATUS WaitMultiple(uint32_t count, XObject** objects,
                               uint32_t wait_type, uint32_t wait_reason,
                               uint32_t processor_mode, uint32_t alertable,
                               uint64_t* opt_timeout);

  static XObject* GetObject(KernelState* kernel_state, void* native_ptr,
                            int32_t as_type = -1);

  virtual void* GetWaitHandle() { return 0; }

 protected:
  void SetNativePointer(uint32_t native_ptr, bool uninitialized = false);

  static uint32_t TimeoutTicksToMs(int64_t timeout_ticks);

  KernelState* kernel_state_;

 private:
  std::atomic<int32_t> handle_ref_count_;
  std::atomic<int32_t> pointer_ref_count_;

  Type type_;
  X_HANDLE handle_;
  std::string name_;  // May be zero length.
};

template <typename T>
class object_ref {
 public:
  object_ref() noexcept : value_(nullptr) {}
  object_ref(nullptr_t) noexcept : value_(nullptr) {}
  object_ref& operator=(nullptr_t) noexcept {
    reset();
    return (*this);
  }

  explicit object_ref(T* value) noexcept : value_(value) {
    // Assumes retained on call.
  }
  explicit object_ref(const object_ref& right) noexcept {
    reset(right.get());
    if (value_) value_->Retain();
  }
  template <class V, class = typename std::enable_if<
                         std::is_convertible<V*, T*>::value, void>::type>
  object_ref(const object_ref<V>& right) noexcept {
    reset(right.get());
    if (value_) value_->Retain();
  }

  object_ref(object_ref&& right) noexcept : value_(right.release()) {}
  object_ref& operator=(object_ref&& right) noexcept {
    object_ref(std::move(right)).swap(*this);
    return (*this);
  }
  template <typename V>
  object_ref& operator=(object_ref<V>&& right) noexcept {
    object_ref(std::move(right)).swap(*this);
    return (*this);
  }

  object_ref& operator=(const object_ref& right) noexcept {
    object_ref(right).swap(*this);
    return (*this);
  }
  template <typename V>
  object_ref& operator=(const object_ref<V>& right) noexcept {
    object_ref(right).swap(*this);
    return (*this);
  }

  void swap(object_ref& right) noexcept {
    std::_Swap_adl(value_, right.value_);
  }

  ~object_ref() noexcept {
    if (value_) {
      value_->Release();
      value_ = nullptr;
    }
  }

  typename std::add_lvalue_reference<T>::type operator*() const {
    return (*get());
  }

  T* operator->() const noexcept {
    return std::pointer_traits<T*>::pointer_to(**this);
  }

  T* get() const noexcept { return value_; }

  template <typename V>
  V* get() const noexcept {
    return reinterpret_cast<V*>(value_);
  }

  explicit operator bool() const noexcept { return value_ != nullptr; }

  T* release() noexcept {
    T* value = value_;
    value_ = nullptr;
    return value;
  }

  static void accept(T* value) {
    reset(value);
    value->Release();
  }

  void reset() noexcept { object_ref().swap(*this); }

  void reset(T* value) noexcept { object_ref(value).swap(*this); }

 private:
  T* value_ = nullptr;
};

template <class _Ty>
bool operator==(const object_ref<_Ty>& _Left, nullptr_t) noexcept {
  return (_Left.get() == (_Ty*)0);
}

template <class _Ty>
bool operator==(nullptr_t, const object_ref<_Ty>& _Right) noexcept {
  return ((_Ty*)0 == _Right.get());
}

template <class _Ty>
bool operator!=(const object_ref<_Ty>& _Left, nullptr_t _Right) noexcept {
  return (!(_Left == _Right));
}

template <class _Ty>
bool operator!=(nullptr_t _Left, const object_ref<_Ty>& _Right) noexcept {
  return (!(_Left == _Right));
}

template <typename T>
object_ref<T> retain_object(T* ptr) {
  if (ptr) ptr->Retain();
  return object_ref<T>(ptr);
}

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XOBJECT_H_
