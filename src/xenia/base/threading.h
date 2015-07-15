/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_THREADING_H_
#define XENIA_BASE_THREADING_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace xe {
namespace threading {

class Fence {
 public:
  Fence() : signaled_(false) {}
  void Signal() {
    std::unique_lock<std::mutex> lock(mutex_);
    signaled_.store(true);
    cond_.notify_all();
  }
  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!signaled_.load()) {
      cond_.wait(lock);
    }
    signaled_.store(false);
  }

 private:
  std::mutex mutex_;
  std::condition_variable cond_;
  std::atomic<bool> signaled_;
};

// TODO(benvanik): processor info API.

// Gets a stable thread-specific ID, but may not be. Use for informative
// purposes only.
uint32_t current_thread_id();

// Sets the current thread name.
void set_name(const std::string& name);
// Sets the target thread name.
void set_name(std::thread::native_handle_type handle, const std::string& name);

// Yields the current thread to the scheduler. Maybe.
void MaybeYield();

// Memory barrier (request - may be ignored).
void SyncMemory();

// Sleeps the current thread for at least as long as the given duration.
void Sleep(std::chrono::microseconds duration);
template <typename Rep, typename Period>
void Sleep(std::chrono::duration<Rep, Period> duration) {
  Sleep(std::chrono::duration_cast<std::chrono::microseconds>(duration));
}

// Results for a WaitHandle operation.
enum class WaitResult {
  // The state of the specified object is signaled.
  // In a WaitAny the tuple will contain the index of the wait handle that
  // caused the wait to be satisfied.
  kSuccess,
  // The wait was ended by one or more user-mode callbacks queued to the thread.
  // This will occur when is_alertable is set true.
  kUserCallback,
  // The time-out interval elapsed, and the object's state is nonsignaled.
  kTimeout,
  // The specified object is a mutex object that was not released by the thread
  // that owned the mutex object before the owning thread terminated. Ownership
  // of the mutex object is granted to the calling thread and the mutex is set
  // to nonsignaled.
  // In a WaitAny the tuple will contain the index of the wait handle that
  // caused the wait to be abandoned.
  kAbandoned,
  // The function has failed.
  kFailed,
};

class WaitHandle {
 public:
  virtual ~WaitHandle() = default;

  // Waits until the wait handle is in the signaled state, an alert triggers and
  // a user callback is queued to the thread, or the timeout interval elapses.
  // If timeout is zero the call will return immediately instead of waiting and
  // if the timeout is max() the wait will not time out.
  inline WaitResult Wait(
      bool is_alertable,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
    return WaitHandle::Wait(this, is_alertable, timeout);
  }

  // Waits until the wait handle is in the signaled state, an alert triggers and
  // a user callback is queued to the thread, or the timeout interval elapses.
  // If timeout is zero the call will return immediately instead of waiting and
  // if the timeout is max() the wait will not time out.
  static WaitResult Wait(
      WaitHandle* wait_handle, bool is_alertable,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

  // Signals one object and waits on another object as a single operation.
  // Waits until the wait handle is in the signaled state, an alert triggers and
  // a user callback is queued to the thread, or the timeout interval elapses.
  // If timeout is zero the call will return immediately instead of waiting and
  // if the timeout is max() the wait will not time out.
  static WaitResult SignalAndWait(
      WaitHandle* wait_handle_to_signal, WaitHandle* wait_handle_to_wait_on,
      bool is_alertable,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

  // Waits until all of the specified objects are in the signaled state, a
  // user callback is queued to the thread, or the time-out interval elapses.
  // If timeout is zero the call will return immediately instead of waiting and
  // if the timeout is max() the wait will not time out.
  inline static WaitResult WaitAll(
      WaitHandle* wait_handles[], size_t wait_handle_count, bool is_alertable,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
    return WaitMultiple(wait_handles, wait_handle_count, true, is_alertable,
                        timeout).first;
  }
  inline static WaitResult WaitAll(
      std::vector<WaitHandle*> wait_handles, bool is_alertable,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
    return WaitAll(wait_handles.data(), wait_handles.size(), is_alertable,
                   timeout);
  }

  // Waits until any of the specified objects are in the signaled state, a
  // user callback is queued to the thread, or the time-out interval elapses.
  // If timeout is zero the call will return immediately instead of waiting and
  // if the timeout is max() the wait will not time out.
  // The second argument of the return tuple indicates which wait handle caused
  // the wait to be satisfied or abandoned.
  inline static std::pair<WaitResult, size_t> WaitAny(
      WaitHandle* wait_handles[], size_t wait_handle_count, bool is_alertable,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
    return WaitMultiple(wait_handles, wait_handle_count, false, is_alertable,
                        timeout);
  }
  inline static std::pair<WaitResult, size_t> WaitAny(
      std::vector<WaitHandle*> wait_handles, bool is_alertable,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
    return WaitAny(wait_handles.data(), wait_handles.size(), is_alertable,
                   timeout);
  }

 protected:
  WaitHandle() = default;

  // Returns the native handle of the object on the host system.
  // This value is platform specific.
  virtual void* native_handle() const = 0;

  static std::pair<WaitResult, size_t> WaitMultiple(
      WaitHandle* wait_handles[], size_t wait_handle_count, bool wait_all,
      bool is_alertable,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
};

// Models a Win32-like event object.
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms682396(v=vs.85).aspx
class Event : public WaitHandle {
 public:
  // Creates a manual-reset event object, which requires the use of the
  // Reset() function to set the event state to nonsignaled.
  // If initial_state is true the event will start in the signaled state.
  static std::unique_ptr<Event> CreateManualResetEvent(bool initial_state);

  // Creates an auto-reset event object, and system automatically resets the
  // event state to nonsignaled after a single waiting thread has been released.
  // If initial_state is true the event will start in the signaled state.
  static std::unique_ptr<Event> CreateAutoResetEvent(bool initial_state);

  // Sets the specified event object to the signaled state.
  // If this is a manual reset event the event stays signaled until Reset() is
  // called. If this is an auto reset event until exactly one wait is satisfied.
  virtual void Set() = 0;

  // Sets the specified event object to the nonsignaled state.
  // Resetting an event that is already reset has no effect.
  virtual void Reset() = 0;

  // Sets the specified event object to the signaled state and then resets it to
  // the nonsignaled state after releasing the appropriate number of waiting
  // threads.
  virtual void Pulse() = 0;
};

// Models a Win32-like semaphore object.
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms682438(v=vs.85).aspx
class Semaphore : public WaitHandle {
 public:
  // Creates a new semaphore object.
  // The initial_count must be greater than or equal to zero and less than or
  // equal to maximum_count. The state of a semaphore is signaled when its count
  // is greater than zero and nonsignaled when it is zero. The count is
  // decreased by one whenever a wait function releases a thread that was
  // waiting for the semaphore. The count is increased  by a specified amount by
  // calling the Release() function.
  static std::unique_ptr<Semaphore> Create(int initial_count,
                                           int maximum_count);

  // Increases the count of the specified semaphore object by a specified
  // amount.
  // release_count must be greater than zero.
  // Returns false if adding release_count would set the semaphore over the
  // initially specified maximum_count.
  virtual bool Release(int release_count, int* out_previous_count) = 0;
};

// Models a Win32-like mutant (mutex) object.
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms682411(v=vs.85).aspx
class Mutant : public WaitHandle {
 public:
  // Creates a new mutant object, initially owned by the calling thread if
  // specified.
  static std::unique_ptr<Mutant> Create(bool initial_owner);

  // Releases ownership of the specified mutex object.
  // Returns false if the calling thread does not own the mutant object.
  virtual bool Release() = 0;
};

class Timer : public WaitHandle {
 public:
  // Creates a timer whose state remains signaled until SetOnce() or
  // SetRepeating() is called to establish a new due time.
  static std::unique_ptr<Timer> CreateManualResetTimer();

  // Creates a timer whose state remains signaled until a thread completes a
  // wait operation on the timer object.
  static std::unique_ptr<Timer> CreateSynchronizationTimer();

  // Activates the specified waitable timer. When the due time arrives, the
  // timer is signaled and the thread that set the timer calls the optional
  // completion routine.
  // Returns true on success.
  virtual bool SetOnce(std::chrono::nanoseconds due_time,
                       std::function<void()> opt_callback = nullptr) = 0;

  // Activates the specified waitable timer. When the due time arrives, the
  // timer is signaled and the thread that set the timer calls the optional
  // completion routine. A periodic timer automatically reactivates each time
  // the period elapses, until the timer is canceled or reset.
  // Returns true on success.
  virtual bool SetRepeating(std::chrono::nanoseconds due_time,
                            std::chrono::milliseconds period,
                            std::function<void()> opt_callback = nullptr) = 0;
  template <typename Rep, typename Period>
  void SetRepeating(std::chrono::nanoseconds due_time,
                    std::chrono::duration<Rep, Period> period,
                    std::function<void()> opt_callback = nullptr) {
    SetRepeating(due_time,
                 std::chrono::duration_cast<std::chrono::milliseconds>(period),
                 std::move(opt_callback));
  }

  // Stops the timer before it can be set to the signaled state and cancels
  // outstanding callbacks. Threads performing a wait operation on the timer
  // remain waiting until they time out or the timer is reactivated and its
  // state is set to signaled. If the timer is already in the signaled state, it
  // remains in that state.
  // Returns true on success.
  virtual bool Cancel() = 0;
};

}  // namespace threading
}  // namespace xe

#endif  // XENIA_BASE_THREADING_H_
