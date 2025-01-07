/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2022 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include <array>

#include "xenia/base/threading.h"

#define CATCH_CONFIG_ENABLE_CHRONO_STRINGMAKER
#include "third_party/catch/include/catch.hpp"

#include "third_party/disruptorplus/include/disruptorplus/spin_wait.hpp"

namespace xe {
namespace base {
namespace test {
using namespace threading;
using namespace std::chrono_literals;

// Helpers to wait on a predicate which do not depend on complex sync primitives
template <class Clock, class Duration, class Predicate>
bool spin_wait_until(
    const std::chrono::time_point<Clock, Duration>& timeout_time,
    Predicate stop_waiting) {
  disruptorplus::spin_wait spinner;
  while (!stop_waiting()) {
    if (std::chrono::steady_clock::now() >= timeout_time) {
      return false;
    }
    spinner.spin_once();
  }
  return true;
}
template <class Period, class Rep, class Predicate>
bool spin_wait_for(const std::chrono::duration<Rep, Period>& rel_time,
                   Predicate stop_waiting) {
  return spin_wait_until(std::chrono::steady_clock::now() + rel_time,
                         stop_waiting);
}

template <class Predicate>
void spin_wait(Predicate stop_waiting) {
  disruptorplus::spin_wait spinner;
  while (!stop_waiting()) {
    spinner.spin_once();
  }
}

TEST_CASE("Fence") {
  std::unique_ptr<threading::Fence> pFence;
  std::unique_ptr<threading::HighResolutionTimer> pTimer;

  // Signal without wait
  pFence = std::make_unique<threading::Fence>();
  pFence->Signal();

  // Signal once and wait
  pFence = std::make_unique<threading::Fence>();
  pFence->Signal();
  pFence->Wait();

  // Signal twice and wait
  pFence = std::make_unique<threading::Fence>();
  pFence->Signal();
  pFence->Signal();
  pFence->Wait();

  // Signal and wait two times
  pFence = std::make_unique<threading::Fence>();
  pFence->Signal();
  pFence->Wait();
  pFence->Signal();
  pFence->Wait();

  // Test to synchronize multiple threads
  std::atomic<int> started(0);
  std::atomic<int> finished(0);
  pFence = std::make_unique<threading::Fence>();
  auto func = [&pFence, &started, &finished] {
    started.fetch_add(1);
    pFence->Wait();
    finished.fetch_add(1);
  };

  auto threads = std::array<std::thread, 5>({
      std::thread(func),
      std::thread(func),
      std::thread(func),
      std::thread(func),
      std::thread(func),
  });

  REQUIRE(spin_wait_for(1s, [&] { return started == threads.size(); }));
  REQUIRE(finished.load() == 0);

  pFence->Signal();

  for (auto& t : threads) t.join();
  REQUIRE(finished.load() == threads.size());
}  // namespace test

TEST_CASE("Get number of logical processors") {
  auto count = std::thread::hardware_concurrency();
  REQUIRE(logical_processor_count() == count);
  REQUIRE(logical_processor_count() == count);
  REQUIRE(logical_processor_count() == count);
}

TEST_CASE("Enable process to set thread affinity") {
  EnableAffinityConfiguration();
}

TEST_CASE("Yield Current Thread", "[maybe_yield]") {
  // Run to see if there are any errors
  MaybeYield();
}

TEST_CASE("Sync with Memory Barrier", "[sync_memory]") {
  // Run to see if there are any errors
  SyncMemory();
}

TEST_CASE("Sleep Current Thread", "[sleep]") {
  auto wait_time = 50ms;
  auto start = std::chrono::steady_clock::now();
  Sleep(wait_time);
  auto duration = std::chrono::steady_clock::now() - start;
  REQUIRE(duration >= wait_time);
}

TEST_CASE("Sleep Current Thread in Alertable State", "[sleep]") {
  auto wait_time = 50ms;
  auto start = std::chrono::steady_clock::now();
  auto result = threading::AlertableSleep(wait_time);
  auto duration = std::chrono::steady_clock::now() - start;
  REQUIRE(duration >= wait_time);
  REQUIRE(result == threading::SleepResult::kSuccess);

  // TODO(bwrsandman): Test a Thread to return kAlerted.
  // Need callback to call extended I/O function (ReadFileEx or WriteFileEx)
}

TEST_CASE("TlsHandle") {
  // Test Allocate
  auto handle = threading::AllocateTlsHandle();

  // Test Free
  REQUIRE(threading::FreeTlsHandle(handle));
  REQUIRE(!threading::FreeTlsHandle(handle));
  REQUIRE(!threading::FreeTlsHandle(threading::kInvalidTlsHandle));

  // Test setting values
  handle = threading::AllocateTlsHandle();
  REQUIRE(threading::GetTlsValue(handle) == 0);
  uint32_t value = 0xDEADBEEF;
  threading::SetTlsValue(handle, reinterpret_cast<uintptr_t>(&value));
  auto p_received_value = threading::GetTlsValue(handle);
  REQUIRE(threading::GetTlsValue(handle) != 0);
  auto received_value = *reinterpret_cast<uint32_t*>(p_received_value);
  REQUIRE(received_value == value);

  uintptr_t non_thread_local_value = 0;
  auto thread = Thread::Create({}, [&non_thread_local_value, &handle] {
    non_thread_local_value = threading::GetTlsValue(handle);
  });
  REQUIRE(thread);

  auto result = Wait(thread.get(), false, 50ms);
  REQUIRE(result == WaitResult::kSuccess);
  REQUIRE(non_thread_local_value == 0);

  // Cleanup
  REQUIRE(threading::FreeTlsHandle(handle));
}

TEST_CASE("HighResolutionTimer") {
  // The wait time is 500ms with an interval of 50ms
  // Smaller values are not as precise and fail the test
  const auto wait_time = 500ms;

  const Thread* timer_thread = nullptr;

  // Time the actual sleep duration
  {
    const auto interval = 50ms;
    std::atomic<uint64_t> counter(0);
    auto start = std::chrono::steady_clock::now();
    auto cb = [&counter, &timer_thread] {
      if (counter == 0) {
        timer_thread = Thread::GetCurrentThread();
      } else {
        REQUIRE(Thread::GetCurrentThread() == timer_thread);
      }
      ++counter;
    };
    auto pTimer = HighResolutionTimer::CreateRepeating(interval, cb);
    Sleep(wait_time);
    pTimer.reset();
    auto duration = std::chrono::steady_clock::now() - start;

    // Should have run as many times as wait_time / timer_interval plus or
    // minus 1 due to imprecision of Sleep
    REQUIRE(duration.count() >= wait_time.count());
    auto ratio = static_cast<uint64_t>(duration / interval);
    REQUIRE(counter >= ratio - 1);
    REQUIRE(counter <= ratio + 1);
  }

  // Test concurrent timers
  {
    const auto interval1 = 100ms;
    const auto interval2 = 200ms;
    std::atomic<uint64_t> counter1(0);
    std::atomic<uint64_t> counter2(0);
    auto start = std::chrono::steady_clock::now();
    auto cb1 = [&counter1, timer_thread] {
      ++counter1;
      REQUIRE(Thread::GetCurrentThread() == timer_thread);
    };
    auto cb2 = [&counter2, timer_thread] {
      ++counter2;
      REQUIRE(Thread::GetCurrentThread() == timer_thread);
    };
    auto pTimer1 = HighResolutionTimer::CreateRepeating(interval1, cb1);
    auto pTimer2 = HighResolutionTimer::CreateRepeating(interval2, cb2);
    Sleep(wait_time);
    pTimer1.reset();
    pTimer2.reset();
    auto duration = std::chrono::steady_clock::now() - start;

    // Should have run as many times as wait_time / timer_interval plus or
    // minus 1 due to imprecision of Sleep
    REQUIRE(duration.count() >= wait_time.count());
    auto ratio1 = static_cast<uint64_t>(duration / interval1);
    auto ratio2 = static_cast<uint64_t>(duration / interval2);
    REQUIRE(counter1 >= ratio1 - 1);
    REQUIRE(counter1 <= ratio1 + 1);
    REQUIRE(counter2 >= ratio2 - 1);
    REQUIRE(counter2 <= ratio2 + 1);
  }

  // Test many timers
  {
    const auto interval = 50ms;
    const size_t timer_count = 128;
    std::atomic<uint64_t> counter(0);
    auto cb = [&counter, &timer_thread] {
      ++counter;
      REQUIRE(Thread::GetCurrentThread() == timer_thread);
    };
    std::vector<std::unique_ptr<HighResolutionTimer>> timers;
    auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < timer_count; i++) {
      timers.emplace_back(HighResolutionTimer::CreateRepeating(interval, cb));
    }
    Sleep(wait_time);
    timers.clear();
    auto duration = std::chrono::steady_clock::now() - start;

    REQUIRE(duration.count() >= wait_time.count());
    auto ratio = static_cast<uint64_t>(timer_count * duration / interval);
    REQUIRE(counter >= ratio - timer_count);
    REQUIRE(counter <= ratio + timer_count);
  }

  // Check timer order
  {
    constexpr size_t timer_count = 16;
    using pair_t = std::pair<std::atomic<uint64_t>,
                             std::chrono::high_resolution_clock::time_point>;
    std::array<pair_t, timer_count> time_points{};
    auto start = std::chrono::steady_clock::now();
    auto gen_callback = [&timer_thread, &time_points](size_t i) {
      return [&timer_thread, &time_points, i]() {
        auto& pair = time_points[i];
        if (pair.first.fetch_add(1) == 1) {
          pair.second = std::chrono::high_resolution_clock::now();
          pair.first++;
        }
        REQUIRE(Thread::GetCurrentThread() == timer_thread);
      };
    };
    std::vector<std::unique_ptr<HighResolutionTimer>> timers;
    for (size_t i = 0; i < timer_count; i++) {
      timers.emplace_back(HighResolutionTimer::CreateRepeating(
          10ms * (timer_count - i), gen_callback(timer_count - i - 1)));
    }
    REQUIRE(spin_wait_for(2s, [&] {
      return std::all_of(time_points.cbegin(), time_points.cend(),
                         [](auto& pair) { return pair.first >= 3; });
    }));

    timers.clear();

    REQUIRE(std::is_sorted(
        time_points.cbegin(), time_points.cend(),
        [](auto& left, auto& right) { return left.second < right.second; }));
  }
}

TEST_CASE("Wait on Multiple Handles", "[wait]") {
  auto mutant = Mutant::Create(true);
  REQUIRE(mutant);
  auto semaphore = Semaphore::Create(10, 10);
  REQUIRE(semaphore);
  auto event_ = Event::CreateManualResetEvent(false);
  REQUIRE(event_);
  auto thread = Thread::Create({}, [&mutant, &semaphore, &event_] {
    event_->Set();
    Wait(mutant.get(), false, 25ms);
    semaphore->Release(1, nullptr);
    Wait(mutant.get(), false, 25ms);
    mutant->Release();
  });

  std::vector<WaitHandle*> handles = {
      mutant.get(),
      semaphore.get(),
      event_.get(),
      thread.get(),
  };

  auto any_result = WaitAny(handles, false, 100ms);
  REQUIRE(any_result.first == WaitResult::kSuccess);
  REQUIRE(any_result.second == 0);

  auto all_result = WaitAll(handles, false, 100ms);
  REQUIRE(all_result == WaitResult::kSuccess);
}

TEST_CASE("Signal and Wait") {
  WaitResult result;
  auto mutant = Mutant::Create(true);
  REQUIRE(mutant);
  auto event_ = Event::CreateAutoResetEvent(false);
  REQUIRE(event_);
  auto thread = Thread::Create({}, [&mutant, &event_] {
    Wait(mutant.get(), false);
    event_->Set();
  });
  result = Wait(event_.get(), false, 50ms);
  REQUIRE(result == WaitResult::kTimeout);
  result = SignalAndWait(mutant.get(), event_.get(), false, 50ms);
  REQUIRE(result == WaitResult::kSuccess);
  result = Wait(thread.get(), false, 50ms);
  REQUIRE(result == WaitResult::kSuccess);
}

TEST_CASE("Wait on Event", "[event]") {
  auto evt = Event::CreateAutoResetEvent(false);
  REQUIRE(evt);
  WaitResult result;

  // Call wait on unset Event
  result = Wait(evt.get(), false, 50ms);
  REQUIRE(result == WaitResult::kTimeout);

  // Call wait on set Event
  evt->Set();
  result = Wait(evt.get(), false, 50ms);
  REQUIRE(result == WaitResult::kSuccess);

  // Call wait on now consumed Event
  result = Wait(evt.get(), false, 50ms);
  REQUIRE(result == WaitResult::kTimeout);
}

TEST_CASE("Reset Event", "[event]") {
  auto evt = Event::CreateAutoResetEvent(false);
  REQUIRE(evt);
  WaitResult result;

  // Call wait on reset Event
  evt->Set();
  evt->Reset();
  result = Wait(evt.get(), false, 50ms);
  REQUIRE(result == WaitResult::kTimeout);

  // Test resetting the unset event
  evt->Reset();
  result = Wait(evt.get(), false, 50ms);
  REQUIRE(result == WaitResult::kTimeout);

  // Test setting the reset event
  evt->Set();
  result = Wait(evt.get(), false, 50ms);
  REQUIRE(result == WaitResult::kSuccess);
}

TEST_CASE("Wait on Multiple Events", "[event]") {
  auto events = std::array<std::unique_ptr<Event>, 4>{
      Event::CreateAutoResetEvent(false),
      Event::CreateAutoResetEvent(false),
      Event::CreateAutoResetEvent(false),
      Event::CreateManualResetEvent(false),
  };
  for (auto& event : events) {
    REQUIRE(event.get() != nullptr);
  }

  std::atomic_uint threads_started(0);
  std::array<char, 8> order = {0};
  std::atomic_uint index(0);
  auto sign_in = [&order, &index](uint32_t id) {
    auto i = index.fetch_add(1, std::memory_order_relaxed);
    order[i] = static_cast<char>('0' + id);
  };

  auto threads = std::array<std::thread, 4>{
      std::thread([&events, &sign_in, &threads_started] {
        set_name("1");
        threads_started++;
        auto res = WaitAll({events[1].get(), events[3].get()}, false);
        REQUIRE(res == WaitResult::kSuccess);
        if (res == WaitResult::kSuccess) {
          sign_in(1);
        }
      }),
      std::thread([&events, &sign_in, &threads_started] {
        set_name("2");
        threads_started++;
        auto res = WaitAny({events[0].get(), events[2].get()}, false);
        REQUIRE(res.first == WaitResult::kSuccess);
        if (res.first == WaitResult::kSuccess) {
          sign_in(2);
        }
      }),
      std::thread([&events, &sign_in, &threads_started] {
        set_name("3");
        threads_started++;
        auto res =
            WaitAll({events[0].get(), events[2].get(), events[3].get()}, false);
        REQUIRE(res == WaitResult::kSuccess);
        if (res == WaitResult::kSuccess) {
          sign_in(3);
        }
      }),
      std::thread([&events, &sign_in, &threads_started] {
        set_name("4");
        threads_started++;
        auto res = WaitAny({events[1].get(), events[3].get()}, false);
        REQUIRE(res.first == WaitResult::kSuccess);
        if (res.first == WaitResult::kSuccess) {
          sign_in(4);
        }
      }),
  };

  // wait for all threads starting up
  REQUIRE(spin_wait_for(1s, [&] { return threads_started == 4; }));
  events[3]->Set();  // Signals thread id=4 and stays on for 1 and 3
  REQUIRE(spin_wait_for(1s, [&] { return index == 1; }));
  events[1]->Set();  // Signals thread id=1
  REQUIRE(spin_wait_for(1s, [&] { return index == 2; }));
  events[0]->Set();  // Signals thread id=2
  REQUIRE(spin_wait_for(1s, [&] { return index == 3; }));
  events[2]->Set();  // Partial signals thread id=3
  events[0]->Set();  // Signals thread id=3

  for (auto& t : threads) {
    t.join();
  }

  REQUIRE(index == 4);

  INFO(order.data());
  REQUIRE(order[0] == '4');
  REQUIRE(order[1] == '1');
  REQUIRE(order[2] == '2');
  REQUIRE(order[3] == '3');
}

TEST_CASE("Wait on Semaphore", "[semaphore]") {
  WaitResult result;
  std::unique_ptr<Semaphore> sem;
  int previous_count = 0;

  // Wait on semaphore with no room
  sem = Semaphore::Create(0, 5);
  REQUIRE(sem);
  result = Wait(sem.get(), false, 10ms);
  REQUIRE(result == WaitResult::kTimeout);

  // Add room in semaphore
  REQUIRE(sem->Release(2, &previous_count));
  REQUIRE(previous_count == 0);
  REQUIRE(sem->Release(1, &previous_count));
  REQUIRE(previous_count == 2);
  result = Wait(sem.get(), false, 10ms);
  REQUIRE(result == WaitResult::kSuccess);
  REQUIRE(sem->Release(1, &previous_count));
  REQUIRE(previous_count == 2);

  // Set semaphore over maximum_count
  sem = Semaphore::Create(5, 5);
  REQUIRE(sem);
  previous_count = -1;
  REQUIRE_FALSE(sem->Release(1, &previous_count));
  REQUIRE(previous_count == -1);
  REQUIRE_FALSE(sem->Release(10, &previous_count));
  REQUIRE(previous_count == -1);
  sem = Semaphore::Create(0, 5);
  REQUIRE(sem);
  REQUIRE_FALSE(sem->Release(10, &previous_count));
  REQUIRE(previous_count == -1);
  REQUIRE_FALSE(sem->Release(10, &previous_count));
  REQUIRE(previous_count == -1);

  // Test invalid Release parameters
  REQUIRE_FALSE(sem->Release(0, &previous_count));
  REQUIRE(previous_count == -1);
  REQUIRE_FALSE(sem->Release(-1, &previous_count));
  REQUIRE(previous_count == -1);

  // Wait on fully available semaphore
  sem = Semaphore::Create(5, 5);
  REQUIRE(sem);
  result = Wait(sem.get(), false, 10ms);
  REQUIRE(result == WaitResult::kSuccess);
  result = Wait(sem.get(), false, 10ms);
  REQUIRE(result == WaitResult::kSuccess);
  result = Wait(sem.get(), false, 10ms);
  REQUIRE(result == WaitResult::kSuccess);
  result = Wait(sem.get(), false, 10ms);
  REQUIRE(result == WaitResult::kSuccess);
  result = Wait(sem.get(), false, 10ms);
  REQUIRE(result == WaitResult::kSuccess);
  result = Wait(sem.get(), false, 10ms);
  REQUIRE(result == WaitResult::kTimeout);

  // Semaphore between threads
  sem = Semaphore::Create(5, 5);
  REQUIRE(sem);
  // Occupy the semaphore with 5 threads
  std::atomic<int> wait_count(0);
  std::atomic<bool> threads_terminate(false);
  auto func = [&sem, &wait_count, &threads_terminate] {
    auto res = Wait(sem.get(), false, 100ms);
    wait_count++;

    REQUIRE(spin_wait_for(2s, [&] { return threads_terminate.load(); }));

    REQUIRE(res == WaitResult::kSuccess);
    sem->Release(1, nullptr);
  };
  auto threads = std::array<std::thread, 5>{
      std::thread(func), std::thread(func), std::thread(func),
      std::thread(func), std::thread(func),
  };
  // Wait for threads to finish semaphore calls
  REQUIRE(spin_wait_for(1s, [&] { return wait_count == 5; }));
  // Attempt to acquire full semaphore with current (6th) thread
  result = Wait(sem.get(), false, 20ms);
  REQUIRE(result == WaitResult::kTimeout);
  // Give threads time to release semaphore
  threads_terminate = true;
  for (auto& t : threads) {
    t.join();
  }
  result = Wait(sem.get(), false, 10ms);
  REQUIRE(result == WaitResult::kSuccess);
  sem->Release(1, &previous_count);
  REQUIRE(previous_count == 4);
}

TEST_CASE("Invalid semaphore parameters", "[semaphore]") {
  std::unique_ptr<Semaphore> sem;

  // Test invalid construction parameters
  // These are invalid according to documentation
  sem = Semaphore::Create(-1, 5);
  REQUIRE(sem == nullptr);
  sem = Semaphore::Create(10, 5);
  REQUIRE(sem == nullptr);
  sem = Semaphore::Create(0, 0);
  REQUIRE(sem == nullptr);
  sem = Semaphore::Create(0, -1);
  REQUIRE(sem == nullptr);
  sem = Semaphore::Create(-1, 0);
  REQUIRE(sem == nullptr);
}

TEST_CASE("Wait on Multiple Semaphores", "[semaphore]") {
  WaitResult all_result;
  std::pair<WaitResult, size_t> any_result;
  int previous_count;
  std::unique_ptr<Semaphore> sem0, sem1;

  // Test Wait all which should fail
  sem0 = Semaphore::Create(0, 5);
  sem1 = Semaphore::Create(5, 5);
  REQUIRE(sem0);
  REQUIRE(sem1);
  all_result = WaitAll({sem0.get(), sem1.get()}, false, 10ms);
  REQUIRE(all_result == WaitResult::kTimeout);
  previous_count = -1;
  REQUIRE(sem0->Release(1, &previous_count));
  REQUIRE(previous_count == 0);
  previous_count = -1;
  REQUIRE_FALSE(sem1->Release(1, &previous_count));
  REQUIRE(previous_count == -1);

  // Test Wait all again which should succeed
  sem0 = Semaphore::Create(1, 5);
  sem1 = Semaphore::Create(5, 5);
  REQUIRE(sem0);
  REQUIRE(sem1);
  all_result = WaitAll({sem0.get(), sem1.get()}, false, 10ms);
  REQUIRE(all_result == WaitResult::kSuccess);
  previous_count = -1;
  REQUIRE(sem0->Release(1, &previous_count));
  REQUIRE(previous_count == 0);
  previous_count = -1;
  REQUIRE(sem1->Release(1, &previous_count));
  REQUIRE(previous_count == 4);

  // Test Wait Any which should fail
  sem0 = Semaphore::Create(0, 5);
  sem1 = Semaphore::Create(0, 5);
  REQUIRE(sem0);
  REQUIRE(sem1);
  any_result = WaitAny({sem0.get(), sem1.get()}, false, 10ms);
  REQUIRE(any_result.first == WaitResult::kTimeout);
  REQUIRE(any_result.second == 0);
  previous_count = -1;
  REQUIRE(sem0->Release(1, &previous_count));
  REQUIRE(previous_count == 0);
  previous_count = -1;
  REQUIRE(sem1->Release(1, &previous_count));
  REQUIRE(previous_count == 0);

  // Test Wait Any which should succeed
  sem0 = Semaphore::Create(0, 5);
  sem1 = Semaphore::Create(5, 5);
  REQUIRE(sem0);
  REQUIRE(sem1);
  any_result = WaitAny({sem0.get(), sem1.get()}, false, 10ms);
  REQUIRE(any_result.first == WaitResult::kSuccess);
  REQUIRE(any_result.second == 1);
  previous_count = -1;
  REQUIRE(sem0->Release(1, &previous_count));
  REQUIRE(previous_count == 0);
  previous_count = -1;
  REQUIRE(sem1->Release(1, &previous_count));
  REQUIRE(previous_count == 4);
}

TEST_CASE("Wait on Mutant", "[mutant]") {
  WaitResult result;
  std::unique_ptr<Mutant> mut;

  // Release on initially owned mutant
  mut = Mutant::Create(true);
  REQUIRE(mut->Release());
  REQUIRE_FALSE(mut->Release());

  // Release on initially not-owned mutant
  mut = Mutant::Create(false);
  REQUIRE_FALSE(mut->Release());

  // Wait on initially owned mutant
  mut = Mutant::Create(true);
  result = Wait(mut.get(), false, 1ms);
  REQUIRE(result == WaitResult::kSuccess);
  REQUIRE(mut->Release());
  REQUIRE(mut->Release());
  REQUIRE_FALSE(mut->Release());

  // Wait on initially not owned mutant
  mut = Mutant::Create(false);
  result = Wait(mut.get(), false, 1ms);
  REQUIRE(result == WaitResult::kSuccess);
  REQUIRE(mut->Release());
  REQUIRE_FALSE(mut->Release());

  // Multiple waits (or locks)
  mut = Mutant::Create(false);
  for (int i = 0; i < 10; ++i) {
    result = Wait(mut.get(), false, 1ms);
    REQUIRE(result == WaitResult::kSuccess);
  }
  for (int i = 0; i < 10; ++i) {
    REQUIRE(mut->Release());
  }
  REQUIRE_FALSE(mut->Release());

  // Test mutants on other threads
  std::atomic<unsigned int> step(0);
  auto thread1 = std::thread([&mut, &step] {
    mut = Mutant::Create(true);
    step++;  // 1
    REQUIRE(spin_wait_for(2s, [&] { return step == 2; }));
    mut->Release();
  });
  REQUIRE(spin_wait_for(1s, [&] { return step == 1; }));
  REQUIRE_FALSE(mut->Release());
  result = Wait(mut.get(), false, 50ms);
  REQUIRE(result == WaitResult::kTimeout);
  step++;  // 2
  thread1.join();
  result = Wait(mut.get(), false, 1ms);
  REQUIRE(result == WaitResult::kSuccess);
  REQUIRE(mut->Release());
}

TEST_CASE("Wait on Multiple Mutants", "[mutant]") {
  WaitResult all_result;
  std::pair<WaitResult, size_t> any_result;
  std::unique_ptr<Mutant> mut0, mut1;
  std::atomic<unsigned int> step(0);

  // Test which should fail for WaitAll and WaitAny
  auto thread0 = std::thread([&mut0, &mut1, &step] {
    mut0 = Mutant::Create(true);
    mut1 = Mutant::Create(true);
    step++;  // 1
    REQUIRE(spin_wait_for(2s, [&] { return step == 2; }));
    mut0->Release();
    mut1->Release();
  });
  REQUIRE(spin_wait_for(1s, [&] { return step == 1; }));
  all_result = WaitAll({mut0.get(), mut1.get()}, false, 10ms);
  REQUIRE(all_result == WaitResult::kTimeout);
  REQUIRE_FALSE(mut0->Release());
  REQUIRE_FALSE(mut1->Release());
  any_result = WaitAny({mut0.get(), mut1.get()}, false, 10ms);
  REQUIRE(any_result.first == WaitResult::kTimeout);
  REQUIRE(any_result.second == 0);
  REQUIRE_FALSE(mut0->Release());
  REQUIRE_FALSE(mut1->Release());
  step++;  // 2
  thread0.join();

  // Test which should fail for WaitAll but not WaitAny
  step = 0;
  auto thread1 = std::thread([&mut0, &mut1, &step] {
    mut0 = Mutant::Create(true);
    mut1 = Mutant::Create(false);
    step++;  // 1
    REQUIRE(spin_wait_for(2s, [&] { return step == 2; }));
    mut0->Release();
  });
  REQUIRE(spin_wait_for(1s, [&] { return step == 1; }));
  all_result = WaitAll({mut0.get(), mut1.get()}, false, 10ms);
  REQUIRE(all_result == WaitResult::kTimeout);
  REQUIRE_FALSE(mut0->Release());
  REQUIRE_FALSE(mut1->Release());
  any_result = WaitAny({mut0.get(), mut1.get()}, false, 10ms);
  REQUIRE(any_result.first == WaitResult::kSuccess);
  REQUIRE(any_result.second == 1);
  REQUIRE_FALSE(mut0->Release());
  REQUIRE(mut1->Release());
  step++;  // 2
  thread1.join();

  // Test which should pass for WaitAll and WaitAny
  step = 0;
  auto thread2 = std::thread([&mut0, &mut1, &step] {
    mut0 = Mutant::Create(false);
    mut1 = Mutant::Create(false);
    step++;  // 1
    REQUIRE(spin_wait_for(2s, [&] { return step == 2; }));
  });
  REQUIRE(spin_wait_for(1s, [&] { return step == 1; }));
  all_result = WaitAll({mut0.get(), mut1.get()}, false, 10ms);
  REQUIRE(all_result == WaitResult::kSuccess);
  REQUIRE(mut0->Release());
  REQUIRE(mut1->Release());
  any_result = WaitAny({mut0.get(), mut1.get()}, false, 10ms);
  REQUIRE(any_result.first == WaitResult::kSuccess);
  REQUIRE(any_result.second == 0);
  REQUIRE(mut0->Release());
  REQUIRE_FALSE(mut1->Release());
  step++;  // 2
  thread2.join();
}

TEST_CASE("Wait on Timer", "[timer]") {
  WaitResult result;
  std::unique_ptr<Timer> timer;

  // Test Manual Reset
  timer = Timer::CreateManualResetTimer();
  REQUIRE(timer);
  result = Wait(timer.get(), false, 1ms);
  REQUIRE(result == WaitResult::kTimeout);
  REQUIRE(timer->SetOnceAfter(1ms));  // Signals it
  result = Wait(timer.get(), false, 20ms);
  REQUIRE(result == WaitResult::kSuccess);
  result = Wait(timer.get(), false, 1ms);
  REQUIRE(result == WaitResult::kSuccess);  // Did not reset

  // Test Synchronization
  timer = Timer::CreateSynchronizationTimer();
  REQUIRE(timer);
  result = Wait(timer.get(), false, 1ms);
  REQUIRE(result == WaitResult::kTimeout);
  REQUIRE(timer->SetOnceAfter(1ms));  // Signals it
  result = Wait(timer.get(), false, 20ms);
  REQUIRE(result == WaitResult::kSuccess);
  result = Wait(timer.get(), false, 1ms);
  REQUIRE(result == WaitResult::kTimeout);  // Did reset

  // Test long due time
  timer = Timer::CreateSynchronizationTimer();
  REQUIRE(timer->SetOnceAfter(10s));
  result = Wait(timer.get(), false, 10ms);
  REQUIRE(result == WaitResult::kTimeout);

  // Test Repeating
  REQUIRE(timer->SetRepeatingAfter(1ms, 10ms));
  for (int i = 0; i < 10; ++i) {
    result = Wait(timer.get(), false, 20ms);
    INFO(i);
    REQUIRE(result == WaitResult::kSuccess);
  }
  MaybeYield();
  Sleep(10ms);  // Skip a few events
  for (int i = 0; i < 10; ++i) {
    result = Wait(timer.get(), false, 20ms);
    REQUIRE(result == WaitResult::kSuccess);
  }
  // Cancel it
  timer->Cancel();
  result = Wait(timer.get(), false, 20ms);
  REQUIRE(result == WaitResult::kTimeout);
  MaybeYield();
  Sleep(10ms);  // Skip a few events
  result = Wait(timer.get(), false, 20ms);
  REQUIRE(result == WaitResult::kTimeout);
  // Cancel with SetOnce
  REQUIRE(timer->SetRepeatingAfter(1ms, 10ms));
  for (int i = 0; i < 10; ++i) {
    result = Wait(timer.get(), false, 20ms);
    REQUIRE(result == WaitResult::kSuccess);
  }
  REQUIRE(timer->SetOnceAfter(1ms));
  result = Wait(timer.get(), false, 20ms);
  REQUIRE(result == WaitResult::kSuccess);  // Signal from Set Once
  result = Wait(timer.get(), false, 20ms);
  REQUIRE(result == WaitResult::kTimeout);  // No more signals from repeating
}

TEST_CASE("Wait on Multiple Timers", "[timer]") {
  WaitResult all_result;
  std::pair<WaitResult, size_t> any_result;

  auto timer0 = Timer::CreateSynchronizationTimer();
  auto timer1 = Timer::CreateManualResetTimer();

  // None signaled
  all_result = WaitAll({timer0.get(), timer1.get()}, false, 1ms);
  REQUIRE(all_result == WaitResult::kTimeout);
  any_result = WaitAny({timer0.get(), timer1.get()}, false, 1ms);
  REQUIRE(any_result.first == WaitResult::kTimeout);
  REQUIRE(any_result.second == 0);

  // Some signaled
  REQUIRE(timer1->SetOnceAfter(1ms));
  all_result = WaitAll({timer0.get(), timer1.get()}, false, 100ms);
  REQUIRE(all_result == WaitResult::kTimeout);
  any_result = WaitAny({timer0.get(), timer1.get()}, false, 100ms);
  REQUIRE(any_result.first == WaitResult::kSuccess);
  REQUIRE(any_result.second == 1);

  // All signaled
  REQUIRE(timer0->SetOnceAfter(1ms));
  all_result = WaitAll({timer0.get(), timer1.get()}, false, 100ms);
  REQUIRE(all_result == WaitResult::kSuccess);
  REQUIRE(timer0->SetOnceAfter(1ms));
  Sleep(2ms);
  any_result = WaitAny({timer0.get(), timer1.get()}, false, 100ms);
  REQUIRE(any_result.first == WaitResult::kSuccess);
  REQUIRE(any_result.second == 0);

  // Check that timer0 reset
  any_result = WaitAny({timer0.get(), timer1.get()}, false, 100ms);
  REQUIRE(any_result.first == WaitResult::kSuccess);
  REQUIRE(any_result.second == 1);
}

TEST_CASE("Create and Trigger Timer Callbacks", "[timer]") {
  // TODO(bwrsandman): Check which thread performs callback and timing of
  // callback
  REQUIRE(true);
}

TEST_CASE("Set and Test Current Thread ID", "[thread]") {
  // System ID
  auto system_id = current_thread_system_id();
  REQUIRE(system_id > 0);

  // Thread ID
  auto thread_id = current_thread_id();
  REQUIRE(thread_id == system_id);

  // Set a new thread id
  const uint32_t new_thread_id = 0xDEADBEEF;
  set_current_thread_id(new_thread_id);
  REQUIRE(current_thread_id() == new_thread_id);

  // Set back original thread id of system
  set_current_thread_id(std::numeric_limits<uint32_t>::max());
  REQUIRE(current_thread_id() == system_id);

  // TODO(bwrsandman): Test on Thread object
}

TEST_CASE("Set and Test Current Thread Name", "[thread]") {
  auto current_thread = Thread::GetCurrentThread();
  REQUIRE(current_thread);
  auto old_thread_name = current_thread->name();

  std::string new_thread_name = "Threading Test";
  REQUIRE_NOTHROW(set_name(new_thread_name));

  // Restore the old catch.hpp thread name
  REQUIRE_NOTHROW(set_name(old_thread_name));
}

TEST_CASE("Create and Run Thread", "[thread]") {
  std::unique_ptr<Thread> thread;
  WaitResult result;
  Thread::CreationParameters params = {};
  std::atomic<unsigned int> fence(0);
  auto func = [&fence] {
    REQUIRE(spin_wait_for(1s, [&] { return fence == 1; }));
    fence++;
  };

  SECTION("Create most basic case of thread") {
    fence = 0;
    thread = Thread::Create(params, func);
    REQUIRE(thread->native_handle() != nullptr);
    REQUIRE_NOTHROW(thread->affinity_mask());
    REQUIRE(thread->name().empty());
    fence++;
    result = Wait(thread.get(), false, 1s);
    REQUIRE(result == WaitResult::kSuccess);
  }

  SECTION("Add thread name") {
    fence = 0;
    std::string new_name = "Test thread name";
    thread = Thread::Create(params, func);
    auto name = thread->name();
    INFO(name.c_str());
    REQUIRE(name.empty());
    thread->set_name(new_name);
    REQUIRE(thread->name() == new_name);
    fence++;
    result = Wait(thread.get(), false, 1s);
    REQUIRE(result == WaitResult::kSuccess);
  }

  SECTION("Use Terminate to end an infinitely looping thread") {
    thread = Thread::Create(params, [] {
      while (true) {
        Sleep(1ms);
      }
    });
    result = Wait(thread.get(), false, 1s);
    REQUIRE(result == WaitResult::kTimeout);
    thread->Terminate(-1);
    result = Wait(thread.get(), false, 1s);
    REQUIRE(result == WaitResult::kSuccess);
  }

  SECTION("Call Exit from inside an infinitely looping thread") {
    thread = Thread::Create(params, [] {
      Thread::Exit(-1);
      FAIL("Function must not return");
      while (true);
    });
    result = Wait(thread.get(), false, 1s);
    REQUIRE(result == WaitResult::kSuccess);
  }

  SECTION("Call timeout wait on self") {
    result = Wait(Thread::GetCurrentThread(), false, 50ms);
    REQUIRE(result == WaitResult::kTimeout);
  }

  SECTION("16Mb stack size") {
    params.stack_size = 16_MiB;
    thread = Thread::Create(params, [] {
      Thread::Exit(-1);
      FAIL("Function must not return");
      while (true);
    });
    REQUIRE(thread != nullptr);
    result = Wait(thread.get(), false, 1s);
    REQUIRE(result == WaitResult::kSuccess);
  }

  // TODO(bwrsandman): Test with different priorities
  // TODO(bwrsandman): Test setting and getting thread affinity
}

TEST_CASE("Test Suspending Thread", "[thread]") {
  std::unique_ptr<Thread> thread;
  WaitResult result;
  Thread::CreationParameters params = {};
  auto func = [] { Sleep(20ms); };

  // Create initially suspended
  params.create_suspended = true;
  thread = threading::Thread::Create(params, func);
  result = threading::Wait(thread.get(), false, 50ms);
  REQUIRE(result == threading::WaitResult::kTimeout);
  thread->Resume();
  result = threading::Wait(thread.get(), false, 50ms);
  REQUIRE(result == threading::WaitResult::kSuccess);
  params.create_suspended = false;

  // Create and then suspend
  thread = threading::Thread::Create(params, func);
  thread->Suspend();
  result = threading::Wait(thread.get(), false, 50ms);
  REQUIRE(result == threading::WaitResult::kTimeout);
  thread->Resume();
  result = threading::Wait(thread.get(), false, 50ms);
  REQUIRE(result == threading::WaitResult::kSuccess);

  // Test recursive suspend
  thread = threading::Thread::Create(params, func);
  thread->Suspend();
  thread->Suspend();
  result = threading::Wait(thread.get(), false, 50ms);
  REQUIRE(result == threading::WaitResult::kTimeout);
  thread->Resume();
  result = threading::Wait(thread.get(), false, 50ms);
  REQUIRE(result == threading::WaitResult::kTimeout);
  thread->Resume();
  result = threading::Wait(thread.get(), false, 50ms);
  REQUIRE(result == threading::WaitResult::kSuccess);

  // Test suspend count
  uint32_t suspend_count = 0;
  thread = threading::Thread::Create(params, func);
  thread->Suspend(&suspend_count);
  REQUIRE(suspend_count == 0);
  thread->Suspend(&suspend_count);
  REQUIRE(suspend_count == 1);
  thread->Suspend(&suspend_count);
  REQUIRE(suspend_count == 2);
  thread->Resume(&suspend_count);
  REQUIRE(suspend_count == 3);
  thread->Resume(&suspend_count);
  REQUIRE(suspend_count == 2);
  thread->Resume(&suspend_count);
  REQUIRE(suspend_count == 1);
  thread->Suspend(&suspend_count);
  REQUIRE(suspend_count == 0);
  thread->Resume(&suspend_count);
  REQUIRE(suspend_count == 1);
  result = threading::Wait(thread.get(), false, 50ms);
  REQUIRE(result == threading::WaitResult::kSuccess);
}

TEST_CASE("Test Thread QueueUserCallback", "[thread]") {
  std::unique_ptr<Thread> thread;
  WaitResult result;
  Thread::CreationParameters params = {};
  std::atomic_int order;
  int is_modified;
  int has_finished;
  auto callback = [&is_modified, &order] {
    is_modified =
        std::atomic_fetch_add_explicit(&order, 1, std::memory_order_relaxed);
  };

  // Without alertable
  order = 0;
  is_modified = -1;
  has_finished = -1;
  thread = Thread::Create(params, [&has_finished, &order] {
    // Not using Alertable so callback is not registered
    order++;  // 1
    Sleep(90ms);
    order++;  // 2
    has_finished =
        std::atomic_fetch_add_explicit(&order, 1, std::memory_order_relaxed);
  });
  REQUIRE(!spin_wait_for(50ms, [&] { return order == 2; }));
  REQUIRE(is_modified == -1);
  thread->QueueUserCallback(callback);
  result = Wait(thread.get(), true, 200ms);
  REQUIRE(result == WaitResult::kSuccess);
  REQUIRE(is_modified == -1);
  REQUIRE(has_finished == 2);

  // With alertable
  order = 0;
  is_modified = -1;
  has_finished = -1;
  thread = Thread::Create(params, [&has_finished, &order] {
    // Using Alertable so callback is registered
    order++;  // 1
    AlertableSleep(90ms);
    order++;  // 3
    has_finished =
        std::atomic_fetch_add_explicit(&order, 1, std::memory_order_relaxed);
  });
  REQUIRE(!spin_wait_for(50ms, [&] { return order == 2; }));
  REQUIRE(is_modified == -1);
  thread->QueueUserCallback(callback);
  result = Wait(thread.get(), true, 200ms);
  REQUIRE(result == WaitResult::kSuccess);
  REQUIRE(is_modified == 1);
  REQUIRE(has_finished == 3);

  // Test Exit command with QueueUserCallback
  order = 0;
  is_modified = -1;
  has_finished = -1;
  thread = Thread::Create(params, [&is_modified, &has_finished, &order] {
    is_modified =
        std::atomic_fetch_add_explicit(&order, 1, std::memory_order_relaxed);
    // Using Alertable so callback is registered
    order++;  // 2
    AlertableSleep(1s);
    FAIL("Thread should have been terminated during alertable sleep");
    while (true);
  });
  REQUIRE(!spin_wait_for(100ms, [&] { return order == 3; }));  // timeout
  thread->QueueUserCallback([] { Thread::Exit(0); });
  result = Wait(thread.get(), true, 500ms);
  REQUIRE(result == WaitResult::kSuccess);
  REQUIRE(is_modified == 0);
  REQUIRE(order == 2);

  // TODO(bwrsandman): Test alertable wait returning kUserCallback by using IO
  // callbacks.
}

}  // namespace test
}  // namespace base
}  // namespace xe
