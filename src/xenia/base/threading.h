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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
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

// Returns the total number of logical processors in the host system.
uint32_t logical_processor_count();

// Enables the current process to set thread affinity.
// Must be called at startup before attempting to set thread affinity.
void EnableAffinityConfiguration();

// Gets a stable thread-specific ID, but may not be. Use for informative
// purposes only.
uint32_t current_thread_system_id();

// Gets a stable thread-specific ID that defaults to the same value as
// current_thread_system_id but may be overridden.
// Guest threads often change this to the guest thread handle.
uint32_t current_thread_id();
void set_current_thread_id(uint32_t id);

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

enum class SleepResult {
  kSuccess,
  kAlerted,
};
// Sleeps the current thread for at least as long as the given duration.
// The thread is put in an alertable state and may wake to dispatch user
// callbacks. If this happens the sleep returns early with
// SleepResult::kAlerted.
SleepResult AlertableSleep(std::chrono::microseconds duration);
template <typename Rep, typename Period>
SleepResult AlertableSleep(std::chrono::duration<Rep, Period> duration) {
  return AlertableSleep(
      std::chrono::duration_cast<std::chrono::microseconds>(duration));
}

typedef uint32_t TlsHandle;
constexpr TlsHandle kInvalidTlsHandle = UINT_MAX;

TlsHandle AllocateTlsHandle();
bool FreeTlsHandle(TlsHandle handle);
uintptr_t GetTlsValue(TlsHandle handle);
bool SetTlsValue(TlsHandle handle, uintptr_t value);

// A high-resolution timer capable of firing at millisecond-precision.
// All timers created in this way are executed in the same thread so
// callbacks must be kept short or else all timers will be impacted.
class HighResolutionTimer {
 public:
  virtual ~HighResolutionTimer() = default;

  // Creates a new repeating timer with the given period.
  // The given function will be called back as close to the given period as
  // possible.
  static std::unique_ptr<HighResolutionTimer> CreateRepeating(
      std::chrono::milliseconds period, std::function<void()> callback);
};

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

  // Returns the native handle of the object on the host system.
  // This value is platform specific.
  virtual void* native_handle() const = 0;

 protected:
  WaitHandle() = default;
};

// Waits until the wait handle is in the signaled state, an alert triggers and
// a user callback is queued to the thread, or the timeout interval elapses.
// If timeout is zero the call will return immediately instead of waiting and
// if the timeout is max() the wait will not time out.
WaitResult Wait(
    WaitHandle* wait_handle, bool is_alertable,
    std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

// Signals one object and waits on another object as a single operation.
// Waits until the wait handle is in the signaled state, an alert triggers and
// a user callback is queued to the thread, or the timeout interval elapses.
// If timeout is zero the call will return immediately instead of waiting and
// if the timeout is max() the wait will not time out.
WaitResult SignalAndWait(
    WaitHandle* wait_handle_to_signal, WaitHandle* wait_handle_to_wait_on,
    bool is_alertable,
    std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

std::pair<WaitResult, size_t> WaitMultiple(
    WaitHandle* wait_handles[], size_t wait_handle_count, bool wait_all,
    bool is_alertable,
    std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

// Waits until all of the specified objects are in the signaled state, a
// user callback is queued to the thread, or the time-out interval elapses.
// If timeout is zero the call will return immediately instead of waiting and
// if the timeout is max() the wait will not time out.
inline WaitResult WaitAll(
    WaitHandle* wait_handles[], size_t wait_handle_count, bool is_alertable,
    std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
  return WaitMultiple(wait_handles, wait_handle_count, true, is_alertable,
                      timeout)
      .first;
}
inline WaitResult WaitAll(
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
inline std::pair<WaitResult, size_t> WaitAny(
    WaitHandle* wait_handles[], size_t wait_handle_count, bool is_alertable,
    std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
  return WaitMultiple(wait_handles, wait_handle_count, false, is_alertable,
                      timeout);
}
inline std::pair<WaitResult, size_t> WaitAny(
    std::vector<WaitHandle*> wait_handles, bool is_alertable,
    std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
  return WaitAny(wait_handles.data(), wait_handles.size(), is_alertable,
                 timeout);
}

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

// Models a Win32-like timer object.
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms687012(v=vs.85).aspx
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

struct ThreadPriority {
  static const int32_t kLowest = -2;
  static const int32_t kBelowNormal = -1;
  static const int32_t kNormal = 0;
  static const int32_t kAboveNormal = 1;
  static const int32_t kHighest = 2;
};

// Models a Win32-like thread object.
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms682453(v=vs.85).aspx
class Thread : public WaitHandle {
 public:
  struct CreationParameters {
    size_t stack_size = 4 * 1024 * 1024;
    bool create_suspended = false;
    int32_t initial_priority = 0;
  };

  // Creates a thread with the given parameters and calls the start routine from
  // within that thread.
  static std::unique_ptr<Thread> Create(CreationParameters params,
                                        std::function<void()> start_routine);
  static Thread* GetCurrentThread();

  // Ends the calling thread.
  // No destructors are called, and this function does not return.
  // The state of the thread object becomes signaled, releasing any other
  // threads that had been waiting for the thread to terminate.
  static void Exit(int exit_code);

  // Returns the ID of the thread.
  virtual uint32_t system_id() const = 0;

  // Returns the current name of the thread, if previously specified.
  std::string name() const { return name_; }

  // Sets the name of the thread, used in debugging and logging.
  virtual void set_name(std::string name) { name_ = std::move(name); }

  // Returns the current priority value for the thread.
  virtual int32_t priority() = 0;

  // Sets the priority value for the thread. This value, together with the
  // priority class of the thread's process, determines the thread's base
  // priority level. ThreadPriority contains useful constants.
  virtual void set_priority(int32_t new_priority) = 0;

  // Returns the current processor affinity mask for the thread.
  virtual uint64_t affinity_mask() = 0;

  // Sets a processor affinity mask for the thread.
  // A thread affinity mask is a bit vector in which each bit represents a
  // logical processor that a thread is allowed to run on. A thread affinity
  // mask must be a subset of the process affinity mask for the containing
  // process of a thread.
  virtual void set_affinity_mask(uint64_t new_affinity_mask) = 0;

  // Adds a user-mode asynchronous procedure call request to the thread queue.
  // When a user-mode APC is queued, the thread is not directed to call the APC
  // function unless it is in an alertable state. After the thread is in an
  // alertable state, the thread handles all pending APCs in first in, first out
  // (FIFO) order, and the wait operation returns WaitResult::kUserCallback.
  virtual void QueueUserCallback(std::function<void()> callback) = 0;

  // Decrements a thread's suspend count. When the suspend count is decremented
  // to zero, the execution of the thread is resumed.
  virtual bool Resume(uint32_t* out_new_suspend_count = nullptr) = 0;

  // Suspends the specified thread.
  virtual bool Suspend(uint32_t* out_previous_suspend_count = nullptr) = 0;

  // Terminates the thread.
  // No destructors are called, and this function does not return.
  // The state of the thread object becomes signaled, releasing any other
  // threads that had been waiting for the thread to terminate.
  virtual void Terminate(int exit_code) = 0;

 protected:
  std::string name_;
};

}  // namespace threading
}  // namespace xe

#endif  // XENIA_BASE_THREADING_H_
