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

namespace xe {

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
  static std::recursive_mutex& mutex();

  // Acquires a lock on the global critical section.
  // Use this when keeping an instance is not possible. Otherwise, prefer
  // to keep an instance of global_critical_region near the members requiring
  // it to keep things readable.
  static std::unique_lock<std::recursive_mutex> AcquireDirect() {
    return std::unique_lock<std::recursive_mutex>(mutex());
  }

  // Acquires a lock on the global critical section.
  inline std::unique_lock<std::recursive_mutex> Acquire() {
    return std::unique_lock<std::recursive_mutex>(mutex());
  }

  // Tries to acquire a lock on the glboal critical section.
  // Check owns_lock() to see if the lock was successfully acquired.
  inline std::unique_lock<std::recursive_mutex> TryAcquire() {
    return std::unique_lock<std::recursive_mutex>(mutex(), std::try_to_lock);
  }
};

}  // namespace xe

#endif  // XENIA_BASE_MUTEX_H_
