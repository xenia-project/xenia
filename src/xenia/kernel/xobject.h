/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XOBJECT_H_
#define XENIA_KERNEL_XOBJECT_H_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <string>

#include "xenia/base/threading.h"
#include "xenia/memory.h"
#include "xenia/xbox.h"

namespace xe {
class ByteStream;
class Emulator;
}  // namespace xe

namespace xe {
namespace kernel {

constexpr fourcc_t kXObjSignature = make_fourcc('X', 'E', 'N', '\0');

class KernelState;

template <typename T>
class object_ref;

// https://www.nirsoft.net/kernel_struct/vista/DISPATCHER_HEADER.html
typedef struct {
  struct {
    uint8_t type;

    union {
      uint8_t abandoned;
      uint8_t absolute;
      uint8_t npx_irql;
      uint8_t signalling;
    };
    union {
      uint8_t size;
      uint8_t hand;
    };
    union {
      uint8_t inserted;
      uint8_t debug_active;
      uint8_t dpc_active;
    };
  };

  xe::be<uint32_t> signal_state;
  xe::be<uint32_t> wait_list_flink;
  xe::be<uint32_t> wait_list_blink;
} X_DISPATCH_HEADER;
static_assert_size(X_DISPATCH_HEADER, 0x10);

// https://www.nirsoft.net/kernel_struct/vista/OBJECT_HEADER.html
struct X_OBJECT_HEADER {
  xe::be<uint32_t> pointer_count;
  union {
    xe::be<uint32_t> handle_count;
    xe::be<uint32_t> next_to_free;
  };
  uint8_t name_info_offset;
  uint8_t handle_info_offset;
  uint8_t quota_info_offset;
  uint8_t flags;
  union {
    xe::be<uint32_t> object_create_info;  // X_OBJECT_CREATE_INFORMATION
    xe::be<uint32_t> quota_block_charged;
  };
  xe::be<uint32_t> object_type_ptr;  // -0x8 POBJECT_TYPE
  xe::be<uint32_t> unk_04;           // -0x4

  // Object lives after this header.
  // (There's actually a body field here which is the object itself)
};

// https://www.nirsoft.net/kernel_struct/vista/OBJECT_CREATE_INFORMATION.html
struct X_OBJECT_CREATE_INFORMATION {
  xe::be<uint32_t> attributes;                  // 0x0
  xe::be<uint32_t> root_directory_ptr;          // 0x4
  xe::be<uint32_t> parse_context_ptr;           // 0x8
  xe::be<uint32_t> probe_mode;                  // 0xC
  xe::be<uint32_t> paged_pool_charge;           // 0x10
  xe::be<uint32_t> non_paged_pool_charge;       // 0x14
  xe::be<uint32_t> security_descriptor_charge;  // 0x18
  xe::be<uint32_t> security_descriptor;         // 0x1C
  xe::be<uint32_t> security_qos_ptr;            // 0x20

  // Security QoS here (SECURITY_QUALITY_OF_SERVICE) too!
};

class XObject {
 public:
  // 45410806 needs proper handle value for certain calculations
  // It gets handle value from TLS (without base handle value is 0x88)
  // and substract 0xF8000088. Without base we're receiving wrong address
  // Instead of receiving address that starts with 0x82... we're receiving
  // one with 0x8A... which causes crash
  static constexpr uint32_t kHandleBase = 0xF8000000;
  static constexpr uint32_t kHandleHostBase = 0x01000000;

  enum class Type : uint32_t {
    Undefined,
    Enumerator,
    Event,
    File,
    IOCompletion,
    Module,
    Mutant,
    NotifyListener,
    Semaphore,
    Session,
    Socket,
    SymbolicLink,
    Thread,
    Timer,
    Device
  };

  static bool HasDispatcherHeader(Type type) {
    switch (type) {
      case Type::Event:
      case Type::Mutant:
      case Type::Semaphore:
      case Type::Thread:
      case Type::Timer:
        return true;
      default:
        return false;
    }
    return false;
  }

  static Type MapGuestTypeToHost(uint16_t type) {
    // todo: this is not fully filled in
    switch (type) {
      case 0:
      case 1:
        return Type::Event;
      case 2:
        return Type::Mutant;
      case 5:
        return Type::Semaphore;
      case 6:
        return Type::Thread;
      case 8:
      case 9:
        return Type::Timer;
    }
    return Type::Undefined;
  }
  XObject(Type type);
  XObject(KernelState* kernel_state, Type type, bool host_object = false);
  virtual ~XObject();

  Emulator* emulator() const;
  KernelState* kernel_state() const;
  Memory* memory() const;

  Type type() const;

  // Returns the primary handle of this object.
  X_HANDLE handle() const { return handles_[0]; }

  // Returns all associated handles with this object.
  std::vector<X_HANDLE> handles() const { return handles_; }
  std::vector<X_HANDLE>& handles() { return handles_; }

  const std::string& name() const { return name_; }
  uint32_t guest_object() const { return guest_object_ptr_; }

  // Has this object been created for use by the host?
  // Host objects are persisted through reloads/etc.
  bool is_host_object() const { return host_object_; }
  void set_host_object(bool host_object) { host_object_ = host_object; }

  template <typename T>
  T* guest_object() {
    return memory()->TranslateVirtual<T*>(guest_object_ptr_);
  }

  void RetainHandle();
  bool ReleaseHandle();
  void Retain();
  void Release();
  X_STATUS Delete();

  virtual bool Save(ByteStream* stream) { return false; }
  static object_ref<XObject> Restore(KernelState* kernel_state, Type type,
                                     ByteStream* stream);

  static constexpr bool is_handle_host_object(X_HANDLE handle) {
    return handle > XObject::kHandleHostBase && handle < XObject::kHandleBase;
  };
  // Reference()
  // Dereference()

  void SetAttributes(uint32_t obj_attributes_ptr);

  X_STATUS Wait(uint32_t wait_reason, uint32_t processor_mode,
                uint32_t alertable, uint64_t* opt_timeout);
  static X_STATUS SignalAndWait(XObject* signal_object, XObject* wait_object,
                                uint32_t wait_reason, uint32_t processor_mode,
                                uint32_t alertable, uint64_t* opt_timeout);
  static X_STATUS WaitMultiple(uint32_t count, XObject** objects,
                               uint32_t wait_type, uint32_t wait_reason,
                               uint32_t processor_mode, uint32_t alertable,
                               uint64_t* opt_timeout);

  static object_ref<XObject> GetNativeObject(KernelState* kernel_state,
                                             void* native_ptr,
                                             int32_t as_type = -1,
                                             bool already_locked = false);
  template <typename T>
  static object_ref<T> GetNativeObject(KernelState* kernel_state,
                                       void* native_ptr, int32_t as_type = -1,
                                       bool already_locked = false);

 protected:
  bool SaveObject(ByteStream* stream);
  bool RestoreObject(ByteStream* stream);

  // Called on successful wait.
  virtual void WaitCallback() {}
  virtual xe::threading::WaitHandle* GetWaitHandle() { return nullptr; }

  // Creates the kernel object for guest code to use. Typically not needed.
  uint8_t* CreateNative(uint32_t size);
  void SetNativePointer(uint32_t native_ptr, bool uninitialized = false);

  template <typename T>
  T* CreateNative() {
    return reinterpret_cast<T*>(CreateNative(sizeof(T)));
  }

  // Stash native pointer into X_DISPATCH_HEADER
  static void StashHandle(X_DISPATCH_HEADER* header, uint32_t handle) {
    header->wait_list_flink = kXObjSignature;
    header->wait_list_blink = handle;
  }

  static uint32_t TimeoutTicksToMs(int64_t timeout_ticks);

  KernelState* kernel_state_;

  // Host objects are persisted through resets/etc.
  bool host_object_ = false;

 private:
  std::atomic<int32_t> pointer_ref_count_;

  Type type_;
  std::vector<X_HANDLE> handles_;
  std::string name_;  // May be zero length.

  // Guest pointer for kernel object. Remember: X_OBJECT_HEADER precedes this
  // if we allocated it!
  uint32_t guest_object_ptr_ = 0;
  bool allocated_guest_object_ = false;
};

template <typename T>
class object_ref {
 public:
  object_ref() noexcept : value_(nullptr) {}
  object_ref(std::nullptr_t) noexcept  // NOLINT(runtime/explicit)
      : value_(nullptr) {}
  object_ref& operator=(std::nullptr_t) noexcept {
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
    object_ref(right).swap(*this);  // NOLINT(runtime/explicit): misrecognized?
    return (*this);
  }
  template <typename V>
  object_ref& operator=(const object_ref<V>& right) noexcept {
    object_ref(right).swap(*this);  // NOLINT(runtime/explicit): misrecognized?
    return (*this);
  }

  void swap(object_ref& right) noexcept { std::swap(value_, right.value_); }

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

  void reset() noexcept { object_ref().swap(*this); }

  void reset(T* value) noexcept { object_ref(value).swap(*this); }

  inline bool operator==(const T* right) const noexcept {
    return value_ == right;
  }

 private:
  T* value_ = nullptr;
};

template <class _Ty>
bool operator==(const object_ref<_Ty>& _Left, std::nullptr_t) noexcept {
  return (_Left.get() == reinterpret_cast<_Ty*>(0));
}

template <class _Ty>
bool operator==(std::nullptr_t, const object_ref<_Ty>& _Right) noexcept {
  return (reinterpret_cast<_Ty*>(0) == _Right.get());
}

template <class _Ty>
bool operator!=(const object_ref<_Ty>& _Left, std::nullptr_t _Right) noexcept {
  return (!(_Left == _Right));
}

template <class _Ty>
bool operator!=(std::nullptr_t _Left, const object_ref<_Ty>& _Right) noexcept {
  return (!(_Left == _Right));
}

template <class T, class... Args>
std::enable_if_t<!std::is_array<T>::value, object_ref<T>> make_object(
    Args&&... args) {
  return object_ref<T>(new T(std::forward<Args>(args)...));
}

template <typename T>
object_ref<T> retain_object(T* ptr) {
  if (ptr) ptr->Retain();
  return object_ref<T>(ptr);
}

template <typename T>
object_ref<T> XObject::GetNativeObject(KernelState* kernel_state,
                                       void* native_ptr, int32_t as_type,
                                       bool already_locked) {
  return object_ref<T>(reinterpret_cast<T*>(
      GetNativeObject(kernel_state, native_ptr, as_type, already_locked)
          .release()));
}

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XOBJECT_H_
