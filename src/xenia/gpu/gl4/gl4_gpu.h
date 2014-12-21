/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_GL4_GPU_H_
#define XENIA_GPU_GL4_GL4_GPU_H_

#include <memory>

#include <xenia/common.h>
#include <xenia/gpu/graphics_system.h>

namespace xe {
namespace gpu {
namespace gl4 {

std::unique_ptr<GraphicsSystem> Create(Emulator* emulator);

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_GL4_GPU_H_
