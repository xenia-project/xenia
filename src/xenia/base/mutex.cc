/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/mutex.h"

namespace xe {
// chrispy: moved this out of body of function to eliminate the initialization
// guards
static std::recursive_mutex global_mutex;
std::recursive_mutex& global_critical_region::mutex() { return global_mutex; }

}  // namespace xe
