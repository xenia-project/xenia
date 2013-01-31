/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/mutex.h>


struct xe_mutex {
  CRITICAL_SECTION value;
};

xe_mutex_t* xe_mutex_alloc(uint32_t spin_count) {
  xe_mutex_t* mutex = (xe_mutex_t*)xe_calloc(sizeof(xe_mutex_t));

  if (spin_count) {
    InitializeCriticalSectionAndSpinCount(&mutex->value, spin_count);
  } else {
    InitializeCriticalSection(&mutex->value);
  }

  return mutex;
}

void xe_mutex_free(xe_mutex_t* mutex) {
  DeleteCriticalSection(&mutex->value);
  xe_free(mutex);
}

int xe_mutex_lock(xe_mutex_t* mutex) {
  EnterCriticalSection(&mutex->value);
  return 0;
}

int xe_mutex_trylock(xe_mutex_t* mutex) {
  if (TryEnterCriticalSection(&mutex->value) == TRUE) {
    return 0;
  } else {
    return 1;
  }
}

int xe_mutex_unlock(xe_mutex_t* mutex) {
  LeaveCriticalSection(&mutex->value);
  return 0;
}
