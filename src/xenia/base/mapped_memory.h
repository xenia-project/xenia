/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MAPPED_MEMORY_H_
#define XENIA_BASE_MAPPED_MEMORY_H_

#include <memory>
#include <string>

namespace xe {

class MappedMemory {
 public:
  enum class Mode {
    kRead,
    kReadWrite,
  };

  virtual ~MappedMemory() = default;

  static std::unique_ptr<MappedMemory> Open(const std::wstring& path, Mode mode,
                                            size_t offset = 0,
                                            size_t length = 0);

  uint8_t* data() const { return reinterpret_cast<uint8_t*>(data_); }
  size_t size() const { return size_; }

 protected:
  MappedMemory(const std::wstring& path, Mode mode)
      : path_(path), mode_(mode), data_(nullptr), size_(0) {}

  std::wstring path_;
  Mode mode_;
  void* data_;
  size_t size_;
};

}  // namespace xe

#endif  // XENIA_BASE_MAPPED_MEMORY_H_
