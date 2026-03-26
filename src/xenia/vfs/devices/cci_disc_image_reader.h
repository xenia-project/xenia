/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright (c) Xenia contributors. All rights reserved.                       *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * CCI (Cerbios Compressed Image) reader. Format matches Team Resurgent /
 * XboxToolkit (see CCIContainerReader.cs / CCISectorDecoder.cs).
 *
 */

#ifndef XENIA_VFS_DEVICES_CCI_DISC_IMAGE_READER_H_
#define XENIA_VFS_DEVICES_CCI_DISC_IMAGE_READER_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace xe {
namespace vfs {

class CciDiscImageReader {
 public:
  static constexpr uint32_t kMagic = 0x4D494343;  // "CCIM" LE
  static constexpr uint32_t kSectorSize = 2048;

  static bool IsCciExtension(std::string_view ext);
  static std::vector<std::filesystem::path> CollectSlices(
      const std::filesystem::path& path);

  CciDiscImageReader();
  ~CciDiscImageReader();

  CciDiscImageReader(const CciDiscImageReader&) = delete;
  CciDiscImageReader& operator=(const CciDiscImageReader&) = delete;

  bool Open(const std::vector<std::filesystem::path>& slice_paths);
  void Close();

  uint64_t logical_size() const { return logical_size_; }

  // Read from the uncompressed linear disc image.
  bool ReadLogical(uint64_t offset, void* buffer, size_t length);

 private:
  struct IndexEntry {
    uint64_t file_offset = 0;
    bool lz4_compressed = false;
  };

  struct Slice {
    std::filesystem::path path;
    FILE* file = nullptr;
    uint64_t global_start_sector = 0;
    uint32_t sector_count = 0;
    std::vector<IndexEntry> index;
  };

  bool ReadSector(uint64_t global_sector, uint8_t* out_sector);
  bool ReadSectorFromSlice(Slice& slice, uint32_t local_sector,
                           uint64_t global_sector, uint8_t* out_sector);

  std::vector<std::unique_ptr<Slice>> slices_;
  uint64_t logical_size_ = 0;
  mutable std::mutex io_mutex_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_CCI_DISC_IMAGE_READER_H_
