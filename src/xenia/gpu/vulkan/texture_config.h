/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_TEXTURE_CONFIG_H_
#define XENIA_GPU_VULKAN_TEXTURE_CONFIG_H_

#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

typedef enum VectorSwizzle {
  VECTOR_SWIZZLE_X = 0,
  VECTOR_SWIZZLE_Y = 1,
  VECTOR_SWIZZLE_Z = 2,
  VECTOR_SWIZZLE_W = 3,
} VectorSwizzle;

struct TextureConfig {
  VkFormat host_format;
  struct {
    VkComponentSwizzle r = VK_COMPONENT_SWIZZLE_R;
    VkComponentSwizzle g = VK_COMPONENT_SWIZZLE_G;
    VkComponentSwizzle b = VK_COMPONENT_SWIZZLE_B;
    VkComponentSwizzle a = VK_COMPONENT_SWIZZLE_A;
  } component_swizzle;
  struct {
    VectorSwizzle x = VECTOR_SWIZZLE_X;
    VectorSwizzle y = VECTOR_SWIZZLE_Y;
    VectorSwizzle z = VECTOR_SWIZZLE_Z;
    VectorSwizzle w = VECTOR_SWIZZLE_W;
  } vector_swizzle;
};

extern const TextureConfig texture_configs[64];

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_TEXTURE_CONFIG_H_
