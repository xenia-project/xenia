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
#include <mutex>
#include "platform.h"
#include "memory.h"
#define XE_ENABLE_FAST_WIN32_MUTEX 1
namespace xe {

#if XE_PLATFORM_WIN32 == 1 && XE_ENABLE_FAST_WIN32_MUTEX == 1
/*
   must conform to
   BasicLockable:https://en.cppreference.com/w/cpp/named_req/BasicLockable as
   well as Lockable: https://en.cppreference.com/w/cpp/named_req/Lockable

   this emulates a recursive mutex, except with far less overhead
*/

class alignas(4096) xe_global_mutex {
	XE_MAYBE_UNUSED
  char detail[64];

 public:
  xe_global_mutex();
  ~xe_global_mutex();

  void lock();
  void unlock();
  bool try_lock();
};
using global_mutex_type = xe_global_mutex;

class alignas(64) xe_fast_mutex {
	XE_MAYBE_UNUSED
  char detail[64];

 public:
  xe_fast_mutex();
  ~xe_fast_mutex();

  void lock();
  void unlock();
  bool try_lock();
};
// a mutex that is extremely unlikely to ever be locked
// use for race conditions that have extremely remote odds of happening
class xe_unlikely_mutex {
  std::atomic<uint32_t> mut;
  bool _tryget() {
    uint32_t lock_expected = 0;
    return mut.compare_exchange_strong(lock_expected, 1);
  }

 public:
  xe_unlikely_mutex() : mut(0) {}
  ~xe_unlikely_mutex() { mut = 0; }

  void lock() {
    if (XE_LIKELY(_tryget())) {
      return;
    } else {
      do {
        // chrispy: warning, if no SMT, mm_pause does nothing...
#if XE_ARCH_AMD64 == 1
        _mm_pause();
#endif

      } while (!_tryget());
    }
  }
  void unlock() { mut.exchange(0); }
  bool try_lock() { return _tryget(); }
};
using xe_mutex = xe_fast_mutex;
#else
using global_mutex_type = std::recursive_mutex;
using xe_mutex = std::mutex;
using xe_unlikely_mutex = std::mutex;
#endif
struct null_mutex {
 public:
  static void lock() {}
  static void unlock() {}
  static bool try_lock() { return true; }
};

using global_unique_lock_type = std::unique_lock<global_mutex_type>;
// The global critical region mutex singleton.
// This must guard any operation that may suspend threads or be sensitive to
// being suspended such as global table locks and such.
// To prevent deadlocks this should be the first lock acquired and be held
// for the entire duration of the critical region (longer than any other lock).
//
// As a general rule if some code can only be accessed from the guest you can
// guard it with only the global critical region and be assured nothing else
// will touch it. If it will be accessed from non-guest threads you may need
// some additional protection.
//
// You can think of this as disabling interrupts in the guest. The thread in the
// global critical region has exclusive access to the entire system and cannot
// be preempted. This also means that all activity done while in the critical
// region must be extremely fast (no IO!), as it has the chance to block any
// other thread until its done.
//
// For example, in the following situation thread 1 will not be able to suspend
// thread 0 until it has exited its critical region, preventing it from being
// suspended while holding the table lock:
//   [thread 0]:
//     DoKernelStuff():
//       auto global_lock = global_critical_region_.Acquire();
//       std::lock_guard<std::mutex> table_lock(table_mutex_);
//       table_->InsertStuff();
//   [thread 1]:
//     MySuspendThread():
//       auto global_lock = global_critical_region_.Acquire();
//       ::SuspendThread(thread0);
//
// To use the region it's strongly recommended that you keep an instance near
// the data requiring it. This makes it clear to those reading that the data
// is protected by the global critical region. For example:
// class MyType {
//   // Implies my_list_ is protected:
//   xe::global_critical_region global_critical_region_;
//   std::list<...> my_list_;
// };
class global_critical_region {
 public:
  constexpr global_critical_region() {}
  static global_mutex_type& mutex();

  // Acquires a lock on the global critical section.
  // Use this when keeping an instance is not possible. Otherwise, prefer
  // to keep an instance of global_critical_region near the members requiring
  // it to keep things readable.
  static global_unique_lock_type AcquireDirect() {
    return global_unique_lock_type(mutex());
  }

  // Acquires a lock on the global critical section.
  static inline global_unique_lock_type Acquire() {
    return global_unique_lock_type(mutex());
  }

  static inline void PrepareToAcquire() {
#if XE_PLATFORM_WIN32 == 1
    swcache::PrefetchW(&mutex());
#endif
  }

  // Acquires a deferred lock on the global critical section.
  static inline global_unique_lock_type AcquireDeferred() {
    return global_unique_lock_type(mutex(), std::defer_lock);
  }

  // Tries to acquire a lock on the glboal critical section.
  // Check owns_lock() to see if the lock was successfully acquired.
  static inline global_unique_lock_type TryAcquire() {
    return global_unique_lock_type(mutex(), std::try_to_lock);
  }
};

}  // namespace xe

#endif  // XENIA_BASE_MUTEX_H_
