/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/threading.h"

#include "xenia/base/assert.h"
#include "xenia/base/chrono_steady_cast.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading_timer_queue.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <array>
#include <cstddef>
#include <ctime>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <limits>
#include <algorithm>
#include <functional>

namespace xe {
namespace threading {

template <typename _Rep, typename _Period>
inline timespec DurationToTimeSpec(
    std::chrono::duration<_Rep, _Period> duration) {
  auto nanoseconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
  auto div = ldiv(nanoseconds.count(), 1000000000L);
  return timespec{div.quot, div.rem};
}

void EnableAffinityConfiguration() {}

uint32_t current_thread_system_id() {
  return static_cast<uint32_t>(pthread_mach_thread_np(pthread_self()));
}

void MaybeYield() {
  sched_yield();
  __sync_synchronize();
}

void SyncMemory() { __sync_synchronize(); }

void Sleep(std::chrono::microseconds duration) {
  timespec rqtp = DurationToTimeSpec(duration);
  nanosleep(&rqtp, nullptr);
}

// Thread-local storage to indicate if the thread is in an alertable state
thread_local bool alertable_state_ = false;

// Forward declarations
class PosixWaitHandle;
class PosixConditionBase;
class PosixThread;

// Define PosixConditionBase
class PosixConditionBase {
 public:
  virtual ~PosixConditionBase() = default;
  virtual bool Signal() = 0;

  virtual WaitResult Wait(std::chrono::milliseconds timeout) {
    bool executed;
    auto predicate = [this] { return this->signaled(); };
    std::unique_lock<std::mutex> lock(mutex_);
    if (predicate()) {
      executed = true;
    } else {
      if (timeout == std::chrono::milliseconds::max()) {
        cond_.wait(lock, predicate);
        executed = true;  // Did not time out
      } else {
        executed = cond_.wait_for(lock, timeout, predicate);
      }
    }
    if (executed) {
      post_execution();
      return WaitResult::kSuccess;
    } else {
      return WaitResult::kTimeout;
    }
  }

  static std::pair<WaitResult, size_t> WaitMultiple(
      std::vector<PosixConditionBase*>&& handles, bool wait_all,
      std::chrono::milliseconds timeout) {
    assert_true(handles.size() > 0);

    // Construct a condition for all or any depending on wait_all
    std::function<bool()> predicate;
    {
      using iter_t = std::vector<PosixConditionBase*>::const_iterator;
      const auto predicate_inner = [](auto h) { return h->signaled(); };
      const auto operation =
          wait_all ? std::all_of<iter_t, decltype(predicate_inner)>
                   : std::any_of<iter_t, decltype(predicate_inner)>;
      predicate = [&handles, operation, predicate_inner] {
        return operation(handles.cbegin(), handles.cend(), predicate_inner);
      };
    }

    std::unique_lock<std::mutex> lock(PosixConditionBase::mutex_);

    bool wait_success = true;
    if (timeout == std::chrono::milliseconds::max()) {
      PosixConditionBase::cond_.wait(lock, predicate);
    } else {
      wait_success =
          PosixConditionBase::cond_.wait_for(lock, timeout, predicate);
    }
    if (wait_success) {
      auto first_signaled = std::numeric_limits<size_t>::max();
      for (auto i = 0u; i < handles.size(); ++i) {
        if (handles[i]->signaled()) {
          if (first_signaled > i) {
            first_signaled = i;
          }
          handles[i]->post_execution();
          if (!wait_all) break;
        }
      }
      assert_true(std::numeric_limits<size_t>::max() != first_signaled);
      return std::make_pair(WaitResult::kSuccess, first_signaled);
    } else {
      return std::make_pair<WaitResult, size_t>(WaitResult::kTimeout, 0);
    }
  }

  virtual void* native_handle() const { return nullptr; }

 protected:
  virtual bool signaled() const = 0;
  virtual void post_execution() = 0;
  static std::condition_variable cond_;
  static std::mutex mutex_;
};

std::condition_variable PosixConditionBase::cond_;
std::mutex PosixConditionBase::mutex_;

// PosixWaitHandle
class PosixWaitHandle {
 public:
  virtual PosixConditionBase& condition() = 0;
};

// Now, PosixThread
struct ThreadStartData {
  std::function<void()> start_routine;
  bool create_suspended;
  Thread* thread_obj;
};

class PosixThread : public Thread,
                    public PosixConditionBase,
                    public PosixWaitHandle {
 public:
  PosixThread() = default;
  explicit PosixThread(pthread_t thread);
  ~PosixThread() override;

  bool Initialize(Thread::CreationParameters params,
                  std::function<void()> start_routine);

  void set_name(std::string name) override;
  uint32_t system_id() const override;
  uint64_t affinity_mask() override;
  void set_affinity_mask(uint64_t mask) override;
  int priority() override;
  void set_priority(int new_priority) override;
  void QueueUserCallback(std::function<void()> callback) override;
  bool Resume(uint32_t* out_previous_suspend_count) override;
  bool Suspend(uint32_t* out_previous_suspend_count) override;
  void Terminate(int exit_code) override;
  void* native_handle() const override;
  PosixConditionBase& condition() override;

  void WaitStarted() const;

  // Alertable synchronization
  mutable std::mutex alertable_mutex_;
  std::condition_variable alertable_cv_;
  std::function<void()> user_callback_;
  bool user_callback_pending_ = false;

  bool Signal() override;

 protected:
  bool signaled() const override;
  void post_execution() override;

 private:
  static void* ThreadStartRoutine(void* parameter);

  // Thread state
  enum class State {
    kUninitialized,
    kRunning,
    kSuspended,
    kFinished,
  };

  pthread_t thread_ = 0;
  bool signaled_ = false;
  int exit_code_ = 0;
  volatile State state_ = State::kUninitialized;
  volatile uint32_t suspend_count_ = 0;
  mutable std::mutex state_mutex_;
  mutable std::condition_variable state_signal_;
  std::string thread_name_;
  mach_port_t mach_thread_ = MACH_PORT_NULL;
};

// Thread-local variable to keep track of the current thread
thread_local PosixThread* current_thread_ = nullptr;

// Implementation of PosixThread methods
PosixThread::PosixThread(pthread_t thread)
    : thread_(thread),
      signaled_(false),
      exit_code_(0),
      state_(State::kRunning),
      suspend_count_(0),
      mach_thread_(pthread_mach_thread_np(thread)) {}

PosixThread::~PosixThread() {
  if (thread_ && !signaled_) {
    pthread_cancel(thread_);
    pthread_join(thread_, nullptr);
  }
  if (mach_thread_ != MACH_PORT_NULL) {
    mach_port_deallocate(mach_task_self(), mach_thread_);
    mach_thread_ = MACH_PORT_NULL;
  }
}

bool PosixThread::Initialize(Thread::CreationParameters params,
                             std::function<void()> start_routine) {
  auto start_data = new ThreadStartData(
      {std::move(start_routine), params.create_suspended, this});

  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0) return false;
  if (pthread_attr_setstacksize(&attr, params.stack_size) != 0) {
    pthread_attr_destroy(&attr);
    return false;
  }
  // Set detach state to joinable
  if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) != 0) {
    pthread_attr_destroy(&attr);
    return false;
  }
  {
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (pthread_create(&thread_, &attr, ThreadStartRoutine, start_data) != 0) {
      pthread_attr_destroy(&attr);
      return false;
    }
  }
  pthread_attr_destroy(&attr);

  WaitStarted();

  if (params.create_suspended) {
    std::unique_lock<std::mutex> lock(state_mutex_);
    kern_return_t kr = thread_suspend(mach_thread_);
    if (kr != KERN_SUCCESS) {
      assert_always();
      return false;
    }
    state_ = State::kSuspended;
    suspend_count_ = 1;
  }

  return true;
}

void PosixThread::WaitStarted() const {
  std::unique_lock<std::mutex> lock(state_mutex_);
  state_signal_.wait(lock, [this]() { return state_ != State::kUninitialized; });
}

void* PosixThread::ThreadStartRoutine(void* parameter) {
  threading::set_name("");

  auto start_data = static_cast<ThreadStartData*>(parameter);
  assert_not_null(start_data);
  assert_not_null(start_data->thread_obj);

  auto thread = static_cast<PosixThread*>(start_data->thread_obj);
  auto start_routine = std::move(start_data->start_routine);
  delete start_data;

  current_thread_ = thread;

  // Set mach_thread_
  thread->mach_thread_ = pthread_mach_thread_np(pthread_self());

  {
    std::unique_lock<std::mutex> lock(thread->state_mutex_);
    thread->state_ = State::kRunning;
    thread->state_signal_.notify_all();
  }

  start_routine();

  {
    std::unique_lock<std::mutex> lock(thread->state_mutex_);
    thread->state_ = State::kFinished;
  }

  {
    std::lock_guard<std::mutex> lock(PosixConditionBase::mutex_);
    thread->signaled_ = true;
    PosixConditionBase::cond_.notify_all();
  }

  current_thread_ = nullptr;
  return nullptr;
}

void PosixThread::set_name(std::string name) {
  WaitStarted();
  Thread::set_name(name);
  std::unique_lock<std::mutex> lock(state_mutex_);
  if (pthread_self() == thread_) {
    pthread_setname_np(name.c_str());
  } else {
    // Cannot set the name of another thread on macOS.
  }
  thread_name_ = std::move(name);
}

uint32_t PosixThread::system_id() const {
  return static_cast<uint32_t>(pthread_mach_thread_np(thread_));
}

uint64_t PosixThread::affinity_mask() {
  // macOS does not support thread affinity via pthreads.
  return 0;
}

void PosixThread::set_affinity_mask(uint64_t mask) {
  // macOS does not support thread affinity via pthreads.
}

int PosixThread::priority() {
  WaitStarted();
  int policy;
  sched_param param{};
  int ret = pthread_getschedparam(thread_, &policy, &param);
  if (ret != 0) {
    return -1;
  }

  return param.sched_priority;
}

void PosixThread::set_priority(int new_priority) {
  WaitStarted();
  sched_param param{};
  param.sched_priority = new_priority;
  if (pthread_setschedparam(thread_, SCHED_FIFO, &param) != 0)
    assert_always();
}

void PosixThread::QueueUserCallback(std::function<void()> callback) {
  WaitStarted();
  {
    std::lock_guard<std::mutex> lock(alertable_mutex_);
    user_callback_ = std::move(callback);
    user_callback_pending_ = true;
  }
  alertable_cv_.notify_one();
}

bool PosixThread::Resume(uint32_t* out_previous_suspend_count) {
  if (out_previous_suspend_count) {
    *out_previous_suspend_count = 0;
  }
  WaitStarted();
  std::unique_lock<std::mutex> lock(state_mutex_);
  if (state_ != State::kSuspended) return false;
  if (out_previous_suspend_count) {
    *out_previous_suspend_count = suspend_count_;
  }
  --suspend_count_;
  if (suspend_count_ == 0) {
    state_ = State::kRunning;
    kern_return_t kr = thread_resume(mach_thread_);
    if (kr != KERN_SUCCESS) {
      assert_always();
      return false;
    }
  }
  return true;
}

bool PosixThread::Suspend(uint32_t* out_previous_suspend_count) {
  if (out_previous_suspend_count) {
    *out_previous_suspend_count = 0;
  }
  WaitStarted();
  std::unique_lock<std::mutex> lock(state_mutex_);
  if (out_previous_suspend_count) {
    *out_previous_suspend_count = suspend_count_;
  }
  ++suspend_count_;
  if (suspend_count_ == 1) {
    state_ = State::kSuspended;
    kern_return_t kr = thread_suspend(mach_thread_);
    if (kr != KERN_SUCCESS) {
      assert_always();
      return false;
    }
  }
  return true;
}

void PosixThread::Terminate(int exit_code) {
  bool is_current_thread = pthread_self() == thread_;
  {
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (state_ == State::kFinished) {
      if (is_current_thread) {
        assert_always();
        for (;;) {
        }
      }
      return;
    }
    state_ = State::kFinished;
  }

  {
    std::lock_guard<std::mutex> lock(PosixConditionBase::mutex_);
    exit_code_ = exit_code;
    signaled_ = true;
    PosixConditionBase::cond_.notify_all();
  }
  if (is_current_thread) {
    pthread_exit(reinterpret_cast<void*>(exit_code));
  } else {
    pthread_cancel(thread_);
  }
}

void* PosixThread::native_handle() const {
  return reinterpret_cast<void*>(thread_);
}

PosixConditionBase& PosixThread::condition() {
  return *this;
}

bool PosixThread::Signal() {
  // Implement Signal() as required.
  // For threads, Signal() could be used to indicate the thread has completed.
  return true;
}

bool PosixThread::signaled() const {
  return signaled_;
}

void PosixThread::post_execution() {
  if (thread_) {
    pthread_join(thread_, nullptr);
  }
}

// Now, we can implement AlertableSleep using PosixThread
SleepResult AlertableSleep(std::chrono::microseconds duration) {
  if (duration <= std::chrono::microseconds(0)) {
    return SleepResult::kSuccess;
  }

  auto* current_thread = Thread::GetCurrentThread();
  if (!current_thread) {
    Sleep(duration);
    return SleepResult::kSuccess;
  }

  auto* posix_thread = dynamic_cast<PosixThread*>(current_thread);
  if (!posix_thread) {
    Sleep(duration);
    return SleepResult::kSuccess;
  }

  std::unique_lock<std::mutex> lock(posix_thread->alertable_mutex_);

  // Wait until duration expires or a user callback is queued
  if (posix_thread->alertable_cv_.wait_for(
          lock, duration,
          [posix_thread] { return posix_thread->user_callback_pending_; })) {
    // A user callback is pending
    posix_thread->user_callback_pending_ = false;
    std::function<void()> callback = std::move(posix_thread->user_callback_);
    lock.unlock();

    if (callback) {
      callback();
    }

    return SleepResult::kAlerted;
  } else {
    // Sleep completed without interruption
    return SleepResult::kSuccess;
  }
}

// Threading functions
std::unique_ptr<Thread> Thread::Create(CreationParameters params,
                                       std::function<void()> start_routine) {
  auto thread = std::make_unique<PosixThread>();
  if (!thread->Initialize(params, std::move(start_routine))) return nullptr;
  assert_not_null(thread);
  return thread;
}

Thread* Thread::GetCurrentThread() {
  if (current_thread_) {
    return current_thread_;
  }

  // Should take this route only for threads not created by Thread::Create.
  // The only thread not created by Thread::Create should be the main thread.
  pthread_t handle = pthread_self();

  current_thread_ = new PosixThread(handle);
  return current_thread_;
}

void Thread::Exit(int exit_code) {
  if (current_thread_) {
    current_thread_->Terminate(exit_code);
  } else {
    // Should only happen with the main thread
    pthread_exit(reinterpret_cast<void*>(exit_code));
  }
  // Function must not return
  assert_always();
}

void set_name(const std::string_view name) {
  pthread_setname_np(std::string(name).c_str());
}

// Event implementation
class PosixEvent : public Event,
                   public PosixConditionBase,
                   public PosixWaitHandle {
 public:
  PosixEvent(bool manual_reset, bool initial_state)
      : manual_reset_(manual_reset), signal_(initial_state) {}
  ~PosixEvent() override = default;

  void Set() override {
    std::lock_guard<std::mutex> lock(mutex_);
    signal_ = true;
    PosixConditionBase::cond_.notify_all();
  }

  void Reset() override {
    std::lock_guard<std::mutex> lock(mutex_);
    signal_ = false;
  }

  void Pulse() override {
    Set();
    MaybeYield();
    Sleep(std::chrono::microseconds(10));
    Reset();
  }

  PosixConditionBase& condition() override { return *this; }

  void* native_handle() const override { return nullptr; }

  bool Signal() override {
    Set();
    return true;
  }

 protected:
  bool signaled() const override { return signal_; }

  void post_execution() override {
    if (!manual_reset_) {
      signal_ = false;
    }
  }

 private:
  bool manual_reset_;
  bool signal_;
  mutable std::mutex mutex_;
};

std::unique_ptr<Event> Event::CreateManualResetEvent(bool initial_state) {
  return std::make_unique<PosixEvent>(true, initial_state);
}

std::unique_ptr<Event> Event::CreateAutoResetEvent(bool initial_state) {
  return std::make_unique<PosixEvent>(false, initial_state);
}

// Semaphore implementation
class PosixSemaphore : public Semaphore,
                       public PosixConditionBase,
                       public PosixWaitHandle {
 public:
  PosixSemaphore(int initial_count, int maximum_count)
      : count_(initial_count), maximum_count_(maximum_count) {}
  ~PosixSemaphore() override = default;

  bool Release(int release_count, int* out_previous_count) override {
    if (release_count < 1) {
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (maximum_count_ - count_ >= release_count) {
      if (out_previous_count) *out_previous_count = count_;
      count_ += release_count;
      PosixConditionBase::cond_.notify_all();
      return true;
    }
    return false;
  }

  PosixConditionBase& condition() override { return *this; }

  void* native_handle() const override { return nullptr; }

  bool Signal() override {
    return Release(1, nullptr);
  }

 protected:
  bool signaled() const override { return count_ > 0; }

  void post_execution() override {
    count_--;
    PosixConditionBase::cond_.notify_all();
  }

 private:
  int count_;
  int maximum_count_;
  mutable std::mutex mutex_;
};

std::unique_ptr<Semaphore> Semaphore::Create(int initial_count,
                                             int maximum_count) {
  if (initial_count < 0 || initial_count > maximum_count ||
      maximum_count <= 0) {
    return nullptr;
  }
  return std::make_unique<PosixSemaphore>(initial_count, maximum_count);
}

// Mutant (Mutex) implementation
class PosixMutant : public Mutant,
                    public PosixConditionBase,
                    public PosixWaitHandle {
 public:
  explicit PosixMutant(bool initial_owner)
      : count_(0) {
    if (initial_owner) {
      count_ = 1;
      owner_ = std::this_thread::get_id();
    }
  }
  ~PosixMutant() override = default;

  bool Release() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (owner_ == std::this_thread::get_id() && count_ > 0) {
      --count_;
      if (count_ == 0) {
        owner_ = std::thread::id();
        PosixConditionBase::cond_.notify_all();
      }
      return true;
    }
    return false;
  }

  PosixConditionBase& condition() override { return *this; }

  void* native_handle() const override { return nullptr; }

  bool Signal() override {
    return Release();
  }

 protected:
  bool signaled() const override {
    return count_ == 0 || owner_ == std::this_thread::get_id();
  }

  void post_execution() override {
    count_++;
    owner_ = std::this_thread::get_id();
  }

 private:
  uint32_t count_;
  std::thread::id owner_;
  mutable std::mutex mutex_;
};

std::unique_ptr<Mutant> Mutant::Create(bool initial_owner) {
  return std::make_unique<PosixMutant>(initial_owner);
}

// Timer implementation
class PosixTimer : public Timer,
                   public PosixConditionBase,
                   public PosixWaitHandle {
  using WClock_ = Timer::WClock_;
  using GClock_ = Timer::GClock_;

 public:
  explicit PosixTimer(bool manual_reset)
      : manual_reset_(manual_reset), signal_(false) {}
  ~PosixTimer() override { Cancel(); }

  bool SetOnceAfter(xe::chrono::hundrednanoseconds rel_time,
                    std::function<void()> opt_callback = nullptr) override {
    return SetOnceAt(GClock_::now() + rel_time, std::move(opt_callback));
  }

  bool SetOnceAt(WClock_::time_point due_time,
                 std::function<void()> opt_callback = nullptr) override {
    return SetOnceAt(date::clock_cast<GClock_>(due_time),
                     std::move(opt_callback));
  }

  bool SetOnceAt(GClock_::time_point due_time,
                 std::function<void()> opt_callback = nullptr) override {
    Cancel();

    std::lock_guard<std::mutex> lock(mutex_);

    callback_ = std::move(opt_callback);
    signal_ = false;
    wait_item_ = QueueTimerOnce(&CompletionRoutine, this, due_time);
    return true;
  }

  bool SetRepeatingAfter(
      xe::chrono::hundrednanoseconds rel_time, std::chrono::milliseconds period,
      std::function<void()> opt_callback = nullptr) override {
    return SetRepeatingAt(GClock_::now() + rel_time, period,
                          std::move(opt_callback));
  }

  bool SetRepeatingAt(WClock_::time_point due_time,
                      std::chrono::milliseconds period,
                      std::function<void()> opt_callback = nullptr) override {
    return SetRepeatingAt(date::clock_cast<GClock_>(due_time), period,
                          std::move(opt_callback));
  }

  bool SetRepeatingAt(GClock_::time_point due_time,
                      std::chrono::milliseconds period,
                      std::function<void()> opt_callback = nullptr) override {
    Cancel();

    std::lock_guard<std::mutex> lock(mutex_);

    callback_ = std::move(opt_callback);
    signal_ = false;
    wait_item_ =
        QueueTimerRecurring(&CompletionRoutine, this, due_time, period);
    return true;
  }

  bool Cancel() override {
    if (auto wait_item = wait_item_.lock()) {
      wait_item->Disarm();
    }
    return true;
  }

  PosixConditionBase& condition() override { return *this; }

  void* native_handle() const override { return nullptr; }

  bool Signal() override {
    std::lock_guard<std::mutex> lock(mutex_);
    signal_ = true;
    PosixConditionBase::cond_.notify_all();
    return true;
  }

 protected:
  bool signaled() const override { return signal_; }

  void post_execution() override {
    if (!manual_reset_) {
      signal_ = false;
    }
  }

 private:
  static void CompletionRoutine(void* userdata) {
    assert_not_null(userdata);
    auto timer = reinterpret_cast<PosixTimer*>(userdata);
    timer->Signal();
    std::function<void()> callback;
    {
      std::lock_guard<std::mutex> lock(timer->mutex_);
      callback = timer->callback_;
    }
    if (callback) {
      callback();
    }
  }

  std::weak_ptr<TimerQueueWaitItem> wait_item_;
  std::function<void()> callback_;
  bool manual_reset_;
  volatile bool signal_;
  mutable std::mutex mutex_;
};

std::unique_ptr<Timer> Timer::CreateManualResetTimer() {
  return std::make_unique<PosixTimer>(true);
}

std::unique_ptr<Timer> Timer::CreateSynchronizationTimer() {
  return std::make_unique<PosixTimer>(false);
}

// Wait functions
WaitResult Wait(WaitHandle* wait_handle, bool is_alertable,
                std::chrono::milliseconds timeout) {
  auto posix_wait_handle = dynamic_cast<PosixWaitHandle*>(wait_handle);
  if (posix_wait_handle == nullptr) {
    return WaitResult::kFailed;
  }

  auto* current_thread = Thread::GetCurrentThread();
  auto* posix_thread = dynamic_cast<PosixThread*>(current_thread);

  if (is_alertable && posix_thread) {
    alertable_state_ = true;
    std::unique_lock<std::mutex> lock(posix_thread->alertable_mutex_);

    auto wait_result = posix_wait_handle->condition().Wait(timeout);
    if (posix_thread->user_callback_pending_) {
      // Execute the user callback
      posix_thread->user_callback_pending_ = false;
      std::function<void()> callback = std::move(posix_thread->user_callback_);
      lock.unlock();

      if (callback) {
        callback();
      }

      alertable_state_ = false;
      return WaitResult::kUserCallback;
    }

    alertable_state_ = false;
    return wait_result;
  } else {
    return posix_wait_handle->condition().Wait(timeout);
  }
}

WaitResult SignalAndWait(WaitHandle* wait_handle_to_signal,
                         WaitHandle* wait_handle_to_wait_on, bool is_alertable,
                         std::chrono::milliseconds timeout) {
  auto posix_wait_handle_to_signal =
      dynamic_cast<PosixWaitHandle*>(wait_handle_to_signal);
  auto posix_wait_handle_to_wait_on =
      dynamic_cast<PosixWaitHandle*>(wait_handle_to_wait_on);
  if (posix_wait_handle_to_signal == nullptr ||
      posix_wait_handle_to_wait_on == nullptr) {
    return WaitResult::kFailed;
  }

  if (!posix_wait_handle_to_signal->condition().Signal()) {
    return WaitResult::kFailed;
  }

  return Wait(wait_handle_to_wait_on, is_alertable, timeout);
}

std::pair<WaitResult, size_t> WaitMultiple(WaitHandle* wait_handles[],
                                           size_t wait_handle_count,
                                           bool wait_all, bool is_alertable,
                                           std::chrono::milliseconds timeout) {
  std::vector<PosixConditionBase*> conditions;
  conditions.reserve(wait_handle_count);
  for (size_t i = 0u; i < wait_handle_count; ++i) {
    auto handle = dynamic_cast<PosixWaitHandle*>(wait_handles[i]);
    if (handle == nullptr) {
      return std::make_pair(WaitResult::kFailed, 0);
    }
    conditions.push_back(&handle->condition());
  }

  auto* current_thread = Thread::GetCurrentThread();
  auto* posix_thread = dynamic_cast<PosixThread*>(current_thread);

  if (is_alertable && posix_thread) {
    alertable_state_ = true;
    std::unique_lock<std::mutex> lock(posix_thread->alertable_mutex_);

    auto result = PosixConditionBase::WaitMultiple(std::move(conditions),
                                                   wait_all, timeout);
    if (posix_thread->user_callback_pending_) {
      // Execute the user callback
      posix_thread->user_callback_pending_ = false;
      std::function<void()> callback = std::move(posix_thread->user_callback_);
      lock.unlock();

      if (callback) {
        callback();
      }

      alertable_state_ = false;
      return std::make_pair(WaitResult::kUserCallback, 0);
    }

    alertable_state_ = false;
    return result;
  } else {
    return PosixConditionBase::WaitMultiple(std::move(conditions), wait_all,
                                            timeout);
  }
}

TlsHandle AllocateTlsHandle() {
  pthread_key_t key;
  int res = pthread_key_create(&key, nullptr);
  assert_zero(res);
  return static_cast<TlsHandle>(key);
}

bool FreeTlsHandle(TlsHandle handle) {
  return pthread_key_delete(static_cast<pthread_key_t>(handle)) == 0;
}

uintptr_t GetTlsValue(TlsHandle handle) {
  return reinterpret_cast<uintptr_t>(
      pthread_getspecific(static_cast<pthread_key_t>(handle)));
}

bool SetTlsValue(TlsHandle handle, uintptr_t value) {
  return pthread_setspecific(static_cast<pthread_key_t>(handle),
                             reinterpret_cast<void*>(value)) == 0;
}

}  // namespace threading
}  // namespace xe