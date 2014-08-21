/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_NOP_NOP_GPU_H_
#define XENIA_GPU_NOP_NOP_GPU_H_

#include <memory>

#include <xenia/core.h>

namespace xe {
class Emulator;
}  // namespace xe

namespace xe {
namespace gpu {
class GraphicsSystem;
namespace nop {

std::unique_ptr<GraphicsSystem> Create(Emulator* emulator);

}  // namespace nop
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_NOP_NOP_GPU_H_
