/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <memory>
#include <mutex>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/logging.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/platform_win.h"

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | \
                            WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_GAMES)
#define XE_BASE_MAPPED_MEMORY_WIN_USE_DESKTOP_FUNCTIONS
#endif

namespace xe {

class Win32MappedMemory : public MappedMemory {
 public:
  // CreateFile returns INVALID_HANDLE_VALUE in case of failure.
  // chrispy: made inline const to get around clang error
  static inline const HANDLE kFileHandleInvalid = INVALID_HANDLE_VALUE;
  // CreateFileMapping returns nullptr in case of failure.
  static constexpr HANDLE kMappingHandleInvalid = nullptr;

  ~Win32MappedMemory() override {
    if (data_) {
      UnmapViewOfFile(data_);
    }
    if (mapping_handle != kMappingHandleInvalid) {
      CloseHandle(mapping_handle);
    }
    if (file_handle != kFileHandleInvalid) {
      CloseHandle(file_handle);
    }
  }

  void Close(uint64_t truncate_size) override {
    if (data_) {
      UnmapViewOfFile(data_);
      data_ = nullptr;
    }
    if (mapping_handle != kMappingHandleInvalid) {
      CloseHandle(mapping_handle);
      mapping_handle = kMappingHandleInvalid;
    }
    if (file_handle != kFileHandleInvalid) {
      if (truncate_size) {
        LONG distance_high = truncate_size >> 32;
        SetFilePointer(file_handle, truncate_size & 0xFFFFFFFF, &distance_high,
                       FILE_BEGIN);
        SetEndOfFile(file_handle);
      }

      CloseHandle(file_handle);
      file_handle = kFileHandleInvalid;
    }
  }

  void Flush() override { FlushViewOfFile(data(), size()); }
  bool Remap(size_t offset, size_t length) override {
    size_t aligned_offset = offset & ~(memory::allocation_granularity() - 1);
    size_t aligned_length = length + (offset - aligned_offset);

    UnmapViewOfFile(data_);
#ifdef XE_BASE_MAPPED_MEMORY_WIN_USE_DESKTOP_FUNCTIONS
    data_ = MapViewOfFile(mapping_handle, view_access_, aligned_offset >> 32,
                          aligned_offset & 0xFFFFFFFF, aligned_length);
#else
    data_ = MapViewOfFileFromApp(mapping_handle, ULONG(view_access_),
                                 ULONG64(aligned_offset), aligned_length);
#endif
    if (!data_) {
      return false;
    }

    if (length) {
      size_ = aligned_length;
    } else {
      DWORD length_high;
      size_t map_length = GetFileSize(file_handle, &length_high);
      map_length |= static_cast<uint64_t>(length_high) << 32;
      size_ = map_length - aligned_offset;
    }

    return true;
  }

  HANDLE file_handle = kFileHandleInvalid;
  HANDLE mapping_handle = kMappingHandleInvalid;
  DWORD view_access_ = 0;
};

std::unique_ptr<MappedMemory> MappedMemory::Open(
    const std::filesystem::path& path, Mode mode, size_t offset,
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

  auto mm = std::make_unique<Win32MappedMemory>();
  mm->view_access_ = view_access;

  mm->file_handle = CreateFile(path.c_str(), file_access, file_share, nullptr,
                               create_mode, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (mm->file_handle == Win32MappedMemory::kFileHandleInvalid) {
    return nullptr;
  }

#ifdef XE_BASE_MAPPED_MEMORY_WIN_USE_DESKTOP_FUNCTIONS
  mm->mapping_handle = CreateFileMapping(
      mm->file_handle, nullptr, mapping_protect, DWORD(aligned_length >> 32),
      DWORD(aligned_length), nullptr);
#else
  mm->mapping_handle =
      CreateFileMappingFromApp(mm->file_handle, nullptr, ULONG(mapping_protect),
                               ULONG64(aligned_length), nullptr);
#endif
  if (mm->mapping_handle == Win32MappedMemory::kMappingHandleInvalid) {
    return nullptr;
  }

#ifdef XE_BASE_MAPPED_MEMORY_WIN_USE_DESKTOP_FUNCTIONS
  mm->data_ = reinterpret_cast<uint8_t*>(MapViewOfFile(
      mm->mapping_handle, view_access, DWORD(aligned_offset >> 32),
      DWORD(aligned_offset), aligned_length));
#else
  mm->data_ = reinterpret_cast<uint8_t*>(
      MapViewOfFileFromApp(mm->mapping_handle, ULONG(view_access),
                           ULONG64(aligned_offset), aligned_length));
#endif
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
  Win32ChunkedMappedMemoryWriter(const std::filesystem::path& path,
                                 size_t chunk_size, bool low_address_space)
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
    auto chunk_path =
        path_.replace_extension(fmt::format(".{}", chunks_.size()));
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
        : file_handle_(Win32MappedMemory::kFileHandleInvalid),
          mapping_handle_(Win32MappedMemory::kMappingHandleInvalid),
          data_(nullptr),
          offset_(0),
          capacity_(capacity),
          last_flush_offset_(0) {}

    ~Chunk() {
      if (data_) {
        UnmapViewOfFile(data_);
      }
      if (mapping_handle_ != Win32MappedMemory::kMappingHandleInvalid) {
        CloseHandle(mapping_handle_);
      }
      if (file_handle_ != Win32MappedMemory::kFileHandleInvalid) {
        CloseHandle(file_handle_);
      }
    }

    bool Open(const std::filesystem::path& path, bool low_address_space) {
      DWORD file_access = GENERIC_READ | GENERIC_WRITE;
      DWORD file_share = FILE_SHARE_READ;
      DWORD create_mode = CREATE_ALWAYS;
      DWORD mapping_protect = PAGE_READWRITE;
      DWORD view_access = FILE_MAP_READ | FILE_MAP_WRITE;

      file_handle_ = CreateFile(path.c_str(), file_access, file_share, nullptr,
                                create_mode, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file_handle_ == Win32MappedMemory::kFileHandleInvalid) {
        return false;
      }

#ifdef XE_BASE_MAPPED_MEMORY_WIN_USE_DESKTOP_FUNCTIONS
      mapping_handle_ =
          CreateFileMapping(file_handle_, nullptr, mapping_protect,
                            DWORD(capacity_ >> 32), DWORD(capacity_), nullptr);
#else
      mapping_handle_ = CreateFileMappingFromApp(file_handle_, nullptr,
                                                 ULONG(mapping_protect),
                                                 ULONG64(capacity_), nullptr);
#endif
      if (mapping_handle_ == Win32MappedMemory::kMappingHandleInvalid) {
        return false;
      }

      // If specified, ensure the allocation occurs in the lower 32-bit address
      // space.
      if (low_address_space) {
        bool successful = false;
        data_ = reinterpret_cast<uint8_t*>(0x10000000);
#ifndef XE_BASE_MAPPED_MEMORY_WIN_USE_DESKTOP_FUNCTIONS
        HANDLE process = GetCurrentProcess();
#endif
        for (int i = 0; i < 1000; ++i) {
#ifdef XE_BASE_MAPPED_MEMORY_WIN_USE_DESKTOP_FUNCTIONS
          if (MapViewOfFileEx(mapping_handle_, view_access, 0, 0, capacity_,
                              data_)) {
            successful = true;
          }
#else
          // VirtualAlloc2FromApp and MapViewOfFile3FromApp were added in
          // 10.0.17134.0.
          // https://docs.microsoft.com/en-us/uwp/win32-and-com/win32-apis
          if (VirtualAlloc2FromApp(process, data_, capacity_,
                                   MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                                   PAGE_NOACCESS, nullptr, 0)) {
            if (MapViewOfFile3FromApp(mapping_handle_, process, data_, 0,
                                      capacity_, MEM_REPLACE_PLACEHOLDER,
                                      ULONG(mapping_protect), nullptr, 0)) {
              successful = true;
            } else {
              VirtualFree(data_, capacity_, MEM_RELEASE);
            }
          }
#endif
          if (successful) {
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
#ifdef XE_BASE_MAPPED_MEMORY_WIN_USE_DESKTOP_FUNCTIONS
        data_ = reinterpret_cast<uint8_t*>(
            MapViewOfFile(mapping_handle_, view_access, 0, 0, capacity_));
#else
        data_ = reinterpret_cast<uint8_t*>(MapViewOfFileFromApp(
            mapping_handle_, ULONG(view_access), 0, capacity_));
#endif
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
    const std::filesystem::path& path, size_t chunk_size,
    bool low_address_space) {
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  size_t aligned_chunk_size =
      xe::round_up(chunk_size, system_info.dwAllocationGranularity);
  return std::make_unique<Win32ChunkedMappedMemoryWriter>(
      path, aligned_chunk_size, low_address_space);
}

}  // namespace xe
