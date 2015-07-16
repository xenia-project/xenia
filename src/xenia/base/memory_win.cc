/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/memory.h"

#include "xenia/base/platform_win.h"

namespace xe {
namespace memory {

size_t page_size() {
  static size_t value = 0;
  if (!value) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    value = si.dwAllocationGranularity;
  }
  return value;
}

bool Protect(void* base_address, size_t length, PageAccess access,
             PageAccess* out_old_access) {
  if (out_old_access) {
    *out_old_access = PageAccess::kNoAccess;
  }
  DWORD new_protect;
  switch (access) {
    case PageAccess::kNoAccess:
      new_protect = PAGE_NOACCESS;
      break;
    case PageAccess::kReadOnly:
      new_protect = PAGE_READONLY;
      break;
    case PageAccess::kReadWrite:
      new_protect = PAGE_READWRITE;
      break;
    default:
      assert_unhandled_case(access);
      break;
  }
  DWORD old_protect = 0;
  BOOL result = VirtualProtect(base_address, length, new_protect, &old_protect);
  if (result) {
    if (out_old_access) {
      switch (old_protect) {
        case PAGE_NOACCESS:
          *out_old_access = PageAccess::kNoAccess;
          break;
        case PAGE_READONLY:
          *out_old_access = PageAccess::kReadOnly;
          break;
        case PAGE_READWRITE:
          *out_old_access = PageAccess::kReadWrite;
          break;
        default:
          assert_unhandled_case(access);
          break;
      }
    }
    return true;
  } else {
    return false;
  }
}

}  // namespace memory
}  // namespace xe
