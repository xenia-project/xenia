/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_BIT_RANGE_H_
#define XENIA_BASE_BIT_RANGE_H_

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "xenia/base/math.h"

namespace xe {
namespace bit_range {

// Provided length is in bits since the first. Returns <first, length> of the
// range in bits, with length == 0 if not found.
template <typename Block>
std::pair<size_t, size_t> NextUnsetRange(const Block* bits, size_t first,
                                         size_t length) {
  if (!length) {
    return std::make_pair(size_t(first), size_t(0));
  }
  size_t last = first + length - 1;
  constexpr size_t block_bits = sizeof(Block) * CHAR_BIT;
  size_t block_first = first / block_bits;
  size_t block_last = last / block_bits;
  size_t range_start = SIZE_MAX;
  for (size_t i = block_first; i <= block_last; ++i) {
    Block block = bits[i];
    // Ignore bits in the block outside the specified range by considering them
    // set.
    if (i == block_first) {
      block |= (Block(1) << (first & (block_bits - 1))) - 1;
    }
    if (i == block_last && (last & (block_bits - 1)) != block_bits - 1) {
      block |= ~((Block(1) << ((last & (block_bits - 1)) + 1)) - 1);
    }
    while (true) {
      uint32_t block_bit;
      if (range_start == SIZE_MAX) {
        // Check if need to open a new range.
        if (!xe::bit_scan_forward(~block, &block_bit)) {
          break;
        }
        range_start = i * block_bits + block_bit;
      } else {
        // Check if need to close the range.
        // Ignore the set bits before the beginning of the range.
        Block block_bits_set_from_start = block;
        if (i == range_start / block_bits) {
          block_bits_set_from_start &=
              ~((Block(1) << (range_start & (block_bits - 1))) - 1);
        }
        if (!xe::bit_scan_forward(block_bits_set_from_start, &block_bit)) {
          break;
        }
        return std::make_pair(range_start,
                              (i * block_bits) + block_bit - range_start);
      }
    }
  }
  if (range_start != SIZE_MAX) {
    return std::make_pair(range_start, last + size_t(1) - range_start);
  }
  return std::make_pair(first + length, size_t(0));
}

template <typename Block>
void SetRange(Block* bits, size_t first, size_t length) {
  if (!length) {
    return;
  }
  size_t last = first + length - 1;
  constexpr size_t block_bits = sizeof(Block) * CHAR_BIT;
  size_t block_first = first / block_bits;
  size_t block_last = last / block_bits;
  Block set_first = ~((Block(1) << (first & (block_bits - 1))) - 1);
  Block set_last = ~Block(0);
  if ((last & (block_bits - 1)) != (block_bits - 1)) {
    set_last &= (Block(1) << ((last & (block_bits - 1)) + 1)) - 1;
  }
  if (block_first == block_last) {
    bits[block_first] |= set_first & set_last;
    return;
  }
  bits[block_first] |= set_first;
  if (block_first + 1 < block_last) {
    std::memset(bits + block_first + 1, CHAR_MAX,
                (block_last - (block_first + 1)) * sizeof(Block));
  }
  bits[block_last] |= set_last;
}

}  // namespace bit_range
}  // namespace xe

#endif  // XENIA_BASE_BIT_RANGE_H_
