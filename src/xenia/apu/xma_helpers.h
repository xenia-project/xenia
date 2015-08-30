/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// This file contains some functions used to help parse XMA data.

#ifndef XENIA_APU_XMA_HELPERS_H_
#define XENIA_APU_XMA_HELPERS_H_

#include <stdint.h>

namespace xe {
namespace apu {
namespace xma {

// Get number of frames that /begin/ in this packet.
uint32_t GetPacketFrameCount(uint8_t* packet) {
  return (uint8_t)(packet[0] >> 2);
}

// Get the first frame offset in bits
uint32_t GetPacketFrameOffset(uint8_t* packet) {
  uint32_t val = (uint16_t)(((packet[0] & 0x3) << 13) | (packet[1] << 5) |
                            (packet[2] >> 3));
  if (val == 0x7FFF) {
    return -1;
  } else {
    return val + 32;
  }
}

uint32_t GetPacketMetadata(uint8_t* packet) {
  return (uint8_t)(packet[2] & 0x7);
}

uint32_t GetPacketSkipCount(uint8_t* packet) { return (uint8_t)(packet[3]); }

}  // namespace xma
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_HELPERS_H_
