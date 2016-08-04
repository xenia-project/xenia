/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/null/null_command_processor.h"

namespace xe {
namespace gpu {
namespace null {

NullCommandProcessor::NullCommandProcessor(NullGraphicsSystem* graphics_system,
                                           kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}
NullCommandProcessor::~NullCommandProcessor() = default;

bool NullCommandProcessor::SetupContext() {
  return CommandProcessor::SetupContext();
}

void NullCommandProcessor::ShutdownContext() {
  return CommandProcessor::ShutdownContext();
}

void NullCommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                       uint32_t frontbuffer_width,
                                       uint32_t frontbuffer_height) {}

Shader* NullCommandProcessor::LoadShader(ShaderType shader_type,
                                         uint32_t guest_address,
                                         const uint32_t* host_address,
                                         uint32_t dword_count) {
  return nullptr;
}

bool NullCommandProcessor::IssueDraw(PrimitiveType prim_type,
                                     uint32_t index_count,
                                     IndexBufferInfo* index_buffer_info) {
  return true;
}

bool NullCommandProcessor::IssueCopy() { return true; }

}  // namespace null
}  // namespace gpu
}  // namespace xe