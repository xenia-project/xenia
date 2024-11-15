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

#include "xenia/base/filesystem.h"
#include "xenia/base/platform.h"

namespace xe {

class PosixMappedMemory : public MappedMemory {
 public:
  PosixMappedMemory(void* data, size_t size, int file_descriptor)
      : MappedMemory(data, size), file_descriptor_(file_descriptor) {}

  ~PosixMappedMemory() override {
    if (data_) {
      munmap(data_, size());
    }
    if (file_descriptor_ >= 0) {
      close(file_descriptor_);
    }
  }

  static std::unique_ptr<PosixMappedMemory> WrapFileDescriptor(
      int file_descriptor, Mode mode, size_t offset = 0, size_t length = 0) {
    int protection = 0;
    switch (mode) {
      case Mode::kRead:
        protection |= PROT_READ;
        break;
      case Mode::kReadWrite:
        protection |= PROT_READ | PROT_WRITE;
        break;
    }

    size_t map_length = length;
    if (!length) {
      struct stat file_stat;
      if (fstat(file_descriptor, &file_stat)) {
        close(file_descriptor);
        return nullptr;
      }
      map_length = size_t(file_stat.st_size);
    }

    void* data =
        mmap(0, map_length, protection, MAP_SHARED, file_descriptor, offset);
    if (!data) {
      close(file_descriptor);
      return nullptr;
    }

    return std::make_unique<PosixMappedMemory>(data, map_length,
                                               file_descriptor);
  }

  void Close(uint64_t truncate_size) override {
    if (data_) {
      munmap(data_, size());
      data_ = nullptr;
    }
    if (file_descriptor_ >= 0) {
      if (truncate_size) {
          ftruncate(file_descriptor_, off_t(truncate_size));
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
  switch (mode) {
    case Mode::kRead:
      open_flags |= O_RDONLY;
      break;
    case Mode::kReadWrite:
      open_flags |= O_RDWR;
      break;
  }
  int file_descriptor = open(path.c_str(), open_flags);
  if (file_descriptor < 0) {
    return nullptr;
  }
  return PosixMappedMemory::WrapFileDescriptor(file_descriptor, mode, offset,
                                               length);
}

#if XE_PLATFORM_ANDROID
std::unique_ptr<MappedMemory> MappedMemory::OpenForAndroidContentUri(
    const std::string_view uri, Mode mode, size_t offset, size_t length) {
  const char* open_mode = nullptr;
  switch (mode) {
    case Mode::kRead:
      open_mode = "r";
      break;
    case Mode::kReadWrite:
      open_mode = "rw";
      break;
  }
  int file_descriptor =
      xe::filesystem::OpenAndroidContentFileDescriptor(uri, open_mode);
  if (file_descriptor < 0) {
    return nullptr;
  }
  return PosixMappedMemory::WrapFileDescriptor(file_descriptor, mode, offset,
                                               length);
}
#endif  // XE_PLATFORM_ANDROID

std::unique_ptr<ChunkedMappedMemoryWriter> ChunkedMappedMemoryWriter::Open(
    const std::filesystem::path& path, size_t chunk_size,
    bool low_address_space) {
  // TODO(DrChat)
  return nullptr;
}

}  // namespace xe
