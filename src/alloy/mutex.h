/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_MUTEX_H_
#define ALLOY_MUTEX_H_

#include <alloy/core.h>


namespace alloy {


typedef struct Mutex_t Mutex;

Mutex* AllocMutex(uint32_t spin_count = 10000);
void FreeMutex(Mutex* mutex);

int LockMutex(Mutex* mutex);
int TryLockMutex(Mutex* mutex);
int UnlockMutex(Mutex* mutex);


}  // namespace alloy


#endif  // ALLOY_MUTEX_H_
