/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
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

static const uint32_t kMaxFrameLength = 0x7FFF;

// Get number of frames that /begin/ in this packet. This is valid only for XMA2
// packets
static const uint8_t GetPacketFrameCount(const uint8_t* packet) {
  return packet[0] >> 2;
}

static const uint8_t GetPacketMetadata(const uint8_t* packet) {
  return packet[2] & 0x7;
}

static const bool IsPacketXma2Type(const uint8_t* packet) {
  return GetPacketMetadata(packet) == 1;
}

static const uint8_t GetPacketSkipCount(const uint8_t* packet) {
  return packet[3];
}

// Get the first frame offset in bits
static uint32_t GetPacketFrameOffset(const uint8_t* packet) {
  uint32_t val = (uint16_t)(((packet[0] & 0x3) << 13) | (packet[1] << 5) |
                            (packet[2] >> 3));
  return val + 32;
}

}  // namespace xma
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_HELPERS_H_