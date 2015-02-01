/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "poly/mapped_memory.h"

#include <Windows.h>

namespace poly {

class Win32MappedMemory : public MappedMemory {
 public:
  Win32MappedMemory(const std::wstring& path, Mode mode)
      : MappedMemory(path, mode),
        file_handle(nullptr),
        mapping_handle(nullptr) {}

  ~Win32MappedMemory() override {
    if (data_) {
      UnmapViewOfFile(data_);
    }
    if (mapping_handle) {
      CloseHandle(mapping_handle);
    }
    if (file_handle) {
      CloseHandle(file_handle);
    }
  }

  HANDLE file_handle;
  HANDLE mapping_handle;
};

std::unique_ptr<MappedMemory> MappedMemory::Open(const std::wstring& path,
                                                 Mode mode, size_t offset,
                                                 size_t length) {
  DWORD file_access = 0;
  DWORD file_share = 0;
  DWORD create_mode = 0;
  DWORD mapping_protect = 0;
  DWORD view_access = 0;
  switch (mode) {
    case Mode::READ:
      file_access |= GENERIC_READ;
      file_share |= FILE_SHARE_READ;
      create_mode |= OPEN_EXISTING;
      mapping_protect |= PAGE_READONLY;
      view_access |= FILE_MAP_READ;
      break;
    case Mode::READ_WRITE:
      file_access |= GENERIC_READ | GENERIC_WRITE;
      file_share |= 0;
      create_mode |= OPEN_EXISTING;
      mapping_protect |= PAGE_READWRITE;
      view_access |= FILE_MAP_READ | FILE_MAP_WRITE;
      break;
  }

  SYSTEM_INFO systemInfo;
  GetSystemInfo(&systemInfo);

  const size_t aligned_offset =
      offset & ~static_cast<size_t>(systemInfo.dwAllocationGranularity - 1);
  const size_t aligned_length = length + (offset - aligned_offset);

  auto mm = std::make_unique<Win32MappedMemory>(path, mode);

  mm->file_handle = CreateFile(path.c_str(), file_access, file_share, nullptr,
                               create_mode, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (!mm->file_handle) {
    return nullptr;
  }

  mm->mapping_handle = CreateFileMapping(mm->file_handle, nullptr,
                                         mapping_protect, 0, 0, nullptr);
  //(DWORD)(aligned_length >> 32), (DWORD)(aligned_length & 0xFFFFFFFF), NULL);
  if (!mm->mapping_handle) {
    return nullptr;
  }

  mm->data_ = reinterpret_cast<uint8_t*>(MapViewOfFile(
      mm->mapping_handle, view_access, static_cast<DWORD>(aligned_offset >> 32),
      static_cast<DWORD>(aligned_offset & 0xFFFFFFFF), aligned_length));
  if (!mm->data_) {
    return nullptr;
  }

  if (length) {
    mm->size_ = aligned_length;
  } else {
    DWORD length_high;
    size_t map_length = GetFileSize(mm->file_handle, &length_high);
    map_length |= static_cast<uint64_t>(length_high) << 32;
    mm->size_ = map_length - aligned_offset;
  }

  return std::move(mm);
}

}  // namespace poly
