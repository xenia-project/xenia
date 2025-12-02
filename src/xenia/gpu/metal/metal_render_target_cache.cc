/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_render_target_cache.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "third_party/stb/stb_image_write.h"
#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/shaders/bytecode/metal/resolve_full_32bpp_cs.h"

#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace metal {

// MetalRenderTarget implementation
MetalRenderTargetCache::MetalRenderTarget::~MetalRenderTarget() {
  if (texture_) {
    texture_->release();
    texture_ = nullptr;
  }
  if (msaa_texture_) {
    msaa_texture_->release();
    msaa_texture_ = nullptr;
  }
}

// MetalRenderTargetCache implementation
MetalRenderTargetCache::MetalRenderTargetCache(
    const RegisterFile& register_file, const Memory& memory,
    TraceWriter* trace_writer, uint32_t draw_resolution_scale_x,
    uint32_t draw_resolution_scale_y, MetalCommandProcessor& command_processor)
    : RenderTargetCache(register_file, memory, trace_writer,
                        draw_resolution_scale_x, draw_resolution_scale_y),
      command_processor_(command_processor),
      trace_writer_(trace_writer) {}

MetalRenderTargetCache::~MetalRenderTargetCache() { Shutdown(true); }

bool MetalRenderTargetCache::Initialize() {
  device_ = command_processor_.GetMetalDevice();
  if (!device_) {
    XELOGE("MetalRenderTargetCache: No Metal device available");
    return false;
  }

  // Create EDRAM buffer (10MB)
  constexpr size_t kEdramSizeBytes = 10 * 1024 * 1024;
  edram_buffer_ =
      device_->newBuffer(kEdramSizeBytes, MTL::ResourceStorageModePrivate);
  if (!edram_buffer_) {
    XELOGE("MetalRenderTargetCache: Failed to create EDRAM buffer");
    return false;
  }
  edram_buffer_->setLabel(
      NS::String::string("EDRAM Buffer", NS::UTF8StringEncoding));

  // Initialize EDRAM compute shaders
  if (!InitializeEdramComputeShaders()) {
    XELOGE(
        "MetalRenderTargetCache: Failed to initialize EDRAM compute shaders");
    return false;
  }

  // Initialize base class
  InitializeCommon();

  XELOGI("MetalRenderTargetCache: Initialized with {}x{} resolution scale",
         draw_resolution_scale_x(), draw_resolution_scale_y());

  return true;
}

void MetalRenderTargetCache::Shutdown(bool from_destructor) {
  if (!from_destructor) {
    ClearCache();
  }

  // Clean up dummy target
  dummy_color_target_.reset();

  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->release();
    cached_render_pass_descriptor_ = nullptr;
  }

  // Clean up EDRAM compute shaders
  ShutdownEdramComputeShaders();

  if (edram_buffer_) {
    edram_buffer_->release();
    edram_buffer_ = nullptr;
  }

  // Destroy all render targets
  DestroyAllRenderTargets(!from_destructor);

  // Shutdown base class
  if (!from_destructor) {
    ShutdownCommon();
  }
}

bool MetalRenderTargetCache::InitializeEdramComputeShaders() {
  // For now, only initialize the resolve compute pipeline for 32bpp color.
  edram_load_pipeline_ = nullptr;
  edram_store_pipeline_ = nullptr;
  resolve_full_32bpp_pipeline_ = nullptr;

  NS::Error* error = nullptr;

  // Wrap the embedded resolve_full_32bpp metallib into a Metal library.
  dispatch_data_t data = dispatch_data_create(
      resolve_full_32bpp_cs_metallib, sizeof(resolve_full_32bpp_cs_metallib),
      nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);

  MTL::Library* lib = device_->newLibrary(data, &error);
  dispatch_release(data);
  if (!lib) {
    XELOGE("Metal: failed to create resolve_full_32bpp library: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    return false;
  }

  // XeSL compute entrypoint name.
  NS::String* fn_name =
      NS::String::string("xesl_entry", NS::UTF8StringEncoding);
  MTL::Function* fn = lib->newFunction(fn_name);
  if (!fn) {
    XELOGE("Metal: resolve_full_32bpp missing xesl_entry");
    lib->release();
    return false;
  }

  resolve_full_32bpp_pipeline_ = device_->newComputePipelineState(fn, &error);
  fn->release();
  lib->release();

  if (!resolve_full_32bpp_pipeline_) {
    XELOGE("Metal: failed to create resolve_full_32bpp pipeline: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    return false;
  }

  XELOGI(
      "MetalRenderTargetCache::InitializeEdramComputeShaders: "
      "initialized resolve_full_32bpp pipeline");
  return true;
}

void MetalRenderTargetCache::ShutdownEdramComputeShaders() {
  if (edram_load_pipeline_) {
    edram_load_pipeline_->release();
    edram_load_pipeline_ = nullptr;
  }
  if (edram_store_pipeline_) {
    edram_store_pipeline_->release();
    edram_store_pipeline_ = nullptr;
  }
  if (resolve_full_32bpp_pipeline_) {
    resolve_full_32bpp_pipeline_->release();
    resolve_full_32bpp_pipeline_ = nullptr;
  }
  XELOGI("MetalRenderTargetCache::ShutdownEdramComputeShaders");
}

void MetalRenderTargetCache::ClearCache() {
  // Clear current bindings
  for (uint32_t i = 0; i < 4; ++i) {
    current_color_targets_[i] = nullptr;
  }
  current_depth_target_ = nullptr;
  render_pass_descriptor_dirty_ = true;

  // Clear the tracking of which render targets have been cleared
  cleared_render_targets_this_frame_.clear();

  // Call base implementation
  RenderTargetCache::ClearCache();
}

void MetalRenderTargetCache::BeginFrame() {
  // Reset dummy target clear flag
  dummy_color_target_needs_clear_ = true;

  // Clear the tracking of which render targets have been cleared this frame
  cleared_render_targets_this_frame_.clear();

  // Call base implementation
  RenderTargetCache::BeginFrame();
}

bool MetalRenderTargetCache::Update(
    bool is_rasterization_done, reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask, const Shader& vertex_shader) {
  XELOGI("MetalRenderTargetCache::Update called - is_rasterization_done={}",
         is_rasterization_done);

  // Call base implementation - this does all the heavy lifting
  if (!RenderTargetCache::Update(is_rasterization_done,
                                 normalized_depth_control,
                                 normalized_color_mask, vertex_shader)) {
    XELOGE("MetalRenderTargetCache::Update - Base class Update failed");
    return false;
  }

  // After base class update, retrieve the actual render targets that were
  // selected This is the KEY to connecting base class management with
  // Metal-specific rendering
  RenderTarget* const* accumulated_targets =
      last_update_accumulated_render_targets();

  XELOGI(
      "MetalRenderTargetCache::Update - Got accumulated targets from base "
      "class");

  // Debug: Check what we actually got AND verify textures exist
  for (uint32_t i = 0; i < 5; ++i) {
    if (accumulated_targets[i]) {
      MetalRenderTarget* metal_rt =
          static_cast<MetalRenderTarget*>(accumulated_targets[i]);
      MTL::Texture* tex = metal_rt->texture();
      XELOGI(
          "  accumulated_targets[{}] = {:p} (key 0x{:08X}, texture={:p}, "
          "{}x{})",
          i, (void*)accumulated_targets[i], accumulated_targets[i]->key().key,
          (void*)tex, tex ? tex->width() : 0, tex ? tex->height() : 0);

      // CRITICAL FIX: Ensure texture exists
      if (!tex) {
        XELOGE("  ERROR: MetalRenderTarget has no texture! Need to create it.");
        // TODO: Create texture here if missing
      }
    } else {
      XELOGI("  accumulated_targets[{}] = nullptr", i);
    }
  }

  // Check if render targets actually changed
  bool targets_changed = false;

  // Check depth target
  MetalRenderTarget* new_depth_target =
      accumulated_targets[0]
          ? static_cast<MetalRenderTarget*>(accumulated_targets[0])
          : nullptr;
  if (new_depth_target != current_depth_target_) {
    targets_changed = true;
    current_depth_target_ = new_depth_target;
    if (current_depth_target_) {
      XELOGD(
          "MetalRenderTargetCache::Update - Depth target changed: key={:08X}",
          current_depth_target_->key().key);
    }
  }

  // Check color targets
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    MetalRenderTarget* new_color_target =
        accumulated_targets[i + 1]
            ? static_cast<MetalRenderTarget*>(accumulated_targets[i + 1])
            : nullptr;
    if (new_color_target != current_color_targets_[i]) {
      targets_changed = true;
      current_color_targets_[i] = new_color_target;
      if (current_color_targets_[i]) {
        XELOGD(
            "MetalRenderTargetCache::Update - Color target {} changed: "
            "key={:08X}",
            i, current_color_targets_[i]->key().key);
      }
    }
  }

  // Perform ownership transfers - this is critical for correct rendering when
  // EDRAM regions are aliased between different RT configurations.
  // The base class Update() populates last_update_transfers() with the needed
  // transfers based on EDRAM tile overlaps.
  if (GetPath() == Path::kHostRenderTargets) {
    PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets,
                                     accumulated_targets,
                                     last_update_transfers());
  }

  // Only mark render pass descriptor as dirty if targets actually changed
  if (targets_changed) {
    render_pass_descriptor_dirty_ = true;
    XELOGI(
        "MetalRenderTargetCache::Update - Render targets changed, marking "
        "descriptor dirty");
  }

  return true;
}

uint32_t MetalRenderTargetCache::GetMaxRenderTargetWidth() const {
  // Metal maximum texture dimension
  return 16384;
}

uint32_t MetalRenderTargetCache::GetMaxRenderTargetHeight() const {
  // Metal maximum texture dimension
  return 16384;
}

RenderTargetCache::RenderTarget* MetalRenderTargetCache::CreateRenderTarget(
    RenderTargetKey key) {
  XELOGI(
      "MetalRenderTargetCache::CreateRenderTarget called - key={:08X}, "
      "is_depth={}",
      key.key, key.is_depth);

  // Calculate dimensions
  uint32_t width = key.GetWidth();
  uint32_t height =
      GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples);

  // Apply resolution scaling
  width *= draw_resolution_scale_x();
  height *= draw_resolution_scale_y();

  XELOGI(
      "MetalRenderTargetCache: Creating render target with calculated "
      "dimensions {}x{} (before scaling: {}x{})",
      width, height, key.GetWidth(),
      GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples));

  // Create Metal render target
  auto* render_target = new MetalRenderTarget(key);

  // Create the texture based on format
  MTL::Texture* texture = nullptr;
  uint32_t samples = 1 << uint32_t(key.msaa_samples);

  if (key.is_depth) {
    texture = CreateDepthTexture(width, height, key.GetDepthFormat(), samples);
  } else {
    texture = CreateColorTexture(width, height, key.GetColorFormat(), samples);
  }

  if (!texture) {
    delete render_target;
    return nullptr;
  }

  render_target->SetTexture(texture);

  // NOTE: Unlike the previous implementation, we do NOT load EDRAM data here.
  // This matches D3D12's approach where:
  // 1. CreateRenderTarget creates an empty texture
  // 2. Data transfer happens via ownership transfers in
  // PerformTransfersAndResolveClears
  // 3. The EDRAM buffer is only used as scratch space for resolves
  //
  // The ownership transfer system (called from Update()) handles copying data
  // between render target textures when EDRAM regions are aliased between
  // different RT configurations.

  // Store in our map for later retrieval
  render_target_map_[key.key] = render_target;

  XELOGI(
      "MetalRenderTargetCache: Created render target - {}x{}, {} samples, key "
      "0x{:08X}",
      width, height, samples, key.key);

  return render_target;
}

bool MetalRenderTargetCache::IsHostDepthEncodingDifferent(
    xenos::DepthRenderTargetFormat format) const {
  // Metal uses different depth encoding than Xbox 360
  // D24S8 on Xbox 360 vs D32Float_S8 on Metal
  return format == xenos::DepthRenderTargetFormat::kD24S8 ||
         format == xenos::DepthRenderTargetFormat::kD24FS8;
}

void MetalRenderTargetCache::RestoreEdramSnapshot(const void* snapshot) {
  if (!snapshot) {
    XELOGI("MetalRenderTargetCache::RestoreEdramSnapshot: null snapshot");
    return;
  }

  if (IsDrawResolutionScaled()) {
    XELOGI(
        "MetalRenderTargetCache::RestoreEdramSnapshot: draw resolution scaled, "
        "skipping");
    return;
  }

  switch (GetPath()) {
    case Path::kHostRenderTargets: {
      RenderTarget* full_edram_rt =
          PrepareFullEdram1280xRenderTargetForSnapshotRestoration(
              xenos::ColorRenderTargetFormat::k_32_FLOAT);
      if (!full_edram_rt) {
        XELOGI(
            "MetalRenderTargetCache::RestoreEdramSnapshot: failed to get "
            "full-EDRAM RT");
        return;
      }

      MetalRenderTarget* metal_rt =
          static_cast<MetalRenderTarget*>(full_edram_rt);
      MTL::Texture* texture = metal_rt->texture();
      if (!texture) {
        XELOGI(
            "MetalRenderTargetCache::RestoreEdramSnapshot: full-EDRAM RT has "
            "no texture");
        return;
      }

      constexpr uint32_t kPitchTilesAt32bpp = 16;
      constexpr uint32_t kWidth =
          kPitchTilesAt32bpp * xenos::kEdramTileWidthSamples;
      constexpr uint32_t kTileRows =
          xenos::kEdramTileCount / kPitchTilesAt32bpp;
      constexpr uint32_t kHeight = kTileRows * xenos::kEdramTileHeightSamples;

      size_t staging_size = size_t(kWidth) * size_t(kHeight) * sizeof(uint32_t);
      MTL::Buffer* staging =
          device_->newBuffer(staging_size, MTL::ResourceStorageModeShared);
      if (!staging) {
        XELOGI(
            "MetalRenderTargetCache::RestoreEdramSnapshot: failed to create "
            "staging buffer");
        return;
      }

      auto* dst_base = static_cast<uint8_t*>(staging->contents());
      const uint8_t* src = static_cast<const uint8_t*>(snapshot);
      uint32_t bytes_per_row = kWidth * sizeof(uint32_t);

      for (uint32_t y_tile = 0; y_tile < kTileRows; ++y_tile) {
        for (uint32_t x_tile = 0; x_tile < kPitchTilesAt32bpp; ++x_tile) {
          uint32_t tile_index = y_tile * kPitchTilesAt32bpp + x_tile;
          const uint8_t* tile_src =
              src + tile_index * xenos::kEdramTileWidthSamples *
                        xenos::kEdramTileHeightSamples * sizeof(uint32_t);

          for (uint32_t sample_row = 0;
               sample_row < xenos::kEdramTileHeightSamples; ++sample_row) {
            uint32_t dst_y =
                y_tile * xenos::kEdramTileHeightSamples + sample_row;
            uint32_t dst_x = x_tile * xenos::kEdramTileWidthSamples;

            uint8_t* dst_row =
                dst_base + dst_y * bytes_per_row + dst_x * sizeof(uint32_t);
            const uint8_t* src_row =
                tile_src +
                sample_row * xenos::kEdramTileWidthSamples * sizeof(uint32_t);

            std::memcpy(dst_row, src_row,
                        xenos::kEdramTileWidthSamples * sizeof(uint32_t));
          }
        }
      }

      MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
      if (!queue) {
        staging->release();
        XELOGI(
            "MetalRenderTargetCache::RestoreEdramSnapshot: no command queue");
        return;
      }

      MTL::CommandBuffer* cmd = queue->commandBuffer();
      if (!cmd) {
        staging->release();
        XELOGI(
            "MetalRenderTargetCache::RestoreEdramSnapshot: no command buffer");
        return;
      }

      MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
      if (!blit) {
        cmd->release();
        staging->release();
        XELOGI("MetalRenderTargetCache::RestoreEdramSnapshot: no blit encoder");
        return;
      }

      blit->copyFromBuffer(staging, 0, bytes_per_row, 0,
                           MTL::Size::Make(kWidth, kHeight, 1), texture, 0, 0,
                           MTL::Origin::Make(0, 0, 0));
      blit->endEncoding();
      cmd->commit();
      cmd->waitUntilCompleted();
      cmd->release();
      staging->release();

      XELOGI(
          "MetalRenderTargetCache::RestoreEdramSnapshot: restored snapshot "
          "into "
          "full-EDRAM RT ({}x{})",
          kWidth, kHeight);
      break;
    }

    default:
      XELOGI(
          "MetalRenderTargetCache::RestoreEdramSnapshot: unsupported path {}",
          int(GetPath()));
      break;
  }
}

MTL::Texture* MetalRenderTargetCache::CreateColorTexture(
    uint32_t width, uint32_t height, xenos::ColorRenderTargetFormat format,
    uint32_t samples) {
  MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
  desc->setWidth(width);
  desc->setHeight(height ? height : 720);  // Default height if not specified
  desc->setPixelFormat(GetColorPixelFormat(format));
  desc->setTextureType(samples > 1 ? MTL::TextureType2DMultisample
                                   : MTL::TextureType2D);
  desc->setSampleCount(samples);
  desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                 MTL::TextureUsageShaderWrite);
  desc->setStorageMode(MTL::StorageModePrivate);

  MTL::Texture* texture = device_->newTexture(desc);
  desc->release();

  return texture;
}

MTL::Texture* MetalRenderTargetCache::CreateDepthTexture(
    uint32_t width, uint32_t height, xenos::DepthRenderTargetFormat format,
    uint32_t samples) {
  MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
  desc->setWidth(width);
  desc->setHeight(height ? height : 720);  // Default height if not specified
  desc->setPixelFormat(GetDepthPixelFormat(format));
  desc->setTextureType(samples > 1 ? MTL::TextureType2DMultisample
                                   : MTL::TextureType2D);
  desc->setSampleCount(samples);
  desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                 MTL::TextureUsageShaderWrite);
  desc->setStorageMode(MTL::StorageModePrivate);

  MTL::Texture* texture = device_->newTexture(desc);
  desc->release();

  return texture;
}

MTL::PixelFormat MetalRenderTargetCache::GetColorPixelFormat(
    xenos::ColorRenderTargetFormat format) const {
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8:
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return MTL::PixelFormatBGRA8Unorm;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      return MTL::PixelFormatRGB10A2Unorm;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return MTL::PixelFormatRGB10A2Unorm;  // Approximation
    case xenos::ColorRenderTargetFormat::k_16_16:
      return MTL::PixelFormatRG16Snorm;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
      return MTL::PixelFormatRGBA16Snorm;
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      return MTL::PixelFormatRG16Float;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return MTL::PixelFormatRGBA16Float;
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      return MTL::PixelFormatR32Float;
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      return MTL::PixelFormatRG32Float;
    default:
      XELOGE("MetalRenderTargetCache: Unsupported color format {}",
             static_cast<uint32_t>(format));
      return MTL::PixelFormatBGRA8Unorm;
  }
}

MTL::PixelFormat MetalRenderTargetCache::GetDepthPixelFormat(
    xenos::DepthRenderTargetFormat format) const {
  switch (format) {
    case xenos::DepthRenderTargetFormat::kD24S8:
    case xenos::DepthRenderTargetFormat::kD24FS8:
      // Metal doesn't have D24S8, use D32Float_S8
      return MTL::PixelFormatDepth32Float_Stencil8;
    default:
      XELOGE("MetalRenderTargetCache: Unsupported depth format {}",
             static_cast<uint32_t>(format));
      return MTL::PixelFormatDepth32Float_Stencil8;
  }
}

MTL::RenderPassDescriptor* MetalRenderTargetCache::GetRenderPassDescriptor(
    uint32_t expected_sample_count) {
  XELOGI(
      "MetalRenderTargetCache::GetRenderPassDescriptor - dirty={}, cached={:p}",
      render_pass_descriptor_dirty_, (void*)cached_render_pass_descriptor_);

  if (!render_pass_descriptor_dirty_ && cached_render_pass_descriptor_) {
    return cached_render_pass_descriptor_;
  }

  // Release old descriptor
  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->release();
    cached_render_pass_descriptor_ = nullptr;
  }

  // Create new descriptor
  cached_render_pass_descriptor_ =
      MTL::RenderPassDescriptor::renderPassDescriptor();
  if (!cached_render_pass_descriptor_) {
    XELOGE("MetalRenderTargetCache: Failed to create render pass descriptor");
    return nullptr;
  }
  cached_render_pass_descriptor_->retain();

  bool has_any_render_target = false;

  // Debug: Log current targets
  XELOGI("GetRenderPassDescriptor: Current targets state:");
  XELOGI("  current_depth_target_ = {:p}", (void*)current_depth_target_);
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_color_targets_[i]) {
      MTL::Texture* tex = current_color_targets_[i]->texture();
      XELOGI("  current_color_targets_[{}] = {:p}, texture={:p} ({}x{})", i,
             (void*)current_color_targets_[i], (void*)tex,
             tex ? tex->width() : 0, tex ? tex->height() : 0);
    } else {
      XELOGI("  current_color_targets_[{}] = nullptr", i);
    }
  }

  // Bind the actual render targets retrieved from base class in Update()

  // Bind depth target if present
  if (current_depth_target_ && current_depth_target_->texture()) {
    auto* depth_attachment = cached_render_pass_descriptor_->depthAttachment();
    depth_attachment->setTexture(current_depth_target_->texture());

    // Check if this render target has been cleared this frame
    uint32_t depth_key = current_depth_target_->key().key;
    bool needs_clear = cleared_render_targets_this_frame_.find(depth_key) ==
                       cleared_render_targets_this_frame_.end();

    if (needs_clear) {
      depth_attachment->setLoadAction(MTL::LoadActionClear);
      depth_attachment->setClearDepth(1.0);
      cleared_render_targets_this_frame_.insert(depth_key);
      XELOGI(
          "MetalRenderTargetCache: Clearing depth target 0x{:08X} on first use",
          depth_key);
    } else {
      depth_attachment->setLoadAction(MTL::LoadActionLoad);
      XELOGI(
          "MetalRenderTargetCache: Loading depth target 0x{:08X} on subsequent "
          "use",
          depth_key);
    }

    depth_attachment->setStoreAction(MTL::StoreActionStore);

    // Check if this format has stencil
    xenos::DepthRenderTargetFormat depth_format =
        current_depth_target_->key().GetDepthFormat();
    if (depth_format == xenos::DepthRenderTargetFormat::kD24S8) {
      auto* stencil_attachment =
          cached_render_pass_descriptor_->stencilAttachment();
      stencil_attachment->setTexture(current_depth_target_->texture());

      if (needs_clear) {
        stencil_attachment->setLoadAction(MTL::LoadActionClear);
        stencil_attachment->setClearStencil(0);
      } else {
        stencil_attachment->setLoadAction(MTL::LoadActionLoad);
      }

      stencil_attachment->setStoreAction(MTL::StoreActionStore);
    }

    has_any_render_target = true;

    // Track this as a real render target for capture
    last_real_depth_target_ = current_depth_target_;

    XELOGI(
        "MetalRenderTargetCache: Bound depth target to render pass (REAL "
        "target)");
  }

  // Bind color targets
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_color_targets_[i] && current_color_targets_[i]->texture()) {
      auto* color_attachment =
          cached_render_pass_descriptor_->colorAttachments()->object(i);
      color_attachment->setTexture(current_color_targets_[i]->texture());

      // Check if this render target has been cleared this frame
      uint32_t color_key = current_color_targets_[i]->key().key;
      bool needs_clear = cleared_render_targets_this_frame_.find(color_key) ==
                         cleared_render_targets_this_frame_.end();

      if (needs_clear) {
        color_attachment->setLoadAction(MTL::LoadActionClear);
        color_attachment->setClearColor(
            MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));  // Transparent black
        cleared_render_targets_this_frame_.insert(color_key);
        XELOGI(
            "MetalRenderTargetCache: Clearing color target {} (0x{:08X}) on "
            "first use",
            i, color_key);
      } else {
        color_attachment->setLoadAction(MTL::LoadActionLoad);
        XELOGI(
            "MetalRenderTargetCache: Loading color target {} (0x{:08X}) on "
            "subsequent use",
            i, color_key);
      }

      color_attachment->setStoreAction(MTL::StoreActionStore);

      has_any_render_target = true;

      // Track this as a real render target for capture
      last_real_color_targets_[i] = current_color_targets_[i];

      XELOGI(
          "MetalRenderTargetCache: Bound color target {} to render pass (REAL "
          "target, {}x{}, key 0x{:08X})",
          i, current_color_targets_[i]->texture()->width(),
          current_color_targets_[i]->texture()->height(),
          current_color_targets_[i]->key().key);
    }
  }

  // If no render targets are bound, create / use a dummy color target so
  // Metal still has a valid render target. This mirrors the D3D12/Vulkan
  // behavior of always binding *some* RT when drawing.
  if (!has_any_render_target) {
    if (!dummy_color_target_) {
      uint32_t width = 1;
      uint32_t height = 1;
      xenos::ColorRenderTargetFormat fmt =
          xenos::ColorRenderTargetFormat::k_8_8_8_8;
      uint32_t samples = std::max(1u, expected_sample_count);
      RenderTargetKey dummy_key;
      dummy_key.key = 0;
      dummy_key.is_depth = 0;
      dummy_key.resource_format = uint32_t(fmt);
      dummy_key.msaa_samples = xenos::MsaaSamples(samples);
      dummy_color_target_ = std::make_unique<MetalRenderTarget>(dummy_key);
      MTL::Texture* tex = CreateColorTexture(width, height, fmt, samples);
      dummy_color_target_->SetTexture(tex);
      dummy_color_target_needs_clear_ = true;
    }

    auto* color_attachment =
        cached_render_pass_descriptor_->colorAttachments()->object(0);
    color_attachment->setTexture(dummy_color_target_->texture());
    if (dummy_color_target_needs_clear_) {
      color_attachment->setLoadAction(MTL::LoadActionClear);
      color_attachment->setClearColor(
          MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
      dummy_color_target_needs_clear_ = false;
    } else {
      color_attachment->setLoadAction(MTL::LoadActionLoad);
    }
    color_attachment->setStoreAction(MTL::StoreActionStore);

    has_any_render_target = true;
    XELOGI("MetalRenderTargetCache::GetRenderPassDescriptor: using dummy RT0");
  }

  return cached_render_pass_descriptor_;
}

MTL::Texture* MetalRenderTargetCache::GetColorTarget(uint32_t index) const {
  if (index >= 4 || !current_color_targets_[index]) {
    return nullptr;
  }
  return current_color_targets_[index]->texture();
}

MTL::Texture* MetalRenderTargetCache::GetDepthTarget() const {
  if (!current_depth_target_) {
    return nullptr;
  }
  return current_depth_target_->texture();
}

MTL::Texture* MetalRenderTargetCache::GetDummyColorTarget() const {
  if (dummy_color_target_ && dummy_color_target_->texture()) {
    return dummy_color_target_->texture();
  }
  return nullptr;
}

MTL::Texture* MetalRenderTargetCache::GetLastRealColorTarget(
    uint32_t index) const {
  if (index >= 4 || !last_real_color_targets_[index]) {
    return nullptr;
  }
  return last_real_color_targets_[index]->texture();
}

MTL::Texture* MetalRenderTargetCache::GetLastRealDepthTarget() const {
  if (!last_real_depth_target_) {
    return nullptr;
  }
  return last_real_depth_target_->texture();
}

void MetalRenderTargetCache::StoreTiledData(MTL::CommandBuffer* command_buffer,
                                            MTL::Texture* texture,
                                            uint32_t edram_base,
                                            uint32_t pitch_tiles,
                                            uint32_t height_tiles,
                                            bool is_depth) {
  XELOGI(
      "MetalRenderTargetCache::StoreTiledData - texture {}x{}, base={}, "
      "pitch={}, height={}, depth={}",
      texture->width(), texture->height(), edram_base, pitch_tiles,
      height_tiles, is_depth);

  MTL::Texture* source_texture = texture;
  MTL::Texture* temp_texture = nullptr;

  // Check if this is a depth/stencil texture
  bool is_depth_stencil_format =
      texture->pixelFormat() == MTL::PixelFormatDepth32Float_Stencil8 ||
      texture->pixelFormat() == MTL::PixelFormatDepth32Float ||
      texture->pixelFormat() == MTL::PixelFormatDepth16Unorm ||
      texture->pixelFormat() == MTL::PixelFormatDepth24Unorm_Stencil8 ||
      texture->pixelFormat() == MTL::PixelFormatX32_Stencil8;

  if (is_depth_stencil_format) {
    // For storing depth/stencil data back to EDRAM, we would need to read
    // from the depth texture This is complex because depth textures can't be
    // directly read in compute shaders. For now, we'll skip storing depth
    // data back to EDRAM since depth buffers are typically write-only during
    // rendering and don't need to be preserved across frames.
    XELOGI(
        "MetalRenderTargetCache::StoreTiledData - Skipping depth/stencil "
        "texture store (not supported)");
    return;
  }

  // If texture is multisample, create a temporary non-multisample texture and
  // resolve to it first
  if (texture->textureType() == MTL::TextureType2DMultisample) {
    XELOGI(
        "MetalRenderTargetCache::StoreTiledData - Creating temporary "
        "non-multisample texture for EDRAM operation");

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setWidth(texture->width());
    desc->setHeight(texture->height());
    desc->setPixelFormat(texture->pixelFormat());
    desc->setTextureType(MTL::TextureType2D);  // Regular 2D texture
    desc->setSampleCount(1);                   // Non-multisample
    desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                   MTL::TextureUsageShaderWrite);
    desc->setStorageMode(MTL::StorageModePrivate);

    temp_texture = device_->newTexture(desc);
    desc->release();

    if (!temp_texture) {
      XELOGE(
          "MetalRenderTargetCache::StoreTiledData - Failed to create "
          "temporary "
          "texture");
      return;
    }

    // Resolve multisample texture to temporary texture
    MTL::RenderPassDescriptor* resolve_desc =
        MTL::RenderPassDescriptor::renderPassDescriptor();
    if (resolve_desc) {
      auto* color_attachment = resolve_desc->colorAttachments()->object(0);
      color_attachment->setTexture(texture);              // Multisample source
      color_attachment->setResolveTexture(temp_texture);  // Resolved output
      color_attachment->setLoadAction(MTL::LoadActionLoad);
      color_attachment->setStoreAction(MTL::StoreActionMultisampleResolve);

      MTL::RenderCommandEncoder* render_encoder =
          command_buffer->renderCommandEncoder(resolve_desc);
      if (render_encoder) {
        render_encoder->endEncoding();
        render_encoder->release();
      }
      resolve_desc->release();
    }

    source_texture = temp_texture;
  }

  // Create compute encoder
  MTL::ComputeCommandEncoder* encoder = command_buffer->computeCommandEncoder();
  if (!encoder) {
    if (temp_texture) temp_texture->release();
    return;
  }

  // Set compute pipeline
  encoder->setComputePipelineState(edram_store_pipeline_);

  // Bind input texture (either original or resolved)
  encoder->setTexture(source_texture, 0);

  // Bind EDRAM buffer
  encoder->setBuffer(edram_buffer_, 0, 0);

  // Create parameter buffers
  uint32_t params[2] = {edram_base, pitch_tiles};
  MTL::Buffer* param_buffer = device_->newBuffer(
      &params, sizeof(params), MTL::ResourceStorageModeShared);
  encoder->setBuffer(param_buffer, 0, 1);
  encoder->setBuffer(param_buffer, sizeof(uint32_t), 2);

  // Calculate thread group sizes
  MTL::Size threads_per_threadgroup = MTL::Size::Make(8, 8, 1);
  MTL::Size threadgroups = MTL::Size::Make(
      (source_texture->width() + 7) / 8, (source_texture->height() + 7) / 8, 1);

  // Dispatch compute
  encoder->dispatchThreadgroups(threadgroups, threads_per_threadgroup);
  encoder->endEncoding();
  encoder->release();

  if (temp_texture) {
    temp_texture->release();
  }

  param_buffer->release();
}

bool MetalRenderTargetCache::Resolve(Memory& memory, uint32_t& written_address,
                                     uint32_t& written_length) {
  written_address = 0;
  written_length = 0;

  const RegisterFile& regs = register_file();
  draw_util::ResolveInfo resolve_info;

  // For now, assume fixed16 formats are emulated with full range.
  // This is sufficient for the k_8_8_8_8 A-Train path.
  bool fixed_rg16_trunc = false;
  bool fixed_rgba16_trunc = false;

  if (!trace_writer_) {
    XELOGE("MetalRenderTargetCache::Resolve: trace_writer_ is null");
    return false;
  }

  if (!draw_util::GetResolveInfo(regs, memory, *trace_writer_,
                                 draw_resolution_scale_x(),
                                 draw_resolution_scale_y(), fixed_rg16_trunc,
                                 fixed_rgba16_trunc, resolve_info)) {
    XELOGE("MetalRenderTargetCache::Resolve: GetResolveInfo failed");
    return false;
  }

  // Nothing to do.
  if (!resolve_info.coordinate_info.width_div_8 || !resolve_info.height_div_8) {
    XELOGI("MetalRenderTargetCache::Resolve: empty resolve region");
    return true;
  }

  // For now only handle color resolves (A-Train background path).
  if (resolve_info.IsCopyingDepth()) {
    XELOGI("MetalRenderTargetCache::Resolve: depth resolve not implemented");
    return true;
  }

  if (!resolve_info.copy_dest_extent_length) {
    XELOGI("MetalRenderTargetCache::Resolve: zero copy_dest_extent_length");
    return true;
  }

  if (GetPath() != Path::kHostRenderTargets) {
    XELOGI("MetalRenderTargetCache::Resolve: non-host-RT path not supported");
    return true;
  }

  // Select resolve source RT like D3D12 (based on copy_src_select).
  uint32_t copy_src = resolve_info.rb_copy_control.copy_src_select;
  if (copy_src >= xenos::kMaxColorRenderTargets) {
    XELOGI("MetalRenderTargetCache::Resolve: copy_src_select out of range ({})",
           copy_src);
    return true;
  }

  MetalRenderTarget* src_rt = nullptr;
  RenderTarget* const* accumulated_targets =
      last_update_accumulated_render_targets();
  if (accumulated_targets && accumulated_targets[1 + copy_src]) {
    src_rt = static_cast<MetalRenderTarget*>(accumulated_targets[1 + copy_src]);
  }
  if (!src_rt || !src_rt->texture()) {
    XELOGI(
        "MetalRenderTargetCache::Resolve: no source RT for copy_src_select={}",
        copy_src);
    return true;
  }

  MTL::Texture* source_texture = src_rt->texture();
  uint32_t src_width = static_cast<uint32_t>(source_texture->width());
  uint32_t src_height = static_cast<uint32_t>(source_texture->height());
  uint32_t msaa_samples = static_cast<uint32_t>(source_texture->sampleCount());
  if (!msaa_samples) {
    msaa_samples = 1;
  }

  const auto& coord = resolve_info.coordinate_info;
  uint32_t resolve_width = coord.width_div_8 * 8;
  uint32_t resolve_height = resolve_info.height_div_8 * 8;

  uint32_t dest_base = resolve_info.copy_dest_base;
  uint32_t dest_local_start = resolve_info.copy_dest_extent_start - dest_base;
  uint32_t dest_local_end =
      dest_local_start + resolve_info.copy_dest_extent_length;

  // For now, only handle 32bpp k_8_8_8_8 resolves.
  xenos::ColorFormat dest_color_format =
      resolve_info.copy_dest_info.copy_dest_format;
  if (dest_color_format != xenos::ColorFormat::k_8_8_8_8) {
    XELOGI(
        "MetalRenderTargetCache::Resolve: non-8888 color format {} not "
        "implemented",
        uint32_t(dest_color_format));
    return true;
  }
  uint32_t bytes_per_pixel = 4;

  // Staging buffer path: copyFromTexture -> CPU tiling to guest. This mirrors
  // D3D12/Vulkan ResolveInfo usage and copy_dest_extent clamping, but performs
  // the actual copy on the CPU for now.
  MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
  if (!queue) {
    XELOGE("MetalRenderTargetCache::Resolve: no command queue");
    return false;
  }

  uint32_t copy_row_bytes = src_width * bytes_per_pixel * msaa_samples;
  size_t staging_size = size_t(copy_row_bytes) * size_t(src_height);
  MTL::Buffer* staging =
      device_->newBuffer(staging_size, MTL::ResourceStorageModeShared);
  if (!staging) {
    XELOGE("MetalRenderTargetCache::Resolve: failed to create staging buffer");
    return false;
  }

  MTL::CommandBuffer* cmd = queue->commandBuffer();
  if (!cmd) {
    staging->release();
    XELOGE("MetalRenderTargetCache::Resolve: failed to get command buffer");
    return false;
  }

  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
  if (!blit) {
    cmd->release();
    staging->release();
    XELOGE("MetalRenderTargetCache::Resolve: failed to get blit encoder");
    return false;
  }

  blit->copyFromTexture(source_texture, 0, 0, MTL::Origin::Make(0, 0, 0),
                        MTL::Size::Make(src_width, src_height, 1), staging, 0,
                        copy_row_bytes, copy_row_bytes * src_height,
                        MTL::BlitOptionNone);
  blit->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  cmd->release();

  const uint8_t* src_ptr = static_cast<const uint8_t*>(staging->contents());
  uint8_t* dest_cpu_ptr =
      static_cast<uint8_t*>(memory.TranslatePhysical(dest_base));

  // Also write resolved data directly into the Metal shared memory buffer so
  // textures see it without relying solely on CPU→GPU UploadRanges.
  MTL::Buffer* shared_buffer = nullptr;
  uint8_t* dest_gpu_ptr = nullptr;
  if (auto* shared = command_processor_.shared_memory()) {
    shared_buffer = shared->GetBuffer();
    if (shared_buffer && dest_base < shared_buffer->length()) {
      dest_gpu_ptr =
          static_cast<uint8_t*>(shared_buffer->contents()) + dest_base;
    }
  }

  const auto& dest_coord = resolve_info.copy_dest_coordinate_info;
  uint32_t dest_pitch = dest_coord.pitch_aligned_div_32
                        << xenos::kTextureTileWidthHeightLog2;
  uint32_t offset_x = dest_coord.offset_x_div_8
                      << xenos::kResolveAlignmentPixelsLog2;
  uint32_t offset_y = dest_coord.offset_y_div_8
                      << xenos::kResolveAlignmentPixelsLog2;
  uint32_t bytes_per_block_log2 = 2;  // 4 bytes

  uint32_t min_tiled = UINT32_MAX;
  uint32_t max_tiled_plus = 0;

  for (uint32_t y = 0; y < resolve_height; ++y) {
    if (y >= src_height) {
      break;
    }
    uint32_t src_row_offset = y * copy_row_bytes;

    for (uint32_t x = 0; x < resolve_width; ++x) {
      if (x >= src_width) {
        break;
      }

      uint32_t src_offset = src_row_offset + x * bytes_per_pixel;
      uint32_t pixel_data = 0;
      std::memcpy(&pixel_data, src_ptr + src_offset, sizeof(pixel_data));

      // Staging is BGRA8; guest wants ARGB32 big-endian.
      uint32_t swapped = xe::byte_swap(pixel_data);

      int32_t tiled_offset = texture_util::GetTiledOffset2D(
          int32_t(offset_x + x), int32_t(offset_y + y), dest_pitch,
          bytes_per_block_log2);
      if (tiled_offset < 0) {
        continue;
      }

      uint32_t t = uint32_t(tiled_offset);
      if (t < dest_local_start || t + bytes_per_pixel > dest_local_end) {
        continue;
      }

      if (dest_cpu_ptr) {
        std::memcpy(dest_cpu_ptr + t, &swapped, bytes_per_pixel);
      }
      if (dest_gpu_ptr && shared_buffer &&
          dest_base + t + bytes_per_pixel <= shared_buffer->length()) {
        std::memcpy(dest_gpu_ptr + t, &swapped, bytes_per_pixel);
      }

      if (t < min_tiled) {
        min_tiled = t;
      }
      if (t + bytes_per_pixel > max_tiled_plus) {
        max_tiled_plus = t + bytes_per_pixel;
      }
    }
  }

  staging->release();

  if (max_tiled_plus <= min_tiled || min_tiled == UINT32_MAX) {
    XELOGI("MetalRenderTargetCache::Resolve: no pixels written");
    return true;
  }

  written_address = dest_base + min_tiled;
  written_length = max_tiled_plus - min_tiled;

  XELOGI(
      "MetalRenderTargetCache::Resolve: wrote {} bytes at 0x{:08X} "
      "(dest_base=0x{:08X})",
      written_length, written_address, dest_base);
  return true;
}

void MetalRenderTargetCache::PerformTransfersAndResolveClears(

    uint32_t render_target_count, RenderTarget* const* render_targets,
    const std::vector<Transfer>* render_target_transfers) {
  // This function handles ownership transfers between render targets when
  // EDRAM regions are aliased. When the game binds different RT configurations
  // that share the same EDRAM base, we need to copy data from the old RT
  // texture to the new one.
  //
  // NOTE: This is a simplified implementation compared to D3D12/Vulkan.
  // D3D12/Vulkan use specialized pixel shaders for transfers that handle:
  // - Format conversion (color <-> depth)
  // - MSAA sample mapping between different sample counts
  // - Stencil bit manipulation
  // - Host depth precision preservation
  //
  // This implementation only handles basic same-format, same-MSAA blits.
  // TODO: Implement full transfer shader support like D3D12/Vulkan.

  if (!render_targets || !render_target_transfers) {
    return;
  }

  MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
  if (!queue) {
    XELOGE(
        "MetalRenderTargetCache::PerformTransfersAndResolveClears: No command "
        "queue");
    return;
  }

  bool any_transfers_done = false;
  uint32_t transfers_skipped_format = 0;
  uint32_t transfers_skipped_msaa = 0;
  uint32_t transfers_skipped_depth = 0;

  for (uint32_t i = 0; i < render_target_count; ++i) {
    RenderTarget* dest_rt = render_targets[i];
    if (!dest_rt) {
      continue;
    }

    const std::vector<Transfer>& transfers = render_target_transfers[i];
    if (transfers.empty()) {
      continue;
    }

    MetalRenderTarget* dest_metal_rt = static_cast<MetalRenderTarget*>(dest_rt);
    MTL::Texture* dest_texture = dest_metal_rt->texture();
    if (!dest_texture) {
      XELOGW(
          "MetalRenderTargetCache::PerformTransfersAndResolveClears: "
          "Destination RT {} has no texture",
          i);
      continue;
    }

    RenderTargetKey dest_key = dest_metal_rt->key();

    for (const Transfer& transfer : transfers) {
      RenderTarget* source_rt = transfer.source;
      if (!source_rt) {
        continue;
      }

      MetalRenderTarget* source_metal_rt =
          static_cast<MetalRenderTarget*>(source_rt);
      MTL::Texture* source_texture = source_metal_rt->texture();
      if (!source_texture) {
        continue;
      }

      RenderTargetKey source_key = source_metal_rt->key();

      // Log the transfer request for debugging
      XELOGI(
          "MetalRenderTargetCache::PerformTransfersAndResolveClears: "
          "Transfer request - src key=0x{:08X} ({}x{}, {}samples, depth={}) "
          "-> dst key=0x{:08X} ({}x{}, {}samples, depth={}), "
          "tiles=[{}, {}), host_depth_src={:p}",
          source_key.key, source_texture->width(), source_texture->height(),
          source_texture->sampleCount(), source_key.is_depth ? 1 : 0,
          dest_key.key, dest_texture->width(), dest_texture->height(),
          dest_texture->sampleCount(), dest_key.is_depth ? 1 : 0,
          transfer.start_tiles, transfer.end_tiles,
          (void*)transfer.host_depth_source);

      // Determine transfer mode (matching D3D12's TransferMode enum)
      bool source_is_depth = source_key.is_depth;
      bool dest_is_depth = dest_key.is_depth;

      // Skip depth transfers - not implemented yet
      if (source_is_depth || dest_is_depth) {
        XELOGI(
            "  SKIPPED: Depth transfer not implemented (src_depth={}, "
            "dst_depth={})",

            source_is_depth ? 1 : 0, dest_is_depth ? 1 : 0);
        transfers_skipped_depth++;
        continue;
      }

      // Skip format mismatches - would need format conversion shader
      if (source_key.resource_format != dest_key.resource_format) {
        XELOGI(
            "  SKIPPED: Format mismatch not implemented (src_fmt={}, "
            "dst_fmt={})",

            source_key.resource_format, dest_key.resource_format);
        transfers_skipped_format++;
        continue;
      }

      // Calculate the transfer rectangles
      Transfer::Rectangle rectangles[Transfer::kMaxRectanglesWithCutout];
      uint32_t rectangle_count = transfer.GetRectangles(
          dest_key.base_tiles, dest_key.pitch_tiles_at_32bpp,
          dest_key.msaa_samples, dest_key.Is64bpp(), rectangles);

      if (rectangle_count == 0) {
        continue;
      }

      XELOGI(
          "MetalRenderTargetCache::PerformTransfersAndResolveClears: "
          "Transferring {} rectangles from RT key 0x{:08X} ({}x{}) to RT key "
          "0x{:08X} ({}x{})",
          rectangle_count, source_key.key, source_texture->width(),
          source_texture->height(), dest_key.key, dest_texture->width(),
          dest_texture->height());

      // For each rectangle, we need to copy the corresponding region
      // from source to destination. The rectangles are in pixel coordinates
      // relative to the destination RT's tile layout.

      MTL::CommandBuffer* cmd = queue->commandBuffer();
      if (!cmd) {
        continue;
      }

      MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
      if (!blit) {
        cmd->release();
        continue;
      }

      for (uint32_t rect_idx = 0; rect_idx < rectangle_count; ++rect_idx) {
        const Transfer::Rectangle& rect = rectangles[rect_idx];

        // Apply resolution scale
        uint32_t scaled_x = rect.x_pixels * draw_resolution_scale_x();
        uint32_t scaled_y = rect.y_pixels * draw_resolution_scale_y();
        uint32_t scaled_width = rect.width_pixels * draw_resolution_scale_x();
        uint32_t scaled_height = rect.height_pixels * draw_resolution_scale_y();

        // Clamp to source texture bounds
        uint32_t src_width = static_cast<uint32_t>(source_texture->width());
        uint32_t src_height = static_cast<uint32_t>(source_texture->height());
        if (scaled_x >= src_width || scaled_y >= src_height) {
          continue;
        }
        scaled_width = std::min(scaled_width, src_width - scaled_x);
        scaled_height = std::min(scaled_height, src_height - scaled_y);

        // Clamp to destination texture bounds
        uint32_t dst_width = static_cast<uint32_t>(dest_texture->width());
        uint32_t dst_height = static_cast<uint32_t>(dest_texture->height());
        if (scaled_x >= dst_width || scaled_y >= dst_height) {
          continue;
        }
        scaled_width = std::min(scaled_width, dst_width - scaled_x);
        scaled_height = std::min(scaled_height, dst_height - scaled_y);

        if (scaled_width == 0 || scaled_height == 0) {
          continue;
        }

        // Check if source and dest have compatible sample counts.
        // For same-sample-count ColorToColor we currently use a simple blit.
        // For MSAA mismatches (such as the 4x->1x A-Train case), route
        // through GetOrCreateTransferPipelines so logs clearly show missing
        // implementations.
        if (source_texture->sampleCount() != dest_texture->sampleCount()) {
          TransferShaderKey shader_key;
          shader_key.mode = TransferMode::kColorToColor;
          shader_key.source_msaa_samples = source_key.msaa_samples;
          shader_key.dest_msaa_samples = dest_key.msaa_samples;
          shader_key.source_resource_format = source_key.resource_format;
          shader_key.dest_resource_format = dest_key.resource_format;

          MTL::PixelFormat dest_pixel_format =
              GetColorPixelFormat(dest_key.GetColorFormat());
          MTL::RenderPipelineState* pipeline =
              GetOrCreateTransferPipelines(shader_key, dest_pixel_format);

          if (!pipeline) {
            XELOGI(
                "  SKIPPED rect {}: Sample count mismatch (src={}, dst={}) "
                "and no transfer pipeline for key (mode={}, src_msaa={}, "
                "dst_msaa={}, src_fmt={}, dst_fmt={})",
                rect_idx, source_texture->sampleCount(),
                dest_texture->sampleCount(), int(shader_key.mode),
                int(shader_key.source_msaa_samples),
                int(shader_key.dest_msaa_samples),
                shader_key.source_resource_format,
                shader_key.dest_resource_format);
            transfers_skipped_msaa++;
            continue;
          }

          // Use a render pass and the transfer pipeline to draw this
          // rectangle. This mirrors the D3D12/Vulkan approach of using a
          // graphics pipeline (VS+PS) for ownership transfers.
          MTL::RenderPassDescriptor* rp =
              MTL::RenderPassDescriptor::renderPassDescriptor();
          auto* ca = rp->colorAttachments()->object(0);
          ca->setTexture(dest_texture);
          ca->setLoadAction(MTL::LoadActionLoad);
          ca->setStoreAction(MTL::StoreActionStore);

          MTL::CommandBuffer* xfer_cmd = queue->commandBuffer();
          if (!xfer_cmd) {
            transfers_skipped_msaa++;
            continue;
          }
          MTL::RenderCommandEncoder* enc = xfer_cmd->renderCommandEncoder(rp);
          if (!enc) {
            xfer_cmd->release();
            transfers_skipped_msaa++;
            continue;
          }

          enc->setRenderPipelineState(pipeline);
          enc->setFragmentTexture(source_texture, 0);

          struct RectInfo {
            float dst[4];  // x, y, w, h
            float srcSize[2];
          } ri;
          ri.dst[0] = float(scaled_x);
          ri.dst[1] = float(scaled_y);
          ri.dst[2] = float(scaled_width);
          ri.dst[3] = float(scaled_height);
          ri.srcSize[0] = float(src_width);
          ri.srcSize[1] = float(src_height);

          enc->setFragmentBytes(&ri, sizeof(ri), 0);

          MTL::Viewport vp;
          vp.originX = double(scaled_x);
          vp.originY = double(scaled_y);
          vp.width = double(scaled_width);
          vp.height = double(scaled_height);
          vp.znear = 0.0;
          vp.zfar = 1.0;
          enc->setViewport(vp);

          enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0),
                              NS::UInteger(3));
          enc->endEncoding();

          xfer_cmd->commit();
          xfer_cmd->waitUntilCompleted();
          xfer_cmd->release();

          any_transfers_done = true;

          XELOGI(
              "  Transferred rect {} via ColorToColor 4x->1x pipeline: {}x{} "
              "at ({}, {})",
              rect_idx, rect.width_pixels, rect.height_pixels, rect.x_pixels,
              rect.y_pixels);
          continue;
        }

        // Copy the region from source to destination
        blit->copyFromTexture(
            source_texture, 0, 0, MTL::Origin::Make(scaled_x, scaled_y, 0),
            MTL::Size::Make(scaled_width, scaled_height, 1), dest_texture, 0, 0,
            MTL::Origin::Make(scaled_x, scaled_y, 0));

        any_transfers_done = true;

        XELOGI(
            "  Transferred rect {}: {}x{} at ({}, {}) scaled to {}x{} at ({}, "
            "{})",
            rect_idx, rect.width_pixels, rect.height_pixels, rect.x_pixels,
            rect.y_pixels, scaled_width, scaled_height, scaled_x, scaled_y);
      }

      blit->endEncoding();
      cmd->commit();
      cmd->waitUntilCompleted();
      cmd->release();
    }
  }

  if (any_transfers_done || transfers_skipped_depth ||
      transfers_skipped_format || transfers_skipped_msaa) {
    XELOGI(
        "MetalRenderTargetCache::PerformTransfersAndResolveClears: "
        "completed={}, skipped: depth={}, format={}, msaa={}",
        any_transfers_done ? 1 : 0, transfers_skipped_depth,
        transfers_skipped_format, transfers_skipped_msaa);
  }
}

MTL::RenderPipelineState* MetalRenderTargetCache::GetOrCreateTransferPipelines(
    const TransferShaderKey& key, MTL::PixelFormat dest_format) {
  auto it = transfer_pipelines_.find(key);
  if (it != transfer_pipelines_.end()) {
    return it->second;
  }

  // First, try to handle the specific ColorToColor 4x->1x k_8_8_8_8 case used
  // by the A-Train background path. Other modes / formats will still be
  // reported as unimplemented.
  bool is_color_to_color_4x_to_1x =
      key.mode == TransferMode::kColorToColor &&
      key.source_msaa_samples == xenos::MsaaSamples::k4X &&
      key.dest_msaa_samples == xenos::MsaaSamples::k1X &&
      key.source_resource_format == key.dest_resource_format &&
      (key.source_resource_format ==
           uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8) ||
       key.source_resource_format ==
           uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA));

  if (!is_color_to_color_4x_to_1x) {
    XELOGI(
        "MetalRenderTargetCache::GetOrCreateTransferPipelines: no "
        "implementation for mode={} src_msaa={} dst_msaa={} src_fmt={} "
        "dst_fmt={} (dest_pixel_format={})",
        int(key.mode), int(key.source_msaa_samples), int(key.dest_msaa_samples),
        key.source_resource_format, key.dest_resource_format, int(dest_format));
    return nullptr;
  }

  static const char* kTransferShaderMSL = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct VSOut {
      float4 position [[position]];
      float2 texcoord;
    };

    vertex VSOut transfer_vs(uint vid [[vertex_id]]) {
      float2 pt = float2((vid << 1) & 2, vid & 2);
      VSOut out;
      out.position = float4(pt * 2.0 - 1.0, 0.0, 1.0);
      out.texcoord = float2(pt.x, 1.0 - pt.y);
      return out;
    }

    struct RectInfo {
      float4 dst;      // x, y, w, h in pixels (dest RT space)
      float2 srcSize;  // src_width, src_height
    };

    fragment float4 transfer_ps_color_4x_to_1x(
        VSOut in [[stage_in]],
        texture2d_ms<float> src_msaa [[texture(0)]],
        constant RectInfo& ri [[buffer(0)]]) {
      // Compute dest pixel coords inside the rectangle.
      float2 dst_px = float2(ri.dst.x, ri.dst.y) +
                      in.texcoord * ri.dst.zw;

      // Map dest X range [dst.x, dst.x+dst.w) over src X [0, srcWidth)
      // with a 4:1 horizontal expansion: 4 dest pixels per 1 src pixel.
      float src_x = floor(dst_px.x / 4.0);
      float src_y = dst_px.y;

      // Clamp to valid source range.
      float2 src_clamped = clamp(float2(src_x, src_y),
                                 float2(0.0, 0.0),
                                 ri.srcSize - float2(1.0, 1.0));
      uint2 src_px = uint2(src_clamped + 0.5);

      float4 c = float4(0.0);
      uint sample_count = src_msaa.get_num_samples();
      if (sample_count == 4) {
        // Simple 4x MSAA resolve: average all 4 samples.
        for (uint i = 0; i < 4; ++i) {
          c += src_msaa.read(src_px, i);
        }
        c *= 0.25;
      } else {
        c = src_msaa.read(src_px, 0);
      }
      return c;
    }
  )";

  NS::Error* error = nullptr;
  auto src_str = NS::String::string(kTransferShaderMSL, NS::UTF8StringEncoding);
  MTL::Library* lib = device_->newLibrary(src_str, nullptr, &error);
  if (!lib) {
    XELOGI(
        "GetOrCreateTransferPipelines: failed to compile transfer MSL "
        "(mode={}): "
        "{}",
        int(key.mode),
        error && error->localizedDescription()
            ? error->localizedDescription()->utf8String()
            : "unknown error");
    return nullptr;
  }

  auto vs_name = NS::String::string("transfer_vs", NS::UTF8StringEncoding);
  auto ps_name =
      NS::String::string("transfer_ps_color_4x_to_1x", NS::UTF8StringEncoding);
  MTL::Function* vs = lib->newFunction(vs_name);
  MTL::Function* ps = lib->newFunction(ps_name);
  if (!vs || !ps) {
    XELOGI("GetOrCreateTransferPipelines: failed to get transfer_vs/ps");
    if (vs) vs->release();
    if (ps) ps->release();
    lib->release();
    return nullptr;
  }

  MTL::RenderPipelineDescriptor* desc =
      MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(vs);
  desc->setFragmentFunction(ps);
  desc->colorAttachments()->object(0)->setPixelFormat(dest_format);
  desc->setSampleCount(1);

  MTL::RenderPipelineState* pipeline =
      device_->newRenderPipelineState(desc, &error);

  desc->release();
  vs->release();
  ps->release();
  lib->release();

  if (!pipeline) {
    XELOGI(
        "GetOrCreateTransferPipelines: failed to create pipeline (mode={}): {}",
        int(key.mode),
        error && error->localizedDescription()
            ? error->localizedDescription()->utf8String()
            : "unknown error");
    return nullptr;
  }

  transfer_pipelines_.emplace(key, pipeline);
  XELOGI(
      "GetOrCreateTransferPipelines: created ColorToColor 4x->1x pipeline "
      "(src_fmt={}, dst_fmt={})",
      key.source_resource_format, key.dest_resource_format);

  return pipeline;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
