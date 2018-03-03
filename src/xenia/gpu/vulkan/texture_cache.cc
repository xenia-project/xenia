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

#include "third_party/vulkan/vk_mem_alloc.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::CheckResult;

constexpr uint32_t kMaxTextureSamplers = 32;
constexpr VkDeviceSize kStagingBufferSize = 32 * 1024 * 1024;

struct TextureConfig {
  VkFormat host_format;
};

static const TextureConfig texture_configs[64] = {
    /* k_1_REVERSE              */ {VK_FORMAT_UNDEFINED},
    /* k_1                      */ {VK_FORMAT_UNDEFINED},
    /* k_8                      */ {VK_FORMAT_R8_UNORM},
    // ! A1BGR5
    /* k_1_5_5_5                */ {VK_FORMAT_A1R5G5B5_UNORM_PACK16},
    /* k_5_6_5                  */ {VK_FORMAT_R5G6B5_UNORM_PACK16},
    /* k_6_5_5                  */ {VK_FORMAT_UNDEFINED},
    /* k_8_8_8_8                */ {VK_FORMAT_R8G8B8A8_UNORM},
    /* k_2_10_10_10             */ {VK_FORMAT_A2R10G10B10_UNORM_PACK32},
    /* k_8_A                    */ {VK_FORMAT_UNDEFINED},
    /* k_8_B                    */ {VK_FORMAT_UNDEFINED},
    /* k_8_8                    */ {VK_FORMAT_R8G8_UNORM},
    /* k_Cr_Y1_Cb_Y0            */ {VK_FORMAT_UNDEFINED},
    /* k_Y1_Cr_Y0_Cb            */ {VK_FORMAT_UNDEFINED},
    /* k_Shadow                 */ {VK_FORMAT_UNDEFINED},
    /* k_8_8_8_8_A              */ {VK_FORMAT_UNDEFINED},
    /* k_4_4_4_4                */ {VK_FORMAT_R4G4B4A4_UNORM_PACK16},
    // TODO: Verify if these two are correct (I think not).
    /* k_10_11_11               */ {VK_FORMAT_B10G11R11_UFLOAT_PACK32},
    /* k_11_11_10               */ {VK_FORMAT_B10G11R11_UFLOAT_PACK32},

    /* k_DXT1                   */ {VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
    /* k_DXT2_3                 */ {VK_FORMAT_BC2_UNORM_BLOCK},
    /* k_DXT4_5                 */ {VK_FORMAT_BC3_UNORM_BLOCK},
    /* k_DXV                    */ {VK_FORMAT_UNDEFINED},

    // TODO: D24 unsupported on AMD.
    /* k_24_8                   */ {VK_FORMAT_D24_UNORM_S8_UINT},
    /* k_24_8_FLOAT             */ {VK_FORMAT_D24_UNORM_S8_UINT},
    /* k_16                     */ {VK_FORMAT_R16_UNORM},
    /* k_16_16                  */ {VK_FORMAT_R16G16_UNORM},
    /* k_16_16_16_16            */ {VK_FORMAT_R16G16B16A16_UNORM},
    /* k_16_EXPAND              */ {VK_FORMAT_R16_UNORM},
    /* k_16_16_EXPAND           */ {VK_FORMAT_R16G16_UNORM},
    /* k_16_16_16_16_EXPAND     */ {VK_FORMAT_R16G16B16A16_UNORM},
    /* k_16_FLOAT               */ {VK_FORMAT_R16_SFLOAT},
    /* k_16_16_FLOAT            */ {VK_FORMAT_R16G16_SFLOAT},
    /* k_16_16_16_16_FLOAT      */ {VK_FORMAT_R16G16B16A16_SFLOAT},

    // ! These are UNORM formats, not SINT.
    /* k_32                     */ {VK_FORMAT_R32_SINT},
    /* k_32_32                  */ {VK_FORMAT_R32G32_SINT},
    /* k_32_32_32_32            */ {VK_FORMAT_R32G32B32A32_SINT},
    /* k_32_FLOAT               */ {VK_FORMAT_R32_SFLOAT},
    /* k_32_32_FLOAT            */ {VK_FORMAT_R32G32_SFLOAT},
    /* k_32_32_32_32_FLOAT      */ {VK_FORMAT_R32G32B32A32_SFLOAT},
    /* k_32_AS_8                */ {VK_FORMAT_UNDEFINED},
    /* k_32_AS_8_8              */ {VK_FORMAT_UNDEFINED},
    /* k_16_MPEG                */ {VK_FORMAT_UNDEFINED},
    /* k_16_16_MPEG             */ {VK_FORMAT_UNDEFINED},
    /* k_8_INTERLACED           */ {VK_FORMAT_UNDEFINED},
    /* k_32_AS_8_INTERLACED     */ {VK_FORMAT_UNDEFINED},
    /* k_32_AS_8_8_INTERLACED   */ {VK_FORMAT_UNDEFINED},
    /* k_16_INTERLACED          */ {VK_FORMAT_UNDEFINED},
    /* k_16_MPEG_INTERLACED     */ {VK_FORMAT_UNDEFINED},
    /* k_16_16_MPEG_INTERLACED  */ {VK_FORMAT_UNDEFINED},

    // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
    /* k_DXN                    */ {VK_FORMAT_BC5_UNORM_BLOCK},  // ?

    /* k_8_8_8_8_AS_16_16_16_16 */ {VK_FORMAT_R8G8B8A8_UNORM},
    /* k_DXT1_AS_16_16_16_16    */ {VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
    /* k_DXT2_3_AS_16_16_16_16  */ {VK_FORMAT_BC2_UNORM_BLOCK},
    /* k_DXT4_5_AS_16_16_16_16  */ {VK_FORMAT_BC3_UNORM_BLOCK},

    /* k_2_10_10_10_AS_16_16_16_16 */ {VK_FORMAT_A2R10G10B10_UNORM_PACK32},

    // TODO: Verify if these two are correct (I think not).
    /* k_10_11_11_AS_16_16_16_16 */ {VK_FORMAT_B10G11R11_UFLOAT_PACK32},  // ?
    /* k_11_11_10_AS_16_16_16_16 */ {VK_FORMAT_B10G11R11_UFLOAT_PACK32},  // ?
    /* k_32_32_32_FLOAT         */ {VK_FORMAT_R32G32B32_SFLOAT},
    /* k_DXT3A                  */ {VK_FORMAT_UNDEFINED},
    /* k_DXT5A                  */ {VK_FORMAT_UNDEFINED},  // ATI1N

    // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
    /* k_CTX1                   */ {VK_FORMAT_R8G8_UINT},

    /* k_DXT3A_AS_1_1_1_1       */ {VK_FORMAT_UNDEFINED},

    // Unused.
    /* kUnknown                 */ {VK_FORMAT_UNDEFINED},
    /* kUnknown                 */ {VK_FORMAT_UNDEFINED},
};

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
  VmaAllocatorCreateInfo alloc_info = {
      0, *device_, *device_, 0, 0, nullptr, nullptr,
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
  // Create an image first.
  VkImageCreateInfo image_info = {};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  switch (texture_info.dimension) {
    case Dimension::k1D:
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

  assert_not_null(texture_info.format_info());
  auto& config = texture_configs[int(texture_info.format_info()->format)];
  VkFormat format = config.host_format;
  assert(format != VK_FORMAT_UNDEFINED);

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
    assert_always();
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
  image_info.extent = {texture_info.width + 1, texture_info.height + 1, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = texture_info.depth + 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.queueFamilyIndexCount = 0;
  image_info.pQueueFamilyIndices = nullptr;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage image;

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
  // Clear watch handle first so we don't redundantly
  // remove.
  assert_not_zero(touched_texture->access_watch_handle);
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
      trace_writer_->WriteMemoryReadCachedNop(texture_info.guest_address,
                                              texture_info.input_length);

      return it->second;
    }
  }

  VkFormatFeatureFlags required_flags = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  if (texture_info.texture_format == TextureFormat::k_24_8 ||
      texture_info.texture_format == TextureFormat::k_24_8_FLOAT) {
    required_flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
  } else {
    required_flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
  }

  // No texture at this location. Make a new one.
  auto texture = AllocateTexture(texture_info, required_flags);
  if (!texture) {
    // Failed to allocate texture (out of memory?)
    assert_always();
    XELOGE("Vulkan Texture Cache: Failed to allocate texture!");
    return nullptr;
  }

  // Setup a debug name for the texture.
  device_->DbgSetObjectName(
      reinterpret_cast<uint64_t>(texture->image),
      VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT,
      xe::format_string(
          "0x%.8X - 0x%.8X", texture_info.guest_address,
          texture_info.guest_address + texture_info.input_length));

  // Setup an access watch. If this texture is touched, it is destroyed.
  texture->access_watch_handle = memory_->AddPhysicalAccessWatch(
      texture_info.guest_address, texture_info.input_length,
      cpu::MMIOHandler::kWatchWrite, &WatchCallback, this, texture);

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

      trace_writer_->WriteMemoryReadCached(texture_info.guest_address,
                                           texture_info.input_length);

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
    // Failed to allocate texture (out of memory?)
    assert_always();
    XELOGE("Vulkan Texture Cache: Failed to allocate texture!");
    return nullptr;
  }

  // Though we didn't find an exact match, that doesn't mean we're out of the
  // woods yet. This texture could either be a portion of another texture or
  // vice versa. Copy any overlapping textures into this texture.
  // TODO: Byte count -> pixel count (on x and y axes)
  VkOffset2D offset;
  auto collide_tex = LookupAddress(
      texture_info.guest_address, texture_info.width + 1,
      texture_info.height + 1, texture_info.format_info()->format, &offset);
  if (collide_tex != nullptr) {
    // assert_always();
  }

  trace_writer_->WriteMemoryRead(texture_info.guest_address,
                                 texture_info.input_length);

  // Okay. Put a writewatch on it to tell us if it's been modified from the
  // guest.
  texture->access_watch_handle = memory_->AddPhysicalAccessWatch(
      texture_info.guest_address, texture_info.input_length,
      cpu::MMIOHandler::kWatchWrite, &WatchCallback, this, texture);

  if (!UploadTexture(command_buffer, completion_fence, texture, texture_info)) {
    FreeTexture(texture);
    return nullptr;
  }

  // Setup a debug name for the texture.
  device_->DbgSetObjectName(
      reinterpret_cast<uint64_t>(texture->image),
      VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT,
      xe::format_string(
          "0x%.8X - 0x%.8X", texture_info.guest_address,
          texture_info.guest_address + texture_info.input_length));

  textures_[texture_hash] = texture;
  COUNT_profile_set("gpu/texture_cache/textures", textures_.size());
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
  if (texture->format == VK_FORMAT_D16_UNORM_S8_UINT ||
      texture->format == VK_FORMAT_D24_UNORM_S8_UINT ||
      texture->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
    // This applies to any depth/stencil format, but we only use D24S8 / D32FS8.
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
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

  sampler_create_info.mipLodBias = sampler_info.lod_bias;

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
    COMPARE_FIELD(guest_address);
    COMPARE_FIELD(dimension);
    COMPARE_FIELD(width);
    COMPARE_FIELD(height);
    COMPARE_FIELD(depth);
    COMPARE_FIELD(endianness);
    COMPARE_FIELD(is_tiled);
    COMPARE_FIELD(has_packed_mips);
    COMPARE_FIELD(input_length);
#undef COMPARE_FIELD
    if (!TextureFormatIsSimilar(texture_info.texture_format,
                                other_texture_info.texture_format)) {
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

void TextureCache::ConvertTexelCTX1(uint8_t* dest, size_t dest_pitch,
                                    const uint8_t* src, Endian src_endianness) {
  // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
  union {
    uint8_t data[8];
    struct {
      uint8_t r0, g0, r1, g1;
      uint32_t xx;
    };
  } block;
  static_assert(sizeof(block) == 8, "CTX1 block mismatch");

  const uint32_t bytes_per_block = 8;
  TextureSwap(src_endianness, block.data, src, bytes_per_block);

  uint8_t cr[4] = {
      block.r0, block.r1,
      static_cast<uint8_t>(2.f / 3.f * block.r0 + 1.f / 3.f * block.r1),
      static_cast<uint8_t>(1.f / 3.f * block.r0 + 2.f / 3.f * block.r1)};
  uint8_t cg[4] = {
      block.g0, block.g1,
      static_cast<uint8_t>(2.f / 3.f * block.g0 + 1.f / 3.f * block.g1),
      static_cast<uint8_t>(1.f / 3.f * block.g0 + 2.f / 3.f * block.g1)};

  for (uint32_t oy = 0; oy < 4; ++oy) {
    for (uint32_t ox = 0; ox < 4; ++ox) {
      uint8_t xx = (block.xx >> (((ox + (oy * 4)) * 2))) & 3;
      dest[(oy * dest_pitch) + (ox * 2) + 0] = cr[xx];
      dest[(oy * dest_pitch) + (ox * 2) + 1] = cg[xx];
    }
  }
}

bool TextureCache::ConvertTexture2D(uint8_t* dest,
                                    VkBufferImageCopy* copy_region,
                                    const TextureInfo& src) {
  void* host_address = memory_->TranslatePhysical(src.guest_address);
  if (!src.is_tiled) {
    uint32_t offset_x, offset_y;
    if (src.has_packed_mips &&
        TextureInfo::GetPackedTileOffset(src, &offset_x, &offset_y)) {
      uint32_t bytes_per_block = src.format_info()->block_width *
                                 src.format_info()->block_height *
                                 src.format_info()->bits_per_pixel / 8;

      const uint8_t* src_mem = reinterpret_cast<const uint8_t*>(host_address);
      src_mem += offset_y * src.size_2d.input_pitch;
      src_mem += offset_x * bytes_per_block;
      for (uint32_t y = 0;
           y < std::min(src.size_2d.block_height, src.size_2d.logical_height);
           y++) {
        TextureSwap(src.endianness, dest, src_mem, src.size_2d.input_pitch);
        src_mem += src.size_2d.input_pitch;
        dest += src.size_2d.input_pitch;
      }
      copy_region->bufferRowLength = src.size_2d.input_width;
      copy_region->bufferImageHeight = src.size_2d.input_height;
      copy_region->imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy_region->imageExtent = {src.size_2d.logical_width,
                                  src.size_2d.logical_height, 1};
      return true;
    } else {
      // Fast path copy entire image.
      TextureSwap(src.endianness, dest, host_address, src.input_length);
      copy_region->bufferRowLength = src.size_2d.input_width;
      copy_region->bufferImageHeight = src.size_2d.input_height;
      copy_region->imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy_region->imageExtent = {src.size_2d.logical_width,
                                  src.size_2d.logical_height, 1};
      return true;
    }
  } else {
    // Untile image.
    // We could do this in a shader to speed things up, as this is pretty
    // slow.

    // TODO(benvanik): optimize this inner loop (or work by tiles).
    const uint8_t* src_mem = reinterpret_cast<const uint8_t*>(host_address);
    uint32_t bytes_per_block = src.format_info()->block_width *
                               src.format_info()->block_height *
                               src.format_info()->bits_per_pixel / 8;

    uint32_t output_pitch = src.size_2d.input_width *
                            src.format_info()->block_width *
                            src.format_info()->bits_per_pixel / 8;

    uint32_t output_row_height = 1;
    if (src.texture_format == TextureFormat::k_CTX1) {
      // TODO: Can we calculate this?
      output_row_height = 4;
    }

    // Tiled textures can be packed; get the offset into the packed texture.
    uint32_t offset_x;
    uint32_t offset_y;
    TextureInfo::GetPackedTileOffset(src, &offset_x, &offset_y);
    auto log2_bpp = (bytes_per_block >> 2) +
                    ((bytes_per_block >> 1) >> (bytes_per_block >> 2));

    // Offset to the current row, in bytes.
    uint32_t output_row_offset = 0;
    for (uint32_t y = 0; y < src.size_2d.block_height; y++) {
      auto input_row_offset = TextureInfo::TiledOffset2DOuter(
          offset_y + y, src.size_2d.block_width, log2_bpp);

      // Go block-by-block on this row.
      uint32_t output_offset = output_row_offset;
      for (uint32_t x = 0; x < src.size_2d.block_width; x++) {
        auto input_offset = TextureInfo::TiledOffset2DInner(
            offset_x + x, offset_y + y, log2_bpp, input_row_offset);
        input_offset >>= log2_bpp;

        if (src.texture_format == TextureFormat::k_CTX1) {
          // Convert to R8G8.
          ConvertTexelCTX1(&dest[output_offset], output_pitch, src_mem,
                           src.endianness);
        } else {
          // Generic swap to destination.
          TextureSwap(src.endianness, dest + output_offset,
                      src_mem + input_offset * bytes_per_block,
                      bytes_per_block);
        }

        output_offset += bytes_per_block;
      }

      output_row_offset += output_pitch * output_row_height;
    }

    copy_region->bufferRowLength = src.size_2d.input_width;
    copy_region->bufferImageHeight = src.size_2d.input_height;
    copy_region->imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy_region->imageExtent = {src.size_2d.logical_width,
                                src.size_2d.logical_height, 1};
    return true;
  }

  return false;
}

bool TextureCache::ConvertTextureCube(uint8_t* dest,
                                      VkBufferImageCopy* copy_region,
                                      const TextureInfo& src) {
  void* host_address = memory_->TranslatePhysical(src.guest_address);
  if (!src.is_tiled) {
    // Fast path copy entire image.
    TextureSwap(src.endianness, dest, host_address, src.input_length);
    copy_region->bufferRowLength = src.size_cube.input_width;
    copy_region->bufferImageHeight = src.size_cube.input_height;
    copy_region->imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 6};
    copy_region->imageExtent = {src.size_cube.logical_width,
                                src.size_cube.logical_height, 1};
    return true;
  } else {
    // TODO(benvanik): optimize this inner loop (or work by tiles).
    const uint8_t* src_mem = reinterpret_cast<const uint8_t*>(host_address);
    uint32_t bytes_per_block = src.format_info()->block_width *
                               src.format_info()->block_height *
                               src.format_info()->bits_per_pixel / 8;
    // Tiled textures can be packed; get the offset into the packed texture.
    uint32_t offset_x;
    uint32_t offset_y;
    TextureInfo::GetPackedTileOffset(src, &offset_x, &offset_y);
    auto bpp = (bytes_per_block >> 2) +
               ((bytes_per_block >> 1) >> (bytes_per_block >> 2));
    for (int face = 0; face < 6; ++face) {
      for (uint32_t y = 0, output_base_offset = 0;
           y < src.size_cube.block_height;
           y++, output_base_offset += src.size_cube.input_pitch) {
        auto input_base_offset = TextureInfo::TiledOffset2DOuter(
            offset_y + y,
            (src.size_cube.input_width / src.format_info()->block_width), bpp);
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
      dest += src.size_cube.input_face_length;
    }

    copy_region->bufferRowLength = src.size_cube.input_width;
    copy_region->bufferImageHeight = src.size_cube.input_height;
    copy_region->imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 6};
    copy_region->imageExtent = {src.size_cube.logical_width,
                                src.size_cube.logical_height, 1};
    return true;
  }

  return false;
}

bool TextureCache::ConvertTexture(uint8_t* dest, VkBufferImageCopy* copy_region,
                                  const TextureInfo& src) {
  switch (src.dimension) {
    case Dimension::k1D:
      assert_always();
      break;
    case Dimension::k2D:
      return ConvertTexture2D(dest, copy_region, src);
    case Dimension::k3D:
      assert_always();
      break;
    case Dimension::kCube:
      return ConvertTextureCube(dest, copy_region, src);
  }
  return false;
}

bool TextureCache::ComputeTextureStorage(size_t* output_length,
                                         const TextureInfo& src) {
  if (src.texture_format == TextureFormat::k_CTX1) {
    switch (src.dimension) {
      case Dimension::k1D: {
        assert_always();
      } break;
      case Dimension::k2D: {
        *output_length = src.size_2d.input_width * src.size_2d.input_height * 2;
        return true;
      }
      case Dimension::k3D: {
        assert_always();
      } break;
      case Dimension::kCube: {
        *output_length =
            src.size_cube.input_width * src.size_cube.input_height * 2 * 6;
        return true;
      }
    }
    return false;
  } else {
    *output_length = src.input_length;
    return true;
  }
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

  VkBufferImageCopy region = {
      alloc->offset,
      0,
      0,
      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      {0, 0, 0},
      {texture->texture_info.width + 1, texture->texture_info.height + 1, 1},
  };

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

  auto dest = memory_->TranslatePhysical(texture->texture_info.guest_address);
  if (status == VK_SUCCESS) {
    std::memcpy(dest, alloc->host_ptr, texture->texture_info.input_length);
  }

  wb_staging_buffer_.Scavenge();
}

bool TextureCache::UploadTexture(VkCommandBuffer command_buffer,
                                 VkFence completion_fence, Texture* dest,
                                 const TextureInfo& src) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  size_t unpack_length;
  if (!ComputeTextureStorage(&unpack_length, src)) {
    XELOGW("Failed to compute texture storage");
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

  // DEBUG: Check the source address. If it's completely zero'd out, print it.
  bool valid = false;
  auto src_data = memory_->TranslatePhysical(src.guest_address);
  for (uint32_t i = 0; i < src.input_length; i++) {
    if (src_data[i] != 0) {
      valid = true;
      break;
    }
  }

  if (!valid) {
    XELOGW(
        "Warning: Uploading blank texture at address 0x%.8X "
        "(length: 0x%.8X, format: %d)",
        src.guest_address, src.input_length, src.texture_format);
  }

  // Upload texture into GPU memory.
  // TODO: If the GPU supports it, we can submit a compute batch to convert the
  // texture and copy it to its destination. Otherwise, fallback to conversion
  // on the CPU.
  VkBufferImageCopy copy_region;
  if (!ConvertTexture(reinterpret_cast<uint8_t*>(alloc->host_ptr), &copy_region,
                      src)) {
    XELOGW("Failed to convert texture");
    return false;
  }

  // Transition the texture into a transfer destination layout.
  VkImageMemoryBarrier barrier;
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.pNext = nullptr;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.oldLayout = dest->image_layout;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = dest->image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                              copy_region.imageSubresource.baseArrayLayer,
                              copy_region.imageSubresource.layerCount};
  if (dest->format == VK_FORMAT_D16_UNORM_S8_UINT ||
      dest->format == VK_FORMAT_D24_UNORM_S8_UINT ||
      dest->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
    barrier.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  }

  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  // Now move the converted texture into the destination.
  copy_region.bufferOffset = alloc->offset;
  copy_region.imageOffset = {0, 0, 0};
  if (dest->format == VK_FORMAT_D16_UNORM_S8_UINT ||
      dest->format == VK_FORMAT_D24_UNORM_S8_UINT ||
      dest->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
    // Do just a depth upload (for now).
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  }
  vkCmdCopyBufferToImage(command_buffer, staging_buffer_.gpu_buffer(),
                         dest->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &copy_region);

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
    XELOGW("Failed to setup one or more texture bindings");
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
  texture_info.texture_format = GetBaseFormat(texture_info.texture_format);

  auto texture = Demand(texture_info, command_buffer, completion_fence);
  auto sampler = Demand(sampler_info);
  if (texture == nullptr || sampler == nullptr) {
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
