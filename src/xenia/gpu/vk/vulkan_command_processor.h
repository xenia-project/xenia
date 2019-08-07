/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VK_VULKAN_COMMAND_PROCESSOR_H_
#define XENIA_GPU_VK_VULKAN_COMMAND_PROCESSOR_H_

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/vk/vulkan_graphics_system.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace gpu {
namespace vk {

class VulkanCommandProcessor : public CommandProcessor {
 public:
  explicit VulkanCommandProcessor(VulkanGraphicsSystem* graphics_system,
                                  kernel::KernelState* kernel_state);
  ~VulkanCommandProcessor();

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;

  void PerformSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                   uint32_t frontbuffer_height) override;

  Shader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(PrimitiveType primitive_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info) override;
  bool IssueCopy() override;
};

}  // namespace vk
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VK_VULKAN_COMMAND_PROCESSOR_H_
