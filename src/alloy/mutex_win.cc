/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/mutex.h>

using namespace alloy;


namespace alloy {
  struct Mutex_t {
    CRITICAL_SECTION value;
  };
}  // namespace alloy


Mutex* alloy::AllocMutex(uint32_t spin_count) {
  Mutex* mutex = (Mutex*)calloc(1, sizeof(Mutex));

  if (spin_count) {
    InitializeCriticalSectionAndSpinCount(&mutex->value, spin_count);
  } else {
    InitializeCriticalSection(&mutex->value);
  }

  return mutex;
}

void alloy::FreeMutex(Mutex* mutex) {
  DeleteCriticalSection(&mutex->value);
  free(mutex);
}

int alloy::LockMutex(Mutex* mutex) {
  EnterCriticalSection(&mutex->value);
  return 0;
}

int alloy::TryLockMutex(Mutex* mutex) {
  if (TryEnterCriticalSection(&mutex->value) == TRUE) {
    return 0;
  } else {
    return 1;
  }
}

int alloy::UnlockMutex(Mutex* mutex) {
  LeaveCriticalSection(&mutex->value);
  return 0;
}
