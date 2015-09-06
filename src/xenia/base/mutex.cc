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

xe::recursive_mutex& global_critical_region::mutex() {
  static xe::recursive_mutex global_mutex;
  return global_mutex;
}

}  // namespace xe
