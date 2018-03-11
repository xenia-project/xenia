/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2018 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "xenia/base/threading.h"

#include "third_party/catch/include/catch.hpp"

namespace xe {
namespace base {
namespace test {
using namespace threading;
using namespace std::chrono_literals;

TEST_CASE("Fence") {
  // TODO(bwrsandman):
  REQUIRE(true);
}

TEST_CASE("Get number of logical processors") {
  auto count = std::thread::hardware_concurrency();
  REQUIRE(logical_processor_count() == count);
  REQUIRE(logical_processor_count() == count);
  REQUIRE(logical_processor_count() == count);
}

TEST_CASE("Enable process to set thread affinity") {
  EnableAffinityConfiguration();
}

TEST_CASE("Yield Current Thread", "MaybeYield") {
  // Run to see if there are any errors
  MaybeYield();
}

TEST_CASE("Sync with Memory Barrier", "SyncMemory") {
  // Run to see if there are any errors
  SyncMemory();
}

TEST_CASE("Sleep Current Thread", "Sleep") {
  auto wait_time = 50ms;
  auto start = std::chrono::steady_clock::now();
  Sleep(wait_time);
  auto duration = std::chrono::steady_clock::now() - start;
  REQUIRE(duration >= wait_time);
}

TEST_CASE("Sleep Current Thread in Alertable State", "Sleep") {
  // TODO(bwrsandman):
  REQUIRE(true);
}

TEST_CASE("TlsHandle") {
  // TODO(bwrsandman):
  REQUIRE(true);
}

TEST_CASE("HighResolutionTimer") {
  // TODO(bwrsandman):
  REQUIRE(true);
}

TEST_CASE("Wait on Multiple Handles", "Wait") {
  // TODO(bwrsandman):
  REQUIRE(true);
}

TEST_CASE("Signal and Wait") {
  // TODO(bwrsandman): Test semaphore, mutex and event
  REQUIRE(true);
}

TEST_CASE("Wait on Event", "Event") {
  // TODO(bwrsandman):
  REQUIRE(true);
}

TEST_CASE("Wait on Semaphore", "Semaphore") {
  // TODO(bwrsandman):
  REQUIRE(true);
}

TEST_CASE("Wait on Mutant", "Mutant") {
  // TODO(bwrsandman):
  REQUIRE(true);
}

TEST_CASE("Create and Trigger Timer", "Timer") {
  // TODO(bwrsandman):
  REQUIRE(true);
}

TEST_CASE("Set and Test Current Thread ID", "Thread") {
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

TEST_CASE("Set and Test Current Thread Name", "Thread") {
  std::string new_thread_name = "Threading Test";
  set_name(new_thread_name);
}

TEST_CASE("Create and Run Thread", "Thread") {
  // TODO(bwrsandman):
  REQUIRE(true);
}

}  // namespace test
}  // namespace base
}  // namespace xe
