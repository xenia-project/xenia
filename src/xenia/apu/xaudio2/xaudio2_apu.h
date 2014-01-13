/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XAUDIO2_XAUDIO2_APU_H_
#define XENIA_APU_XAUDIO2_XAUDIO2_APU_H_

#include <xenia/core.h>


XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS2(xe, apu, AudioSystem);


namespace xe {
namespace apu {
namespace xaudio2 {


AudioSystem* Create(Emulator* emulator);


}  // namespace xaudio2
}  // namespace apu
}  // namespace xe


#endif  // XENIA_APU_XAUDIO2_XAUDIO2_APU_H_
