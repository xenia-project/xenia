/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/texture_cache.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::CheckResult;

constexpr uint32_t kMaxTextureSamplers = 32;

struct TextureConfig {
  TextureFormat guest_format;
  VkFormat host_format;
};

TextureCache::TextureCache(Memory* memory, RegisterFile* register_file,
                           TraceWriter* trace_writer,
                           ui::vulkan::VulkanDevice* device)
    : memory_(memory),
      register_file_(register_file),
      trace_writer_(trace_writer),
      device_(device),
      staging_buffer_(device) {
  // Descriptor pool used for all of our cached descriptors.
  VkDescriptorPoolCreateInfo descriptor_pool_info;
  descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptor_pool_info.pNext = nullptr;
  descriptor_pool_info.flags =
      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  descriptor_pool_info.maxSets = 4096;
  VkDescriptorPoolSize pool_sizes[2];
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
  pool_sizes[0].descriptorCount = 32;
  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_sizes[1].descriptorCount = 32;
  descriptor_pool_info.poolSizeCount = 2;
  descriptor_pool_info.pPoolSizes = pool_sizes;
  auto err = vkCreateDescriptorPool(*device_, &descriptor_pool_info, nullptr,
                                    &descriptor_pool_);
  CheckResult(err, "vkCreateDescriptorPool");

  // Create the descriptor set layout used for rendering.
  // We always have the same number of samplers but only some are used.
  VkDescriptorSetLayoutBinding bindings[5];
  auto& sampler_binding = bindings[0];
  sampler_binding.binding = 0;
  sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  sampler_binding.descriptorCount = kMaxTextureSamplers;
  sampler_binding.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  sampler_binding.pImmutableSamplers = nullptr;
  for (int i = 0; i < 4; ++i) {
    auto& texture_binding = bindings[1 + i];
    texture_binding.binding = 1 + i;
    texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_binding.descriptorCount = kMaxTextureSamplers;
    texture_binding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    texture_binding.pImmutableSamplers = nullptr;
  }
  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info;
  descriptor_set_layout_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_info.pNext = nullptr;
  descriptor_set_layout_info.flags = 0;
  descriptor_set_layout_info.bindingCount =
      static_cast<uint32_t>(xe::countof(bindings));
  descriptor_set_layout_info.pBindings = bindings;
  err = vkCreateDescriptorSetLayout(*device_, &descriptor_set_layout_info,
                                    nullptr, &texture_descriptor_set_layout_);
  CheckResult(err, "vkCreateDescriptorSetLayout");

  int width = 4096;
  int height = 4096;
  if (!staging_buffer_.Initialize(width * height * 4,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
    assert_always();
  }

  // Upload a grid into the staging buffer.
  auto gpu_data = reinterpret_cast<uint32_t*>(staging_buffer_.host_base());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      gpu_data[y * width + x] =
          ((y % 32 < 16) ^ (x % 32 >= 16)) ? 0xFF0000FF : 0xFFFFFFFF;
    }
  }
}

TextureCache::~TextureCache() {
  vkDestroyDescriptorSetLayout(*device_, texture_descriptor_set_layout_,
                               nullptr);
  vkDestroyDescriptorPool(*device_, descriptor_pool_, nullptr);
}

TextureCache::Texture* TextureCache::AllocateTexture(
    const TextureInfo& texture_info) {
  // Create an image first.
  VkImageCreateInfo image_info = {};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  switch (texture_info.dimension) {
    case Dimension::k1D:
      image_info.imageType = VK_IMAGE_TYPE_1D;
      break;
    case Dimension::k2D:
      image_info.imageType = VK_IMAGE_TYPE_2D;
      break;
    case Dimension::k3D:
      image_info.imageType = VK_IMAGE_TYPE_3D;
      break;
    case Dimension::kCube:
      image_info.imageType = VK_IMAGE_TYPE_2D;
      image_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
      break;
    default:
      assert_unhandled_case(texture_info.dimension);
      return nullptr;
  }

  // TODO: Format
  image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  image_info.extent = {texture_info.width + 1, texture_info.height + 1,
                       texture_info.depth + 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.queueFamilyIndexCount = 0;
  image_info.pQueueFamilyIndices = nullptr;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage image;
  auto err = vkCreateImage(*device_, &image_info, nullptr, &image);
  CheckResult(err, "vkCreateImage");

  VkMemoryRequirements mem_requirements;
  vkGetImageMemoryRequirements(*device_, image, &mem_requirements);

  // TODO: Use a circular buffer or something else to allocate this memory.
  // The device has a limited amount (around 64) of memory allocations that we
  // can make.
  // Now that we have the size, back the image with GPU memory.
  auto memory = device_->AllocateMemory(mem_requirements, 0);
  if (!memory) {
    // Crap.
    assert_always();
    vkDestroyImage(*device_, image, nullptr);
    return nullptr;
  }

  err = vkBindImageMemory(*device_, image, memory, 0);
  CheckResult(err, "vkBindImageMemory");

  auto texture = new Texture();
  texture->format = image_info.format;
  texture->image = image;
  texture->image_layout = image_info.initialLayout;
  texture->image_memory = memory;
  texture->memory_offset = 0;
  texture->memory_size = mem_requirements.size;
  texture->texture_info = texture_info;

  // Create a default view, just for kicks.
  VkImageViewCreateInfo view_info;
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.pNext = nullptr;
  view_info.flags = 0;
  view_info.image = image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = image_info.format;
  view_info.components = {
      VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
      VK_COMPONENT_SWIZZLE_A,
  };
  view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView view;
  err = vkCreateImageView(*device_, &view_info, nullptr, &view);
  CheckResult(err, "vkCreateImageView");
  if (err == VK_SUCCESS) {
    auto texture_view = std::make_unique<TextureView>();
    texture_view->texture = texture;
    texture_view->view = view;
    texture_view->swiz_x = 0;
    texture_view->swiz_y = 1;
    texture_view->swiz_z = 2;
    texture_view->swiz_w = 3;
    texture->views.push_back(std::move(texture_view));
  }

  return texture;
}

bool TextureCache::FreeTexture(Texture* texture) {
  // TODO(DrChat)
  return false;
}

TextureCache::Texture* TextureCache::DemandResolveTexture(
    const TextureInfo& texture_info, TextureFormat format,
    uint32_t* out_offset_x, uint32_t* out_offset_y) {
  // Check to see if we've already used a texture at this location.
  auto texture = LookupAddress(
      texture_info.guest_address, texture_info.size_2d.block_width,
      texture_info.size_2d.block_height, format, out_offset_x, out_offset_y);
  if (texture) {
    return texture;
  }

  // No texture at this location. Make a new one.
  texture = AllocateTexture(texture_info);
  texture->is_full_texture = false;
  resolve_textures_.push_back(std::unique_ptr<Texture>(texture));
  return texture;
}

TextureCache::Texture* TextureCache::Demand(
    const TextureInfo& texture_info, VkCommandBuffer command_buffer,
    std::shared_ptr<ui::vulkan::Fence> completion_fence) {
  // Run a tight loop to scan for an exact match existing texture.
  auto texture_hash = texture_info.hash();
  for (auto it = textures_.find(texture_hash); it != textures_.end(); ++it) {
    if (it->second->texture_info == texture_info) {
      return it->second.get();
    }
  }

  // Check resolve textures.
  for (auto it = resolve_textures_.begin(); it != resolve_textures_.end();
       ++it) {
    auto texture = (*it).get();
    if (texture_info.guest_address == texture->texture_info.guest_address &&
        texture_info.size_2d.logical_width ==
            texture->texture_info.size_2d.logical_width &&
        texture_info.size_2d.logical_height ==
            texture->texture_info.size_2d.logical_height) {
      // Exact match.
      // TODO: Lazy match (at an offset)
      // Upgrade this texture to a full texture.
      texture->is_full_texture = true;
      texture->texture_info = texture_info;
      textures_[texture_hash] = std::move(*it);
      it = resolve_textures_.erase(it);
      return textures_[texture_hash].get();
    }
  }

  if (!command_buffer) {
    // Texture not found and no command buffer was passed, preventing us from
    // uploading a new one.
    return nullptr;
  }

  // Create a new texture and cache it.
  auto texture = AllocateTexture(texture_info);
  if (!texture) {
    // Failed to allocate texture (out of memory?)
    assert_always();
    return nullptr;
  }

  bool uploaded = false;
  switch (texture_info.dimension) {
    case Dimension::k2D: {
      uploaded = UploadTexture2D(command_buffer, completion_fence, texture,
                                 texture_info);
    } break;
    default:
      assert_unhandled_case(texture_info.dimension);
      break;
  }

  // Okay. Now that the texture is uploaded from system memory, put a writewatch
  // on it to tell us if it's been modified from the guest.

  if (!uploaded) {
    // TODO: Destroy the texture.
    assert_always();
    return nullptr;
  }

  // Though we didn't find an exact match, that doesn't mean we're out of the
  // woods yet. This texture could either be a portion of another texture or
  // vice versa. Copy any overlapping textures into this texture.
  // TODO: Byte count -> pixel count (on x and y axes)
  for (auto it = textures_.begin(); it != textures_.end(); ++it) {
  }

  textures_[texture_hash] = std::unique_ptr<Texture>(texture);

  return texture;
}

TextureCache::TextureView* TextureCache::DemandView(Texture* texture,
                                                    uint16_t swizzle) {
  for (auto it = texture->views.begin(); it != texture->views.end(); ++it) {
    if ((*it)->swizzle == swizzle) {
      return (*it).get();
    }
  }

  VkImageViewCreateInfo view_info;
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.pNext = nullptr;
  view_info.flags = 0;
  view_info.image = texture->image;
  view_info.format = texture->format;

  switch (texture->texture_info.dimension) {
    case Dimension::k1D:
      view_info.viewType = VK_IMAGE_VIEW_TYPE_1D;
      break;
    case Dimension::k2D:
      view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
      break;
    case Dimension::k3D:
      view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
      break;
    case Dimension::kCube:
      view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
      break;
    default:
      assert_always();
  }

  VkComponentSwizzle swiz_component_map[] = {
      VK_COMPONENT_SWIZZLE_R,        VK_COMPONENT_SWIZZLE_G,
      VK_COMPONENT_SWIZZLE_B,        VK_COMPONENT_SWIZZLE_A,
      VK_COMPONENT_SWIZZLE_ONE,      VK_COMPONENT_SWIZZLE_ZERO,
      VK_COMPONENT_SWIZZLE_IDENTITY,
  };

  view_info.components = {
      swiz_component_map[(swizzle >> 0) & 0x7],
      swiz_component_map[(swizzle >> 3) & 0x7],
      swiz_component_map[(swizzle >> 6) & 0x7],
      swiz_component_map[(swizzle >> 9) & 0x7],
  };
  view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView view;
  auto status = vkCreateImageView(*device_, &view_info, nullptr, &view);
  CheckResult(status, "vkCreateImageView");
  if (status == VK_SUCCESS) {
    auto texture_view = new TextureView();
    texture_view->texture = texture;
    texture_view->view = view;
    texture_view->swizzle = swizzle;
    texture->views.push_back(std::unique_ptr<TextureView>(texture_view));
    return texture_view;
  }

  return nullptr;
}

TextureCache::Sampler* TextureCache::Demand(const SamplerInfo& sampler_info) {
  auto sampler_hash = sampler_info.hash();
  for (auto it = samplers_.find(sampler_hash); it != samplers_.end(); ++it) {
    if (it->second->sampler_info == sampler_info) {
      // Found a compatible sampler.
      return it->second.get();
    }
  }

  VkResult status = VK_SUCCESS;

  // Create a new sampler and cache it.
  // TODO: Actually set the properties
  VkSamplerCreateInfo sampler_create_info;
  sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_create_info.pNext = nullptr;
  sampler_create_info.flags = 0;
  sampler_create_info.minFilter = VK_FILTER_NEAREST;
  sampler_create_info.magFilter = VK_FILTER_NEAREST;
  sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

  // FIXME: Both halfway / mirror clamp to border aren't mapped properly.
  VkSamplerAddressMode address_mode_map[] = {
      /* kRepeat               */ VK_SAMPLER_ADDRESS_MODE_REPEAT,
      /* kMirroredRepeat       */ VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
      /* kClampToEdge          */ VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      /* kMirrorClampToEdge    */ VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
      /* kClampToHalfway       */ VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      /* kMirrorClampToHalfway */ VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
      /* kClampToBorder        */ VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      /* kMirrorClampToBorder  */ VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
  };
  sampler_create_info.addressModeU =
      address_mode_map[static_cast<int>(sampler_info.clamp_u)];
  sampler_create_info.addressModeV =
      address_mode_map[static_cast<int>(sampler_info.clamp_v)];
  sampler_create_info.addressModeW =
      address_mode_map[static_cast<int>(sampler_info.clamp_w)];

  sampler_create_info.mipLodBias = 0.0f;
  sampler_create_info.anisotropyEnable = VK_FALSE;
  sampler_create_info.maxAnisotropy = 1.0f;
  sampler_create_info.compareEnable = VK_FALSE;
  sampler_create_info.compareOp = VK_COMPARE_OP_NEVER;
  sampler_create_info.minLod = 0.0f;
  sampler_create_info.maxLod = 0.0f;
  sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
  sampler_create_info.unnormalizedCoordinates = VK_FALSE;
  VkSampler vk_sampler;
  status =
      vkCreateSampler(*device_, &sampler_create_info, nullptr, &vk_sampler);
  CheckResult(status, "vkCreateSampler");
  if (status != VK_SUCCESS) {
    return nullptr;
  }

  auto sampler = new Sampler();
  sampler->sampler = vk_sampler;
  sampler->sampler_info = sampler_info;
  samplers_[sampler_hash] = std::unique_ptr<Sampler>(sampler);

  return sampler;
}

TextureCache::Texture* TextureCache::LookupAddress(
    uint32_t guest_address, uint32_t width, uint32_t height,
    TextureFormat format, uint32_t* offset_x, uint32_t* offset_y) {
  for (auto it = textures_.begin(); it != textures_.end(); ++it) {
    const auto& texture_info = it->second->texture_info;
    if (guest_address >= texture_info.guest_address &&
        guest_address <
            texture_info.guest_address + texture_info.input_length &&
        offset_x && offset_y) {
      auto offset_bytes = guest_address - texture_info.guest_address;

      if (texture_info.dimension == Dimension::k2D) {
        *offset_y = offset_bytes / texture_info.size_2d.input_pitch;
        if (offset_bytes % texture_info.size_2d.input_pitch != 0) {
          // TODO: offset_x
        }
      }

      return it->second.get();
    }

    if (texture_info.guest_address == guest_address &&
        texture_info.dimension == Dimension::k2D &&
        texture_info.size_2d.input_width == width &&
        texture_info.size_2d.input_height == height) {
      return it->second.get();
    }
  }

  // Check resolve textures
  for (auto it = resolve_textures_.begin(); it != resolve_textures_.end();
       ++it) {
    const auto& texture_info = (*it)->texture_info;
    if (guest_address >= texture_info.guest_address &&
        guest_address <
            texture_info.guest_address + texture_info.input_length &&
        offset_x && offset_y) {
      auto offset_bytes = guest_address - texture_info.guest_address;

      if (texture_info.dimension == Dimension::k2D) {
        *offset_y = offset_bytes / texture_info.size_2d.input_pitch;
        if (offset_bytes % texture_info.size_2d.input_pitch != 0) {
          // TODO: offset_x
        }
      }

      return (*it).get();
    }

    if (texture_info.guest_address == guest_address &&
        texture_info.dimension == Dimension::k2D &&
        texture_info.size_2d.input_width == width &&
        texture_info.size_2d.input_height == height) {
      return (*it).get();
    }
  }

  return nullptr;
}

void TextureSwap(Endian endianness, void* dest, const void* src,
                 size_t length) {
  switch (endianness) {
    case Endian::k8in16:
      xe::copy_and_swap_16_aligned(dest, src, length / 2);
      break;
    case Endian::k8in32:
      xe::copy_and_swap_32_aligned(dest, src, length / 4);
      break;
    case Endian::k16in32:  // Swap high and low 16 bits within a 32 bit word
      xe::copy_and_swap_16_in_32_aligned(dest, src, length);
      break;
    default:
    case Endian::kUnspecified:
      std::memcpy(dest, src, length);
      break;
  }
}

bool TextureCache::UploadTexture2D(
    VkCommandBuffer command_buffer,
    std::shared_ptr<ui::vulkan::Fence> completion_fence, Texture* dest,
    TextureInfo src) {
  SCOPE_profile_cpu_f("gpu");
  assert_true(src.dimension == Dimension::k2D);

  if (!staging_buffer_.CanAcquire(src.input_length)) {
    // Need to have unique memory for every upload for at least one frame. If we
    // run out of memory, we need to flush all queued upload commands to the
    // GPU.
    // TODO: Actually flush commands.
    assert_always();
  }

  // Grab some temporary memory for staging.
  auto alloc = staging_buffer_.Acquire(src.input_length, completion_fence);
  assert_not_null(alloc);

  // TODO: Support these cases.
  // assert_false(src.is_tiled);
  // assert_false(src.is_compressed());

  // Upload texture into GPU memory.
  // TODO: If the GPU supports it, we can submit a compute batch to convert the
  // texture and copy it to its destination. Otherwise, fallback to conversion
  // on the CPU.
  auto guest_ptr = memory_->TranslatePhysical(src.guest_address);
  TextureSwap(src.endianness, alloc->host_ptr, guest_ptr, src.input_length);
  staging_buffer_.Flush(alloc);

  // Insert a memory barrier into the command buffer to ensure the upload has
  // finished before we copy it into the destination texture.
  VkBufferMemoryBarrier upload_barrier = {
      VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      NULL,
      VK_ACCESS_HOST_WRITE_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      staging_buffer_.gpu_buffer(),
      alloc->offset,
      alloc->aligned_length,
  };
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 1,
                       &upload_barrier, 0, nullptr);

  // Transition the texture into a transfer destination layout.
  VkImageMemoryBarrier barrier;
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.pNext = nullptr;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask =
      VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = dest->image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  assert_true(src.size_2d.input_width >=
              dest->texture_info.size_2d.output_width);
  assert_true(src.size_2d.input_height >=
              dest->texture_info.size_2d.output_height);

  // For now, just transfer the grid we uploaded earlier into the texture.
  VkBufferImageCopy copy_region;
  copy_region.bufferOffset = alloc->offset;
  copy_region.bufferRowLength = src.width + 1;
  copy_region.bufferImageHeight = src.height + 1;
  copy_region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy_region.imageOffset = {0, 0, 0};
  copy_region.imageExtent = {dest->texture_info.width + 1,
                             dest->texture_info.height + 1,
                             dest->texture_info.depth + 1};
  vkCmdCopyBufferToImage(command_buffer, staging_buffer_.gpu_buffer(),
                         dest->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &copy_region);

  // Now transition the texture into a shader readonly source.
  barrier.srcAccessMask = barrier.dstAccessMask;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barrier.oldLayout = barrier.newLayout;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  dest->image_layout = barrier.newLayout;
  return true;
}

VkDescriptorSet TextureCache::PrepareTextureSet(
    VkCommandBuffer command_buffer,
    std::shared_ptr<ui::vulkan::Fence> completion_fence,
    const std::vector<Shader::TextureBinding>& vertex_bindings,
    const std::vector<Shader::TextureBinding>& pixel_bindings) {
  // Clear state.
  auto update_set_info = &update_set_info_;
  update_set_info->has_setup_fetch_mask = 0;
  update_set_info->image_1d_write_count = 0;
  update_set_info->image_2d_write_count = 0;
  update_set_info->image_3d_write_count = 0;
  update_set_info->image_cube_write_count = 0;

  std::memset(update_set_info, 0, sizeof(update_set_info_));

  // Process vertex and pixel shader bindings.
  // This does things lazily and de-dupes fetch constants reused in both
  // shaders.
  bool any_failed = false;
  any_failed = !SetupTextureBindings(command_buffer, completion_fence,
                                     update_set_info, vertex_bindings) ||
               any_failed;
  any_failed = !SetupTextureBindings(command_buffer, completion_fence,
                                     update_set_info, pixel_bindings) ||
               any_failed;
  if (any_failed) {
    XELOGW("Failed to setup one or more texture bindings");
    // TODO(benvanik): actually bail out here?
  }

  // TODO(benvanik): reuse.
  VkDescriptorSet descriptor_set = nullptr;
  VkDescriptorSetAllocateInfo set_alloc_info;
  set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_alloc_info.pNext = nullptr;
  set_alloc_info.descriptorPool = descriptor_pool_;
  set_alloc_info.descriptorSetCount = 1;
  set_alloc_info.pSetLayouts = &texture_descriptor_set_layout_;
  auto err =
      vkAllocateDescriptorSets(*device_, &set_alloc_info, &descriptor_set);
  CheckResult(err, "vkAllocateDescriptorSets");

  // Write all updated descriptors.
  // TODO(benvanik): optimize? split into multiple sets? set per type?
  VkWriteDescriptorSet descriptor_writes[4];
  std::memset(descriptor_writes, 0, sizeof(descriptor_writes));
  uint32_t descriptor_write_count = 0;
  /*
  // TODO(DrChat): Do we really need to separate samplers and images here?
  if (update_set_info->sampler_write_count) {
    auto& sampler_write = descriptor_writes[descriptor_write_count++];
    sampler_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sampler_write.pNext = nullptr;
    sampler_write.dstSet = descriptor_set;
    sampler_write.dstBinding = 0;
    sampler_write.dstArrayElement = 0;
    sampler_write.descriptorCount = update_set_info->sampler_write_count;
    sampler_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    sampler_write.pImageInfo = update_set_info->sampler_infos;
  }
  */
  // FIXME: These are not be lined up properly with tf binding points!!!!!
  if (update_set_info->image_1d_write_count) {
    auto& image_write = descriptor_writes[descriptor_write_count++];
    image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    image_write.pNext = nullptr;
    image_write.dstSet = descriptor_set;
    image_write.dstBinding = 1;
    image_write.dstArrayElement = 0;
    image_write.descriptorCount = update_set_info->image_1d_write_count;
    image_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    image_write.pImageInfo = update_set_info->image_1d_infos;
  }
  if (update_set_info->image_2d_write_count) {
    auto& image_write = descriptor_writes[descriptor_write_count++];
    image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    image_write.pNext = nullptr;
    image_write.dstSet = descriptor_set;
    image_write.dstBinding = 2;
    image_write.dstArrayElement = 0;
    image_write.descriptorCount = update_set_info->image_2d_write_count;
    image_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    image_write.pImageInfo = update_set_info->image_2d_infos;
  }
  if (update_set_info->image_3d_write_count) {
    auto& image_write = descriptor_writes[descriptor_write_count++];
    image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    image_write.pNext = nullptr;
    image_write.dstSet = descriptor_set;
    image_write.dstBinding = 3;
    image_write.dstArrayElement = 0;
    image_write.descriptorCount = update_set_info->image_3d_write_count;
    image_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    image_write.pImageInfo = update_set_info->image_3d_infos;
  }
  if (update_set_info->image_cube_write_count) {
    auto& image_write = descriptor_writes[descriptor_write_count++];
    image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    image_write.pNext = nullptr;
    image_write.dstSet = descriptor_set;
    image_write.dstBinding = 4;
    image_write.dstArrayElement = 0;
    image_write.descriptorCount = update_set_info->image_cube_write_count;
    image_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    image_write.pImageInfo = update_set_info->image_cube_infos;
  }
  if (descriptor_write_count) {
    vkUpdateDescriptorSets(*device_, descriptor_write_count, descriptor_writes,
                           0, nullptr);
  }

  in_flight_sets_.push_back({descriptor_set, completion_fence});
  return descriptor_set;
}

bool TextureCache::SetupTextureBindings(
    VkCommandBuffer command_buffer,
    std::shared_ptr<ui::vulkan::Fence> completion_fence,
    UpdateSetInfo* update_set_info,
    const std::vector<Shader::TextureBinding>& bindings) {
  bool any_failed = false;
  for (auto& binding : bindings) {
    uint32_t fetch_bit = 1 << binding.fetch_constant;
    if ((update_set_info->has_setup_fetch_mask & fetch_bit) == 0) {
      // Needs setup.
      any_failed = !SetupTextureBinding(command_buffer, completion_fence,
                                        update_set_info, binding) ||
                   any_failed;
      update_set_info->has_setup_fetch_mask |= fetch_bit;
    }
  }
  return !any_failed;
}

bool TextureCache::SetupTextureBinding(
    VkCommandBuffer command_buffer,
    std::shared_ptr<ui::vulkan::Fence> completion_fence,
    UpdateSetInfo* update_set_info, const Shader::TextureBinding& binding) {
  auto& regs = *register_file_;
  int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + binding.fetch_constant * 6;
  auto group =
      reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(&regs.values[r]);
  auto& fetch = group->texture_fetch;

  // Disabled?
  // TODO(benvanik): reset sampler.
  if (!fetch.type) {
    return true;
  }
  assert_true(fetch.type == 0x2);

  TextureInfo texture_info;
  if (!TextureInfo::Prepare(fetch, &texture_info)) {
    XELOGE("Unable to parse texture fetcher info");
    return false;  // invalid texture used
  }
  SamplerInfo sampler_info;
  if (!SamplerInfo::Prepare(fetch, binding.fetch_instr, &sampler_info)) {
    XELOGE("Unable to parse sampler info");
    return false;  // invalid texture used
  }

  auto texture = Demand(texture_info, command_buffer, completion_fence);
  auto sampler = Demand(sampler_info);
  assert_true(texture != nullptr && sampler != nullptr);
  if (texture == nullptr || sampler == nullptr) {
    return false;
  }

  uint16_t swizzle = static_cast<uint16_t>(fetch.swizzle);
  auto view = DemandView(texture, swizzle);

  trace_writer_->WriteMemoryRead(texture_info.guest_address,
                                 texture_info.input_length);

  VkDescriptorImageInfo* image_write = nullptr;
  switch (texture_info.dimension) {
    case Dimension::k1D:
      image_write =
          &update_set_info
               ->image_1d_infos[update_set_info->image_1d_write_count++];
      break;
    case Dimension::k2D:
      image_write =
          &update_set_info
               ->image_2d_infos[update_set_info->image_2d_write_count++];
      break;
    case Dimension::k3D:
      image_write =
          &update_set_info
               ->image_3d_infos[update_set_info->image_3d_write_count++];
      break;
    case Dimension::kCube:
      image_write =
          &update_set_info
               ->image_cube_infos[update_set_info->image_cube_write_count++];
      break;
    default:
      assert_unhandled_case(texture_info.dimension);
      return false;
  }
  image_write->imageView = view->view;
  image_write->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  image_write->sampler = sampler->sampler;

  return true;
}

void TextureCache::ClearCache() {
  // TODO(benvanik): caching.
}

void TextureCache::Scavenge() {
  // Free unused descriptor sets
  for (auto it = in_flight_sets_.begin(); it != in_flight_sets_.end();) {
    if (vkGetFenceStatus(*device_, *it->second) == VK_SUCCESS) {
      // We can free this one.
      vkFreeDescriptorSets(*device_, descriptor_pool_, 1, &it->first);
      it = in_flight_sets_.erase(it);
      continue;
    }

    ++it;
  }

  staging_buffer_.Scavenge();
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
