/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_texture_cache.h"

#include <algorithm>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_128bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_128bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_16bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_16bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_32bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_32bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_64bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_64bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_8bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_8bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_ctx1_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_depth_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_depth_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_depth_unorm_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_depth_unorm_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxn_rg8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt1_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt3_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt3a_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt3aas1111_argb4_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt5_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt5a_r8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r10g11b11_rgba16_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r10g11b11_rgba16_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r10g11b11_rgba16_snorm_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r10g11b11_rgba16_snorm_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r11g11b10_rgba16_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r11g11b10_rgba16_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r11g11b10_rgba16_snorm_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r11g11b10_rgba16_snorm_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r16_snorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r16_snorm_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r16_unorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r16_unorm_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r4g4b4a4_a4r4g4b4_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r4g4b4a4_a4r4g4b4_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g5b5a1_b5g5r5a1_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g5b5a1_b5g5r5a1_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g5b6_b5g6r5_swizzle_rbga_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g6b5_b5g6r5_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g6b5_b5g6r5_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rg16_snorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rg16_snorm_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rg16_unorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rg16_unorm_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rgba16_snorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rgba16_snorm_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rgba16_unorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rgba16_unorm_float_scaled_cs.h"
}  // namespace shaders

VulkanTextureCache::~VulkanTextureCache() {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  if (null_image_view_3d_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, null_image_view_3d_, nullptr);
  }
  if (null_image_view_cube_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, null_image_view_cube_, nullptr);
  }
  if (null_image_view_2d_array_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, null_image_view_2d_array_, nullptr);
  }
  if (null_image_3d_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, null_image_3d_, nullptr);
  }
  if (null_image_2d_array_cube_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, null_image_2d_array_cube_, nullptr);
  }
  for (VkDeviceMemory null_images_memory : null_images_memory_) {
    if (null_images_memory != VK_NULL_HANDLE) {
      dfn.vkFreeMemory(device, null_images_memory, nullptr);
    }
  }
}

void VulkanTextureCache::BeginSubmission(uint64_t new_submission_index) {
  TextureCache::BeginSubmission(new_submission_index);

  if (!null_images_cleared_) {
    VkImage null_images[] = {null_image_2d_array_cube_, null_image_3d_};
    VkImageSubresourceRange null_image_subresource_range(
        ui::vulkan::util::InitializeSubresourceRange());
    for (size_t i = 0; i < xe::countof(null_images); ++i) {
      command_processor_.PushImageMemoryBarrier(
          null_images[i], null_image_subresource_range, 0,
          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }
    command_processor_.SubmitBarriers(true);
    DeferredCommandBuffer& command_buffer =
        command_processor_.deferred_command_buffer();
    // TODO(Triang3l): Find the return value for invalid texture fetch constants
    // on the real hardware.
    VkClearColorValue null_image_clear_color;
    null_image_clear_color.float32[0] = 0.0f;
    null_image_clear_color.float32[1] = 0.0f;
    null_image_clear_color.float32[2] = 0.0f;
    null_image_clear_color.float32[3] = 0.0f;
    for (size_t i = 0; i < xe::countof(null_images); ++i) {
      command_buffer.CmdVkClearColorImage(
          null_images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          &null_image_clear_color, 1, &null_image_subresource_range);
    }
    for (size_t i = 0; i < xe::countof(null_images); ++i) {
      command_processor_.PushImageMemoryBarrier(
          null_images[i], null_image_subresource_range,
          VK_PIPELINE_STAGE_TRANSFER_BIT, guest_shader_pipeline_stages_,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    null_images_cleared_ = true;
  }
}

uint32_t VulkanTextureCache::GetHostFormatSwizzle(TextureKey key) const {
  // TODO(Triang3l): Implement GetHostFormatSwizzle.
  return xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
}

uint32_t VulkanTextureCache::GetMaxHostTextureWidthHeight(
    xenos::DataDimension dimension) const {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const VkPhysicalDeviceLimits& device_limits =
      provider.device_properties().limits;
  switch (dimension) {
    case xenos::DataDimension::k1D:
    case xenos::DataDimension::k2DOrStacked:
      // 1D and 2D are emulated as 2D arrays.
      return device_limits.maxImageDimension2D;
    case xenos::DataDimension::k3D:
      return device_limits.maxImageDimension3D;
    case xenos::DataDimension::kCube:
      return device_limits.maxImageDimensionCube;
    default:
      assert_unhandled_case(dimension);
      return 0;
  }
}

uint32_t VulkanTextureCache::GetMaxHostTextureDepthOrArraySize(
    xenos::DataDimension dimension) const {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const VkPhysicalDeviceLimits& device_limits =
      provider.device_properties().limits;
  switch (dimension) {
    case xenos::DataDimension::k1D:
    case xenos::DataDimension::k2DOrStacked:
      // 1D and 2D are emulated as 2D arrays.
      return device_limits.maxImageArrayLayers;
    case xenos::DataDimension::k3D:
      return device_limits.maxImageDimension3D;
    case xenos::DataDimension::kCube:
      // Not requesting the imageCubeArray feature, and the Xenos doesn't
      // support cube map arrays.
      return 6;
    default:
      assert_unhandled_case(dimension);
      return 0;
  }
}

std::unique_ptr<TextureCache::Texture> VulkanTextureCache::CreateTexture(
    TextureKey key) {
  // TODO(Triang3l): Implement CreateTexture.
  return std::unique_ptr<Texture>(new VulkanTexture(*this, key));
}

bool VulkanTextureCache::LoadTextureDataFromResidentMemoryImpl(Texture& texture,
                                                               bool load_base,
                                                               bool load_mips) {
  // TODO(Triang3l): Implement LoadTextureDataFromResidentMemoryImpl.
  return true;
}

VulkanTextureCache::VulkanTexture::VulkanTexture(
    VulkanTextureCache& texture_cache, const TextureKey& key)
    : Texture(texture_cache, key) {}

VulkanTextureCache::VulkanTextureCache(
    const RegisterFile& register_file, VulkanSharedMemory& shared_memory,
    uint32_t draw_resolution_scale_x, uint32_t draw_resolution_scale_y,
    VulkanCommandProcessor& command_processor,
    VkPipelineStageFlags guest_shader_pipeline_stages)
    : TextureCache(register_file, shared_memory, draw_resolution_scale_x,
                   draw_resolution_scale_y),
      command_processor_(command_processor),
      guest_shader_pipeline_stages_(guest_shader_pipeline_stages) {
  // TODO(Triang3l): Support draw resolution scaling.
  assert_true(draw_resolution_scale_x == 1 && draw_resolution_scale_y == 1);
}

bool VulkanTextureCache::Initialize() {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  const VkPhysicalDevicePortabilitySubsetFeaturesKHR*
      device_portability_subset_features =
          provider.device_portability_subset_features();

  // Null images as a replacement for unneeded bindings and for bindings for
  // which the real image hasn't been created.

  VkImageCreateInfo null_image_create_info;
  null_image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  null_image_create_info.pNext = nullptr;
  null_image_create_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  null_image_create_info.imageType = VK_IMAGE_TYPE_2D;
  // Four components to return (0, 0, 0, 0).
  // TODO(Triang3l): Find the return value for invalid texture fetch constants
  // on the real hardware.
  null_image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  null_image_create_info.extent.width = 1;
  null_image_create_info.extent.height = 1;
  null_image_create_info.extent.depth = 1;
  null_image_create_info.mipLevels = 1;
  null_image_create_info.arrayLayers = 6;
  null_image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  null_image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  null_image_create_info.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  null_image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  null_image_create_info.queueFamilyIndexCount = 0;
  null_image_create_info.pQueueFamilyIndices = nullptr;
  null_image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (dfn.vkCreateImage(device, &null_image_create_info, nullptr,
                        &null_image_2d_array_cube_) != VK_SUCCESS) {
    XELOGE(
        "VulkanTextureCache: Failed to create the null 2D array and cube "
        "image");
    return false;
  }

  null_image_create_info.flags &= ~VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  null_image_create_info.imageType = VK_IMAGE_TYPE_3D;
  null_image_create_info.arrayLayers = 1;
  if (dfn.vkCreateImage(device, &null_image_create_info, nullptr,
                        &null_image_3d_) != VK_SUCCESS) {
    XELOGE("VulkanTextureCache: Failed to create the null 3D image");
    return false;
  }

  VkMemoryRequirements null_image_memory_requirements_2d_array_cube_;
  dfn.vkGetImageMemoryRequirements(
      device, null_image_2d_array_cube_,
      &null_image_memory_requirements_2d_array_cube_);
  VkMemoryRequirements null_image_memory_requirements_3d_;
  dfn.vkGetImageMemoryRequirements(device, null_image_3d_,
                                   &null_image_memory_requirements_3d_);
  uint32_t null_image_memory_type_common = ui::vulkan::util::ChooseMemoryType(
      provider,
      null_image_memory_requirements_2d_array_cube_.memoryTypeBits &
          null_image_memory_requirements_3d_.memoryTypeBits,
      ui::vulkan::util::MemoryPurpose::kDeviceLocal);
  if (null_image_memory_type_common != UINT32_MAX) {
    // Place both null images in one memory allocation because maximum total
    // memory allocation count is limited.
    VkDeviceSize null_image_memory_offset_3d_ =
        xe::align(null_image_memory_requirements_2d_array_cube_.size,
                  std::max(null_image_memory_requirements_3d_.alignment,
                           VkDeviceSize(1)));
    VkMemoryAllocateInfo null_image_memory_allocate_info;
    null_image_memory_allocate_info.sType =
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    null_image_memory_allocate_info.pNext = nullptr;
    null_image_memory_allocate_info.allocationSize =
        null_image_memory_offset_3d_ + null_image_memory_requirements_3d_.size;
    null_image_memory_allocate_info.memoryTypeIndex =
        null_image_memory_type_common;
    if (dfn.vkAllocateMemory(device, &null_image_memory_allocate_info, nullptr,
                             &null_images_memory_[0]) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to allocate the memory for the null "
          "images");
      return false;
    }
    if (dfn.vkBindImageMemory(device, null_image_2d_array_cube_,
                              null_images_memory_[0], 0) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to bind the memory to the null 2D array "
          "and cube image");
      return false;
    }
    if (dfn.vkBindImageMemory(device, null_image_3d_, null_images_memory_[0],
                              null_image_memory_offset_3d_) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to bind the memory to the null 3D image");
      return false;
    }
  } else {
    // Place each null image in separate allocations.
    uint32_t null_image_memory_type_2d_array_cube_ =
        ui::vulkan::util::ChooseMemoryType(
            provider,
            null_image_memory_requirements_2d_array_cube_.memoryTypeBits,
            ui::vulkan::util::MemoryPurpose::kDeviceLocal);
    uint32_t null_image_memory_type_3d_ = ui::vulkan::util::ChooseMemoryType(
        provider, null_image_memory_requirements_3d_.memoryTypeBits,
        ui::vulkan::util::MemoryPurpose::kDeviceLocal);
    if (null_image_memory_type_2d_array_cube_ == UINT32_MAX ||
        null_image_memory_type_3d_ == UINT32_MAX) {
      XELOGE(
          "VulkanTextureCache: Failed to get the memory types for the null "
          "images");
      return false;
    }

    VkMemoryAllocateInfo null_image_memory_allocate_info;
    VkMemoryAllocateInfo* null_image_memory_allocate_info_last =
        &null_image_memory_allocate_info;
    null_image_memory_allocate_info.sType =
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    null_image_memory_allocate_info.pNext = nullptr;
    null_image_memory_allocate_info.allocationSize =
        null_image_memory_requirements_2d_array_cube_.size;
    null_image_memory_allocate_info.memoryTypeIndex =
        null_image_memory_type_2d_array_cube_;
    VkMemoryDedicatedAllocateInfoKHR null_image_memory_dedicated_allocate_info;
    if (provider.device_extensions().khr_dedicated_allocation) {
      null_image_memory_allocate_info_last->pNext =
          &null_image_memory_dedicated_allocate_info;
      null_image_memory_allocate_info_last =
          reinterpret_cast<VkMemoryAllocateInfo*>(
              &null_image_memory_dedicated_allocate_info);
      null_image_memory_dedicated_allocate_info.sType =
          VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
      null_image_memory_dedicated_allocate_info.pNext = nullptr;
      null_image_memory_dedicated_allocate_info.image =
          null_image_2d_array_cube_;
      null_image_memory_dedicated_allocate_info.buffer = VK_NULL_HANDLE;
    }
    if (dfn.vkAllocateMemory(device, &null_image_memory_allocate_info, nullptr,
                             &null_images_memory_[0]) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to allocate the memory for the null 2D "
          "array and cube image");
      return false;
    }
    if (dfn.vkBindImageMemory(device, null_image_2d_array_cube_,
                              null_images_memory_[0], 0) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to bind the memory to the null 2D array "
          "and cube image");
      return false;
    }

    null_image_memory_allocate_info.allocationSize =
        null_image_memory_requirements_3d_.size;
    null_image_memory_allocate_info.memoryTypeIndex =
        null_image_memory_type_3d_;
    null_image_memory_dedicated_allocate_info.image = null_image_3d_;
    if (dfn.vkAllocateMemory(device, &null_image_memory_allocate_info, nullptr,
                             &null_images_memory_[1]) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to allocate the memory for the null 3D "
          "image");
      return false;
    }
    if (dfn.vkBindImageMemory(device, null_image_3d_, null_images_memory_[1],
                              0) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to bind the memory to the null 3D image");
      return false;
    }
  }

  VkImageViewCreateInfo null_image_view_create_info;
  null_image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  null_image_view_create_info.pNext = nullptr;
  null_image_view_create_info.flags = 0;
  null_image_view_create_info.image = null_image_2d_array_cube_;
  null_image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  null_image_view_create_info.format = null_image_create_info.format;
  // TODO(Triang3l): Find the return value for invalid texture fetch constants
  // on the real hardware.
  // Micro-optimization if this has any effect on the host GPU at all, use only
  // constant components instead of the real texels. The image will be cleared
  // to (0, 0, 0, 0) anyway.
  VkComponentSwizzle null_image_view_swizzle =
      (!device_portability_subset_features ||
       device_portability_subset_features->imageViewFormatSwizzle)
          ? VK_COMPONENT_SWIZZLE_ZERO
          : VK_COMPONENT_SWIZZLE_IDENTITY;
  null_image_view_create_info.components.r = null_image_view_swizzle;
  null_image_view_create_info.components.g = null_image_view_swizzle;
  null_image_view_create_info.components.b = null_image_view_swizzle;
  null_image_view_create_info.components.a = null_image_view_swizzle;
  null_image_view_create_info.subresourceRange =
      ui::vulkan::util::InitializeSubresourceRange(
          VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1);
  if (dfn.vkCreateImageView(device, &null_image_view_create_info, nullptr,
                            &null_image_view_2d_array_) != VK_SUCCESS) {
    XELOGE("VulkanTextureCache: Failed to create the null 2D array image view");
    return false;
  }
  null_image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
  null_image_view_create_info.subresourceRange.layerCount = 6;
  if (dfn.vkCreateImageView(device, &null_image_view_create_info, nullptr,
                            &null_image_view_cube_) != VK_SUCCESS) {
    XELOGE("VulkanTextureCache: Failed to create the null cube image view");
    return false;
  }
  null_image_view_create_info.image = null_image_3d_;
  null_image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
  null_image_view_create_info.subresourceRange.layerCount = 1;
  if (dfn.vkCreateImageView(device, &null_image_view_create_info, nullptr,
                            &null_image_view_3d_) != VK_SUCCESS) {
    XELOGE("VulkanTextureCache: Failed to create the null 3D image view");
    return false;
  }

  null_images_cleared_ = false;

  return true;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
