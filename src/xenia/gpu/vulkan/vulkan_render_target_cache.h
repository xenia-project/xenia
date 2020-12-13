/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_RENDER_TARGET_CACHE_H_
#define XENIA_GPU_VULKAN_VULKAN_RENDER_TARGET_CACHE_H_

#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "xenia/base/xxhash.h"
#include "xenia/gpu/register_file.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor;

// TODO(Triang3l): Create a common base for both the Vulkan and the Direct3D
// implementations.
class VulkanRenderTargetCache {
 public:
  union RenderPassKey {
    uint32_t key = 0;
  };
  static_assert(sizeof(RenderPassKey) == sizeof(uint32_t));

  struct FramebufferKey {
    RenderPassKey render_pass_key;

    // Including all the padding, for a stable hash.
    FramebufferKey() { Reset(); }
    FramebufferKey(const FramebufferKey& key) {
      std::memcpy(this, &key, sizeof(*this));
    }
    FramebufferKey& operator=(const FramebufferKey& key) {
      std::memcpy(this, &key, sizeof(*this));
      return *this;
    }
    bool operator==(const FramebufferKey& key) const {
      return std::memcmp(this, &key, sizeof(*this)) == 0;
    }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
    uint64_t GetHash() const { return XXH3_64bits(this, sizeof(*this)); }
    struct Hasher {
      size_t operator()(const FramebufferKey& description) const {
        return size_t(description.GetHash());
      }
    };
  };
  static_assert(sizeof(FramebufferKey) == sizeof(uint32_t));

  VulkanRenderTargetCache(VulkanCommandProcessor& command_processor,
                          const RegisterFile& register_file);
  ~VulkanRenderTargetCache();

  bool Initialize();
  void Shutdown();
  void ClearCache();

  // Returns the render pass object, or VK_NULL_HANDLE if failed to create.
  // A render pass managed by the render target cache may be ended and resumed
  // at any time (to allow for things like copying and texture loading).
  VkRenderPass GetRenderPass(RenderPassKey key);

  // Returns the framebuffer object, or VK_NULL_HANDLE if failed to create.
  VkFramebuffer GetFramebuffer(FramebufferKey key);

  // May dispatch computations.
  bool UpdateRenderTargets(FramebufferKey& framebuffer_key_out);

 private:
  VulkanCommandProcessor& command_processor_;
  const RegisterFile& register_file_;

  // RenderPassKey::key -> VkRenderPass.
  std::unordered_map<uint32_t, VkRenderPass> render_passes_;

  std::unordered_map<FramebufferKey, VkFramebuffer, FramebufferKey::Hasher>
      framebuffers_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_RENDER_TARGET_CACHE_H_
