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


namespace xe {
namespace gpu {

class CreationParams;
class GraphicsSystem;

namespace nop {


GraphicsSystem* Create(const CreationParams* params);


}  // namespace nop
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_NOP_NOP_H_
