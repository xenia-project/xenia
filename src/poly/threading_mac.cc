/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/threading.h>

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <time.h>

namespace poly {
namespace threading {

uint64_t ticks() { return mach_absolute_time(); }

uint32_t current_thread_id() {
  mach_port_t tid = pthread_mach_thread_np(pthread_self());
  return static_cast<uint32_t>(tid);
}

void Yield() { pthread_yield_np(); }

void Sleep(std::chrono::microseconds duration) {
  timespec rqtp = {duration.count() / 1000000, duration.count() % 1000};
  nanosleep(&rqtp, nullptr);
  // TODO(benvanik): spin while rmtp >0?
}

}  // namespace threading
}  // namespace poly
