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
  pthread_mutex_t value;
};

xe_mutex_t* xe_mutex_alloc(uint32_t spin_count) {
  xe_mutex_t* mutex = (xe_mutex_t*)xe_calloc(sizeof(xe_mutex_t));

  int result = pthread_mutex_init(&mutex->value, NULL);
  switch (result) {
    case ENOMEM:
    case EINVAL:
      xe_free(mutex);
      return NULL;
  }

  return mutex;
}

void xe_mutex_free(xe_mutex_t* mutex) {
  int result = pthread_mutex_destroy(&mutex->value);
  switch (result) {
    case EBUSY:
    case EINVAL:
      break;
    default:
      break;
  }
  xe_free(mutex);
}

int xe_mutex_lock(xe_mutex_t* mutex) {
  return pthread_mutex_lock(&mutex->value) == EINVAL ? 1 : 0;
}

int xe_mutex_trylock(xe_mutex_t* mutex) {
  int result = pthread_mutex_trylock(&mutex->value);
  switch (result) {
    case EBUSY:
    case EINVAL:
      return 1;
    default:
      return 0;
  }
}

int xe_mutex_unlock(xe_mutex_t* mutex) {
  return pthread_mutex_unlock(&mutex->value) == EINVAL ? 1 : 0;
}
