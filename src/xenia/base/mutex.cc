/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/mutex.h"
#if XE_PLATFORM_WIN32 == 1
#include "xenia/base/platform_win.h"
#endif

namespace xe {
#if XE_PLATFORM_WIN32 == 1 && XE_ENABLE_FAST_WIN32_MUTEX == 1
// default spincount for entercriticalsection is insane on windows, 0x20007D0i64
// (33556432 times!!) when a lock is highly contended performance degrades
// sharply on some processors todo: perhaps we should have a set of optional
// jobs that processors can do instead of spinning, for instance, sorting a list
// so we have better locality later or something
#define XE_CRIT_SPINCOUNT 128
/*
chrispy: todo, if a thread exits before releasing the global mutex we need to
check this and release the mutex one way to do this is by using FlsAlloc and
PFLS_CALLBACK_FUNCTION, which gets called with the fiber local data when a
thread exits
*/

static CRITICAL_SECTION* global_critical_section(xe_global_mutex* mutex) {
  return reinterpret_cast<CRITICAL_SECTION*>(mutex);
}

xe_global_mutex::xe_global_mutex() {
  InitializeCriticalSectionEx(global_critical_section(this), XE_CRIT_SPINCOUNT,
                              CRITICAL_SECTION_NO_DEBUG_INFO);
}
xe_global_mutex ::~xe_global_mutex() {
  DeleteCriticalSection(global_critical_section(this));
}

void xe_global_mutex::lock() {
  EnterCriticalSection(global_critical_section(this));
}
void xe_global_mutex::unlock() {
  LeaveCriticalSection(global_critical_section(this));
}
bool xe_global_mutex::try_lock() {
  BOOL success = TryEnterCriticalSection(global_critical_section(this));
  return success;
}

CRITICAL_SECTION* fast_crit(xe_fast_mutex* mutex) {
  return reinterpret_cast<CRITICAL_SECTION*>(mutex);
}
xe_fast_mutex::xe_fast_mutex() {
  InitializeCriticalSectionEx(fast_crit(this), XE_CRIT_SPINCOUNT,
                              CRITICAL_SECTION_NO_DEBUG_INFO);
}
xe_fast_mutex::~xe_fast_mutex() { DeleteCriticalSection(fast_crit(this)); }

void xe_fast_mutex::lock() { EnterCriticalSection(fast_crit(this)); }
void xe_fast_mutex::unlock() { LeaveCriticalSection(fast_crit(this)); }
bool xe_fast_mutex::try_lock() {
  return TryEnterCriticalSection(fast_crit(this));
}
#endif
// chrispy: moved this out of body of function to eliminate the initialization
// guards
static global_mutex_type global_mutex;
global_mutex_type& global_critical_region::mutex() { return global_mutex; }

}  // namespace xe
