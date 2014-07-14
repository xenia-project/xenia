/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/memory.h>

#if !XE_LIKE_WIN32
#include <unistd.h>
#endif  // !XE_LIKE_WIN32

namespace poly {

size_t page_size() {
  static size_t value = 0;
  if (!value) {
#if XE_LIKE_WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    value = si.dwPageSize;
#else
    value = getpagesize();
#endif  // XE_LIKE_WIN32
  }
  return value;
}

}  // namespace poly
