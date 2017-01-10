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
constexpr VkDeviceSize kStagingBufferSize = 32 * 1024 * 1024;

struct TextureConfig {
  TextureFormat guest_format;
  VkFormat host_format;
};

static const TextureConfig texture_configs[64] = {
    {TextureFormat::k_1_REVERSE, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_1, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_8, VK_FORMAT_R8_UNORM},
    {TextureFormat::k_1_5_5_5, VK_FORMAT_R5G5B5A1_UNORM_PACK16},
    {TextureFormat::k_5_6_5, VK_FORMAT_R5G6B5_UNORM_PACK16},
    {TextureFormat::k_6_5_5, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_8_8_8_8, VK_FORMAT_R8G8B8A8_UNORM},
    {TextureFormat::k_2_10_10_10, VK_FORMAT_A2R10G10B10_UNORM_PACK32},
    {TextureFormat::k_8_A, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_8_B, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_8_8, VK_FORMAT_R8G8_UNORM},
    {TextureFormat::k_Cr_Y1_Cb_Y0, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_Y1_Cr_Y0_Cb, VK_FORMAT_UNDEFINED},
    {TextureFormat::kUnknown, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_8_8_8_8_A, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_4_4_4_4, VK_FORMAT_R4G4B4A4_UNORM_PACK16},
    {TextureFormat::k_10_11_11, VK_FORMAT_B10G11R11_UFLOAT_PACK32},  // ?
    {TextureFormat::k_11_11_10, VK_FORMAT_B10G11R11_UFLOAT_PACK32},  // ?
    {TextureFormat::k_DXT1, VK_FORMAT_BC1_RGBA_SRGB_BLOCK},
    {TextureFormat::k_DXT2_3, VK_FORMAT_BC2_SRGB_BLOCK},
    {TextureFormat::k_DXT4_5, VK_FORMAT_BC3_SRGB_BLOCK},
    {TextureFormat::kUnknown, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_24_8, VK_FORMAT_D24_UNORM_S8_UINT},
    {TextureFormat::k_24_8_FLOAT, VK_FORMAT_D24_UNORM_S8_UINT},  // ?
    {TextureFormat::k_16, VK_FORMAT_R16_UNORM},
    {TextureFormat::k_16_16, VK_FORMAT_R16G16_UNORM},
    {TextureFormat::k_16_16_16_16, VK_FORMAT_R16G16B16A16_UNORM},
    {TextureFormat::k_16_EXPAND, VK_FORMAT_R16_UNORM},                    // ?
    {TextureFormat::k_16_16_EXPAND, VK_FORMAT_R16G16_UNORM},              // ?
    {TextureFormat::k_16_16_16_16_EXPAND, VK_FORMAT_R16G16B16A16_UNORM},  // ?
    {TextureFormat::k_16_FLOAT, VK_FORMAT_R16_SFLOAT},
    {TextureFormat::k_16_16_FLOAT, VK_FORMAT_R16G16_SFLOAT},
    {TextureFormat::k_16_16_16_16_FLOAT, VK_FORMAT_R16G16B16A16_SFLOAT},
    {TextureFormat::k_32, VK_FORMAT_R32_SINT},
    {TextureFormat::k_32_32, VK_FORMAT_R32G32_SINT},
    {TextureFormat::k_32_32_32_32, VK_FORMAT_R32G32B32A32_SINT},
    {TextureFormat::k_32_FLOAT, VK_FORMAT_R32_SFLOAT},
    {TextureFormat::k_32_32_FLOAT, VK_FORMAT_R32G32_SFLOAT},
    {TextureFormat::k_32_32_32_32_FLOAT, VK_FORMAT_R32G32B32A32_SFLOAT},
    {TextureFormat::k_32_AS_8, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_32_AS_8_8, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_16_MPEG, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_16_16_MPEG, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_8_INTERLACED, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_32_AS_8_INTERLACED, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_32_AS_8_8_INTERLACED, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_16_INTERLACED, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_16_MPEG_INTERLACED, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_16_16_MPEG_INTERLACED, VK_FORMAT_UNDEFINED},

    // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
    {TextureFormat::k_DXN, VK_FORMAT_BC5_UNORM_BLOCK},  // ?
    {TextureFormat::k_8_8_8_8_AS_16_16_16_16, VK_FORMAT_R8G8B8A8_UNORM},
    {TextureFormat::k_DXT1_AS_16_16_16_16, VK_FORMAT_BC1_RGB_UNORM_BLOCK},
    {TextureFormat::k_DXT2_3_AS_16_16_16_16, VK_FORMAT_BC2_UNORM_BLOCK},
    {TextureFormat::k_DXT4_5_AS_16_16_16_16, VK_FORMAT_BC3_UNORM_BLOCK},
    {TextureFormat::k_2_10_10_10_AS_16_16_16_16,
     VK_FORMAT_A2R10G10B10_UNORM_PACK32},
    {TextureFormat::k_10_11_11_AS_16_16_16_16,
     VK_FORMAT_B10G11R11_UFLOAT_PACK32},  // ?
    {TextureFormat::k_11_11_10_AS_16_16_16_16,
     VK_FORMAT_B10G11R11_UFLOAT_PACK32},  // ?
    {TextureFormat::k_32_32_32_FLOAT, VK_FORMAT_R32G32B32_SFLOAT},
    {TextureFormat::k_DXT3A, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_DXT5A, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_CTX1, VK_FORMAT_UNDEFINED},
    {TextureFormat::k_DXT3A_AS_1_1_1_1, VK_FORMAT_UNDEFINED},
    {TextureFormat::kUnknown, VK_FORMAT_UNDEFINED},
    {TextureFormat::kUnknown, VK_FORMAT_UNDEFINED},
};

TextureCache::TextureCache(Memory* memory, RegisterFile* register_file,
                           TraceWriter* trace_writer,
                           ui::vulkan::VulkanDevice* device)
    : memory_(memory),
      register_file_(register_file),
      trace_writer_(trace_writer),
      device_(device),
      staging_buffer_(device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      kStagingBufferSize) {
  // Descriptor pool used for all of our cached descriptors.
  VkDescriptorPoolCreateInfo descriptor_pool_info;
  descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptor_pool_info.pNext = nullptr;
  descriptor_pool_info.flags =
      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  descriptor_pool_info.maxSets = 32768;
  VkDescriptorPoolSize pool_sizes[1];
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_sizes[0].descriptorCount = 32768;
  descriptor_pool_info.poolSizeCount = 1;
  descriptor_pool_info.pPoolSizes = pool_sizes;
  auto err = vkCreateDescriptorPool(*device_, &descriptor_pool_info, nullptr,
                                    &descriptor_pool_);
  CheckResult(err, "vkCreateDescriptorPool");

  // Create the descriptor set layout used for rendering.
  // We always have the same number of samplers but only some are used.
  VkDescriptorSetLayoutBinding bindings[4];
  for (int i = 0; i < 4; ++i) {
    auto& texture_binding = bindings[i];
    texture_binding.binding = i;
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

  if (!staging_buffer_.Initialize()) {
    assert_always();
  }

  invalidated_textures_sets_[0].reserve(64);
  invalidated_textures_sets_[1].reserve(64);
  invalidated_textures_ = &invalidated_textures_sets_[0];

  device_queue_ = device_->AcquireQueue();
}

TextureCache::~TextureCache() {
  if (device_queue_) {
    device_->ReleaseQueue(device_queue_);
  }

  for (auto it = samplers_.begin(); it != samplers_.end(); ++it) {
    vkDestroySampler(*device_, it->second->sampler, nullptr);
    delete it->second;
  }
  samplers_.clear();

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

  assert_not_null(texture_info.format_info);
  auto& config = texture_configs[int(texture_info.format_info->format)];
  VkFormat format = config.host_format != VK_FORMAT_UNDEFINED
                        ? config.host_format
                        : VK_FORMAT_R8G8B8A8_UNORM;

  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  // Check the device limits for the format before we create it.
  VkFormatProperties props;
  vkGetPhysicalDeviceFormatProperties(*device_, format, &props);
  uint32_t required_flags = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                            VK_FORMAT_FEATURE_BLIT_DST_BIT |
                            VK_FORMAT_FEATURE_BLIT_SRC_BIT;
  if ((props.optimalTilingFeatures & required_flags) != required_flags) {
    // Texture needs conversion on upload to a native format.
    // assert_always();
  }

  VkImageFormatProperties image_props;
  vkGetPhysicalDeviceImageFormatProperties(
      *device_, format, image_info.imageType, image_info.tiling,
      image_info.usage, image_info.flags, &image_props);

  // TODO(DrChat): Actually check the image properties.

  image_info.format = format;
  image_info.extent = {texture_info.width + 1, texture_info.height + 1,
                       texture_info.depth + 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
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
  texture->access_watch_handle = 0;
  texture->texture_info = texture_info;
  return texture;
}

bool TextureCache::FreeTexture(Texture* texture) {
  if (texture->in_flight_fence &&
      vkGetFenceStatus(*device_, texture->in_flight_fence) != VK_SUCCESS) {
    // Texture still in flight.
    return false;
  }

  for (auto it = texture->views.begin(); it != texture->views.end();) {
    vkDestroyImageView(*device_, (*it)->view, nullptr);
    it = texture->views.erase(it);
  }

  if (texture->access_watch_handle) {
    memory_->CancelAccessWatch(texture->access_watch_handle);
    texture->access_watch_handle = 0;
  }

  vkDestroyImage(*device_, texture->image, nullptr);
  vkFreeMemory(*device_, texture->image_memory, nullptr);
  delete texture;
  return true;
}

TextureCache::Texture* TextureCache::DemandResolveTexture(
    const TextureInfo& texture_info, TextureFormat format,
    VkOffset2D* out_offset) {
  // Check to see if we've already used a texture at this location.
  auto texture = LookupAddress(
      texture_info.guest_address, texture_info.size_2d.block_width,
      texture_info.size_2d.block_height, format, out_offset);
  if (texture) {
    return texture;
  }

  // No texture at this location. Make a new one.
  texture = AllocateTexture(texture_info);
  texture->is_full_texture = false;

  // Setup a debug name for the texture.
  device_->DbgSetObjectName(
      reinterpret_cast<uint64_t>(texture->image),
      VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT,
      xe::format_string(
          "0x%.8X - 0x%.8X", texture_info.guest_address,
          texture_info.guest_address + texture_info.output_length));

  // Setup an access watch. If this texture is touched, it is destroyed.
  texture->access_watch_handle = memory_->AddPhysicalAccessWatch(
      texture_info.guest_address, texture_info.input_length,
      cpu::MMIOHandler::kWatchWrite,
      [](void* context_ptr, void* data_ptr, uint32_t address) {
        auto self = reinterpret_cast<TextureCache*>(context_ptr);
        auto touched_texture = reinterpret_cast<Texture*>(data_ptr);
        // Clear watch handle first so we don't redundantly
        // remove.
        touched_texture->access_watch_handle = 0;
        touched_texture->pending_invalidation = true;
        // Add to pending list so Scavenge will clean it up.
        self->invalidated_resolve_textures_mutex_.lock();
        self->invalidated_resolve_textures_.push_back(touched_texture);
        self->invalidated_resolve_textures_mutex_.unlock();
      },
      this, texture);

  resolve_textures_.push_back(texture);
  return texture;
}

TextureCache::Texture* TextureCache::Demand(const TextureInfo& texture_info,
                                            VkCommandBuffer command_buffer,
                                            VkFence completion_fence) {
  // Run a tight loop to scan for an exact match existing texture.
  auto texture_hash = texture_info.hash();
  for (auto it = textures_.find(texture_hash); it != textures_.end(); ++it) {
    if (it->second->texture_info == texture_info) {
      if (it->second->pending_invalidation) {
        // This texture has been invalidated!
        Scavenge();
        break;
      }

      return it->second;
    }
  }

  // Check resolve textures.
  for (auto it = resolve_textures_.begin(); it != resolve_textures_.end();
       ++it) {
    auto texture = (*it);
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

      if (texture->access_watch_handle) {
        memory_->CancelAccessWatch(texture->access_watch_handle);
      }

      texture->access_watch_handle = memory_->AddPhysicalAccessWatch(
          texture_info.guest_address, texture_info.input_length,
          cpu::MMIOHandler::kWatchWrite,
          [](void* context_ptr, void* data_ptr, uint32_t address) {
            auto self = reinterpret_cast<TextureCache*>(context_ptr);
            auto touched_texture = reinterpret_cast<Texture*>(data_ptr);
            // Clear watch handle first so we don't redundantly
            // remove.
            touched_texture->access_watch_handle = 0;
            touched_texture->pending_invalidation = true;
            // Add to pending list so Scavenge will clean it up.
            self->invalidated_textures_mutex_.lock();
            self->invalidated_textures_->push_back(touched_texture);
            self->invalidated_textures_mutex_.unlock();
          },
          this, texture);

      textures_[texture_hash] = *it;
      it = resolve_textures_.erase(it);
      return textures_[texture_hash];
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
    case Dimension::k1D: {
      uploaded = UploadTexture1D(command_buffer, completion_fence, texture,
                                 texture_info);
    } break;

    case Dimension::k2D: {
      uploaded = UploadTexture2D(command_buffer, completion_fence, texture,
                                 texture_info);
    } break;

    case Dimension::kCube: {
      uploaded = UploadTextureCube(command_buffer, completion_fence, texture,
                                   texture_info);
    } break;

    default:
      assert_unhandled_case(texture_info.dimension);
      break;
  }

  if (!uploaded) {
    FreeTexture(texture);
    return nullptr;
  }

  // Setup a debug name for the texture.
  device_->DbgSetObjectName(
      reinterpret_cast<uint64_t>(texture->image),
      VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT,
      xe::format_string(
          "0x%.8X - 0x%.8X", texture_info.guest_address,
          texture_info.guest_address + texture_info.output_length));

  // Copy in overlapping resolve textures.
  // FIXME: RDR appears to take textures from small chunks of a resolve texture?
  if (texture_info.dimension == Dimension::k2D) {
    for (auto it = resolve_textures_.begin(); it != resolve_textures_.end();
         ++it) {
      auto texture = (*it);
      if (texture_info.guest_address >= texture->texture_info.guest_address &&
          texture_info.guest_address < texture->texture_info.guest_address +
                                           texture->texture_info.input_length) {
        // Lazy matched a resolve texture. Copy it in and destroy it.
        // Future resolves will just copy directly into this texture.
        // assert_always();
      }
    }
  }

  // Though we didn't find an exact match, that doesn't mean we're out of the
  // woods yet. This texture could either be a portion of another texture or
  // vice versa. Copy any overlapping textures into this texture.
  // TODO: Byte count -> pixel count (on x and y axes)
  /*
  for (auto it = textures_.begin(); it != textures_.end(); ++it) {
    // TODO(DrChat)
  }
  */

  // Okay. Now that the texture is uploaded from system memory, put a writewatch
  // on it to tell us if it's been modified from the guest.
  texture->access_watch_handle = memory_->AddPhysicalAccessWatch(
      texture_info.guest_address, texture_info.input_length,
      cpu::MMIOHandler::kWatchWrite,
      [](void* context_ptr, void* data_ptr, uint32_t address) {
        auto self = reinterpret_cast<TextureCache*>(context_ptr);
        auto touched_texture = reinterpret_cast<Texture*>(data_ptr);
        // Clear watch handle first so we don't redundantly
        // remove.
        touched_texture->access_watch_handle = 0;
        touched_texture->pending_invalidation = true;
        // Add to pending list so Scavenge will clean it up.
        self->invalidated_textures_mutex_.lock();
        self->invalidated_textures_->push_back(touched_texture);
        self->invalidated_textures_mutex_.unlock();
      },
      this, texture);

  textures_[texture_hash] = texture;
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
      VK_COMPONENT_SWIZZLE_ZERO,     VK_COMPONENT_SWIZZLE_ONE,
      VK_COMPONENT_SWIZZLE_IDENTITY,
  };

  view_info.components = {
      swiz_component_map[(swizzle >> 0) & 0x7],
      swiz_component_map[(swizzle >> 3) & 0x7],
      swiz_component_map[(swizzle >> 6) & 0x7],
      swiz_component_map[(swizzle >> 9) & 0x7],
  };
  view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (texture->format == VK_FORMAT_D24_UNORM_S8_UINT) {
    // This applies to any depth/stencil format, but we only use D24S8.
    view_info.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  }

  if (texture->texture_info.dimension == Dimension::kCube) {
    view_info.subresourceRange.layerCount = 6;
  }

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
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto sampler_hash = sampler_info.hash();
  for (auto it = samplers_.find(sampler_hash); it != samplers_.end(); ++it) {
    if (it->second->sampler_info == sampler_info) {
      // Found a compatible sampler.
      return it->second;
    }
  }

  VkResult status = VK_SUCCESS;

  // Create a new sampler and cache it.
  // TODO: Actually set the properties
  VkSamplerCreateInfo sampler_create_info;
  sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_create_info.pNext = nullptr;
  sampler_create_info.flags = 0;
  sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

  // Texture level filtering.
  VkSamplerMipmapMode mip_filter;
  switch (sampler_info.mip_filter) {
    case TextureFilter::kBaseMap:
      // TODO(DrChat): ?
      mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case TextureFilter::kPoint:
      mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case TextureFilter::kLinear:
      mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    default:
      assert_unhandled_case(sampler_info.mip_filter);
      return nullptr;
  }

  VkFilter min_filter;
  switch (sampler_info.min_filter) {
    case TextureFilter::kPoint:
      min_filter = VK_FILTER_NEAREST;
      break;
    case TextureFilter::kLinear:
      min_filter = VK_FILTER_LINEAR;
      break;
    default:
      assert_unhandled_case(sampler_info.min_filter);
      return nullptr;
  }
  VkFilter mag_filter;
  switch (sampler_info.mag_filter) {
    case TextureFilter::kPoint:
      mag_filter = VK_FILTER_NEAREST;
      break;
    case TextureFilter::kLinear:
      mag_filter = VK_FILTER_LINEAR;
      break;
    default:
      assert_unhandled_case(mag_filter);
      return nullptr;
  }

  sampler_create_info.minFilter = min_filter;
  sampler_create_info.magFilter = mag_filter;
  sampler_create_info.mipmapMode = mip_filter;

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

  float aniso = 0.f;
  switch (sampler_info.aniso_filter) {
    case AnisoFilter::kDisabled:
      aniso = 1.0f;
      break;
    case AnisoFilter::kMax_1_1:
      aniso = 1.0f;
      break;
    case AnisoFilter::kMax_2_1:
      aniso = 2.0f;
      break;
    case AnisoFilter::kMax_4_1:
      aniso = 4.0f;
      break;
    case AnisoFilter::kMax_8_1:
      aniso = 8.0f;
      break;
    case AnisoFilter::kMax_16_1:
      aniso = 16.0f;
      break;
    default:
      assert_unhandled_case(aniso);
      return nullptr;
  }

  sampler_create_info.anisotropyEnable =
      sampler_info.aniso_filter != AnisoFilter::kDisabled ? VK_TRUE : VK_FALSE;
  sampler_create_info.maxAnisotropy = aniso;

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
  samplers_[sampler_hash] = sampler;

  return sampler;
}

TextureCache::Texture* TextureCache::LookupAddress(uint32_t guest_address,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   TextureFormat format,
                                                   VkOffset2D* out_offset) {
  for (auto it = textures_.begin(); it != textures_.end(); ++it) {
    const auto& texture_info = it->second->texture_info;
    if (guest_address >= texture_info.guest_address &&
        guest_address <
            texture_info.guest_address + texture_info.input_length &&
        texture_info.size_2d.input_width >= width &&
        texture_info.size_2d.input_height >= height && out_offset) {
      auto offset_bytes = guest_address - texture_info.guest_address;

      if (texture_info.dimension == Dimension::k2D) {
        out_offset->x = 0;
        out_offset->y = offset_bytes / texture_info.size_2d.input_pitch;
        if (offset_bytes % texture_info.size_2d.input_pitch != 0) {
          // TODO: offset_x
        }
      }

      return it->second;
    }

    if (texture_info.guest_address == guest_address &&
        texture_info.dimension == Dimension::k2D &&
        texture_info.size_2d.input_width == width &&
        texture_info.size_2d.input_height == height) {
      if (out_offset) {
        out_offset->x = 0;
        out_offset->y = 0;
      }

      return it->second;
    }
  }

  // Check resolve textures
  for (auto it = resolve_textures_.begin(); it != resolve_textures_.end();
       ++it) {
    const auto& texture_info = (*it)->texture_info;
    if (texture_info.guest_address == guest_address &&
        texture_info.dimension == Dimension::k2D &&
        texture_info.size_2d.input_width == width &&
        texture_info.size_2d.input_height == height) {
      if (out_offset) {
        out_offset->x = 0;
        out_offset->y = 0;
      }

      return (*it);
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

void TextureCache::FlushPendingCommands(VkCommandBuffer command_buffer,
                                        VkFence completion_fence) {
  auto status = vkEndCommandBuffer(command_buffer);
  CheckResult(status, "vkEndCommandBuffer");

  VkSubmitInfo submit_info;
  std::memset(&submit_info, 0, sizeof(submit_info));
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;

  if (device_queue_) {
    auto status =
        vkQueueSubmit(device_queue_, 1, &submit_info, completion_fence);
    CheckResult(status, "vkQueueSubmit");
  } else {
    std::lock_guard<std::mutex>(device_->primary_queue_mutex());

    auto status = vkQueueSubmit(device_->primary_queue(), 1, &submit_info,
                                completion_fence);
    CheckResult(status, "vkQueueSubmit");
  }

  vkWaitForFences(*device_, 1, &completion_fence, VK_TRUE, -1);
  staging_buffer_.Scavenge();
  vkResetFences(*device_, 1, &completion_fence);

  // Reset the command buffer and put it back into the recording state.
  vkResetCommandBuffer(command_buffer, 0);
  VkCommandBufferBeginInfo begin_info;
  std::memset(&begin_info, 0, sizeof(begin_info));
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(command_buffer, &begin_info);
}

void TextureCache::ConvertTexture1D(uint8_t* dest, const TextureInfo& src) {
  void* host_address = memory_->TranslatePhysical(src.guest_address);
  if (!src.is_tiled) {
    if (src.size_1d.input_pitch == src.size_1d.output_pitch) {
      TextureSwap(src.endianness, dest, host_address, src.output_length);
    } else {
      assert_always();
    }
  } else {
    assert_always();
  }
}

void TextureCache::ConvertTexture2D(uint8_t* dest, const TextureInfo& src) {
  void* host_address = memory_->TranslatePhysical(src.guest_address);
  if (!src.is_tiled) {
    if (src.size_2d.input_pitch == src.size_2d.output_pitch) {
      // Fast path copy entire image.
      TextureSwap(src.endianness, dest, host_address, src.output_length);
    } else {
      // Slow path copy row-by-row because strides differ.
      // UNPACK_ROW_LENGTH only works for uncompressed images, and likely does
      // this exact thing under the covers, so we just always do it here.
      const uint8_t* src_mem = reinterpret_cast<const uint8_t*>(host_address);
      uint32_t pitch =
          std::min(src.size_2d.input_pitch, src.size_2d.output_pitch);
      for (uint32_t y = 0;
           y < std::min(src.size_2d.block_height, src.size_2d.logical_height);
           y++) {
        TextureSwap(src.endianness, dest, src_mem, pitch);
        src_mem += src.size_2d.input_pitch;
        dest += src.size_2d.output_pitch;
      }
    }
  } else {
    // Untile image.
    // We could do this in a shader to speed things up, as this is pretty slow.

    // TODO(benvanik): optimize this inner loop (or work by tiles).
    const uint8_t* src_mem = reinterpret_cast<const uint8_t*>(host_address);
    uint32_t bytes_per_block = src.format_info->block_width *
                               src.format_info->block_height *
                               src.format_info->bits_per_pixel / 8;

    // Tiled textures can be packed; get the offset into the packed texture.
    uint32_t offset_x;
    uint32_t offset_y;
    TextureInfo::GetPackedTileOffset(src, &offset_x, &offset_y);
    auto bpp = (bytes_per_block >> 2) +
               ((bytes_per_block >> 1) >> (bytes_per_block >> 2));
    for (uint32_t y = 0, output_base_offset = 0;
         y < std::min(src.size_2d.block_height, src.size_2d.logical_height);
         y++, output_base_offset += src.size_2d.output_pitch) {
      auto input_base_offset = TextureInfo::TiledOffset2DOuter(
          offset_y + y,
          (src.size_2d.input_width / src.format_info->block_width), bpp);
      for (uint32_t x = 0, output_offset = output_base_offset;
           x < src.size_2d.block_width; x++, output_offset += bytes_per_block) {
        auto input_offset =
            TextureInfo::TiledOffset2DInner(offset_x + x, offset_y + y, bpp,
                                            input_base_offset) >>
            bpp;
        TextureSwap(src.endianness, dest + output_offset,
                    src_mem + input_offset * bytes_per_block, bytes_per_block);
      }
    }
  }
}

void TextureCache::ConvertTextureCube(uint8_t* dest, const TextureInfo& src) {
  void* host_address = memory_->TranslatePhysical(src.guest_address);
  if (!src.is_tiled) {
    if (src.size_cube.input_pitch == src.size_cube.output_pitch) {
      // Fast path copy entire image.
      TextureSwap(src.endianness, dest, host_address, src.output_length);
    } else {
      // Slow path copy row-by-row because strides differ.
      // UNPACK_ROW_LENGTH only works for uncompressed images, and likely does
      // this exact thing under the covers, so we just always do it here.
      const uint8_t* src_mem = reinterpret_cast<const uint8_t*>(host_address);
      for (int face = 0; face < 6; ++face) {
        uint32_t pitch =
            std::min(src.size_cube.input_pitch, src.size_cube.output_pitch);
        for (uint32_t y = 0; y < src.size_cube.block_height; y++) {
          TextureSwap(src.endianness, dest, src_mem, pitch);
          src_mem += src.size_cube.input_pitch;
          dest += src.size_cube.output_pitch;
        }
      }
    }
  } else {
    // TODO(benvanik): optimize this inner loop (or work by tiles).
    const uint8_t* src_mem = reinterpret_cast<const uint8_t*>(host_address);
    uint32_t bytes_per_block = src.format_info->block_width *
                               src.format_info->block_height *
                               src.format_info->bits_per_pixel / 8;
    // Tiled textures can be packed; get the offset into the packed texture.
    uint32_t offset_x;
    uint32_t offset_y;
    TextureInfo::GetPackedTileOffset(src, &offset_x, &offset_y);
    auto bpp = (bytes_per_block >> 2) +
               ((bytes_per_block >> 1) >> (bytes_per_block >> 2));
    for (int face = 0; face < 6; ++face) {
      for (uint32_t y = 0, output_base_offset = 0;
           y < src.size_cube.block_height;
           y++, output_base_offset += src.size_cube.output_pitch) {
        auto input_base_offset = TextureInfo::TiledOffset2DOuter(
            offset_y + y,
            (src.size_cube.input_width / src.format_info->block_width), bpp);
        for (uint32_t x = 0, output_offset = output_base_offset;
             x < src.size_cube.block_width;
             x++, output_offset += bytes_per_block) {
          auto input_offset =
              TextureInfo::TiledOffset2DInner(offset_x + x, offset_y + y, bpp,
                                              input_base_offset) >>
              bpp;
          TextureSwap(src.endianness, dest + output_offset,
                      src_mem + input_offset * bytes_per_block,
                      bytes_per_block);
        }
      }
      src_mem += src.size_cube.input_face_length;
      dest += src.size_cube.output_face_length;
    }
  }
}

bool TextureCache::UploadTexture1D(VkCommandBuffer command_buffer,
                                   VkFence completion_fence, Texture* dest,
                                   const TextureInfo& src) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  assert_true(src.dimension == Dimension::k1D);

  size_t unpack_length = src.output_length;
  if (!staging_buffer_.CanAcquire(unpack_length)) {
    // Need to have unique memory for every upload for at least one frame. If we
    // run out of memory, we need to flush all queued upload commands to the
    // GPU.
    FlushPendingCommands(command_buffer, completion_fence);

    // Uploads have been flushed. Continue.
    if (!staging_buffer_.CanAcquire(unpack_length)) {
      // The staging buffer isn't big enough to hold this texture.
      XELOGE(
          "TextureCache staging buffer is too small! (uploading 0x%.8X bytes)",
          unpack_length);
      assert_always();
      return false;
    }
  }

  // Grab some temporary memory for staging.
  auto alloc = staging_buffer_.Acquire(unpack_length, completion_fence);
  assert_not_null(alloc);

  // Upload texture into GPU memory.
  // TODO: If the GPU supports it, we can submit a compute batch to convert the
  // texture and copy it to its destination. Otherwise, fallback to conversion
  // on the CPU.
  ConvertTexture1D(reinterpret_cast<uint8_t*>(alloc->host_ptr), src);
  staging_buffer_.Flush(alloc);

  // Transition the texture into a transfer destination layout.
  VkImageMemoryBarrier barrier;
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.pNext = nullptr;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask =
      VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
  barrier.oldLayout = dest->image_layout;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = dest->image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  // Now move the converted texture into the destination.
  VkBufferImageCopy copy_region;
  copy_region.bufferOffset = alloc->offset;
  copy_region.bufferRowLength = src.size_1d.output_width;
  copy_region.bufferImageHeight = 1;
  copy_region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy_region.imageOffset = {0, 0, 0};
  copy_region.imageExtent = {src.size_1d.output_width, 1, 1};
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

bool TextureCache::UploadTexture2D(VkCommandBuffer command_buffer,
                                   VkFence completion_fence, Texture* dest,
                                   const TextureInfo& src) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  assert_true(src.dimension == Dimension::k2D);

  size_t unpack_length = src.output_length;
  if (!staging_buffer_.CanAcquire(unpack_length)) {
    // Need to have unique memory for every upload for at least one frame. If we
    // run out of memory, we need to flush all queued upload commands to the
    // GPU.
    FlushPendingCommands(command_buffer, completion_fence);

    // Uploads have been flushed. Continue.
    if (!staging_buffer_.CanAcquire(unpack_length)) {
      // The staging buffer isn't big enough to hold this texture.
      XELOGE(
          "TextureCache staging buffer is too small! (uploading 0x%.8X bytes)",
          unpack_length);
      assert_always();
      return false;
    }
  }

  // Grab some temporary memory for staging.
  auto alloc = staging_buffer_.Acquire(unpack_length, completion_fence);
  assert_not_null(alloc);

  // Upload texture into GPU memory.
  // TODO: If the GPU supports it, we can submit a compute batch to convert the
  // texture and copy it to its destination. Otherwise, fallback to conversion
  // on the CPU.
  ConvertTexture2D(reinterpret_cast<uint8_t*>(alloc->host_ptr), src);
  staging_buffer_.Flush(alloc);

  // Transition the texture into a transfer destination layout.
  VkImageMemoryBarrier barrier;
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.pNext = nullptr;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask =
      VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
  barrier.oldLayout = dest->image_layout;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = dest->image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  // Now move the converted texture into the destination.
  VkBufferImageCopy copy_region;
  copy_region.bufferOffset = alloc->offset;
  copy_region.bufferRowLength = src.size_2d.output_width;
  copy_region.bufferImageHeight = src.size_2d.output_height;
  copy_region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy_region.imageOffset = {0, 0, 0};
  copy_region.imageExtent = {src.size_2d.output_width,
                             src.size_2d.output_height, 1};
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

bool TextureCache::UploadTextureCube(VkCommandBuffer command_buffer,
                                     VkFence completion_fence, Texture* dest,
                                     const TextureInfo& src) {
  assert_true(src.dimension == Dimension::kCube);

  size_t unpack_length = src.output_length;
  if (!staging_buffer_.CanAcquire(unpack_length)) {
    // Need to have unique memory for every upload for at least one frame. If we
    // run out of memory, we need to flush all queued upload commands to the
    // GPU.
    FlushPendingCommands(command_buffer, completion_fence);

    // Uploads have been flushed. Continue.
    if (!staging_buffer_.CanAcquire(unpack_length)) {
      // The staging buffer isn't big enough to hold this texture.
      XELOGE(
          "TextureCache staging buffer is too small! (uploading 0x%.8X bytes)",
          unpack_length);
      assert_always();
      return false;
    }
  }

  // Grab some temporary memory for staging.
  auto alloc = staging_buffer_.Acquire(unpack_length, completion_fence);
  assert_not_null(alloc);

  // Upload texture into GPU memory.
  // TODO: If the GPU supports it, we can submit a compute batch to convert the
  // texture and copy it to its destination. Otherwise, fallback to conversion
  // on the CPU.
  ConvertTextureCube(reinterpret_cast<uint8_t*>(alloc->host_ptr), src);
  staging_buffer_.Flush(alloc);

  // Transition the texture into a transfer destination layout.
  VkImageMemoryBarrier barrier;
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.pNext = nullptr;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask =
      VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
  barrier.oldLayout = dest->image_layout;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = dest->image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  // Now move the converted texture into the destination.
  VkBufferImageCopy copy_region;
  copy_region.bufferOffset = alloc->offset;
  copy_region.bufferRowLength = src.size_cube.output_width;
  copy_region.bufferImageHeight = src.size_cube.output_height;
  copy_region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy_region.imageOffset = {0, 0, 0};
  copy_region.imageExtent = {src.size_cube.output_width,
                             src.size_cube.output_height, 6};
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
    VkCommandBuffer command_buffer, VkFence completion_fence,
    const std::vector<Shader::TextureBinding>& vertex_bindings,
    const std::vector<Shader::TextureBinding>& pixel_bindings) {
  // Clear state.
  auto update_set_info = &update_set_info_;
  update_set_info->has_setup_fetch_mask = 0;
  update_set_info->image_write_count = 0;

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

  if (err != VK_SUCCESS) {
    return nullptr;
  }

  for (uint32_t i = 0; i < update_set_info->image_write_count; i++) {
    update_set_info->image_writes[i].dstSet = descriptor_set;
  }

  // Update the descriptor set.
  if (update_set_info->image_write_count > 0) {
    vkUpdateDescriptorSets(*device_, update_set_info->image_write_count,
                           update_set_info->image_writes, 0, nullptr);
  }

  in_flight_sets_.push_back({descriptor_set, completion_fence});
  return descriptor_set;
}

bool TextureCache::SetupTextureBindings(
    VkCommandBuffer command_buffer, VkFence completion_fence,
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

bool TextureCache::SetupTextureBinding(VkCommandBuffer command_buffer,
                                       VkFence completion_fence,
                                       UpdateSetInfo* update_set_info,
                                       const Shader::TextureBinding& binding) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

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
  // assert_true(texture != nullptr && sampler != nullptr);
  if (texture == nullptr || sampler == nullptr) {
    return false;
  }

  uint16_t swizzle = static_cast<uint16_t>(fetch.swizzle);
  auto view = DemandView(texture, swizzle);

  trace_writer_->WriteMemoryRead(texture_info.guest_address,
                                 texture_info.input_length);

  auto image_info =
      &update_set_info->image_infos[update_set_info->image_write_count];
  auto image_write =
      &update_set_info->image_writes[update_set_info->image_write_count];
  update_set_info->image_write_count++;

  image_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

  switch (texture_info.dimension) {
    case Dimension::k1D:
      image_write->dstBinding = 0;
      break;
    case Dimension::k2D:
      image_write->dstBinding = 1;
      break;
    case Dimension::k3D:
      image_write->dstBinding = 2;
      break;
    case Dimension::kCube:
      image_write->dstBinding = 3;
      break;
  }

  image_write->dstArrayElement = binding.fetch_constant;
  image_write->descriptorCount = 1;
  image_write->descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  image_write->pImageInfo = image_info;

  image_info->imageView = view->view;
  image_info->imageLayout = texture->image_layout;
  image_info->sampler = sampler->sampler;
  texture->in_flight_fence = completion_fence;

  return true;
}

void TextureCache::ClearCache() {
  // TODO(DrChat): Nuke everything.
}

void TextureCache::Scavenge() {
  // Free unused descriptor sets
  for (auto it = in_flight_sets_.begin(); it != in_flight_sets_.end();) {
    if (vkGetFenceStatus(*device_, it->second) == VK_SUCCESS) {
      // We can free this one.
      vkFreeDescriptorSets(*device_, descriptor_pool_, 1, &it->first);
      it = in_flight_sets_.erase(it);
      continue;
    }

    // We've encountered an item that hasn't been used yet, so any items
    // afterwards are guaranteed to be unused.
    break;
  }

  staging_buffer_.Scavenge();

  // Kill all pending delete textures.
  if (!pending_delete_textures_.empty()) {
    for (auto it = pending_delete_textures_.begin();
         it != pending_delete_textures_.end();) {
      if (!FreeTexture(*it)) {
        break;
      }

      it = pending_delete_textures_.erase(it);
    }
  }

  // Clean up any invalidated textures.
  invalidated_textures_mutex_.lock();
  std::vector<Texture*>& invalidated_textures = *invalidated_textures_;
  if (invalidated_textures_ == &invalidated_textures_sets_[0]) {
    invalidated_textures_ = &invalidated_textures_sets_[1];
  } else {
    invalidated_textures_ = &invalidated_textures_sets_[0];
  }
  invalidated_textures_mutex_.unlock();
  if (!invalidated_textures.empty()) {
    for (auto it = invalidated_textures.begin();
         it != invalidated_textures.end(); ++it) {
      pending_delete_textures_.push_back(*it);
      textures_.erase((*it)->texture_info.hash());
    }

    invalidated_textures.clear();
  }

  // Invalidated resolve textures.
  invalidated_resolve_textures_mutex_.lock();
  if (!invalidated_resolve_textures_.empty()) {
    for (auto it = invalidated_resolve_textures_.begin();
         it != invalidated_resolve_textures_.end(); ++it) {
      pending_delete_textures_.push_back(*it);

      auto tex =
          std::find(resolve_textures_.begin(), resolve_textures_.end(), *it);
      if (tex != resolve_textures_.end()) {
        resolve_textures_.erase(tex);
      }
    }

    invalidated_resolve_textures_.clear();
  }
  invalidated_resolve_textures_mutex_.unlock();
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
