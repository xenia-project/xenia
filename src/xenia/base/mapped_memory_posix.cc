/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/mapped_memory.h"

#include <sys/mman.h>
#include <cstdio>

#include "xenia/base/string.h"

namespace xe {

class PosixMappedMemory : public MappedMemory {
 public:
  PosixMappedMemory(const std::wstring& path, Mode mode)
      : MappedMemory(path, mode), file_handle(nullptr) {}

  ~PosixMappedMemory() override {
    if (data_) {
      munmap(data_, size_);
    }
    if (file_handle) {
      fclose(file_handle);
    }
  }

  FILE* file_handle;
};

std::unique_ptr<MappedMemory> MappedMemory::Open(const std::wstring& path,
                                                 Mode mode, size_t offset,
                                                 size_t length) {
  const char* mode_str;
  int prot;
  switch (mode) {
    case Mode::kRead:
      mode_str = "rb";
      prot = PROT_READ;
      break;
    case Mode::kReadWrite:
      mode_str = "r+b";
      prot = PROT_READ | PROT_WRITE;
      break;
  }

  auto mm = std::make_unique<PosixMappedMemory>(path, mode);

  mm->file_handle = fopen(xe::to_string(path).c_str(), mode_str);
  if (!mm->file_handle) {
    return nullptr;
  }

  size_t map_length;
  map_length = length;
  if (!length) {
    fseeko(mm->file_handle, 0, SEEK_END);
    map_length = ftello(mm->file_handle);
    fseeko(mm->file_handle, 0, SEEK_SET);
  }
  mm->size_ = map_length;

  mm->data_ =
      mmap(0, map_length, prot, MAP_SHARED, fileno(mm->file_handle), offset);
  if (!mm->data_) {
    return nullptr;
  }

  return std::move(mm);
}

}  // namespace xe
