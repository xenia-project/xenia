/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_MUTEX_H_
#define XENIA_CORE_MUTEX_H_

#include <xenia/common.h>


typedef struct xe_mutex xe_mutex_t;


xe_mutex_t* xe_mutex_alloc(uint32_t spin_count);
void xe_mutex_free(xe_mutex_t* mutex);

int xe_mutex_lock(xe_mutex_t* mutex);
int xe_mutex_trylock(xe_mutex_t* mutex);
int xe_mutex_unlock(xe_mutex_t* mutex);


#endif  // XENIA_CORE_MUTEX_H_
