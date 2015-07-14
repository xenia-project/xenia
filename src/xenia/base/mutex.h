/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MUTEX_H_
#define XENIA_BASE_MUTEX_H_

#include <mutex>

namespace xe {

// This exists to allow us to swap the mutex implementation with one that
// we can mess with in the debugger (such as allowing the debugger to ignore
// locks while all threads are suspended). std::mutex should not be used.

using mutex = std::mutex;
using recursive_mutex = std::recursive_mutex;

}  // namespace xe

#endif  // XENIA_BASE_MUTEX_H_
