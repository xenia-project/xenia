/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_DISC_IMAGE_DEVICE_H_
#define XENIA_VFS_DEVICES_DISC_IMAGE_DEVICE_H_

#include <memory>
#include <string>

#include "xenia/base/mapped_memory.h"
#include "xenia/vfs/device.h"

namespace xe {
namespace vfs {

class DiscImageEntry;

class DiscImageDevice : public Device {
 public:
  DiscImageDevice(const std::string& mount_path,
                  const std::wstring& local_path);
  ~DiscImageDevice() override;

  bool Initialize() override;
  void Dump(StringBuffer* string_buffer) override;
  Entry* ResolvePath(const std::string& path) override;

  uint32_t total_allocation_units() const override {
    return uint32_t(mmap_->size() / sectors_per_allocation_unit() /
                    bytes_per_sector());
  }
  uint32_t available_allocation_units() const override { return 0; }
  uint32_t sectors_per_allocation_unit() const override { return 1; }
  uint32_t bytes_per_sector() const override { return 2 * 1024; }

 private:
  enum class Error {
    kSuccess = 0,
    kErrorOutOfMemory = -1,
    kErrorReadError = -10,
    kErrorFileMismatch = -30,
    kErrorDamagedFile = -31,
  };

  std::wstring local_path_;
  std::unique_ptr<Entry> root_entry_;
  std::unique_ptr<MappedMemory> mmap_;

  typedef struct {
    uint8_t* ptr;
    size_t size;         // Size (bytes) of total image.
    size_t game_offset;  // Offset (bytes) of game partition.
    size_t root_sector;  // Offset (sector) of root.
    size_t root_offset;  // Offset (bytes) of root.
    size_t root_size;    // Size (bytes) of root.
  } ParseState;

  Error Verify(ParseState* state);
  bool VerifyMagic(ParseState* state, size_t offset);
  Error ReadAllEntries(ParseState* state, const uint8_t* root_buffer);
  bool ReadEntry(ParseState* state, const uint8_t* buffer,
                 uint16_t entry_ordinal, DiscImageEntry* parent);
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_DISC_IMAGE_DEVICE_H_
