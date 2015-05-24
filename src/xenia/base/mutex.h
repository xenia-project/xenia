/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MUTEX_H_
#define XENIA_BASE_MUTEX_H_

#include <atomic>
#include <mutex>
#include <type_traits>

#include "xenia/base/platform.h"

namespace xe {

// This type should be interchangable with std::mutex, only it provides the
// ability to resume threads that hold the lock when the debugger needs it.
class mutex {
 public:
  mutex(int _Flags = 0) noexcept {
    _Mtx_init_in_situ(_Mymtx(), _Flags | _Mtx_try);
  }

  ~mutex() noexcept { _Mtx_destroy_in_situ(_Mymtx()); }

  mutex(const mutex&) = delete;
  mutex& operator=(const mutex&) = delete;

  void lock() {
    _Mtx_lock(_Mymtx());
    holding_thread_ = ::GetCurrentThread();
  }

  bool try_lock() {
    if (_Mtx_trylock(_Mymtx()) == _Thrd_success) {
      holding_thread_ = ::GetCurrentThread();
      return true;
    }
    return false;
  }

  void lock_as_debugger() {
    if (_Mtx_trylock(_Mymtx()) == _Thrd_success) {
      // Nothing holding it.
      return;
    }
    // Resume the thread holding it, which will unlock and suspend itself.
    debugger_waiting_ = true;
    ResumeThread(holding_thread_);
    lock();
    debugger_waiting_ = false;
  }

  void unlock() {
    bool debugger_waiting = debugger_waiting_;
    _Mtx_unlock(_Mymtx());
    if (debugger_waiting) {
      SuspendThread(nullptr);
    }
  }

  typedef void* native_handle_type;

  native_handle_type native_handle() { return (_Mtx_getconcrtcs(_Mymtx())); }

 private:
  std::aligned_storage<_Mtx_internal_imp_size,
                       _Mtx_internal_imp_alignment>::type _Mtx_storage;
  HANDLE holding_thread_;
  bool debugger_waiting_;

  _Mtx_t _Mymtx() noexcept { return (reinterpret_cast<_Mtx_t>(&_Mtx_storage)); }
};

class recursive_mutex : public mutex {
 public:
  recursive_mutex() : mutex(_Mtx_recursive) {}

  bool try_lock() noexcept { return mutex::try_lock(); }

  recursive_mutex(const recursive_mutex&) = delete;
  recursive_mutex& operator=(const recursive_mutex&) = delete;
};

}  // namespace xe

#endif  // XENIA_BASE_MUTEX_H_
