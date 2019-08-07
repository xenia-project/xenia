/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vk/vulkan_command_processor.h"

namespace xe {
namespace gpu {
namespace vk {

VulkanCommandProcessor::VulkanCommandProcessor(
    VulkanGraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}
VulkanCommandProcessor::~VulkanCommandProcessor() = default;

bool VulkanCommandProcessor::SetupContext() { return true; }

void VulkanCommandProcessor::ShutdownContext() {}

void VulkanCommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                         uint32_t frontbuffer_width,
                                         uint32_t frontbuffer_height) {}

Shader* VulkanCommandProcessor::LoadShader(ShaderType shader_type,
                                           uint32_t guest_address,
                                           const uint32_t* host_address,
                                           uint32_t dword_count) {
  return nullptr;
}

bool VulkanCommandProcessor::IssueDraw(PrimitiveType primitive_type,
                                       uint32_t index_count,
                                       IndexBufferInfo* index_buffer_info) {
  return true;
}

bool VulkanCommandProcessor::IssueCopy() { return true; }

}  // namespace vk
}  // namespace gpu
}  // namespace xe
