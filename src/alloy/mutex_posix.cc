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
    pthread_mutex_t value;
  };
}  // namespace alloy


Mutex* alloy::AllocMutex(uint32_t spin_count) {
  Mutex* mutex = (Mutex*)calloc(1, sizeof(Mutex));

  int result = pthread_mutex_init(&mutex->value, NULL);
  switch (result) {
    case ENOMEM:
    case EINVAL:
      xe_free(mutex);
      return NULL;
  }

  return mutex;
}

void alloy::FreeMutex(Mutex* mutex) {
  int result = pthread_mutex_destroy(&mutex->value);
  switch (result) {
    case EBUSY:
    case EINVAL:
      break;
    default:
      break;
  }
  free(mutex);
}

int alloy::LockMutex(Mutex* mutex) {
  return pthread_mutex_lock(&mutex->value) == EINVAL ? 1 : 0;
}

int alloy::TryLockMutex(Mutex* mutex) {
  int result = pthread_mutex_trylock(&mutex->value);
  switch (result) {
    case EBUSY:
    case EINVAL:
      return 1;
    default:
      return 0;
  }
}

int alloy::UnlockMutex(Mutex* mutex) {
  return pthread_mutex_unlock(&mutex->value) == EINVAL ? 1 : 0;
}
