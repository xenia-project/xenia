/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/mapped_memory.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>

#include "xenia/base/string.h"

namespace xe {

class PosixMappedMemory : public MappedMemory {
 public:
  PosixMappedMemory(const std::filesystem::path& path, Mode mode, void* data,
                    size_t size, int file_descriptor)
      : MappedMemory(path, mode, data, size),
        file_descriptor_(file_descriptor) {}

  ~PosixMappedMemory() override {
    if (data_) {
      munmap(data_, size());
    }
    if (file_descriptor_ >= 0) {
      close(file_descriptor_);
    }
  }

  void Close(uint64_t truncate_size) override {
    if (data_) {
      munmap(data_, size());
      data_ = nullptr;
    }
    if (file_descriptor_ >= 0) {
      if (truncate_size) {
        ftruncate64(file_descriptor_, off64_t(truncate_size));
      }
      close(file_descriptor_);
      file_descriptor_ = -1;
    }
  }

  void Flush() override { msync(data(), size(), MS_ASYNC); }

 private:
  int file_descriptor_;
};

std::unique_ptr<MappedMemory> MappedMemory::Open(
    const std::filesystem::path& path, Mode mode, size_t offset,
    size_t length) {
  int open_flags = 0;
  int prot;
  switch (mode) {
    case Mode::kRead:
      open_flags |= O_RDONLY;
      prot = PROT_READ;
      break;
    case Mode::kReadWrite:
      open_flags |= O_RDWR;
      prot = PROT_READ | PROT_WRITE;
      break;
  }

  int file_descriptor = open(path.c_str(), open_flags);
  if (file_descriptor < 0) {
    return nullptr;
  }

  size_t map_length = length;
  if (!length) {
    struct stat64 file_stat;
    if (fstat64(file_descriptor, &file_stat)) {
      close(file_descriptor);
      return nullptr;
    }
    map_length = size_t(file_stat.st_size);
  }

  void* data = mmap(0, map_length, prot, MAP_SHARED, file_descriptor, offset);
  if (!data) {
    close(file_descriptor);
    return nullptr;
  }

  return std::unique_ptr<MappedMemory>(
      new PosixMappedMemory(path, mode, data, map_length, file_descriptor));
}

std::unique_ptr<ChunkedMappedMemoryWriter> ChunkedMappedMemoryWriter::Open(
    const std::filesystem::path& path, size_t chunk_size,
    bool low_address_space) {
  // TODO(DrChat)
  return nullptr;
}

}  // namespace xe
