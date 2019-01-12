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
#include <unistd.h>
#include <cstdio>
#include <memory>
#include <mutex>

#include "xenia/base/string.h"
#include "xenia/base/math.h"

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

  void Close(uint64_t truncate_size) override {
    if (data_) {
      munmap(data_, size_);
      data_ = nullptr;
    }

    if (file_handle) {
      if (truncate_size) {
        ftruncate(fileno(file_handle), truncate_size);
      }
      fclose(file_handle);
      file_handle = nullptr;
    }
  }

  void Flush() override { msync(data(), size(), MS_ASYNC); };
  bool Remap(size_t offset, size_t length) override {
    munmap(data_, size_);
    data_ = nullptr;

    if (!length) {
      fseeko(file_handle, 0, SEEK_END);
      length = ftello(file_handle);
      fseeko(file_handle, 0, SEEK_SET);
    }

    data_ = mmap(0, length, prot, MAP_SHARED, fileno(file_handle), offset);
    size_ = length;

    return data_;
  }

  FILE* file_handle;
  int prot;
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

  auto mm =
      std::unique_ptr<PosixMappedMemory>(new PosixMappedMemory(path, mode));
  mm->prot = prot;

  mm->file_handle = fopen(xe::to_string(path).c_str(), mode_str);
  if (!mm->file_handle) {
    return nullptr;
  }

  if (!length) {
    fseeko(mm->file_handle, 0, SEEK_END);
    length = ftello(mm->file_handle);
    fseeko(mm->file_handle, 0, SEEK_SET);
  }
  mm->size_ = length;

  mm->data_ =
      mmap(0, length, prot, MAP_SHARED, fileno(mm->file_handle), offset);
  if (mm->data_ == MAP_FAILED) {
    return nullptr;
  }

  return std::move(mm);
}

class PosixChunkedMappedMemoryWriter : public ChunkedMappedMemoryWriter {
 public:
  PosixChunkedMappedMemoryWriter(const std::wstring& path, size_t chunk_size,
                                 bool low_address_space)
      : ChunkedMappedMemoryWriter(path, chunk_size, low_address_space) {}

  ~PosixChunkedMappedMemoryWriter() override {
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.clear();
  }

  uint8_t* Allocate(size_t length) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!chunks_.empty()) {
      uint8_t* result = chunks_.back()->Allocate(length);
      if (result != nullptr) {
        return result;
      }
    }
    auto chunk = std::make_unique<Chunk>(chunk_size_);
    auto chunk_path = path_ + L"." + std::to_wstring(chunks_.size());
    if (!chunk->Open(chunk_path, low_address_space_)) {
      return nullptr;
    }
    uint8_t* result = chunk->Allocate(length);
    chunks_.push_back(std::move(chunk));
    return result;
  }

  void Flush() override {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& chunk : chunks_) {
      chunk->Flush();
    }
  }

  void FlushNew() override {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& chunk : chunks_) {
      chunk->FlushNew();
    }
  }

private:
  class Chunk {
   public:
    explicit Chunk(size_t capacity)
        : file_handle_(0),
          data_(nullptr),
          offset_(0),
          capacity_(capacity),
          last_flush_offset_(0) {}

    ~Chunk() {
      if (data_) {
        munmap(data_, capacity_);
      }
      if (file_handle_) {
        fclose(file_handle_);
      }
    }

    bool Open(const std::wstring& path, bool low_address_space) {

      file_handle_ = fopen(xe::to_string(path).c_str(), "r+b");

      if (!file_handle_) {
        return false;
      }

      /* Equivalent of CREATE_ALWAYS */
      if (ftruncate(fileno(file_handle_), 0)) {
        return false;
      }

      /* TODO: might be necessary to mandatory-lock write ops on file_handle */

      int flags = MAP_SHARED;
      // If specified, ensure the allocation occurs in the lower 32-bit address
      // space.
      if (low_address_space) {
        flags |= MAP_32BIT;
      }

      data_ = reinterpret_cast<uint8_t*>(
          mmap(0, capacity_, PROT_READ | PROT_WRITE, flags,
               fileno(file_handle_), 0));

      if (data_ == MAP_FAILED) {
        data_ = nullptr; /* for checks later on */
        return false;
      }

      return true;
    }

    uint8_t* Allocate(size_t length) {
      if (capacity_ - offset_ < length) {
        return nullptr;
      }
      uint8_t* result = data_ + offset_;
      offset_ += length;
      return result;
    }

    void Flush() { msync(data_, offset_, MS_ASYNC); }

    void FlushNew() {
      msync(data_ + last_flush_offset_, offset_ - last_flush_offset_, MS_ASYNC);
      last_flush_offset_ = offset_;
    }

   private:
    FILE* file_handle_;
    uint8_t* data_;
    size_t offset_;
    size_t capacity_;
    size_t last_flush_offset_;
  };

  std::mutex mutex_;
  std::vector<std::unique_ptr<Chunk>> chunks_;

};

std::unique_ptr<ChunkedMappedMemoryWriter> ChunkedMappedMemoryWriter::Open(
    const std::wstring& path, size_t chunk_size, bool low_address_space) {
  size_t aligned_chunk_size =
      xe::round_up(chunk_size, sysconf(_SC_PAGE_SIZE));
  return std::make_unique<PosixChunkedMappedMemoryWriter>(
      path, aligned_chunk_size, low_address_space);

  return nullptr;
}

}  // namespace xe
