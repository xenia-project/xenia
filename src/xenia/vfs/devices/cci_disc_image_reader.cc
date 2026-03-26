/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright (c) Xenia contributors. All rights reserved.                       *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/cci_disc_image_reader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>

#include "third_party/lz4/lz4.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/utf8.h"

namespace xe {
namespace vfs {

namespace {
std::mutex cci_err_mu;
unsigned cci_err_remaining = 32;

void CciSectorError(uint64_t global_sector, const std::filesystem::path& path,
                    uint32_t local_sector, uint64_t pos, uint64_t next_pos,
                    int64_t index_span, bool lz4_bit, int padding, size_t n,
                    int src_len, const char* reason) {
  std::lock_guard<std::mutex> lock(cci_err_mu);
  if (!cci_err_remaining) {
    return;
  }
  cci_err_remaining--;
  XELOGE(
      "CCI sector read failed: {} g={} local={} pos={:X} next={:X} span={} "
      "lz4_idx={} pad={} n={} src_len={} path={}",
      reason, global_sector, local_sector, pos, next_pos, index_span, lz4_bit,
      padding, n, src_len, xe::path_to_utf8(path));
  if (!cci_err_remaining) {
    XELOGE(
        "CCI: further sector read error lines suppressed for this mount "
        "(limit 32).");
  }
}
}  // namespace

bool CciDiscImageReader::IsCciExtension(std::string_view ext) {
  return xe::utf8::lower_ascii(ext) == ".cci";
}

std::vector<std::filesystem::path> CciDiscImageReader::CollectSlices(
    const std::filesystem::path& path) {
  const auto ext = path.extension();
  const auto stem_with_part = path.stem();
  const auto part_ext = stem_with_part.extension();
  const auto& part_native = part_ext.native();
  if (part_native.size() != 2 || part_native[0] != '.' ||
      !std::isdigit(static_cast<unsigned char>(part_native[1]))) {
    return {path};
  }

  const auto base_name = stem_with_part.stem();
  const auto dir = path.parent_path();
  std::vector<std::filesystem::path> out;

  std::error_code ec;
  auto it = std::filesystem::directory_iterator(dir, ec);
  if (ec) {
    XELOGE("CCI slice scan failed for {}: {}", xe::path_to_utf8(path),
           ec.message());
    return {path};
  }

  for (const auto& ent : it) {
    if (!ent.is_regular_file()) {
      continue;
    }
    const auto& p2 = ent.path();
    if (p2.extension() != ext) {
      continue;
    }
    const auto stem2 = p2.stem();
    const auto& mid = stem2.extension().native();
    // Match .NET search pattern "{base}.?{ext}": one character between base and ext.
    if (mid.size() != 2 || mid[0] != '.') {
      continue;
    }
    if (stem2.stem() != base_name) {
      continue;
    }
    out.push_back(p2);
  }

  if (out.empty()) {
    return {path};
  }

  std::sort(out.begin(), out.end());
  return out;
}

CciDiscImageReader::CciDiscImageReader() = default;

CciDiscImageReader::~CciDiscImageReader() { Close(); }

void CciDiscImageReader::Close() {
  for (auto& up : slices_) {
    if (up && up->file) {
      fclose(up->file);
      up->file = nullptr;
    }
  }
  slices_.clear();
  logical_size_ = 0;
}

bool CciDiscImageReader::Open(const std::vector<std::filesystem::path>& slice_paths) {
  Close();
  {
    std::lock_guard<std::mutex> lock(cci_err_mu);
    cci_err_remaining = 32;
  }

  uint64_t global_sector = 0;
  for (const auto& path : slice_paths) {
    FILE* file = xe::filesystem::OpenFile(path, "rb");
    if (!file) {
      XELOGE("CCI could not open slice: {}", xe::path_to_utf8(path));
      Close();
      return false;
    }

    uint8_t hdr[32];
    if (fread(hdr, 1, sizeof(hdr), file) != sizeof(hdr)) {
      XELOGE("CCI slice too small: {}", xe::path_to_utf8(path));
      fclose(file);
      Close();
      return false;
    }

    if (xe::load<uint32_t>(hdr) != kMagic) {
      XELOGE("CCI invalid magic: {}", xe::path_to_utf8(path));
      fclose(file);
      Close();
      return false;
    }
    if (xe::load<uint32_t>(hdr + 4) != 32) {
      XELOGE("CCI invalid header size: {}", xe::path_to_utf8(path));
      fclose(file);
      Close();
      return false;
    }

    const uint64_t uncompressed_size = xe::load<uint64_t>(hdr + 8);
    const uint64_t index_offset = xe::load<uint64_t>(hdr + 16);
    const uint32_t block_size = xe::load<uint32_t>(hdr + 24);
    const uint8_t version = hdr[28];
    const uint8_t index_alignment = hdr[29];

    if (block_size != kSectorSize || version != 1 || index_alignment != 2) {
      XELOGE("CCI unsupported header fields in {}", xe::path_to_utf8(path));
      fclose(file);
      Close();
      return false;
    }

    if (uncompressed_size % block_size != 0) {
      XELOGE("CCI uncompressed size not sector-aligned: {}",
             xe::path_to_utf8(path));
      fclose(file);
      Close();
      return false;
    }

    const uint32_t sectors =
        static_cast<uint32_t>(uncompressed_size / block_size);
    const uint32_t index_entries = sectors + 1;

    if (!xe::filesystem::Seek(file, static_cast<int64_t>(index_offset),
                              SEEK_SET)) {
      XELOGE("CCI could not seek index: {}", xe::path_to_utf8(path));
      fclose(file);
      Close();
      return false;
    }

    std::vector<uint32_t> raw_index(index_entries);
    if (fread(raw_index.data(), sizeof(uint32_t), index_entries, file) !=
        index_entries) {
      XELOGE("CCI could not read index: {}", xe::path_to_utf8(path));
      fclose(file);
      Close();
      return false;
    }

    auto slice = std::make_unique<Slice>();
    slice->path = path;
    slice->file = file;
    slice->global_start_sector = global_sector;
    slice->sector_count = sectors;
    slice->index.resize(index_entries);
    for (uint32_t i = 0; i < index_entries; ++i) {
      const uint32_t v = raw_index[i];
      slice->index[i].file_offset =
          (static_cast<uint64_t>(v & 0x7FFFFFFFu) << index_alignment);
      slice->index[i].lz4_compressed = (v & 0x80000000u) != 0;
    }

    global_sector += sectors;
    slices_.push_back(std::move(slice));
  }

  logical_size_ = global_sector * kSectorSize;

  return true;
}

bool CciDiscImageReader::ReadSectorFromSlice(Slice& slice, uint32_t local_sector,
                                             uint64_t global_sector,
                                             uint8_t* out_sector) {
  std::lock_guard<std::mutex> lock(io_mutex_);

  if (local_sector + 1 >= slice.index.size()) {
    CciSectorError(global_sector, slice.path, local_sector, 0, 0, 0, false, -1,
                   0, 0, "local_sector out of range");
    return false;
  }

  const uint64_t pos = slice.index[local_sector].file_offset;
  const uint64_t next_pos = slice.index[local_sector + 1].file_offset;
  const bool lz4_compressed = slice.index[local_sector].lz4_compressed;
  const int64_t index_span = static_cast<int64_t>(next_pos - pos);
  if (index_span <= 0) {
    CciSectorError(global_sector, slice.path, local_sector, pos, next_pos,
                   index_span, lz4_compressed, -1, 0, 0, "index_span<=0");
    return false;
  }

  FILE* file = slice.file;

  // Raw sector: first 2048 bytes at index offset (span may be larger).
  if (!lz4_compressed && index_span >= static_cast<int64_t>(kSectorSize)) {
    if (!xe::filesystem::Seek(file, static_cast<int64_t>(pos), SEEK_SET)) {
      CciSectorError(global_sector, slice.path, local_sector, pos, next_pos,
                     index_span, lz4_compressed, -1, 0, 0, "raw seek failed");
      return false;
    }
    if (fread(out_sector, 1, kSectorSize, file) != kSectorSize) {
      CciSectorError(global_sector, slice.path, local_sector, pos, next_pos,
                     index_span, lz4_compressed, -1, 0, 0, "raw fread!=2048");
      return false;
    }
    return true;
  }

  if (!xe::filesystem::Seek(file, static_cast<int64_t>(pos), SEEK_SET)) {
    CciSectorError(global_sector, slice.path, local_sector, pos, next_pos,
                   index_span, lz4_compressed, -1, 0, 0,
                   "compressed seek failed");
    return false;
  }

  auto try_lz4 = [&](const void* src, int src_len) -> bool {
    if (src_len <= 0) {
      return false;
    }
    const int r =
        LZ4_decompress_safe(static_cast<const char*>(src),
                            reinterpret_cast<char*>(out_sector), src_len,
                            static_cast<int>(kSectorSize));
    return r == static_cast<int>(kSectorSize);
  };

  int padding = -1;
  size_t n = 0;
  int src_len = 0;

  // XboxToolkit CCISectorDecoder only: ReadByte (padding), then Read(..., size)
  // with size = index span. LZ4 input length is index_span - (padding + 1).
  const int pad_byte = fgetc(file);
  if (pad_byte == EOF) {
    CciSectorError(global_sector, slice.path, local_sector, pos, next_pos,
                   index_span, lz4_compressed, -1, 0, 0, "padding EOF");
    return false;
  }
  padding = pad_byte;

  const size_t max_read = static_cast<size_t>(index_span);
  std::vector<uint8_t> buf(max_read);
  n = fread(buf.data(), 1, max_read, file);
  src_len = static_cast<int>(index_span) - (padding + 1);
  if (src_len <= 0 || static_cast<size_t>(src_len) > n) {
    CciSectorError(global_sector, slice.path, local_sector, pos, next_pos,
                   index_span, lz4_compressed, padding, n, src_len,
                   "invalid toolkit src_len vs fread count");
    return false;
  }
  if (try_lz4(buf.data(), src_len)) {
    return true;
  }

  CciSectorError(global_sector, slice.path, local_sector, pos, next_pos,
                 index_span, lz4_compressed, padding, n, src_len,
                 "LZ4_decompress_safe failed (toolkit layout)");
  return false;
}

bool CciDiscImageReader::ReadSector(uint64_t global_sector,
                                    uint8_t* out_sector) {
  for (auto& up : slices_) {
    Slice& s = *up;
    if (global_sector >= s.global_start_sector &&
        global_sector < s.global_start_sector + s.sector_count) {
      const uint32_t local =
          static_cast<uint32_t>(global_sector - s.global_start_sector);
      return ReadSectorFromSlice(s, local, global_sector, out_sector);
    }
  }
  return false;
}

bool CciDiscImageReader::ReadLogical(uint64_t offset, void* buffer,
                                     size_t length) {
  auto* dst = static_cast<uint8_t*>(buffer);
  while (length) {
    const uint64_t sec = offset / kSectorSize;
    const size_t sec_off = static_cast<size_t>(offset % kSectorSize);
    const size_t chunk = std::min(length, kSectorSize - sec_off);
    uint8_t sector[kSectorSize];
    if (!ReadSector(sec, sector)) {
      return false;
    }
    std::memcpy(dst, sector + sec_off, chunk);
    dst += chunk;
    offset += chunk;
    length -= chunk;
  }
  return true;
}

}  // namespace vfs
}  // namespace xe
