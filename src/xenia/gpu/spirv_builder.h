/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_BUILDER_H_
#define XENIA_GPU_SPIRV_BUILDER_H_

#include "third_party/glslang/SPIRV/SpvBuilder.h"

namespace xe {
namespace gpu {

// SpvBuilder with extra helpers.

class SpirvBuilder : public spv::Builder {
 public:
  SpirvBuilder(unsigned int spv_version, unsigned int user_number,
               spv::SpvBuildLogger* logger)
      : spv::Builder(spv_version, user_number, logger) {}

  // Make public rather than protected.
  using spv::Builder::createSelectionMerge;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_BUILDER_H_
