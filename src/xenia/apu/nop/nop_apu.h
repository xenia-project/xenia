/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_NOP_NOP_APU_H_
#define XENIA_APU_NOP_NOP_APU_H_

#include <memory>

#include "xenia/apu/audio_system.h"
#include "xenia/emulator.h"

namespace xe {
namespace apu {
namespace nop {

std::unique_ptr<AudioSystem> Create(Emulator* emulator);

}  // namespace nop
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_NOP_NOP_APU_H_
