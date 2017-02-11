/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/threading.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

namespace xe {
namespace threading {

// uint64_t ticks() { return mach_absolute_time(); }

uint32_t current_thread_system_id() {
  return static_cast<uint32_t>(syscall(SYS_gettid));
}

void set_name(const std::string& name) {
  pthread_setname_np(pthread_self(), name.c_str());
}

void set_name(std::thread::native_handle_type handle, const std::string& name) {
  pthread_setname_np(pthread_self(), name.c_str());
}

void Sleep(std::chrono::microseconds duration) {
  timespec rqtp = {duration.count() / 1000000, duration.count() % 1000};
  nanosleep(&rqtp, nullptr);
  // TODO(benvanik): spin while rmtp >0?
}

template <typename T>
class PosixHandle : public T {
 public:
  explicit PosixHandle(pthread_t handle) : handle_(handle) {}
  ~PosixHandle() override {}

 protected:
  void* native_handle() const override {
    return reinterpret_cast<void*>(handle_);
  }

  pthread_t handle_;
};

class PosixThread : public PosixHandle<Thread> {
 public:
  explicit PosixThread(pthread_t handle) : PosixHandle(handle) {}
  ~PosixThread() = default;

  void set_name(std::string name) override {
    // TODO(DrChat)
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
  current_thread_ = std::make_unique<PosixThread>(::pthread_self());

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
    XELOGE("Unable to pthread_create: %d", last_error);
    delete start_data;
    return nullptr;
  }

  return std::make_unique<PosixThread>(handle);
}

}  // namespace threading
}  // namespace xe
