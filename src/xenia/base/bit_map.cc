/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/bit_map.h"

#include "xenia/base/assert.h"
#include "xenia/base/atomic.h"
#include "xenia/base/math.h"

namespace xe {

BitMap::BitMap() = default;

BitMap::BitMap(size_t size_bits) { Resize(size_bits); }

BitMap::BitMap(uint64_t* data, size_t size_bits) {
  assert_true(size_bits % kDataSizeBits == 0);

  data_.resize(size_bits / kDataSizeBits);
  std::memcpy(data_.data(), data, size_bits / kDataSizeBits);
}
inline size_t BitMap::TryAcquireAt(size_t i) {
  uint64_t entry = 0;
  uint64_t new_entry = 0;
  int64_t acquired_idx = -1LL;

  do {
    entry = data_[i];
    uint8_t index = lzcnt(entry);
    if (index == kDataSizeBits) {
      // None free.
      acquired_idx = -1;
      break;
    }

    // Entry has a free bit. Acquire it.
    uint64_t bit = 1ull << (kDataSizeBits - index - 1);
    new_entry = entry & ~bit;
    assert_not_zero(entry & bit);

    acquired_idx = index;
  } while (!atomic_cas(entry, new_entry, &data_[i]));

  if (acquired_idx != -1) {
    // Acquired.
    return (i * kDataSizeBits) + acquired_idx;
  }
  return -1LL;
}
size_t BitMap::Acquire() {
  for (size_t i = 0; i < data_.size(); i++) {
    size_t attempt_result = TryAcquireAt(i);
    if (attempt_result != -1LL) {
      return attempt_result;
    }
  }

  return -1LL;
}

size_t BitMap::AcquireFromBack() {
  if (!data_.size()) {
    return -1LL;
  }
  for (ptrdiff_t i = data_.size() - 1; i >= 0; i--) {
    size_t attempt_result = TryAcquireAt(static_cast<size_t>(i));
    if (attempt_result != -1LL) {
      return attempt_result;
    }
  }

  return -1LL;
}

void BitMap::Release(size_t index) {
  auto slot = index / kDataSizeBits;
  index -= slot * kDataSizeBits;

  uint64_t bit = 1ull << (kDataSizeBits - index - 1);

  uint64_t entry = 0;
  uint64_t new_entry = 0;
  do {
    entry = data_[slot];
    assert_zero(entry & bit);

    new_entry = entry | bit;
  } while (!atomic_cas(entry, new_entry, &data_[slot]));
}

void BitMap::Resize(size_t new_size_bits) {
  auto old_size = data_.size();
  assert_true(new_size_bits % kDataSizeBits == 0);
  data_.resize(new_size_bits / kDataSizeBits);

  // Initialize new entries.
  if (data_.size() > old_size) {
    for (size_t i = old_size; i < data_.size(); i++) {
      data_[i] = -1;
    }
  }
}

void BitMap::Reset() {
  for (size_t i = 0; i < data_.size(); i++) {
    data_[i] = -1;
  }
}

}  // namespace xe