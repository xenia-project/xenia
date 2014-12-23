/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_UCODE_DISASSEMBLER_H_
#define XENIA_GPU_UCODE_DISASSEMBLER_H_

#include <string>

#include <xenia/gpu/ucode.h>
#include <xenia/gpu/xenos.h>

namespace xe {
namespace gpu {

std::string DisassembleShader(ShaderType type, const uint32_t* dwords,
                              size_t dword_count);

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_UCODE_DISASSEMBLER_H_
