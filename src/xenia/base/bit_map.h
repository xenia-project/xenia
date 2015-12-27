/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_BIT_MAP_H_
#define XENIA_BASE_BIT_MAP_H_

#include <cstdint>
#include <vector>

namespace xe {

// Bit Map: Efficient lookup of free/used entries.
class BitMap {
 public:
  BitMap();

  // Size is the number of entries, must be a multiple of 32.
  BitMap(size_t size_bits);

  // Data does not have to be aligned to a 4-byte boundary, but it is
  // preferable.
  // Size is the number of entries, must be a multiple of 32.
  BitMap(uint32_t* data, size_t size_bits);

  // (threadsafe) Acquires an entry and returns its index. Returns -1 if there
  // are no more free entries.
  size_t Acquire();

  // (threadsafe) Releases an entry by an index.
  void Release(size_t index);

  // Resize the bitmap. Size is the number of entries, must be a multiple of 32.
  void Resize(size_t new_size_bits);

  // Sets all entries to free.
  void Reset();

  const std::vector<uint32_t> data() const { return data_; }
  std::vector<uint32_t>& data() { return data_; }

 private:
  const static uint32_t kDataSize = 4;
  const static uint32_t kDataSizeBits = kDataSize * 8;
  std::vector<uint32_t> data_;
};

}  // namespace xe

#endif  // XENIA_BASE_BIT_MAP_H_