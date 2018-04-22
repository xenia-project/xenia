/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/threading.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

#include <pthread.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctime>

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

// Thread interruption is done using user-defined signals
// This implementation uses the SIGRTMAX - SIGRTMIN to signal to a thread
// gdb tip, for SIG = SIGRTMIN + SignalType : handle SIG nostop
// lldb tip, for SIG = SIGRTMIN + SignalType : process handle SIG -s false
enum class SignalType { kHighResolutionTimer, k_Count };

int GetSystemSignal(SignalType num) {
  auto result = SIGRTMIN + static_cast<int>(num);
  assert_true(result < SIGRTMAX);
  return result;
}

SignalType GetSystemSignalType(int num) {
  return static_cast<SignalType>(num - SIGRTMIN);
}

thread_local std::array<bool, static_cast<size_t>(SignalType::k_Count)>
    signal_handler_installed = {};

static void signal_handler(int signal, siginfo_t* info, void* context);

void install_signal_handler(SignalType type) {
  if (signal_handler_installed[static_cast<size_t>(type)]) return;
  struct sigaction action {};
  action.sa_flags = SA_SIGINFO;
  action.sa_sigaction = signal_handler;
  sigemptyset(&action.sa_mask);
  if (sigaction(GetSystemSignal(type), &action, nullptr) == -1)
    signal_handler_installed[static_cast<size_t>(type)] = true;
}

// TODO(dougvj)
void EnableAffinityConfiguration() {}

// uint64_t ticks() { return mach_absolute_time(); }

uint32_t current_thread_system_id() {
  return static_cast<uint32_t>(syscall(SYS_gettid));
}

void set_name(std::thread::native_handle_type handle,
              const std::string_view name) {
  pthread_setname_np(handle, std::string(name).c_str());
}

void set_name(const std::string_view name) { set_name(pthread_self(), name); }

void MaybeYield() {
  pthread_yield();
  __sync_synchronize();
}

void SyncMemory() { __sync_synchronize(); }

void Sleep(std::chrono::microseconds duration) {
  timespec rqtp = DurationToTimeSpec(duration);
  timespec rmtp = {};
  auto p_rqtp = &rqtp;
  auto p_rmtp = &rmtp;
  int ret = 0;
  do {
    ret = nanosleep(p_rqtp, p_rmtp);
    // Swap requested for remaining in case of signal interruption
    // in which case, we start sleeping again for the remainder
    std::swap(p_rqtp, p_rmtp);
  } while (ret == -1 && errno == EINTR);
}

// TODO(dougvj) Not sure how to implement the equivalent of this on POSIX.
SleepResult AlertableSleep(std::chrono::microseconds duration) {
  sleep(duration.count() / 1000);
  return SleepResult::kSuccess;
}

// TODO(dougvj) We can probably wrap this with pthread_key_t but the type of
// TlsHandle probably needs to be refactored
TlsHandle AllocateTlsHandle() {
  assert_always();
  return 0;
}

bool FreeTlsHandle(TlsHandle handle) { return true; }

uintptr_t GetTlsValue(TlsHandle handle) {
  assert_always();
  return 0;
}

bool SetTlsValue(TlsHandle handle, uintptr_t value) {
  assert_always();
  return false;
}

class PosixHighResolutionTimer : public HighResolutionTimer {
 public:
  explicit PosixHighResolutionTimer(std::function<void()> callback)
      : callback_(std::move(callback)), timer_(nullptr) {}
  ~PosixHighResolutionTimer() override {
    if (timer_) timer_delete(timer_);
  }

  bool Initialize(std::chrono::milliseconds period) {
    // Create timer
    sigevent sev{};
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = GetSystemSignal(SignalType::kHighResolutionTimer);
    sev.sigev_value.sival_ptr = (void*)&callback_;
    if (timer_create(CLOCK_REALTIME, &sev, &timer_) == -1) return false;

    // Start timer
    itimerspec its{};
    its.it_value = DurationToTimeSpec(period);
    its.it_interval = its.it_value;
    return timer_settime(timer_, 0, &its, nullptr) != -1;
  }

 private:
  std::function<void()> callback_;
  timer_t timer_;
};

std::unique_ptr<HighResolutionTimer> HighResolutionTimer::CreateRepeating(
    std::chrono::milliseconds period, std::function<void()> callback) {
  install_signal_handler(SignalType::kHighResolutionTimer);
  auto timer = std::make_unique<PosixHighResolutionTimer>(std::move(callback));
  if (!timer->Initialize(period)) {
    return nullptr;
  }
  return std::unique_ptr<HighResolutionTimer>(timer.release());
}

// TODO(dougvj) There really is no native POSIX handle for a single wait/signal
// construct pthreads is at a lower level with more handles for such a mechanism
// This simple wrapper class could function as our handle, but probably needs
// some more functionality
class PosixCondition {
 public:
  PosixCondition() : signal_(false) {
    pthread_mutex_init(&mutex_, NULL);
    pthread_cond_init(&cond_, NULL);
  }

  ~PosixCondition() {
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&cond_);
  }

  void Signal() {
    pthread_mutex_lock(&mutex_);
    signal_ = true;
    pthread_cond_broadcast(&cond_);
    pthread_mutex_unlock(&mutex_);
  }

  void Reset() {
    pthread_mutex_lock(&mutex_);
    signal_ = false;
    pthread_mutex_unlock(&mutex_);
  }

  bool Wait(unsigned int timeout_ms) {
    // Assume 0 means no timeout, not instant timeout
    if (timeout_ms == 0) {
      Wait();
    }
    struct timespec time_to_wait;
    struct timeval now;
    gettimeofday(&now, NULL);

    // Add the number of seconds we want to wait to the current time
    time_to_wait.tv_sec = now.tv_sec + (timeout_ms / 1000);
    // Add the number of nanoseconds we want to wait to the current nanosecond
    // stride
    long nsec = (now.tv_usec + (timeout_ms % 1000)) * 1000;
    // If we overflowed the nanosecond count then we add a second
    time_to_wait.tv_sec += nsec / 1000000000UL;
    // We only add nanoseconds within the 1 second stride
    time_to_wait.tv_nsec = nsec % 1000000000UL;
    pthread_mutex_lock(&mutex_);
    while (!signal_) {
      int status = pthread_cond_timedwait(&cond_, &mutex_, &time_to_wait);
      if (status == ETIMEDOUT) return false;  // We timed out
    }
    pthread_mutex_unlock(&mutex_);
    return true;  // We didn't time out
  }

  bool Wait() {
    pthread_mutex_lock(&mutex_);
    while (!signal_) {
      pthread_cond_wait(&cond_, &mutex_);
    }
    pthread_mutex_unlock(&mutex_);
    return true;  // Did not time out;
  }

 private:
  bool signal_;
  pthread_cond_t cond_;
  pthread_mutex_t mutex_;
};

// Native posix thread handle
template <typename T>
class PosixThreadHandle : public T {
 public:
  explicit PosixThreadHandle(pthread_t handle) : handle_(handle) {}
  ~PosixThreadHandle() override {}

 protected:
  void* native_handle() const override {
    return reinterpret_cast<void*>(handle_);
  }

  pthread_t handle_;
};

// This is wraps a condition object as our handle because posix has no single
// native handle for higher level concurrency constructs such as semaphores
template <typename T>
class PosixConditionHandle : public T {
 public:
  ~PosixConditionHandle() override {}

 protected:
  void* native_handle() const override {
    return reinterpret_cast<void*>(const_cast<PosixCondition*>(&handle_));
  }

  PosixCondition handle_;
};

template <typename T>
class PosixFdHandle : public T {
 public:
  explicit PosixFdHandle(intptr_t handle) : handle_(handle) {}
  ~PosixFdHandle() override {
    close(handle_);
    handle_ = 0;
  }

 protected:
  void* native_handle() const override {
    return reinterpret_cast<void*>(handle_);
  }

  intptr_t handle_;
};

// TODO(dougvj)
WaitResult Wait(WaitHandle* wait_handle, bool is_alertable,
                std::chrono::milliseconds timeout) {
  intptr_t handle = reinterpret_cast<intptr_t>(wait_handle->native_handle());

  fd_set set;
  struct timeval time_val;
  int ret;

  FD_ZERO(&set);
  FD_SET(handle, &set);

  time_val.tv_sec = timeout.count() / 1000;
  time_val.tv_usec = timeout.count() * 1000;
  ret = select(handle + 1, &set, NULL, NULL, &time_val);
  if (ret == -1) {
    return WaitResult::kFailed;
  } else if (ret == 0) {
    return WaitResult::kTimeout;
  } else {
    uint64_t buf = 0;
    ret = read(handle, &buf, sizeof(buf));
    if (ret < 8) {
      return WaitResult::kTimeout;
    }

    return WaitResult::kSuccess;
  }
}

// TODO(dougvj)
WaitResult SignalAndWait(WaitHandle* wait_handle_to_signal,
                         WaitHandle* wait_handle_to_wait_on, bool is_alertable,
                         std::chrono::milliseconds timeout) {
  assert_always();
  return WaitResult::kFailed;
}

// TODO(dougvj)
std::pair<WaitResult, size_t> WaitMultiple(WaitHandle* wait_handles[],
                                           size_t wait_handle_count,
                                           bool wait_all, bool is_alertable,
                                           std::chrono::milliseconds timeout) {
  assert_always();
  return std::pair<WaitResult, size_t>(WaitResult::kFailed, 0);
}

// TODO(dougvj)
class PosixEvent : public PosixFdHandle<Event> {
 public:
  PosixEvent(intptr_t fd) : PosixFdHandle(fd) {}
  ~PosixEvent() override = default;
  void Set() override {
    uint64_t buf = 1;
    write(handle_, &buf, sizeof(buf));
  }
  void Reset() override { assert_always(); }
  void Pulse() override { assert_always(); }

 private:
  PosixCondition condition_;
};

std::unique_ptr<Event> Event::CreateManualResetEvent(bool initial_state) {
  // Linux's eventfd doesn't appear to support manual reset natively.
  return nullptr;
}

std::unique_ptr<Event> Event::CreateAutoResetEvent(bool initial_state) {
  int fd = eventfd(initial_state ? 1 : 0, EFD_CLOEXEC);
  if (fd == -1) {
    return nullptr;
  }

  return std::make_unique<PosixEvent>(fd);
}

// TODO(dougvj)
class PosixSemaphore : public PosixConditionHandle<Semaphore> {
 public:
  PosixSemaphore(int initial_count, int maximum_count) { assert_always(); }
  ~PosixSemaphore() override = default;
  bool Release(int release_count, int* out_previous_count) override {
    assert_always();
    return false;
  }
};

std::unique_ptr<Semaphore> Semaphore::Create(int initial_count,
                                             int maximum_count) {
  return std::make_unique<PosixSemaphore>(initial_count, maximum_count);
}

// TODO(dougvj)
class PosixMutant : public PosixConditionHandle<Mutant> {
 public:
  PosixMutant(bool initial_owner) { assert_always(); }
  ~PosixMutant() = default;
  bool Release() override {
    assert_always();
    return false;
  }
};

std::unique_ptr<Mutant> Mutant::Create(bool initial_owner) {
  return std::make_unique<PosixMutant>(initial_owner);
}

// TODO(dougvj)
class PosixTimer : public PosixConditionHandle<Timer> {
 public:
  PosixTimer(bool manual_reset) { assert_always(); }
  ~PosixTimer() = default;
  bool SetOnce(std::chrono::nanoseconds due_time,
               std::function<void()> opt_callback) override {
    assert_always();
    return false;
  }
  bool SetRepeating(std::chrono::nanoseconds due_time,
                    std::chrono::milliseconds period,
                    std::function<void()> opt_callback) override {
    assert_always();
    return false;
  }
  bool Cancel() override {
    assert_always();
    return false;
  }
};

std::unique_ptr<Timer> Timer::CreateManualResetTimer() {
  return std::make_unique<PosixTimer>(true);
}

std::unique_ptr<Timer> Timer::CreateSynchronizationTimer() {
  return std::make_unique<PosixTimer>(false);
}

class PosixThread : public PosixThreadHandle<Thread> {
 public:
  explicit PosixThread(pthread_t handle) : PosixThreadHandle(handle) {}
  ~PosixThread() = default;

  void set_name(std::string name) override {
    pthread_setname_np(handle_, name.c_str());
  }

  uint32_t system_id() const override { return 0; }

  // TODO(DrChat)
  uint64_t affinity_mask() override { return 0; }
  void set_affinity_mask(uint64_t mask) override { assert_always(); }

  int priority() override {
    int policy;
    struct sched_param param;
    int ret = pthread_getschedparam(handle_, &policy, &param);
    if (ret != 0) {
      return -1;
    }

    return param.sched_priority;
  }

  void set_priority(int new_priority) override {
    struct sched_param param;
    param.sched_priority = new_priority;
    int ret = pthread_setschedparam(handle_, SCHED_FIFO, &param);
  }

  // TODO(DrChat)
  void QueueUserCallback(std::function<void()> callback) override {
    assert_always();
  }

  bool Resume(uint32_t* out_new_suspend_count = nullptr) override {
    assert_always();
    return false;
  }

  bool Suspend(uint32_t* out_previous_suspend_count = nullptr) override {
    assert_always();
    return false;
  }

  void Terminate(int exit_code) override {}
};

thread_local std::unique_ptr<PosixThread> current_thread_ = nullptr;

struct ThreadStartData {
  std::function<void()> start_routine;
};
void* ThreadStartRoutine(void* parameter) {
  current_thread_ =
      std::unique_ptr<PosixThread>(new PosixThread(::pthread_self()));

  auto start_data = reinterpret_cast<ThreadStartData*>(parameter);
  start_data->start_routine();
  delete start_data;
  return 0;
}

std::unique_ptr<Thread> Thread::Create(CreationParameters params,
                                       std::function<void()> start_routine) {
  auto start_data = new ThreadStartData({std::move(start_routine)});

  assert_false(params.create_suspended);
  pthread_t handle;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  int ret = pthread_create(&handle, &attr, ThreadStartRoutine, start_data);
  if (ret != 0) {
    // TODO(benvanik): pass back?
    auto last_error = errno;
    XELOGE("Unable to pthread_create: {}", last_error);
    delete start_data;
    return nullptr;
  }

  return std::unique_ptr<PosixThread>(new PosixThread(handle));
}

Thread* Thread::GetCurrentThread() {
  if (current_thread_) {
    return current_thread_.get();
  }

  pthread_t handle = pthread_self();

  current_thread_ = std::make_unique<PosixThread>(handle);
  return current_thread_.get();
}

void Thread::Exit(int exit_code) {
  pthread_exit(reinterpret_cast<void*>(exit_code));
}

static void signal_handler(int signal, siginfo_t* info, void* /*context*/) {
  switch (GetSystemSignalType(signal)) {
    case SignalType::kHighResolutionTimer: {
      assert_not_null(info->si_value.sival_ptr);
      auto callback =
          *static_cast<std::function<void()>*>(info->si_value.sival_ptr);
      callback();
    } break;
    default:
      assert_always();
  }
}

}  // namespace threading
}  // namespace xe
