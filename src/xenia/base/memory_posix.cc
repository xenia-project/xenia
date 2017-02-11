/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/memory.h"

#include <unistd.h>

namespace xe {
namespace memory {

size_t page_size() { return getpagesize(); }
size_t allocation_granularity() { return page_size(); }

void* AllocFixed(void* base_address, size_t length,
                 AllocationType allocation_type, PageAccess access) {
  return nullptr;
}

bool DeallocFixed(void* base_address, size_t length,
                  DeallocationType deallocation_type) {
  return false;
}

bool Protect(void* base_address, size_t length, PageAccess access,
             PageAccess* out_old_access) {
  return false;
}

bool QueryProtect(void* base_address, size_t& length, PageAccess& access_out) {
  return false;
}

FileMappingHandle CreateFileMappingHandle(std::wstring path, size_t length,
                                          PageAccess access, bool commit) {
  return nullptr;
}

void CloseFileMappingHandle(FileMappingHandle handle) {}

void* MapFileView(FileMappingHandle handle, void* base_address, size_t length,
                  PageAccess access, size_t file_offset) {
  return nullptr;
}

bool UnmapFileView(FileMappingHandle handle, void* base_address,
                   size_t length) {
  return false;
}

}  // namespace memory
}  // namespace xe
