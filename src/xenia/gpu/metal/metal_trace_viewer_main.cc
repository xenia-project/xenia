/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <memory>
#include <unordered_map>

#include "xenia/base/logging.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/metal/metal_graphics_system.h"
#include "xenia/gpu/metal/metal_render_target_cache.h"
#include "xenia/gpu/metal/metal_texture_cache.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/trace_viewer.h"
#include "xenia/ui/metal/metal_immediate_drawer.h"

namespace xe {
namespace gpu {
namespace metal {

class MetalTraceViewer final : public TraceViewer {
 public:
  static std::unique_ptr<WindowedApp> Create(
      xe::ui::WindowedAppContext& app_context) {
    return std::unique_ptr<WindowedApp>(new MetalTraceViewer(app_context));
  }

  std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() override {
    return std::unique_ptr<gpu::GraphicsSystem>(new MetalGraphicsSystem());
  }

  uintptr_t GetColorRenderTarget(
      uint32_t pitch, xenos::MsaaSamples samples, uint32_t base,
      xenos::ColorRenderTargetFormat format) override {
    auto* command_processor = GetMetalCommandProcessor();
    if (!command_processor) {
      return 0;
    }
    auto* render_target_cache = command_processor->render_target_cache();
    if (!render_target_cache) {
      return 0;
    }
    MTL::Texture* texture = render_target_cache->GetColorRenderTargetTexture(
        pitch, samples, base, format);
    if (!texture || texture->sampleCount() > 1) {
      return 0;
    }
    return WrapTexture(texture, nullptr, 0);
  }

  uintptr_t GetDepthRenderTarget(
      uint32_t pitch, xenos::MsaaSamples samples, uint32_t base,
      xenos::DepthRenderTargetFormat format) override {
    // TODO(Triang3l): Depth viewer. Immediate shader expects RGBA color.
    return 0;
  }

  uintptr_t GetTextureEntry(const TextureInfo& texture_info,
                            const SamplerInfo& sampler_info,
                            uint32_t fetch_constant) override {
    auto* command_processor = GetMetalCommandProcessor();
    if (!command_processor) {
      return 0;
    }
    auto* texture_cache = command_processor->texture_cache();
    if (!texture_cache) {
      return 0;
    }

    xenos::FetchOpDimension dimension =
        GetFetchDimension(texture_info.dimension);
    MTL::Texture* texture = texture_cache->GetTextureForBinding(
        fetch_constant, dimension, false);
    if (!texture || texture == texture_cache->GetNullTexture2D() ||
        texture == texture_cache->GetNullTexture3D() ||
        texture == texture_cache->GetNullTextureCube()) {
      return 0;
    }

    MetalTextureCache::SamplerParameters params =
        GetSamplerParameters(sampler_info);
    MTL::SamplerState* sampler = texture_cache->GetOrCreateSampler(params);
    return WrapTexture(texture, sampler, params.value);
  }

 private:
  explicit MetalTraceViewer(xe::ui::WindowedAppContext& app_context)
      : TraceViewer(app_context, "xenia-gpu-metal-trace-viewer") {}

  struct TextureCacheKey {
    MTL::Texture* texture = nullptr;
    uint32_t sampler_key = 0;

    bool operator==(const TextureCacheKey& other) const {
      return texture == other.texture && sampler_key == other.sampler_key;
    }
  };

  struct TextureCacheKeyHasher {
    size_t operator()(const TextureCacheKey& key) const {
      size_t h = std::hash<void*>{}(key.texture);
      return h ^ (std::hash<uint32_t>{}(key.sampler_key) << 1);
    }
  };

  MetalCommandProcessor* GetMetalCommandProcessor() const {
    auto* system = graphics_system();
    if (!system) {
      return nullptr;
    }
    return dynamic_cast<MetalCommandProcessor*>(system->command_processor());
  }

  static xenos::FetchOpDimension GetFetchDimension(
      xenos::DataDimension dimension) {
    switch (dimension) {
      case xenos::DataDimension::k3D:
        return xenos::FetchOpDimension::k3DOrStacked;
      case xenos::DataDimension::kCube:
        return xenos::FetchOpDimension::kCube;
      case xenos::DataDimension::k1D:
      case xenos::DataDimension::k2DOrStacked:
      default:
        return xenos::FetchOpDimension::k2D;
    }
  }

  static MetalTextureCache::SamplerParameters GetSamplerParameters(
      const SamplerInfo& sampler_info) {
    MetalTextureCache::SamplerParameters params;
    params.value = 0;
    params.clamp_x = sampler_info.clamp_u;
    params.clamp_y = sampler_info.clamp_v;
    params.clamp_z = sampler_info.clamp_w;
    params.border_color = sampler_info.border_color;
    params.mag_linear =
        sampler_info.mag_filter == xenos::TextureFilter::kLinear;
    params.min_linear =
        sampler_info.min_filter == xenos::TextureFilter::kLinear;
    params.mip_linear =
        sampler_info.mip_filter == xenos::TextureFilter::kLinear;
    params.aniso_filter = sampler_info.aniso_filter;
    params.mip_min_level = sampler_info.mip_min_level & 0xF;
    params.mip_base_map =
        sampler_info.mip_filter == xenos::TextureFilter::kBaseMap;
    return params;
  }

  uintptr_t WrapTexture(MTL::Texture* texture, MTL::SamplerState* sampler,
                        uint32_t sampler_key) {
    if (!texture) {
      return 0;
    }
    auto* metal_drawer =
        dynamic_cast<xe::ui::metal::MetalImmediateDrawer*>(
            immediate_drawer());
    if (!metal_drawer) {
      return 0;
    }
    TextureCacheKey cache_key{texture, sampler_key};
    auto it = texture_cache_.find(cache_key);
    if (it != texture_cache_.end()) {
      return reinterpret_cast<uintptr_t>(it->second.get());
    }
    std::unique_ptr<xe::ui::ImmediateTexture> wrapped =
        metal_drawer->CreateTextureFromMetal(texture, sampler);
    if (!wrapped) {
      return 0;
    }
    auto* wrapped_ptr = wrapped.get();
    texture_cache_.emplace(cache_key, std::move(wrapped));
    return reinterpret_cast<uintptr_t>(wrapped_ptr);
  }

  std::unordered_map<TextureCacheKey,
                     std::unique_ptr<xe::ui::ImmediateTexture>,
                     TextureCacheKeyHasher>
      texture_cache_;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

XE_DEFINE_WINDOWED_APP(xenia_gpu_metal_trace_viewer,
                       xe::gpu::metal::MetalTraceViewer::Create);
