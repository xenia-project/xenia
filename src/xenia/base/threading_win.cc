/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/threading.h"

#include "xenia/base/platform_win.h"

namespace xe {
namespace threading {

uint32_t current_thread_id() {
  return static_cast<uint32_t>(GetCurrentThreadId());
}

// http://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
#pragma pack(push, 8)
struct THREADNAME_INFO {
  DWORD dwType;      // Must be 0x1000.
  LPCSTR szName;     // Pointer to name (in user addr space).
  DWORD dwThreadID;  // Thread ID (-1=caller thread).
  DWORD dwFlags;     // Reserved for future use, must be zero.
};
#pragma pack(pop)

void set_name(DWORD thread_id, const std::string& name) {
  if (!IsDebuggerPresent()) {
    return;
  }
  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = name.c_str();
  info.dwThreadID = thread_id;
  info.dwFlags = 0;
  __try {
    RaiseException(0x406D1388, 0, sizeof(info) / sizeof(ULONG_PTR),
                   reinterpret_cast<ULONG_PTR*>(&info));
  }
  __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void set_name(const std::string& name) {
  set_name(static_cast<DWORD>(-1), name);
}

void set_name(std::thread::native_handle_type handle, const std::string& name) {
  set_name(GetThreadId(handle), name);
}

void MaybeYield() {
  SwitchToThread();
  MemoryBarrier();
}

void SyncMemory() { MemoryBarrier(); }

void Sleep(std::chrono::microseconds duration) {
  if (duration.count() < 100) {
    MaybeYield();
  } else {
    ::Sleep(static_cast<DWORD>(duration.count() / 1000));
  }
}

class Win32Handle {
 public:
  Win32Handle(HANDLE handle) : handle_(handle) {}
  virtual ~Win32Handle() {
    CloseHandle(handle_);
    handle_ = nullptr;
  }

 protected:
  HANDLE handle_ = nullptr;
};

class Win32Event : public Event, public Win32Handle {
 public:
  Win32Event(HANDLE handle) : Win32Handle(handle) {}
  ~Win32Event() = default;
  void Set() override { SetEvent(handle_); }
  void Reset() override { ResetEvent(handle_); }
  void Pulse() override { PulseEvent(handle_); }
};

std::unique_ptr<Event> Event::CreateManualResetEvent(bool initial_state) {
  return std::make_unique<Win32Event>(
      CreateEvent(nullptr, TRUE, initial_state ? TRUE : FALSE, nullptr));
}

std::unique_ptr<Event> Event::CreateAutoResetEvent(bool initial_state) {
  return std::make_unique<Win32Event>(
      CreateEvent(nullptr, FALSE, initial_state ? TRUE : FALSE, nullptr));
}

class Win32Semaphore : public Semaphore, public Win32Handle {
 public:
  Win32Semaphore(HANDLE handle) : Win32Handle(handle) {}
  ~Win32Semaphore() override = default;
  bool Release(int release_count, int* out_previous_count) override {
    return ReleaseSemaphore(handle_, release_count,
                            reinterpret_cast<LPLONG>(out_previous_count))
               ? true
               : false;
  }
};

std::unique_ptr<Semaphore> Semaphore::Create(int initial_count,
                                             int maximum_count) {
  return std::make_unique<Win32Semaphore>(
      CreateSemaphore(nullptr, initial_count, maximum_count, nullptr));
}

class Win32Mutant : public Mutant, public Win32Handle {
 public:
  Win32Mutant(HANDLE handle) : Win32Handle(handle) {}
  ~Win32Mutant() = default;
  bool Release() override { return ReleaseMutex(handle_) ? true : false; }
};

std::unique_ptr<Mutant> Mutant::Create(bool initial_owner) {
  return std::make_unique<Win32Mutant>(
      CreateMutex(nullptr, initial_owner ? TRUE : FALSE, nullptr));
}

class Win32Timer : public Timer, public Win32Handle {
 public:
  Win32Timer(HANDLE handle) : Win32Handle(handle) {}
  ~Win32Timer() = default;
  bool SetOnce(std::chrono::nanoseconds due_time,
               std::function<void()> opt_callback) override {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(opt_callback);
    LARGE_INTEGER due_time_li;
    due_time_li.QuadPart = due_time.count() / 100;
    auto completion_routine =
        opt_callback ? reinterpret_cast<PTIMERAPCROUTINE>(CompletionRoutine)
                     : NULL;
    return SetWaitableTimer(handle_, &due_time_li, 0, completion_routine, this,
                            FALSE)
               ? true
               : false;
  }
  bool SetRepeating(std::chrono::nanoseconds due_time,
                    std::chrono::milliseconds period,
                    std::function<void()> opt_callback) override {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(opt_callback);
    LARGE_INTEGER due_time_li;
    due_time_li.QuadPart = due_time.count() / 100;
    auto completion_routine =
        opt_callback ? reinterpret_cast<PTIMERAPCROUTINE>(CompletionRoutine)
                     : NULL;
    return SetWaitableTimer(handle_, &due_time_li, int32_t(period.count()),
                            completion_routine, this, FALSE)
               ? true
               : false;
  }
  bool Cancel() override {
    // Reset the callback immediately so that any completions don't call it.
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = nullptr;
    return CancelWaitableTimer(handle_) ? true : false;
  }

 private:
  static void CompletionRoutine(Win32Timer* timer, DWORD timer_low,
                                DWORD timer_high) {
    // As the callback may reset the timer, store local.
    std::function<void()> callback;
    {
      std::lock_guard<std::mutex> lock(timer->mutex_);
      callback = timer->callback_;
    }
    callback();
  }

  std::mutex mutex_;
  std::function<void()> callback_;
};

std::unique_ptr<Timer> Timer::CreateManualResetTimer() {
  return std::make_unique<Win32Timer>(CreateWaitableTimer(NULL, TRUE, NULL));
}

std::unique_ptr<Timer> Timer::CreateSynchronizationTimer() {
  return std::make_unique<Win32Timer>(CreateWaitableTimer(NULL, FALSE, NULL));
}

}  // namespace threading
}  // namespace xe
