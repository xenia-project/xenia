/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_APU_H_
#define XENIA_APU_APU_H_

#include <memory>

#include "xenia/apu/audio_system.h"
#include "xenia/apu/xma_decoder.h"

namespace xe {
class Emulator;
}  // namespace xe

namespace xe {
namespace apu {

std::unique_ptr<AudioSystem> Create(Emulator* emulator);

std::unique_ptr<AudioSystem> CreateNop(Emulator* emulator);

#if XE_PLATFORM_WIN32
std::unique_ptr<AudioSystem> CreateXAudio2(Emulator* emulator);
#endif  // WIN32

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_APU_H_
