/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/texture_cache.h"
#include "xenia/gpu/vulkan/texture_config.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_conversion.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"
#include "xenia/ui/vulkan/vulkan_mem_alloc.h"

DECLARE_bool(texture_dump);

namespace xe {
namespace gpu {

void TextureDump(const TextureInfo& src, void* buffer, size_t length);

namespace vulkan {

using xe::ui::vulkan::CheckResult;

constexpr uint32_t kMaxTextureSamplers = 32;
constexpr VkDeviceSize kStagingBufferSize = 64 * 1024 * 1024;

const char* get_dimension_name(Dimension dimension) {
  static const char* names[] = {
      "1D",
      "2D",
      "3D",
      "cube",
  };
  auto value = static_cast<int>(dimension);
  if (value < xe::countof(names)) {
    return names[value];
  }
  return "unknown";
}

TextureCache::TextureCache(Memory* memory, RegisterFile* register_file,
                           TraceWriter* trace_writer,
                           ui::vulkan::VulkanDevice* device)
    : memory_(memory),
      register_file_(register_file),
      trace_writer_(trace_writer),
      device_(device),
      staging_buffer_(device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      kStagingBufferSize),
      wb_staging_buffer_(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kStagingBufferSize) {}

TextureCache::~TextureCache() { Shutdown(); }

VkResult TextureCache::Initialize() {
  VkResult status = VK_SUCCESS;

  // Descriptor pool used for all of our cached descriptors.
  VkDescriptorPoolSize pool_sizes[1];
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_sizes[0].descriptorCount = 32768;
  descriptor_pool_ = std::make_unique<ui::vulkan::DescriptorPool>(
      *device_, 32768,
      std::vector<VkDescriptorPoolSize>(pool_sizes, std::end(pool_sizes)));

  wb_command_pool_ = std::make_unique<ui::vulkan::CommandBufferPool>(
      *device_, device_->queue_family_index());

  // Check some device limits
  // On low sampler counts: Rarely would we experience over 16 unique samplers.
  // This code could be refactored to scale up/down to the # of samplers.
  auto& limits = device_->device_info().properties.limits;
  if (limits.maxPerStageDescriptorSamplers < kMaxTextureSamplers ||
      limits.maxPerStageDescriptorSampledImages < kMaxTextureSamplers) {
    XELOGE(
        "Physical device is unable to support required number of sampled "
        "images! Expect instability! (maxPerStageDescriptorSamplers=%d, "
        "maxPerStageDescriptorSampledImages=%d)",
        limits.maxPerStageDescriptorSamplers,
        limits.maxPerStageDescriptorSampledImages);
    // assert_always();
  }

  // Create the descriptor set layout used for rendering.
  // We always have the same number of samplers but only some are used.
  // The shaders will alias the bindings to the 4 dimensional types.
  VkDescriptorSetLayoutBinding bindings[1];
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[0].descriptorCount = kMaxTextureSamplers;
  bindings[0].stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings[0].pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info;
  descriptor_set_layout_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_info.pNext = nullptr;
  descriptor_set_layout_info.flags = 0;
  descriptor_set_layout_info.bindingCount =
      static_cast<uint32_t>(xe::countof(bindings));
  descriptor_set_layout_info.pBindings = bindings;
  status =
      vkCreateDescriptorSetLayout(*device_, &descriptor_set_layout_info,
                                  nullptr, &texture_descriptor_set_layout_);
  if (status != VK_SUCCESS) {
    return status;
  }

  status = staging_buffer_.Initialize();
  if (status != VK_SUCCESS) {
    return status;
  }

  status = wb_staging_buffer_.Initialize();
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create a memory allocator for textures.
  VmaVulkanFunctions vulkan_funcs = {};
  ui::vulkan::FillVMAVulkanFunctions(&vulkan_funcs);

  VmaAllocatorCreateInfo alloc_info = {
      0, *device_, *device_, 0, 0, nullptr, nullptr, 0, nullptr, &vulkan_funcs,
  };
  status = vmaCreateAllocator(&alloc_info, &mem_allocator_);
  if (status != VK_SUCCESS) {
    vkDestroyDescriptorSetLayout(*device_, texture_descriptor_set_layout_,
                                 nullptr);
    return status;
  }

  invalidated_textures_sets_[0].reserve(64);
  invalidated_textures_sets_[1].reserve(64);
  invalidated_textures_ = &invalidated_textures_sets_[0];

  device_queue_ = device_->AcquireQueue(device_->queue_family_index());
  return VK_SUCCESS;
}

void TextureCache::Shutdown() {
  if (device_queue_) {
    device_->ReleaseQueue(device_queue_, device_->queue_family_index());
  }

  // Free all textures allocated.
  ClearCache();
  Scavenge();

  if (mem_allocator_ != nullptr) {
    vmaDestroyAllocator(mem_allocator_);
    mem_allocator_ = nullptr;
  }
  vkDestroyDescriptorSetLayout(*device_, texture_descriptor_set_layout_,
                               nullptr);
}

TextureCache::Texture* TextureCache::AllocateTexture(
    const TextureInfo& texture_info, VkFormatFeatureFlags required_flags) {
  auto format_info = texture_info.format_info();
  assert_not_null(format_info);

  auto& config = texture_configs[int(format_info->format)];
  VkFormat format = config.host_format;
  if (format == VK_FORMAT_UNDEFINED) {
    XELOGE(
        "Texture Cache: Attempted to allocate texture format %s, which is "
        "defined as VK_FORMAT_UNDEFINED!",
        texture_info.format_info()->name);
    return nullptr;
  }

  bool is_cube = false;
  // Create an image first.
  VkImageCreateInfo image_info = {};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.flags = 0;

  switch (texture_info.dimension) {
    case Dimension::k1D:
    case Dimension::k2D:
      if (!texture_info.is_stacked) {
        image_info.imageType = VK_IMAGE_TYPE_2D;
      } else {
        image_info.imageType = VK_IMAGE_TYPE_3D;
        image_info.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
      }
      break;
    case Dimension::k3D:
      image_info.imageType = VK_IMAGE_TYPE_3D;
      break;
    case Dimension::kCube:
      image_info.imageType = VK_IMAGE_TYPE_2D;
      image_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
      is_cube = true;
      break;
    default:
      assert_unhandled_case(texture_info.dimension);
      return nullptr;
  }

  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage =
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  // Check the device limits for the format before we create it.
  VkFormatProperties props;
  vkGetPhysicalDeviceFormatProperties(*device_, format, &props);
  if ((props.optimalTilingFeatures & required_flags) != required_flags) {
    // Texture needs conversion on upload to a native format.
    XELOGE(
        "Texture Cache: Invalid usage flag specified on format %s (%s)\n\t"
        "(requested: %s)",
        texture_info.format_info()->name, ui::vulkan::to_string(format),
        ui::vulkan::to_flags_string(
            static_cast<VkFormatFeatureFlagBits>(required_flags &
                                                 ~props.optimalTilingFeatures))
            .c_str());
  }

  if (texture_info.dimension != Dimension::kCube &&
      props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) {
    // Add color attachment usage if it's supported.
    image_info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  } else if (props.optimalTilingFeatures &
             VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
    // Add depth/stencil usage as well.
    image_info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }

  if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) {
    image_info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  VkImageFormatProperties image_props;
  vkGetPhysicalDeviceImageFormatProperties(
      *device_, format, image_info.imageType, image_info.tiling,
      image_info.usage, image_info.flags, &image_props);

  // TODO(DrChat): Actually check the image properties.

  image_info.format = format;
  image_info.extent.width = texture_info.width + 1;
  image_info.extent.height = texture_info.height + 1;
  image_info.extent.depth = !is_cube ? 1 + texture_info.depth : 1;
  image_info.mipLevels = texture_info.mip_min_level + texture_info.mip_levels();
  image_info.arrayLayers = !is_cube ? 1 : 1 + texture_info.depth;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.queueFamilyIndexCount = 0;
  image_info.pQueueFamilyIndices = nullptr;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage image;

  assert_true(image_props.maxExtent.width >= image_info.extent.width);
  assert_true(image_props.maxExtent.height >= image_info.extent.height);
  assert_true(image_props.maxExtent.depth >= image_info.extent.depth);
  assert_true(image_props.maxMipLevels >= image_info.mipLevels);
  assert_true(image_props.maxArrayLayers >= image_info.arrayLayers);

  VmaAllocation alloc;
  VmaAllocationCreateInfo vma_create_info = {
      0, VMA_MEMORY_USAGE_GPU_ONLY, 0, 0, 0, nullptr, nullptr,
  };
  VmaAllocationInfo vma_info = {};
  VkResult status = vmaCreateImage(mem_allocator_, &image_info,
                                   &vma_create_info, &image, &alloc, &vma_info);
  if (status != VK_SUCCESS) {
    // Allocation failed.
    return nullptr;
  }

  auto texture = new Texture();
  texture->format = image_info.format;
  texture->image = image;
  texture->image_layout = image_info.initialLayout;
  texture->alloc = alloc;
  texture->alloc_info = vma_info;
  texture->framebuffer = nullptr;
  texture->usage_flags = image_info.usage;
  texture->access_watch_handle = 0;
  texture->texture_info = texture_info;
  return texture;
}

bool TextureCache::FreeTexture(Texture* texture) {
  if (texture->in_flight_fence) {
    VkResult status = vkGetFenceStatus(*device_, texture->in_flight_fence);
    if (status != VK_SUCCESS && status != VK_ERROR_DEVICE_LOST) {
      // Texture still in flight.
      return false;
    }
  }

  if (texture->framebuffer) {
    vkDestroyFramebuffer(*device_, texture->framebuffer, nullptr);
  }

  for (auto it = texture->views.begin(); it != texture->views.end();) {
    vkDestroyImageView(*device_, (*it)->view, nullptr);
    it = texture->views.erase(it);
  }

  if (texture->access_watch_handle) {
    memory_->CancelAccessWatch(texture->access_watch_handle);
    texture->access_watch_handle = 0;
  }

  vmaDestroyImage(mem_allocator_, texture->image, texture->alloc);
  delete texture;
  return true;
}

void TextureCache::WatchCallback(void* context_ptr, void* data_ptr,
                                 uint32_t address) {
  auto self = reinterpret_cast<TextureCache*>(context_ptr);
  auto touched_texture = reinterpret_cast<Texture*>(data_ptr);
  if (!touched_texture || !touched_texture->access_watch_handle ||
      touched_texture->pending_invalidation) {
    return;
  }

  assert_not_zero(touched_texture->access_watch_handle);
  // Clear watch handle first so we don't redundantly
  // remove.
  touched_texture->access_watch_handle = 0;
  touched_texture->pending_invalidation = true;

  // Add to pending list so Scavenge will clean it up.
  self->invalidated_textures_mutex_.lock();
  self->invalidated_textures_->insert(touched_texture);
  self->invalidated_textures_mutex_.unlock();
}

TextureCache::Texture* TextureCache::DemandResolveTexture(
    const TextureInfo& texture_info) {
  auto texture_hash = texture_info.hash();
  for (auto it = textures_.find(texture_hash); it != textures_.end(); ++it) {
    if (it->second->texture_info == texture_info) {
      if (it->second->pending_invalidation) {
        // This texture has been invalidated!
        RemoveInvalidatedTextures();
        break;
      }

      // Tell the trace writer to "cache" this memory (but not read it)
      if (texture_info.memory.base_address) {
        trace_writer_->WriteMemoryReadCached(texture_info.memory.base_address,
                                             texture_info.memory.base_size);
      }
      if (texture_info.memory.mip_address) {
        trace_writer_->WriteMemoryReadCached(texture_info.memory.mip_address,
                                             texture_info.memory.mip_size);
      }

      return it->second;
    }
  }

  VkFormatFeatureFlags required_flags = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  if (texture_info.format == TextureFormat::k_24_8 ||
      texture_info.format == TextureFormat::k_24_8_FLOAT) {
    required_flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
  } else {
    required_flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
  }

  // No texture at this location. Make a new one.
  auto texture = AllocateTexture(texture_info, required_flags);
  if (!texture) {
    // Failed to allocate texture (out of memory)
    XELOGE("Vulkan Texture Cache: Failed to allocate texture!");
    return nullptr;
  }

  // Setup a debug name for the texture.
  device_->DbgSetObjectName(
      reinterpret_cast<uint64_t>(texture->image),
      VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT,
      xe::format_string(
          "RT: 0x%.8X - 0x%.8X (%s, %s)", texture_info.memory.base_address,
          texture_info.memory.base_address + texture_info.memory.base_size,
          texture_info.format_info()->name,
          get_dimension_name(texture_info.dimension)));

  // Setup an access watch. If this texture is touched, it is destroyed.
  if (texture_info.memory.base_address && texture_info.memory.base_size) {
    texture->access_watch_handle = memory_->AddPhysicalAccessWatch(
        texture_info.memory.base_address, texture_info.memory.base_size,
        cpu::MMIOHandler::kWatchWrite, &WatchCallback, this, texture);
  } else if (texture_info.memory.mip_address && texture_info.memory.mip_size) {
    texture->access_watch_handle = memory_->AddPhysicalAccessWatch(
        texture_info.memory.mip_address, texture_info.memory.mip_size,
        cpu::MMIOHandler::kWatchWrite, &WatchCallback, this, texture);
  }

  textures_[texture_hash] = texture;
  COUNT_profile_set("gpu/texture_cache/textures", textures_.size());
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
        RemoveInvalidatedTextures();
        break;
      }

      if (texture_info.memory.base_address) {
        trace_writer_->WriteMemoryReadCached(texture_info.memory.base_address,
                                             texture_info.memory.base_size);
      }
      if (texture_info.memory.mip_address) {
        trace_writer_->WriteMemoryReadCached(texture_info.memory.mip_address,
                                             texture_info.memory.mip_size);
      }
      return it->second;
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
    // Failed to allocate texture (out of memory)
    XELOGE("Vulkan Texture Cache: Failed to allocate texture!");
    return nullptr;
  }

  // Though we didn't find an exact match, that doesn't mean we're out of the
  // woods yet. This texture could either be a portion of another texture or
  // vice versa. Copy any overlapping textures into this texture.
  // TODO: Byte count -> pixel count (on x and y axes)
  VkOffset2D offset;
  auto collide_tex = LookupAddress(
      texture_info.memory.base_address, texture_info.width + 1,
      texture_info.height + 1, texture_info.format_info()->format, &offset);
  if (collide_tex != nullptr) {
    // assert_always();
  }

  if (texture_info.memory.base_address) {
    trace_writer_->WriteMemoryReadCached(texture_info.memory.base_address,
                                         texture_info.memory.base_size);
  }
  if (texture_info.memory.mip_address) {
    trace_writer_->WriteMemoryReadCached(texture_info.memory.mip_address,
                                         texture_info.memory.mip_size);
  }

  if (!UploadTexture(command_buffer, completion_fence, texture, texture_info)) {
    FreeTexture(texture);
    return nullptr;
  }

  // Setup a debug name for the texture.
  device_->DbgSetObjectName(
      reinterpret_cast<uint64_t>(texture->image),
      VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT,
      xe::format_string(
          "T: 0x%.8X - 0x%.8X (%s, %s)", texture_info.memory.base_address,
          texture_info.memory.base_address + texture_info.memory.base_size,
          texture_info.format_info()->name,
          get_dimension_name(texture_info.dimension)));

  textures_[texture_hash] = texture;
  COUNT_profile_set("gpu/texture_cache/textures", textures_.size());

  // Okay. Put a writewatch on it to tell us if it's been modified from the
  // guest.
  if (texture_info.memory.base_address && texture_info.memory.base_size) {
    texture->access_watch_handle = memory_->AddPhysicalAccessWatch(
        texture_info.memory.base_address, texture_info.memory.base_size,
        cpu::MMIOHandler::kWatchWrite, &WatchCallback, this, texture);
  } else if (texture_info.memory.mip_address && texture_info.memory.mip_size) {
    texture->access_watch_handle = memory_->AddPhysicalAccessWatch(
        texture_info.memory.mip_address, texture_info.memory.mip_size,
        cpu::MMIOHandler::kWatchWrite, &WatchCallback, this, texture);
  }

  return texture;
}

TextureCache::TextureView* TextureCache::DemandView(Texture* texture,
                                                    uint16_t swizzle) {
  for (auto it = texture->views.begin(); it != texture->views.end(); ++it) {
    if ((*it)->swizzle == swizzle) {
      return (*it).get();
    }
  }

  auto& config = texture_configs[uint32_t(texture->texture_info.format)];

  VkImageViewCreateInfo view_info;
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.pNext = nullptr;
  view_info.flags = 0;
  view_info.image = texture->image;
  view_info.format = texture->format;

  bool is_cube = false;
  switch (texture->texture_info.dimension) {
    case Dimension::k1D:
    case Dimension::k2D:
      if (!texture->texture_info.is_stacked) {
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
      } else {
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      }
      break;
    case Dimension::k3D:
      view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
      break;
    case Dimension::kCube:
      view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
      is_cube = true;
      break;
    default:
      assert_always();
  }

  VkComponentSwizzle swizzle_component_map[] = {
      config.component_swizzle.r,    config.component_swizzle.g,
      config.component_swizzle.b,    config.component_swizzle.a,
      VK_COMPONENT_SWIZZLE_ZERO,     VK_COMPONENT_SWIZZLE_ONE,
      VK_COMPONENT_SWIZZLE_IDENTITY,
  };

  VkComponentSwizzle components[] = {
      swizzle_component_map[(swizzle >> 0) & 0x7],
      swizzle_component_map[(swizzle >> 3) & 0x7],
      swizzle_component_map[(swizzle >> 6) & 0x7],
      swizzle_component_map[(swizzle >> 9) & 0x7],
  };

#define SWIZZLE_VECTOR(r, x)                                        \
  {                                                                 \
    assert_true(config.vector_swizzle.x >= 0 &&                     \
                config.vector_swizzle.x < xe::countof(components)); \
    view_info.components.r = components[config.vector_swizzle.x];   \
  }
  SWIZZLE_VECTOR(r, x);
  SWIZZLE_VECTOR(g, y);
  SWIZZLE_VECTOR(b, z);
  SWIZZLE_VECTOR(a, w);
#undef SWIZZLE_CHANNEL

  if (texture->format == VK_FORMAT_D16_UNORM_S8_UINT ||
      texture->format == VK_FORMAT_D24_UNORM_S8_UINT ||
      texture->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
    // This applies to any depth/stencil format, but we only use D24S8 / D32FS8.
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  } else {
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }
  view_info.subresourceRange.baseMipLevel = texture->texture_info.mip_min_level;
  view_info.subresourceRange.levelCount = texture->texture_info.mip_levels();
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount =
      !is_cube ? 1 : 1 + texture->texture_info.depth;

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
  VkSamplerCreateInfo sampler_create_info;
  sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_create_info.pNext = nullptr;
  sampler_create_info.flags = 0;
  sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_create_info.maxAnisotropy = 1.0f;

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
  sampler_create_info.mipLodBias = sampler_info.lod_bias;
  sampler_create_info.minLod = float(sampler_info.mip_min_level);
  sampler_create_info.maxLod = float(sampler_info.mip_max_level);
  sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
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

bool TextureFormatIsSimilar(TextureFormat left, TextureFormat right) {
#define COMPARE_FORMAT(x, y)                                     \
  if ((left == TextureFormat::x && right == TextureFormat::y) || \
      (left == TextureFormat::y && right == TextureFormat::x)) { \
    return true;                                                 \
  }

  if (left == right) return true;
  if (GetBaseFormat(left) == GetBaseFormat(right)) return true;

  return false;
#undef COMPARE_FORMAT
}

TextureCache::Texture* TextureCache::Lookup(const TextureInfo& texture_info) {
  auto texture_hash = texture_info.hash();
  for (auto it = textures_.find(texture_hash); it != textures_.end(); ++it) {
    if (it->second->texture_info == texture_info) {
      return it->second;
    }
  }

  // slow path
  for (auto it = textures_.begin(); it != textures_.end(); ++it) {
    const auto& other_texture_info = it->second->texture_info;

#define COMPARE_FIELD(x) \
  if (texture_info.x != other_texture_info.x) continue
    COMPARE_FIELD(memory.base_address);
    COMPARE_FIELD(memory.base_size);
    COMPARE_FIELD(dimension);
    COMPARE_FIELD(width);
    COMPARE_FIELD(height);
    COMPARE_FIELD(depth);
    COMPARE_FIELD(endianness);
    COMPARE_FIELD(is_tiled);
#undef COMPARE_FIELD

    if (!TextureFormatIsSimilar(texture_info.format,
                                other_texture_info.format)) {
      continue;
    }

    /*const auto format_info = texture_info.format_info();
    const auto other_format_info = other_texture_info.format_info();
#define COMPARE_FIELD(x) if (format_info->x != other_format_info->x) continue
    COMPARE_FIELD(type);
    COMPARE_FIELD(block_width);
    COMPARE_FIELD(block_height);
    COMPARE_FIELD(bits_per_pixel);
#undef COMPARE_FIELD*/
    return it->second;
  }

  return nullptr;
}

TextureCache::Texture* TextureCache::LookupAddress(uint32_t guest_address,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   TextureFormat format,
                                                   VkOffset2D* out_offset) {
  for (auto it = textures_.begin(); it != textures_.end(); ++it) {
    const auto& texture_info = it->second->texture_info;
    if (guest_address >= texture_info.memory.base_address &&
        guest_address <
            texture_info.memory.base_address + texture_info.memory.base_size &&
        texture_info.pitch >= width && texture_info.height >= height &&
        out_offset) {
      auto offset_bytes = guest_address - texture_info.memory.base_address;

      if (texture_info.dimension == Dimension::k2D) {
        out_offset->x = 0;
        out_offset->y = offset_bytes / texture_info.pitch;
        if (offset_bytes % texture_info.pitch != 0) {
          // TODO: offset_x
        }
      }

      return it->second;
    }

    if (texture_info.memory.base_address == guest_address &&
        texture_info.dimension == Dimension::k2D &&
        texture_info.pitch == width && texture_info.height == height) {
      if (out_offset) {
        out_offset->x = 0;
        out_offset->y = 0;
      }

      return it->second;
    }
  }

  return nullptr;
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

bool TextureCache::ConvertTexture(uint8_t* dest, VkBufferImageCopy* copy_region,
                                  uint32_t mip, const TextureInfo& src) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES
  uint32_t offset_x = 0;
  uint32_t offset_y = 0;
  uint32_t address = src.GetMipLocation(mip, &offset_x, &offset_y, true);
  if (!address) {
    return false;
  }

  void* host_address = memory_->TranslatePhysical(address);

  auto is_cube = src.dimension == Dimension::kCube;
  auto src_extent = src.GetMipExtent(mip, true);
  auto dst_extent = GetMipExtent(src, mip);

  uint32_t src_pitch =
      src_extent.block_pitch_h * src.format_info()->bytes_per_block();
  uint32_t dst_pitch =
      dst_extent.block_pitch_h * GetFormatInfo(src.format)->bytes_per_block();

  auto copy_block = GetFormatCopyBlock(src.format);

  const uint8_t* src_mem = reinterpret_cast<const uint8_t*>(host_address);
  if (!src.is_tiled) {
    for (uint32_t face = 0; face < dst_extent.depth; face++) {
      src_mem += offset_y * src_pitch;
      src_mem += offset_x * src.format_info()->bytes_per_block();
      for (uint32_t y = 0; y < dst_extent.block_height; y++) {
        copy_block(src.endianness, dest + y * dst_pitch,
                   src_mem + y * src_pitch, dst_pitch);
      }
      src_mem += src_pitch * src_extent.block_pitch_v;
      dest += dst_pitch * dst_extent.block_pitch_v;
    }
  } else {
    // Untile image.
    // We could do this in a shader to speed things up, as this is pretty slow.
    for (uint32_t face = 0; face < dst_extent.depth; face++) {
      texture_conversion::UntileInfo untile_info;
      std::memset(&untile_info, 0, sizeof(untile_info));
      untile_info.offset_x = offset_x;
      untile_info.offset_y = offset_y;
      untile_info.width = src_extent.block_width;
      untile_info.height = src_extent.block_height;
      untile_info.input_pitch = src_extent.block_pitch_h;
      untile_info.output_pitch = dst_extent.block_pitch_h;
      untile_info.input_format_info = src.format_info();
      untile_info.output_format_info = GetFormatInfo(src.format);
      untile_info.copy_callback = [=](auto o, auto i, auto l) {
        copy_block(src.endianness, o, i, l);
      };
      texture_conversion::Untile(dest, src_mem, &untile_info);
      src_mem += src_pitch * src_extent.block_pitch_v;
      dest += dst_pitch * dst_extent.block_pitch_v;
    }
  }

  copy_region->bufferRowLength = dst_extent.pitch;
  copy_region->bufferImageHeight = dst_extent.height;
  copy_region->imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy_region->imageSubresource.mipLevel = mip;
  copy_region->imageSubresource.baseArrayLayer = 0;
  copy_region->imageSubresource.layerCount = !is_cube ? 1 : dst_extent.depth;
  copy_region->imageExtent.width = std::max(1u, (src.width + 1) >> mip);
  copy_region->imageExtent.height = std::max(1u, (src.height + 1) >> mip);
  copy_region->imageExtent.depth = !is_cube ? dst_extent.depth : 1;
  return true;
}

bool TextureCache::UploadTexture(VkCommandBuffer command_buffer,
                                 VkFence completion_fence, Texture* dest,
                                 const TextureInfo& src) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  size_t unpack_length = ComputeTextureStorage(src);

  XELOGGPU(
      "Uploading texture @ 0x%.8X/0x%.8X (%ux%ux%u, format: %s, dim: %s, "
      "levels: %u (%u-%u), stacked: %s, pitch: %u, tiled: %s, packed mips: %s, "
      "unpack length: 0x%.8X)",
      src.memory.base_address, src.memory.mip_address, src.width + 1,
      src.height + 1, src.depth + 1, src.format_info()->name,
      get_dimension_name(src.dimension), src.mip_levels(), src.mip_min_level,
      src.mip_max_level, src.is_stacked ? "yes" : "no", src.pitch,
      src.is_tiled ? "yes" : "no", src.has_packed_mips ? "yes" : "no",
      unpack_length);

  XELOGGPU("Extent: %ux%ux%u  %u,%u,%u", src.extent.pitch, src.extent.height,
           src.extent.depth, src.extent.block_pitch_h, src.extent.block_height,
           src.extent.block_pitch_v);

  if (!unpack_length) {
    XELOGW("Failed to compute texture storage!");
    return false;
  }

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
  if (!alloc) {
    XELOGE("%s: Failed to acquire staging memory!", __func__);
    return false;
  }

  // DEBUG: Check the source address. If it's completely zero'd out, print it.
  bool valid = false;
  auto src_data = memory_->TranslatePhysical(src.memory.base_address);
  for (uint32_t i = 0; i < src.memory.base_size; i++) {
    if (src_data[i] != 0) {
      valid = true;
      break;
    }
  }

  if (!valid) {
    XELOGW("Warning: Texture @ 0x%.8X is blank!", src.memory.base_address);
  }

  // Upload texture into GPU memory.
  // TODO: If the GPU supports it, we can submit a compute batch to convert the
  // texture and copy it to its destination. Otherwise, fallback to conversion
  // on the CPU.
  uint32_t copy_region_count = src.mip_levels();
  std::vector<VkBufferImageCopy> copy_regions(copy_region_count);

  // Upload all mips.
  auto unpack_buffer = reinterpret_cast<uint8_t*>(alloc->host_ptr);
  VkDeviceSize unpack_offset = 0;
  for (uint32_t mip = src.mip_min_level, region = 0; mip <= src.mip_max_level;
       mip++, region++) {
    if (!ConvertTexture(&unpack_buffer[unpack_offset], &copy_regions[region],
                        mip, src)) {
      XELOGW("Failed to convert texture mip %u!", mip);
      return false;
    }
    copy_regions[region].bufferOffset = alloc->offset + unpack_offset;
    copy_regions[region].imageOffset = {0, 0, 0};

    /*
    XELOGGPU("Mip %u %ux%ux%u @ 0x%X", mip,
             copy_regions[region].imageExtent.width,
             copy_regions[region].imageExtent.height,
             copy_regions[region].imageExtent.depth, unpack_offset);
    */

    unpack_offset += ComputeMipStorage(src, mip);
  }

  if (FLAGS_texture_dump) {
    TextureDump(src, unpack_buffer, unpack_length);
  }

  // Transition the texture into a transfer destination layout.
  VkImageMemoryBarrier barrier;
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.pNext = nullptr;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.oldLayout = dest->image_layout;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_FALSE;
  barrier.dstQueueFamilyIndex = VK_FALSE;
  barrier.image = dest->image;
  if (dest->format == VK_FORMAT_D16_UNORM_S8_UINT ||
      dest->format == VK_FORMAT_D24_UNORM_S8_UINT ||
      dest->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
    barrier.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  } else {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }
  barrier.subresourceRange.baseMipLevel = src.mip_min_level;
  barrier.subresourceRange.levelCount = src.mip_levels();
  barrier.subresourceRange.baseArrayLayer =
      copy_regions[0].imageSubresource.baseArrayLayer;
  barrier.subresourceRange.layerCount =
      copy_regions[0].imageSubresource.layerCount;

  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  // Now move the converted texture into the destination.
  if (dest->format == VK_FORMAT_D16_UNORM_S8_UINT ||
      dest->format == VK_FORMAT_D24_UNORM_S8_UINT ||
      dest->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
    // Do just a depth upload (for now).
    // This assumes depth buffers don't have mips (hopefully they don't)
    assert_true(src.mip_levels() == 1);
    copy_regions[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  vkCmdCopyBufferToImage(command_buffer, staging_buffer_.gpu_buffer(),
                         dest->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         copy_region_count, copy_regions.data());

  // Now transition the texture into a shader readonly source.
  barrier.srcAccessMask = barrier.dstAccessMask;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barrier.oldLayout = barrier.newLayout;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &barrier);

  dest->image_layout = barrier.newLayout;
  return true;
}

const FormatInfo* TextureCache::GetFormatInfo(TextureFormat format) {
  switch (format) {
    case TextureFormat::k_CTX1:
      return FormatInfo::Get(TextureFormat::k_8_8);
    case TextureFormat::k_DXT3A:
      return FormatInfo::Get(TextureFormat::k_DXT2_3);
    default:
      return FormatInfo::Get(format);
  }
}

texture_conversion::CopyBlockCallback TextureCache::GetFormatCopyBlock(
    TextureFormat format) {
  switch (format) {
    case TextureFormat::k_CTX1:
      return texture_conversion::ConvertTexelCTX1ToR8G8;
    case TextureFormat::k_DXT3A:
      return texture_conversion::ConvertTexelDXT3AToDXT3;
    default:
      return texture_conversion::CopySwapBlock;
  }
}

TextureExtent TextureCache::GetMipExtent(const TextureInfo& src, uint32_t mip) {
  auto format_info = GetFormatInfo(src.format);
  uint32_t width = src.width + 1;
  uint32_t height = src.height + 1;
  uint32_t depth = src.depth + 1;
  TextureExtent extent;
  if (mip == 0) {
    extent = TextureExtent::Calculate(format_info, width, height, depth, false,
                                      false);
  } else {
    uint32_t mip_width = std::max(1u, width >> mip);
    uint32_t mip_height = std::max(1u, height >> mip);
    extent = TextureExtent::Calculate(format_info, mip_width, mip_height, depth,
                                      false, false);
  }
  return extent;
}

uint32_t TextureCache::ComputeMipStorage(const FormatInfo* format_info,
                                         uint32_t width, uint32_t height,
                                         uint32_t depth, uint32_t mip) {
  assert_not_null(format_info);
  TextureExtent extent;
  if (mip == 0) {
    extent = TextureExtent::Calculate(format_info, width, height, depth, false,
                                      false);
  } else {
    uint32_t mip_width = std::max(1u, width >> mip);
    uint32_t mip_height = std::max(1u, height >> mip);
    extent = TextureExtent::Calculate(format_info, mip_width, mip_height, depth,
                                      false, false);
  }
  uint32_t bytes_per_block = format_info->bytes_per_block();
  return extent.all_blocks() * bytes_per_block;
}

uint32_t TextureCache::ComputeMipStorage(const TextureInfo& src, uint32_t mip) {
  uint32_t size = ComputeMipStorage(GetFormatInfo(src.format), src.width + 1,
                                    src.height + 1, src.depth + 1, mip);
  // ensure 4-byte alignment
  return (size + 3) & (~3u);
}

uint32_t TextureCache::ComputeTextureStorage(const TextureInfo& src) {
  auto format_info = GetFormatInfo(src.format);
  uint32_t width = src.width + 1;
  uint32_t height = src.height + 1;
  uint32_t depth = src.depth + 1;
  uint32_t length = 0;
  for (uint32_t mip = src.mip_min_level; mip <= src.mip_max_level; ++mip) {
    if (mip == 0 && !src.memory.base_address) {
      continue;
    } else if (mip > 0 && !src.memory.mip_address) {
      continue;
    }
    length += ComputeMipStorage(format_info, width, height, depth, mip);
  }
  return length;
}

void TextureCache::WritebackTexture(Texture* texture) {
  VkResult status = VK_SUCCESS;
  VkFence fence = wb_command_pool_->BeginBatch();
  auto alloc = wb_staging_buffer_.Acquire(texture->alloc_info.size, fence);
  if (!alloc) {
    wb_command_pool_->EndBatch();
    return;
  }

  auto command_buffer = wb_command_pool_->AcquireEntry();

  VkCommandBufferBeginInfo begin_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      nullptr,
      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      nullptr,
  };
  vkBeginCommandBuffer(command_buffer, &begin_info);

  // TODO: Transition the texture to a transfer source.
  // TODO: copy depth/layers?

  VkBufferImageCopy region;
  region.bufferOffset = alloc->offset;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset.x = 0;
  region.imageOffset.y = 0;
  region.imageOffset.z = 0;
  region.imageExtent.width = texture->texture_info.width + 1;
  region.imageExtent.height = texture->texture_info.height + 1;
  region.imageExtent.depth = 1;

  vkCmdCopyImageToBuffer(command_buffer, texture->image,
                         VK_IMAGE_LAYOUT_GENERAL,
                         wb_staging_buffer_.gpu_buffer(), 1, &region);

  // TODO: Transition the texture back to a shader resource.

  vkEndCommandBuffer(command_buffer);

  // Submit the command buffer.
  // Submit commands and wait.
  {
    std::lock_guard<std::mutex>(device_->primary_queue_mutex());
    VkSubmitInfo submit_info = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0,
        nullptr,
        nullptr,
        1,
        &command_buffer,
        0,
        nullptr,
    };
    status = vkQueueSubmit(device_->primary_queue(), 1, &submit_info, fence);
    CheckResult(status, "vkQueueSubmit");

    if (status == VK_SUCCESS) {
      status = vkQueueWaitIdle(device_->primary_queue());
      CheckResult(status, "vkQueueWaitIdle");
    }
  }

  wb_command_pool_->EndBatch();

  if (status == VK_SUCCESS) {
    auto dest =
        memory_->TranslatePhysical(texture->texture_info.memory.base_address);
    std::memcpy(dest, alloc->host_ptr, texture->texture_info.memory.base_size);
  }

  wb_staging_buffer_.Scavenge();
}

void TextureCache::HashTextureBindings(
    XXH64_state_t* hash_state, uint32_t& fetch_mask,
    const std::vector<Shader::TextureBinding>& bindings) {
  for (auto& binding : bindings) {
    uint32_t fetch_bit = 1 << binding.fetch_constant;
    if (fetch_mask & fetch_bit) {
      // We've covered this binding.
      continue;
    }
    fetch_mask |= fetch_bit;

    auto& regs = *register_file_;
    int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + binding.fetch_constant * 6;
    auto group =
        reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(&regs.values[r]);
    auto& fetch = group->texture_fetch;

    XXH64_update(hash_state, &fetch, sizeof(fetch));
  }
}

VkDescriptorSet TextureCache::PrepareTextureSet(
    VkCommandBuffer command_buffer, VkFence completion_fence,
    const std::vector<Shader::TextureBinding>& vertex_bindings,
    const std::vector<Shader::TextureBinding>& pixel_bindings) {
  XXH64_state_t hash_state;
  XXH64_reset(&hash_state, 0);

  // (quickly) Generate a hash.
  uint32_t fetch_mask = 0;
  HashTextureBindings(&hash_state, fetch_mask, vertex_bindings);
  HashTextureBindings(&hash_state, fetch_mask, pixel_bindings);
  uint64_t hash = XXH64_digest(&hash_state);
  for (auto it = texture_sets_.find(hash); it != texture_sets_.end(); ++it) {
    // TODO(DrChat): We need to compare the bindings and ensure they're equal.
    return it->second;
  }

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
    XELOGW("Failed to setup one or more texture bindings!");
    // TODO(benvanik): actually bail out here?
  }

  // Open a new batch of descriptor sets (for this frame)
  if (!descriptor_pool_->has_open_batch()) {
    descriptor_pool_->BeginBatch(completion_fence);
  }

  auto descriptor_set =
      descriptor_pool_->AcquireEntry(texture_descriptor_set_layout_);
  if (!descriptor_set) {
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

  texture_sets_[hash] = descriptor_set;
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
  if (fetch.type != 0x2) {
    XELOGGPU("Fetch type is not 2!");
    return false;
  }

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

  // Search via the base format.
  texture_info.format = GetBaseFormat(texture_info.format);

  auto texture = Demand(texture_info, command_buffer, completion_fence);
  auto sampler = Demand(sampler_info);
  if (texture == nullptr || sampler == nullptr) {
    XELOGE("Texture or sampler is NULL!");
    return false;
  }

  uint16_t swizzle = static_cast<uint16_t>(fetch.swizzle);
  auto view = DemandView(texture, swizzle);

  auto image_info =
      &update_set_info->image_infos[update_set_info->image_write_count];
  auto image_write =
      &update_set_info->image_writes[update_set_info->image_write_count];
  update_set_info->image_write_count++;

  // Sanity check, we only have 32 binding slots.
  assert(binding.binding_index < 32);

  image_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  image_write->pNext = nullptr;
  // image_write->dstSet is set later...
  image_write->dstBinding = 0;
  image_write->dstArrayElement = uint32_t(binding.binding_index);
  image_write->descriptorCount = 1;
  image_write->descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  image_write->pImageInfo = image_info;
  image_write->pBufferInfo = nullptr;
  image_write->pTexelBufferView = nullptr;

  image_info->imageView = view->view;
  image_info->imageLayout = texture->image_layout;
  image_info->sampler = sampler->sampler;
  texture->in_flight_fence = completion_fence;

  return true;
}

void TextureCache::RemoveInvalidatedTextures() {
  // Clean up any invalidated textures.
  invalidated_textures_mutex_.lock();
  std::unordered_set<Texture*>& invalidated_textures = *invalidated_textures_;
  if (invalidated_textures_ == &invalidated_textures_sets_[0]) {
    invalidated_textures_ = &invalidated_textures_sets_[1];
  } else {
    invalidated_textures_ = &invalidated_textures_sets_[0];
  }
  invalidated_textures_mutex_.unlock();

  // Append all invalidated textures to a deletion queue. They will be deleted
  // when all command buffers using them have finished executing.
  if (!invalidated_textures.empty()) {
    for (auto it = invalidated_textures.begin();
         it != invalidated_textures.end(); ++it) {
      pending_delete_textures_.push_back(*it);
      textures_.erase((*it)->texture_info.hash());
    }

    COUNT_profile_set("gpu/texture_cache/textures", textures_.size());
    COUNT_profile_set("gpu/texture_cache/pending_deletes",
                      pending_delete_textures_.size());
    invalidated_textures.clear();
  }
}

void TextureCache::ClearCache() {
  RemoveInvalidatedTextures();
  for (auto it = textures_.begin(); it != textures_.end(); ++it) {
    while (!FreeTexture(it->second)) {
      // Texture still in use. Busy loop.
      xe::threading::MaybeYield();
    }
  }
  textures_.clear();
  COUNT_profile_set("gpu/texture_cache/textures", 0);

  for (auto it = samplers_.begin(); it != samplers_.end(); ++it) {
    vkDestroySampler(*device_, it->second->sampler, nullptr);
    delete it->second;
  }
  samplers_.clear();
}

void TextureCache::Scavenge() {
  SCOPE_profile_cpu_f("gpu");

  // Close any open descriptor pool batches
  if (descriptor_pool_->has_open_batch()) {
    descriptor_pool_->EndBatch();
  }

  // Free unused descriptor sets
  // TODO(DrChat): These sets could persist across frames, we just need a smart
  // way to detect if they're unused and free them.
  texture_sets_.clear();
  descriptor_pool_->Scavenge();
  staging_buffer_.Scavenge();

  // Kill all pending delete textures.
  RemoveInvalidatedTextures();
  if (!pending_delete_textures_.empty()) {
    for (auto it = pending_delete_textures_.begin();
         it != pending_delete_textures_.end();) {
      if (!FreeTexture(*it)) {
        break;
      }

      it = pending_delete_textures_.erase(it);
    }

    COUNT_profile_set("gpu/texture_cache/pending_deletes",
                      pending_delete_textures_.size());
  }
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
