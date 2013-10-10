/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_XENOS_UCODE_DISASSEMBLER_H_
#define XENIA_GPU_XENOS_UCODE_DISASSEMBLER_H_

#include <xenia/core.h>

#include <xenia/gpu/xenos/ucode.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {
namespace xenos {


const char* DisassembleShader(
    XE_GPU_SHADER_TYPE type,
    const uint32_t* dwords, size_t dword_count);


}  // namespace xenos
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_XENOS_UCODE_DISASSEMBLER_H_
