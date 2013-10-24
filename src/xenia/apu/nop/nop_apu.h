/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_NOP_NOP_H_
#define XENIA_GPU_NOP_NOP_H_

#include <xenia/core.h>


XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS2(xe, apu, AudioSystem);


namespace xe {
namespace apu {
namespace nop {


AudioSystem* Create(Emulator* emulator);


}  // namespace nop
}  // namespace apu
}  // namespace xe


#endif  // XENIA_GPU_NOP_NOP_H_
