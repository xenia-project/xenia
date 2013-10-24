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

#include <xenia/apu/audio_system.h>


XEDECLARECLASS1(xe, Emulator);


namespace xe {
namespace apu {


AudioSystem* Create(Emulator* emulator);

AudioSystem* CreateNop(Emulator* emulator);


}  // namespace apu
}  // namespace xe


#endif  // XENIA_APU_APU_H_
