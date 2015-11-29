/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_UCODE_DISASSEMBLER_H_
#define XENIA_GPU_GL4_UCODE_DISASSEMBLER_H_

#include <string>

#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace gl4 {

std::string DisassembleShader(ShaderType type, const uint32_t* dwords,
                              size_t dword_count);

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_UCODE_DISASSEMBLER_H_
