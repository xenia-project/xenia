/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/memory.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace xe {
namespace memory {

size_t page_size() { return getpagesize(); }
size_t allocation_granularity() { return page_size(); }

uint32_t ToPosixProtectFlags(PageAccess access) {
  switch (access) {
    case PageAccess::kNoAccess:
      return PROT_NONE;
    case PageAccess::kReadOnly:
      return PROT_READ;
    case PageAccess::kReadWrite:
      return PROT_READ | PROT_WRITE;
    case PageAccess::kExecuteReadWrite:
      return PROT_READ | PROT_WRITE | PROT_EXEC;
    default:
      assert_unhandled_case(access);
      return PROT_NONE;
  }
}

void* AllocFixed(void* base_address, size_t length,
                 AllocationType allocation_type, PageAccess access) {
  // mmap does not support reserve / commit, so ignore allocation_type.
  uint32_t prot = ToPosixProtectFlags(access);
  void* result = mmap(base_address, length, prot,
                      MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
  if (result == MAP_FAILED) {
    return nullptr;
  } else {
    return result;
  }
}

bool DeallocFixed(void* base_address, size_t length,
                  DeallocationType deallocation_type) {
  return munmap(base_address, length) == 0;
}

bool Protect(void* base_address, size_t length, PageAccess access,
             PageAccess* out_old_access) {
  // Linux does not have a syscall to query memory permissions.
  assert_null(out_old_access);

  uint32_t prot = ToPosixProtectFlags(access);
  return mprotect(base_address, length, prot) == 0;
}

bool QueryProtect(void* base_address, size_t& length, PageAccess& access_out) {
  return false;
}

FileMappingHandle CreateFileMappingHandle(std::wstring path, size_t length,
                                          PageAccess access, bool commit) {
  int fd = open("/dev/zero", O_RDWR);
  if (fd == -1) {
    return nullptr;
  } else {
    return reinterpret_cast<void*>(static_cast<int64_t>(fd));
  }
}

void CloseFileMappingHandle(FileMappingHandle handle) {
  close(static_cast<int>(reinterpret_cast<int64_t>(handle)));
}

void* MapFileView(FileMappingHandle handle, void* base_address, size_t length,
                  PageAccess access, size_t file_offset) {
  int fd = (int)(long long)handle;
  uint32_t prot = ToPosixProtectFlags(access);
  void* addr = mmap(base_address, length, prot, MAP_PRIVATE | MAP_FIXED, fd,
                    file_offset);
  if (addr == MAP_FAILED) {
    return nullptr;
  } else {
    return addr;
  }
}

bool UnmapFileView(FileMappingHandle handle, void* base_address,
                   size_t length) {
  return munmap(base_address, length) == 0;
}

}  // namespace memory
}  // namespace xe
