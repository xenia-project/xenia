/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_util.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

// Implement AMD's VMA here.
#define VMA_IMPLEMENTATION
#include "third_party/vulkan/vk_mem_alloc.h"

namespace xe {
namespace ui {
namespace vulkan {

uint32_t Version::Make(uint32_t major, uint32_t minor, uint32_t patch) {
  return VK_MAKE_VERSION(major, minor, patch);
}

Version Version::Parse(uint32_t value) {
  Version version;
  version.major = VK_VERSION_MAJOR(value);
  version.minor = VK_VERSION_MINOR(value);
  version.patch = VK_VERSION_PATCH(value);
  version.pretty_string = xe::format_string("%u.%u.%u", version.major,
                                            version.minor, version.patch);
  return version;
}

const char* to_string(VkFormat format) {
  switch (format) {
#define STR(r) \
  case r:      \
    return #r
    STR(VK_FORMAT_UNDEFINED);
    STR(VK_FORMAT_R4G4_UNORM_PACK8);
    STR(VK_FORMAT_R4G4B4A4_UNORM_PACK16);
    STR(VK_FORMAT_B4G4R4A4_UNORM_PACK16);
    STR(VK_FORMAT_R5G6B5_UNORM_PACK16);
    STR(VK_FORMAT_B5G6R5_UNORM_PACK16);
    STR(VK_FORMAT_R5G5B5A1_UNORM_PACK16);
    STR(VK_FORMAT_B5G5R5A1_UNORM_PACK16);
    STR(VK_FORMAT_A1R5G5B5_UNORM_PACK16);
    STR(VK_FORMAT_R8_UNORM);
    STR(VK_FORMAT_R8_SNORM);
    STR(VK_FORMAT_R8_USCALED);
    STR(VK_FORMAT_R8_SSCALED);
    STR(VK_FORMAT_R8_UINT);
    STR(VK_FORMAT_R8_SINT);
    STR(VK_FORMAT_R8_SRGB);
    STR(VK_FORMAT_R8G8_UNORM);
    STR(VK_FORMAT_R8G8_SNORM);
    STR(VK_FORMAT_R8G8_USCALED);
    STR(VK_FORMAT_R8G8_SSCALED);
    STR(VK_FORMAT_R8G8_UINT);
    STR(VK_FORMAT_R8G8_SINT);
    STR(VK_FORMAT_R8G8_SRGB);
    STR(VK_FORMAT_R8G8B8_UNORM);
    STR(VK_FORMAT_R8G8B8_SNORM);
    STR(VK_FORMAT_R8G8B8_USCALED);
    STR(VK_FORMAT_R8G8B8_SSCALED);
    STR(VK_FORMAT_R8G8B8_UINT);
    STR(VK_FORMAT_R8G8B8_SINT);
    STR(VK_FORMAT_R8G8B8_SRGB);
    STR(VK_FORMAT_B8G8R8_UNORM);
    STR(VK_FORMAT_B8G8R8_SNORM);
    STR(VK_FORMAT_B8G8R8_USCALED);
    STR(VK_FORMAT_B8G8R8_SSCALED);
    STR(VK_FORMAT_B8G8R8_UINT);
    STR(VK_FORMAT_B8G8R8_SINT);
    STR(VK_FORMAT_B8G8R8_SRGB);
    STR(VK_FORMAT_R8G8B8A8_UNORM);
    STR(VK_FORMAT_R8G8B8A8_SNORM);
    STR(VK_FORMAT_R8G8B8A8_USCALED);
    STR(VK_FORMAT_R8G8B8A8_SSCALED);
    STR(VK_FORMAT_R8G8B8A8_UINT);
    STR(VK_FORMAT_R8G8B8A8_SINT);
    STR(VK_FORMAT_R8G8B8A8_SRGB);
    STR(VK_FORMAT_B8G8R8A8_UNORM);
    STR(VK_FORMAT_B8G8R8A8_SNORM);
    STR(VK_FORMAT_B8G8R8A8_USCALED);
    STR(VK_FORMAT_B8G8R8A8_SSCALED);
    STR(VK_FORMAT_B8G8R8A8_UINT);
    STR(VK_FORMAT_B8G8R8A8_SINT);
    STR(VK_FORMAT_B8G8R8A8_SRGB);
    STR(VK_FORMAT_A8B8G8R8_UNORM_PACK32);
    STR(VK_FORMAT_A8B8G8R8_SNORM_PACK32);
    STR(VK_FORMAT_A8B8G8R8_USCALED_PACK32);
    STR(VK_FORMAT_A8B8G8R8_SSCALED_PACK32);
    STR(VK_FORMAT_A8B8G8R8_UINT_PACK32);
    STR(VK_FORMAT_A8B8G8R8_SINT_PACK32);
    STR(VK_FORMAT_A8B8G8R8_SRGB_PACK32);
    STR(VK_FORMAT_A2R10G10B10_UNORM_PACK32);
    STR(VK_FORMAT_A2R10G10B10_SNORM_PACK32);
    STR(VK_FORMAT_A2R10G10B10_USCALED_PACK32);
    STR(VK_FORMAT_A2R10G10B10_SSCALED_PACK32);
    STR(VK_FORMAT_A2R10G10B10_UINT_PACK32);
    STR(VK_FORMAT_A2R10G10B10_SINT_PACK32);
    STR(VK_FORMAT_A2B10G10R10_UNORM_PACK32);
    STR(VK_FORMAT_A2B10G10R10_SNORM_PACK32);
    STR(VK_FORMAT_A2B10G10R10_USCALED_PACK32);
    STR(VK_FORMAT_A2B10G10R10_SSCALED_PACK32);
    STR(VK_FORMAT_A2B10G10R10_UINT_PACK32);
    STR(VK_FORMAT_A2B10G10R10_SINT_PACK32);
    STR(VK_FORMAT_R16_UNORM);
    STR(VK_FORMAT_R16_SNORM);
    STR(VK_FORMAT_R16_USCALED);
    STR(VK_FORMAT_R16_SSCALED);
    STR(VK_FORMAT_R16_UINT);
    STR(VK_FORMAT_R16_SINT);
    STR(VK_FORMAT_R16_SFLOAT);
    STR(VK_FORMAT_R16G16_UNORM);
    STR(VK_FORMAT_R16G16_SNORM);
    STR(VK_FORMAT_R16G16_USCALED);
    STR(VK_FORMAT_R16G16_SSCALED);
    STR(VK_FORMAT_R16G16_UINT);
    STR(VK_FORMAT_R16G16_SINT);
    STR(VK_FORMAT_R16G16_SFLOAT);
    STR(VK_FORMAT_R16G16B16_UNORM);
    STR(VK_FORMAT_R16G16B16_SNORM);
    STR(VK_FORMAT_R16G16B16_USCALED);
    STR(VK_FORMAT_R16G16B16_SSCALED);
    STR(VK_FORMAT_R16G16B16_UINT);
    STR(VK_FORMAT_R16G16B16_SINT);
    STR(VK_FORMAT_R16G16B16_SFLOAT);
    STR(VK_FORMAT_R16G16B16A16_UNORM);
    STR(VK_FORMAT_R16G16B16A16_SNORM);
    STR(VK_FORMAT_R16G16B16A16_USCALED);
    STR(VK_FORMAT_R16G16B16A16_SSCALED);
    STR(VK_FORMAT_R16G16B16A16_UINT);
    STR(VK_FORMAT_R16G16B16A16_SINT);
    STR(VK_FORMAT_R16G16B16A16_SFLOAT);
    STR(VK_FORMAT_R32_UINT);
    STR(VK_FORMAT_R32_SINT);
    STR(VK_FORMAT_R32_SFLOAT);
    STR(VK_FORMAT_R32G32_UINT);
    STR(VK_FORMAT_R32G32_SINT);
    STR(VK_FORMAT_R32G32_SFLOAT);
    STR(VK_FORMAT_R32G32B32_UINT);
    STR(VK_FORMAT_R32G32B32_SINT);
    STR(VK_FORMAT_R32G32B32_SFLOAT);
    STR(VK_FORMAT_R32G32B32A32_UINT);
    STR(VK_FORMAT_R32G32B32A32_SINT);
    STR(VK_FORMAT_R32G32B32A32_SFLOAT);
    STR(VK_FORMAT_R64_UINT);
    STR(VK_FORMAT_R64_SINT);
    STR(VK_FORMAT_R64_SFLOAT);
    STR(VK_FORMAT_R64G64_UINT);
    STR(VK_FORMAT_R64G64_SINT);
    STR(VK_FORMAT_R64G64_SFLOAT);
    STR(VK_FORMAT_R64G64B64_UINT);
    STR(VK_FORMAT_R64G64B64_SINT);
    STR(VK_FORMAT_R64G64B64_SFLOAT);
    STR(VK_FORMAT_R64G64B64A64_UINT);
    STR(VK_FORMAT_R64G64B64A64_SINT);
    STR(VK_FORMAT_R64G64B64A64_SFLOAT);
    STR(VK_FORMAT_B10G11R11_UFLOAT_PACK32);
    STR(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32);
    STR(VK_FORMAT_D16_UNORM);
    STR(VK_FORMAT_X8_D24_UNORM_PACK32);
    STR(VK_FORMAT_D32_SFLOAT);
    STR(VK_FORMAT_S8_UINT);
    STR(VK_FORMAT_D16_UNORM_S8_UINT);
    STR(VK_FORMAT_D24_UNORM_S8_UINT);
    STR(VK_FORMAT_D32_SFLOAT_S8_UINT);
    STR(VK_FORMAT_BC1_RGB_UNORM_BLOCK);
    STR(VK_FORMAT_BC1_RGB_SRGB_BLOCK);
    STR(VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    STR(VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
    STR(VK_FORMAT_BC2_UNORM_BLOCK);
    STR(VK_FORMAT_BC2_SRGB_BLOCK);
    STR(VK_FORMAT_BC3_UNORM_BLOCK);
    STR(VK_FORMAT_BC3_SRGB_BLOCK);
    STR(VK_FORMAT_BC4_UNORM_BLOCK);
    STR(VK_FORMAT_BC4_SNORM_BLOCK);
    STR(VK_FORMAT_BC5_UNORM_BLOCK);
    STR(VK_FORMAT_BC5_SNORM_BLOCK);
    STR(VK_FORMAT_BC6H_UFLOAT_BLOCK);
    STR(VK_FORMAT_BC6H_SFLOAT_BLOCK);
    STR(VK_FORMAT_BC7_UNORM_BLOCK);
    STR(VK_FORMAT_BC7_SRGB_BLOCK);
    STR(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK);
    STR(VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK);
    STR(VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK);
    STR(VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK);
    STR(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK);
    STR(VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK);
    STR(VK_FORMAT_EAC_R11_UNORM_BLOCK);
    STR(VK_FORMAT_EAC_R11_SNORM_BLOCK);
    STR(VK_FORMAT_EAC_R11G11_UNORM_BLOCK);
    STR(VK_FORMAT_EAC_R11G11_SNORM_BLOCK);
    STR(VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_4x4_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_5x4_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_5x4_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_5x5_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_5x5_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_6x5_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_6x5_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_6x6_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_6x6_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_8x5_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_8x5_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_8x6_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_8x6_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_8x8_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_8x8_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_10x5_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_10x5_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_10x6_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_10x6_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_10x8_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_10x8_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_10x10_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_10x10_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_12x10_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_12x10_SRGB_BLOCK);
    STR(VK_FORMAT_ASTC_12x12_UNORM_BLOCK);
    STR(VK_FORMAT_ASTC_12x12_SRGB_BLOCK);
#undef STR
    default:
      return "UNKNOWN_FORMAT";
  }
}

const char* to_string(VkPhysicalDeviceType type) {
  switch (type) {
#define STR(r) \
  case r:      \
    return #r
    STR(VK_PHYSICAL_DEVICE_TYPE_OTHER);
    STR(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
    STR(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
    STR(VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU);
    STR(VK_PHYSICAL_DEVICE_TYPE_CPU);
#undef STR
    default:
      return "UNKNOWN_DEVICE";
  }
}

const char* to_string(VkSharingMode sharing_mode) {
  switch (sharing_mode) {
#define STR(r) \
  case r:      \
    return #r
    STR(VK_SHARING_MODE_EXCLUSIVE);
    STR(VK_SHARING_MODE_CONCURRENT);
#undef STR
    default:
      return "UNKNOWN_SHARING_MODE";
  }
}

const char* to_string(VkResult result) {
  switch (result) {
#define STR(r) \
  case r:      \
    return #r
    STR(VK_SUCCESS);
    STR(VK_NOT_READY);
    STR(VK_TIMEOUT);
    STR(VK_EVENT_SET);
    STR(VK_EVENT_RESET);
    STR(VK_INCOMPLETE);
    STR(VK_ERROR_OUT_OF_HOST_MEMORY);
    STR(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    STR(VK_ERROR_INITIALIZATION_FAILED);
    STR(VK_ERROR_DEVICE_LOST);
    STR(VK_ERROR_MEMORY_MAP_FAILED);
    STR(VK_ERROR_LAYER_NOT_PRESENT);
    STR(VK_ERROR_EXTENSION_NOT_PRESENT);
    STR(VK_ERROR_FEATURE_NOT_PRESENT);
    STR(VK_ERROR_INCOMPATIBLE_DRIVER);
    STR(VK_ERROR_TOO_MANY_OBJECTS);
    STR(VK_ERROR_FORMAT_NOT_SUPPORTED);
    STR(VK_ERROR_SURFACE_LOST_KHR);
    STR(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
    STR(VK_SUBOPTIMAL_KHR);
    STR(VK_ERROR_OUT_OF_DATE_KHR);
    STR(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
    STR(VK_ERROR_VALIDATION_FAILED_EXT);
    STR(VK_ERROR_INVALID_SHADER_NV);
    STR(VK_ERROR_OUT_OF_POOL_MEMORY_KHR);
    STR(VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR);
#undef STR
    default:
      return "UNKNOWN_RESULT";
  }
}

std::string to_flags_string(VkImageUsageFlagBits flags) {
  std::string result;
#define OR_FLAG(f)         \
  if (flags & f) {         \
    if (!result.empty()) { \
      result += " | ";     \
    }                      \
    result += #f;          \
  }
  OR_FLAG(VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  OR_FLAG(VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  OR_FLAG(VK_IMAGE_USAGE_SAMPLED_BIT);
  OR_FLAG(VK_IMAGE_USAGE_STORAGE_BIT);
  OR_FLAG(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  OR_FLAG(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
  OR_FLAG(VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);
  OR_FLAG(VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
#undef OR_FLAG
  return result;
}

std::string to_flags_string(VkFormatFeatureFlagBits flags) {
  std::string result;
#define OR_FLAG(f)         \
  if (flags & f) {         \
    if (!result.empty()) { \
      result += " | ";     \
    }                      \
    result += #f;          \
  }
  OR_FLAG(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_BLIT_DST_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
  OR_FLAG(VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_IMG);
  OR_FLAG(VK_FORMAT_FEATURE_TRANSFER_SRC_BIT_KHR);
  OR_FLAG(VK_FORMAT_FEATURE_TRANSFER_DST_BIT_KHR);
  OR_FLAG(VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT_EXT);
#undef OR_FLAG
  return result;
}

std::string to_flags_string(VkSurfaceTransformFlagBitsKHR flags) {
  std::string result;
#define OR_FLAG(f)         \
  if (flags & f) {         \
    if (!result.empty()) { \
      result += " | ";     \
    }                      \
    result += #f;          \
  }
  OR_FLAG(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR);
  OR_FLAG(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR);
  OR_FLAG(VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR);
  OR_FLAG(VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR);
  OR_FLAG(VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR);
  OR_FLAG(VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR);
  OR_FLAG(VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR);
  OR_FLAG(VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR);
  OR_FLAG(VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR);
#undef OR_FLAG
  return result;
}

const char* to_string(VkColorSpaceKHR color_space) {
  switch (color_space) {
#define STR(r) \
  case r:      \
    return #r
    STR(VK_COLORSPACE_SRGB_NONLINEAR_KHR);
#undef STR
    default:
      return "UNKNOWN_COLORSPACE";
  }
}

const char* to_string(VkPresentModeKHR present_mode) {
  switch (present_mode) {
#define STR(r) \
  case r:      \
    return #r
    STR(VK_PRESENT_MODE_IMMEDIATE_KHR);
    STR(VK_PRESENT_MODE_MAILBOX_KHR);
    STR(VK_PRESENT_MODE_FIFO_KHR);
    STR(VK_PRESENT_MODE_FIFO_RELAXED_KHR);
#undef STR
    default:
      return "UNKNOWN_PRESENT_MODE";
  }
}

void FatalVulkanError(std::string error) {
  xe::FatalError(
      error +
      "\nEnsure you have the latest drivers for your GPU and that it supports "
      "Vulkan. See http://xenia.jp/faq/ for more information and a list"
      "of supported GPUs.");
}

void CheckResult(VkResult result, const char* action) {
  if (result) {
    XELOGE("Vulkan check: %s returned %s", action, to_string(result));
  }
  assert_true(result == VK_SUCCESS, action);
}

std::pair<bool, std::vector<const char*>> CheckRequirements(
    const std::vector<Requirement>& requirements,
    const std::vector<LayerInfo>& layer_infos) {
  bool any_missing = false;
  std::vector<const char*> enabled_layers;
  for (auto& requirement : requirements) {
    bool found = false;
    for (size_t j = 0; j < layer_infos.size(); ++j) {
      auto layer_name = layer_infos[j].properties.layerName;
      auto layer_version =
          Version::Parse(layer_infos[j].properties.specVersion);
      if (requirement.name == layer_name) {
        found = true;
        if (requirement.min_version > layer_infos[j].properties.specVersion) {
          if (requirement.is_optional) {
            XELOGVK("- optional layer %s (%s) version mismatch", layer_name,
                    layer_version.pretty_string.c_str());
            continue;
          }
          XELOGE("ERROR: required layer %s (%s) version mismatch", layer_name,
                 layer_version.pretty_string.c_str());
          any_missing = true;
          break;
        }
        XELOGVK("- enabling layer %s (%s)", layer_name,
                layer_version.pretty_string.c_str());
        enabled_layers.push_back(layer_name);
        break;
      }
    }
    if (!found) {
      if (requirement.is_optional) {
        XELOGVK("- optional layer %s not found", requirement.name.c_str());
      } else {
        XELOGE("ERROR: required layer %s not found", requirement.name.c_str());
        any_missing = true;
      }
    }
  }
  return {!any_missing, enabled_layers};
}

std::pair<bool, std::vector<const char*>> CheckRequirements(
    const std::vector<Requirement>& requirements,
    const std::vector<VkExtensionProperties>& extension_properties) {
  bool any_missing = false;
  std::vector<const char*> enabled_extensions;
  for (auto& requirement : requirements) {
    bool found = false;
    for (size_t j = 0; j < extension_properties.size(); ++j) {
      auto extension_name = extension_properties[j].extensionName;
      auto extension_version =
          Version::Parse(extension_properties[j].specVersion);
      if (requirement.name == extension_name) {
        found = true;
        if (requirement.min_version > extension_properties[j].specVersion) {
          if (requirement.is_optional) {
            XELOGVK("- optional extension %s (%s) version mismatch",
                    extension_name, extension_version.pretty_string.c_str());
            continue;
          }
          XELOGE("ERROR: required extension %s (%s) version mismatch",
                 extension_name, extension_version.pretty_string.c_str());
          any_missing = true;
          break;
        }
        XELOGVK("- enabling extension %s (%s)", extension_name,
                extension_version.pretty_string.c_str());
        enabled_extensions.push_back(extension_name);
        break;
      }
    }
    if (!found) {
      if (requirement.is_optional) {
        XELOGVK("- optional extension %s not found", requirement.name.c_str());
      } else {
        XELOGE("ERROR: required extension %s not found",
               requirement.name.c_str());
        any_missing = true;
      }
    }
  }
  return {!any_missing, enabled_extensions};
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
