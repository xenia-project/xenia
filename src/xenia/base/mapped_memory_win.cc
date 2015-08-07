/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/mapped_memory.h"

#include <memory>
#include <mutex>
#include <vector>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform_win.h"

namespace xe {

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

  void Flush() override { FlushViewOfFile(data(), size()); }

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
    case Mode::kRead:
      file_access |= GENERIC_READ;
      file_share |= FILE_SHARE_READ;
      create_mode |= OPEN_EXISTING;
      mapping_protect |= PAGE_READONLY;
      view_access |= FILE_MAP_READ;
      break;
    case Mode::kReadWrite:
      file_access |= GENERIC_READ | GENERIC_WRITE;
      file_share |= 0;
      create_mode |= OPEN_EXISTING;
      mapping_protect |= PAGE_READWRITE;
      view_access |= FILE_MAP_READ | FILE_MAP_WRITE;
      break;
  }

  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);

  const size_t aligned_offset =
      offset & ~static_cast<size_t>(system_info.dwAllocationGranularity - 1);
  const size_t aligned_length = length + (offset - aligned_offset);

  auto mm = std::make_unique<Win32MappedMemory>(path, mode);

  mm->file_handle = CreateFile(path.c_str(), file_access, file_share, nullptr,
                               create_mode, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (!mm->file_handle) {
    return nullptr;
  }

  mm->mapping_handle = CreateFileMapping(mm->file_handle, nullptr,
                                         mapping_protect, 0, 0, nullptr);
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

class Win32ChunkedMappedMemoryWriter : public ChunkedMappedMemoryWriter {
 public:
  Win32ChunkedMappedMemoryWriter(const std::wstring& path, size_t chunk_size,
                                 bool low_address_space)
      : ChunkedMappedMemoryWriter(path, chunk_size, low_address_space) {}

  ~Win32ChunkedMappedMemoryWriter() override {
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
          mapping_handle_(0),
          data_(nullptr),
          offset_(0),
          capacity_(capacity),
          last_flush_offset_(0) {}

    ~Chunk() {
      if (data_) {
        UnmapViewOfFile(data_);
      }
      if (mapping_handle_) {
        CloseHandle(mapping_handle_);
      }
      if (file_handle_) {
        CloseHandle(file_handle_);
      }
    }

    bool Open(const std::wstring& path, bool low_address_space) {
      DWORD file_access = GENERIC_READ | GENERIC_WRITE;
      DWORD file_share = FILE_SHARE_READ;
      DWORD create_mode = CREATE_ALWAYS;
      DWORD mapping_protect = PAGE_READWRITE;
      DWORD view_access = FILE_MAP_READ | FILE_MAP_WRITE;

      file_handle_ = CreateFile(path.c_str(), file_access, file_share, nullptr,
                                create_mode, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (!file_handle_) {
        return false;
      }

      mapping_handle_ =
          CreateFileMapping(file_handle_, nullptr, mapping_protect, 0,
                            static_cast<DWORD>(capacity_), nullptr);
      if (!mapping_handle_) {
        return false;
      }

      // If specified, ensure the allocation occurs in the lower 32-bit address
      // space.
      if (low_address_space) {
        bool successful = false;
        data_ = reinterpret_cast<uint8_t*>(0x10000000);
        for (int i = 0; i < 1000; ++i) {
          if (MapViewOfFileEx(mapping_handle_, view_access, 0, 0, capacity_,
                              data_)) {
            successful = true;
            break;
          }
          data_ += capacity_;
          if (!successful) {
            XELOGE("Unable to find space for mapping");
            data_ = nullptr;
            return false;
          }
        }
      } else {
        data_ = reinterpret_cast<uint8_t*>(
            MapViewOfFile(mapping_handle_, view_access, 0, 0, capacity_));
      }
      if (!data_) {
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

    void Flush() { FlushViewOfFile(data_, offset_); }

    void FlushNew() {
      FlushViewOfFile(data_ + last_flush_offset_, offset_ - last_flush_offset_);
      last_flush_offset_ = offset_;
    }

   private:
    HANDLE file_handle_;
    HANDLE mapping_handle_;
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
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  size_t aligned_chunk_size =
      xe::round_up(chunk_size, system_info.dwAllocationGranularity);
  return std::make_unique<Win32ChunkedMappedMemoryWriter>(
      path, aligned_chunk_size, low_address_space);
}

}  // namespace xe
