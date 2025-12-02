/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_command_processor.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/base/xxhash.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/metal/metal_graphics_system.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/metal/metal_presenter.h"

// Metal IR Converter Runtime - defines IRDescriptorTableEntry and bind points
#define IR_RUNTIME_METALCPP
#define IR_PRIVATE_IMPLEMENTATION
#include "metal_irconverter_runtime/metal_irconverter_runtime.h"

// IR Converter bind points (from metal_irconverter_runtime.h)
// kIRDescriptorHeapBindPoint = 0
// kIRSamplerHeapBindPoint = 1
// kIRArgumentBufferBindPoint = 2
// kIRArgumentBufferDrawArgumentsBindPoint = 4
// kIRArgumentBufferUniformsBindPoint = 5
// kIRVertexBufferBindPoint = 6

namespace xe {
namespace gpu {
namespace metal {

MetalCommandProcessor::MetalCommandProcessor(
    MetalGraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}

MetalCommandProcessor::~MetalCommandProcessor() {
  // End any active render encoder before releasing
  // Note: Only call endEncoding if the encoder is still active
  // (not already ended by a committed command buffer)
  if (current_render_encoder_) {
    // The encoder may already be ended if the command buffer was committed
    // In that case, just release it
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }
  if (current_command_buffer_) {
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }
  if (render_pass_descriptor_) {
    render_pass_descriptor_->release();
    render_pass_descriptor_ = nullptr;
  }
  if (render_target_texture_) {
    render_target_texture_->release();
    render_target_texture_ = nullptr;
  }
  if (depth_stencil_texture_) {
    depth_stencil_texture_->release();
    depth_stencil_texture_ = nullptr;
  }

  // Release pipeline cache
  for (auto& pair : pipeline_cache_) {
    if (pair.second) {
      pair.second->release();
    }
  }
  pipeline_cache_.clear();

  // Release debug pipeline
  if (debug_pipeline_) {
    debug_pipeline_->release();
    debug_pipeline_ = nullptr;
  }

  // Release IR Converter runtime buffers and resources
  if (null_buffer_) {
    null_buffer_->release();
    null_buffer_ = nullptr;
  }
  if (null_texture_) {
    null_texture_->release();
    null_texture_ = nullptr;
  }
  if (null_sampler_) {
    null_sampler_->release();
    null_sampler_ = nullptr;
  }
  if (res_heap_ab_) {
    res_heap_ab_->release();
    res_heap_ab_ = nullptr;
  }
  if (smp_heap_ab_) {
    smp_heap_ab_->release();
    smp_heap_ab_ = nullptr;
  }
  if (cbv_heap_ab_) {
    cbv_heap_ab_->release();
    cbv_heap_ab_ = nullptr;
  }
  if (uniforms_buffer_) {
    uniforms_buffer_->release();
    uniforms_buffer_ = nullptr;
  }
  if (top_level_ab_) {
    top_level_ab_->release();
    top_level_ab_ = nullptr;
  }
  if (draw_args_buffer_) {
    draw_args_buffer_->release();
    draw_args_buffer_ = nullptr;
  }
}

void MetalCommandProcessor::TracePlaybackWroteMemory(uint32_t base_ptr,
                                                     uint32_t length) {
  if (shared_memory_) {
    shared_memory_->MemoryInvalidationCallback(base_ptr, length, true);
  }
}

void MetalCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {
  // Restore the guest EDRAM snapshot captured in the trace into the Metal
  // render-target cache so that subsequent host render targets created from
  // EDRAM (via LoadTiledData) see the same initial contents as other
  // backends like D3D12.
  if (!snapshot) {
    XELOGW(
        "MetalCommandProcessor::RestoreEdramSnapshot called with null "
        "snapshot");
    return;
  }
  if (!render_target_cache_) {
    XELOGW(
        "MetalCommandProcessor::RestoreEdramSnapshot called before render "
        "target "
        "cache initialization");
    return;
  }
  render_target_cache_->RestoreEdramSnapshot(snapshot);
}

ui::metal::MetalProvider& MetalCommandProcessor::GetMetalProvider() const {
  return *static_cast<ui::metal::MetalProvider*>(graphics_system_->provider());
}

void MetalCommandProcessor::MarkResolvedMemory(uint32_t base_ptr,
                                               uint32_t length) {
  if (length == 0) return;
  resolved_memory_ranges_.push_back({base_ptr, length});
  XELOGI("MetalCommandProcessor::MarkResolvedMemory: 0x{:08X} length={}",
         base_ptr, length);
}

bool MetalCommandProcessor::IsResolvedMemory(uint32_t base_ptr,
                                             uint32_t length) const {
  uint32_t end_ptr = base_ptr + length;
  for (const auto& range : resolved_memory_ranges_) {
    uint32_t range_end = range.base + range.length;
    // Check if ranges overlap
    if (base_ptr < range_end && end_ptr > range.base) {
      return true;
    }
  }
  return false;
}

void MetalCommandProcessor::ClearResolvedMemory() {
  resolved_memory_ranges_.clear();
}

void MetalCommandProcessor::ForceIssueSwap() {
  // Force a swap to push any pending render target to presenter
  // This is used by trace dumps to capture output when there's no explicit swap
  IssueSwap(0, render_target_width_, render_target_height_);
}

bool MetalCommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Failed to initialize base command processor context");
    return false;
  }

  const ui::metal::MetalProvider& provider = GetMetalProvider();
  device_ = provider.GetDevice();
  command_queue_ = provider.GetCommandQueue();

  if (!device_ || !command_queue_) {
    XELOGE("MetalCommandProcessor: No Metal device or command queue available");
    return false;
  }

  // Initialize shared memory
  shared_memory_ = std::make_unique<MetalSharedMemory>(*this, *memory_);
  if (!shared_memory_->Initialize()) {
    XELOGE("Failed to initialize shared memory");
    return false;
  }

  texture_cache_ = std::make_unique<MetalTextureCache>(this, *register_file_,
                                                       *shared_memory_, 1, 1);
  if (!texture_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal texture cache");
    return false;
  }

  // Initialize render target cache
  render_target_cache_ = std::make_unique<MetalRenderTargetCache>(
      *register_file_, *memory_, &trace_writer_, 1, 1, *this);
  if (!render_target_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal render target cache");
    return false;
  }

  // Initialize shader translation pipeline
  if (!InitializeShaderTranslation()) {
    XELOGE("Failed to initialize shader translation");
    return false;
  }

  // Create render target texture for offscreen rendering
  MTL::TextureDescriptor* color_desc = MTL::TextureDescriptor::alloc()->init();
  color_desc->setTextureType(MTL::TextureType2D);
  color_desc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  color_desc->setWidth(render_target_width_);
  color_desc->setHeight(render_target_height_);
  color_desc->setStorageMode(MTL::StorageModePrivate);
  color_desc->setUsage(MTL::TextureUsageRenderTarget |
                       MTL::TextureUsageShaderRead);

  render_target_texture_ = device_->newTexture(color_desc);
  color_desc->release();

  if (!render_target_texture_) {
    XELOGE("Failed to create render target texture");
    return false;
  }
  render_target_texture_->setLabel(
      NS::String::string("XeniaRenderTarget", NS::UTF8StringEncoding));

  // Create depth/stencil texture
  MTL::TextureDescriptor* depth_desc = MTL::TextureDescriptor::alloc()->init();
  depth_desc->setTextureType(MTL::TextureType2D);
  depth_desc->setPixelFormat(MTL::PixelFormatDepth32Float_Stencil8);
  depth_desc->setWidth(render_target_width_);
  depth_desc->setHeight(render_target_height_);
  depth_desc->setStorageMode(MTL::StorageModePrivate);
  depth_desc->setUsage(MTL::TextureUsageRenderTarget);

  depth_stencil_texture_ = device_->newTexture(depth_desc);
  depth_desc->release();

  if (!depth_stencil_texture_) {
    XELOGE("Failed to create depth/stencil texture");
    return false;
  }
  depth_stencil_texture_->setLabel(
      NS::String::string("XeniaDepthStencil", NS::UTF8StringEncoding));

  // Create render pass descriptor
  render_pass_descriptor_ = MTL::RenderPassDescriptor::alloc()->init();

  auto color_attachment =
      render_pass_descriptor_->colorAttachments()->object(0);
  color_attachment->setTexture(render_target_texture_);
  color_attachment->setLoadAction(MTL::LoadActionClear);
  color_attachment->setStoreAction(MTL::StoreActionStore);
  color_attachment->setClearColor(MTL::ClearColor(0.0, 0.0, 0.0, 1.0));

  auto depth_attachment = render_pass_descriptor_->depthAttachment();
  depth_attachment->setTexture(depth_stencil_texture_);
  depth_attachment->setLoadAction(MTL::LoadActionClear);
  depth_attachment->setStoreAction(MTL::StoreActionDontCare);
  depth_attachment->setClearDepth(1.0);

  auto stencil_attachment = render_pass_descriptor_->stencilAttachment();
  stencil_attachment->setTexture(depth_stencil_texture_);
  stencil_attachment->setLoadAction(MTL::LoadActionClear);
  stencil_attachment->setStoreAction(MTL::StoreActionDontCare);
  stencil_attachment->setClearStencil(0);

  // Create IR Converter runtime descriptor heap buffers
  // These are arrays of IRDescriptorTableEntry used by Metal Shader Converter
  // IMPORTANT: Metal Shader Converter reads one entry PAST the declared count
  // (likely for bounds checking), so we need NumDescriptors + 2 total slots:
  // - NumDescriptors slots for actual descriptors
  // - +1 slot that the shader reads (at index NumDescriptors)
  // - +1 extra padding for safety
  const size_t kResourceHeapSlots = kResourceHeapSlotCount;
  const size_t kSamplerHeapSlots = kSamplerHeapSlotCount;
  const size_t kResourceHeapBytes =
      kResourceHeapSlots * sizeof(IRDescriptorTableEntry);
  const size_t kSamplerHeapBytes =
      kSamplerHeapSlots * sizeof(IRDescriptorTableEntry);

  // Create a null buffer for unused descriptor entries
  // This prevents shader validation errors when accessing unpopulated
  // descriptors
  const size_t kNullBufferSize = 4096;
  null_buffer_ =
      device_->newBuffer(kNullBufferSize, MTL::ResourceStorageModeShared);
  if (!null_buffer_) {
    XELOGE("Failed to create null buffer");
    return false;
  }
  null_buffer_->setLabel(
      NS::String::string("NullBuffer", NS::UTF8StringEncoding));
  std::memset(null_buffer_->contents(), 0, kNullBufferSize);

  // Create a 1x1x1 placeholder 2D array texture for unbound texture slots
  // Xbox 360 textures are typically 2D arrays (for texture atlases, cubemaps)
  // Using 2DArray prevents "Invalid texture type" validation errors
  MTL::TextureDescriptor* null_tex_desc =
      MTL::TextureDescriptor::alloc()->init();
  null_tex_desc->setTextureType(MTL::TextureType2DArray);
  null_tex_desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  null_tex_desc->setWidth(1);
  null_tex_desc->setHeight(1);
  null_tex_desc->setArrayLength(1);  // Single slice in the array
  null_tex_desc->setStorageMode(MTL::StorageModeShared);
  null_tex_desc->setUsage(MTL::TextureUsageShaderRead);

  null_texture_ = device_->newTexture(null_tex_desc);
  null_tex_desc->release();

  if (!null_texture_) {
    XELOGE("Failed to create null texture");
    return false;
  }
  null_texture_->setLabel(
      NS::String::string("NullTexture2DArray", NS::UTF8StringEncoding));

  // Fill the 1x1x1 texture with opaque white (helps debug if sampled)
  uint32_t white_pixel = 0xFFFFFFFF;
  MTL::Region region =
      MTL::Region(0, 0, 0, 1, 1, 1);  // x,y,z origin, w,h,d size
  null_texture_->replaceRegion(region, 0, 0, &white_pixel, 4, 0);  // slice 0

  // Create a default sampler for unbound sampler slots
  // Must set supportsArgumentBuffers=YES for use in argument buffers
  MTL::SamplerDescriptor* null_smp_desc =
      MTL::SamplerDescriptor::alloc()->init();
  null_smp_desc->setMinFilter(MTL::SamplerMinMagFilterLinear);
  null_smp_desc->setMagFilter(MTL::SamplerMinMagFilterLinear);
  null_smp_desc->setMipFilter(MTL::SamplerMipFilterLinear);
  null_smp_desc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  null_smp_desc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  null_smp_desc->setRAddressMode(MTL::SamplerAddressModeClampToEdge);
  null_smp_desc->setSupportArgumentBuffers(true);

  null_sampler_ = device_->newSamplerState(null_smp_desc);
  null_smp_desc->release();

  if (!null_sampler_) {
    XELOGE("Failed to create null sampler");
    return false;
  }

  res_heap_ab_ =
      device_->newBuffer(kResourceHeapBytes, MTL::ResourceStorageModeShared);
  if (!res_heap_ab_) {
    XELOGE("Failed to create resource descriptor heap buffer");
    return false;
  }
  res_heap_ab_->setLabel(
      NS::String::string("ResourceDescriptorHeap", NS::UTF8StringEncoding));
  // Initialize resource descriptor entries:
  // - Slot 0: Shared memory buffer (set later when shared_memory is available)
  // - Slots 1+: Use null_texture for texture slots, null_buffer for buffer
  // slots The shader may access textures at various slots, so we need to
  // populate them with valid texture references to avoid "Null texture access"
  // errors
  auto* res_entries =
      reinterpret_cast<IRDescriptorTableEntry*>(res_heap_ab_->contents());

  // Initialize slot 0 with null buffer (will be replaced with shared_memory)
  IRDescriptorTableSetBuffer(&res_entries[0], null_buffer_->gpuAddress(),
                             kNullBufferSize);

  // Initialize remaining slots with null_texture for texture access safety
  // Use IRDescriptorTableSetTexture for texture slots
  // minLODClamp = 0.0f, metadata = 0 (metadata must be zero per docs)
  for (size_t i = 1; i < kResourceHeapSlots; i++) {
    IRDescriptorTableSetTexture(&res_entries[i], null_texture_, 0.0f, 0);
  }

  smp_heap_ab_ =
      device_->newBuffer(kSamplerHeapBytes, MTL::ResourceStorageModeShared);
  if (!smp_heap_ab_) {
    XELOGE("Failed to create sampler descriptor heap buffer");
    return false;
  }
  smp_heap_ab_->setLabel(
      NS::String::string("SamplerDescriptorHeap", NS::UTF8StringEncoding));
  // Initialize all sampler descriptor entries with the null sampler
  // This prevents "Null sampler" errors when shaders sample textures
  auto* smp_entries =
      reinterpret_cast<IRDescriptorTableEntry*>(smp_heap_ab_->contents());
  for (size_t i = 0; i < kSamplerHeapSlots; i++) {
    IRDescriptorTableSetSampler(&smp_entries[i], null_sampler_, 0.0f);
  }

  // Create uniforms buffer for constant buffers (b0-b3)
  // Layout (each CBV is 4KB aligned):
  //   b0: system constants (~512B) at offset 0
  //   b1: float constants (4KB) at offset 4096
  //   b2: bool/loop constants (256B) at offset 8192
  //   b3: fetch constants (768B) at offset 12288
  //   b4: descriptor indices (unused) at offset 16384
  // Total: 5 * 4KB = 20KB
  const size_t kUniformsBufferSize = 20 * 1024;
  uniforms_buffer_ =
      device_->newBuffer(kUniformsBufferSize, MTL::ResourceStorageModeShared);
  if (!uniforms_buffer_) {
    XELOGE("Failed to create uniforms buffer");
    return false;
  }
  uniforms_buffer_->setLabel(
      NS::String::string("UniformsBuffer", NS::UTF8StringEncoding));
  std::memset(uniforms_buffer_->contents(), 0, kUniformsBufferSize);

  // Create top-level argument buffer (bound at index 2)
  // IMPORTANT: The top-level AB contains uint64_t GPU addresses pointing to
  // descriptor tables, NOT IRDescriptorTableEntry structures!
  // The root signature has 14 parameters (4 SRV + 1 SRV hull + 4 UAV + 1
  // sampler + 4 CBV) Each entry is sizeof(uint64_t) = 8 bytes
  const size_t kTopLevelABSlots = 20;  // Some extra for safety
  const size_t kTopLevelABBytes = kTopLevelABSlots * sizeof(uint64_t);
  top_level_ab_ =
      device_->newBuffer(kTopLevelABBytes, MTL::ResourceStorageModeShared);
  if (!top_level_ab_) {
    XELOGE("Failed to create top-level argument buffer");
    return false;
  }
  top_level_ab_->setLabel(
      NS::String::string("TopLevelArgumentBuffer", NS::UTF8StringEncoding));
  std::memset(top_level_ab_->contents(), 0, kTopLevelABBytes);

  // Create draw arguments buffer (bound at index 4)
  // Contains IRRuntimeDrawArguments structure
  const size_t kDrawArgsSize = 64;  // Enough for draw arguments struct
  draw_args_buffer_ =
      device_->newBuffer(kDrawArgsSize, MTL::ResourceStorageModeShared);
  if (!draw_args_buffer_) {
    XELOGE("Failed to create draw arguments buffer");
    return false;
  }
  draw_args_buffer_->setLabel(
      NS::String::string("DrawArgumentsBuffer", NS::UTF8StringEncoding));
  std::memset(draw_args_buffer_->contents(), 0, kDrawArgsSize);

  // Create CBV descriptor heap (for root param 10 which covers space 0)
  // Space 0 contains b0-b4 (5 constant buffers):
  //   b0 = system constants
  //   b1 = float constants
  //   b2 = bool/loop constants
  //   b3 = fetch constants
  //   b4 = descriptor indices (unused for bindful mode)
  // Each entry is IRDescriptorTableEntry (24 bytes)
  // Add +1 for sentinel entry that shader reads past declared count
  const size_t kCBVHeapSlots = kCbvHeapSlotCount;
  const size_t kCBVHeapBytes = kCBVHeapSlots * sizeof(IRDescriptorTableEntry);
  cbv_heap_ab_ =
      device_->newBuffer(kCBVHeapBytes, MTL::ResourceStorageModeShared);
  if (!cbv_heap_ab_) {
    XELOGE("Failed to create CBV descriptor heap buffer");
    return false;
  }
  cbv_heap_ab_->setLabel(
      NS::String::string("CBVDescriptorHeap", NS::UTF8StringEncoding));
  std::memset(cbv_heap_ab_->contents(), 0, kCBVHeapBytes);

  XELOGI(
      "MetalCommandProcessor: Created IR Converter buffers (res_heap={} "
      "entries, smp_heap={} entries, cbv_heap={} entries, uniforms={}B, "
      "top_level={}B, draw_args={}B)",
      kResourceHeapSlots, kSamplerHeapSlots, kCBVHeapSlots, kUniformsBufferSize,
      kTopLevelABBytes, kDrawArgsSize);

  XELOGI("MetalCommandProcessor::SetupContext() completed successfully");
  return true;
}

bool MetalCommandProcessor::InitializeShaderTranslation() {
  // Initialize DXBC shader translator (use Apple vendor ID for Metal)
  // Parameters: vendor_id, bindless_resources_used, edram_rov_used,
  //             gamma_render_target_as_srgb, msaa_2x_supported,
  //             draw_resolution_scale_x, draw_resolution_scale_y
  shader_translator_ = std::make_unique<DxbcShaderTranslator>(
      ui::GraphicsProvider::GpuVendorID::kApple,
      false,  // bindless_resources_used - not using bindless for now
      false,  // edram_rov_used - not using ROV for EDRAM emulation
      false,  // gamma_render_target_as_srgb
      true,   // msaa_2x_supported - Metal supports MSAA
      1,      // draw_resolution_scale_x - 1x for now
      1);     // draw_resolution_scale_y - 1x for now

  // Initialize DXBC to DXIL converter
  dxbc_to_dxil_converter_ = std::make_unique<DxbcToDxilConverter>();
  if (!dxbc_to_dxil_converter_->Initialize()) {
    XELOGE("Failed to initialize DXBC to DXIL converter");
    return false;
  }

  // Initialize Metal Shader Converter
  metal_shader_converter_ = std::make_unique<MetalShaderConverter>();
  if (!metal_shader_converter_->Initialize()) {
    XELOGE("Failed to initialize Metal Shader Converter");
    return false;
  }

  XELOGI("Shader translation pipeline initialized successfully");
  return true;
}

void MetalCommandProcessor::ShutdownContext() {
  // End any active render encoder before shutdown
  if (current_render_encoder_) {
    current_render_encoder_->endEncoding();
    // Don't release yet - wait until command buffer completes
  }

  // Submit and wait for any pending command buffer
  if (current_command_buffer_) {
    current_command_buffer_->commit();
    current_command_buffer_->waitUntilCompleted();
  }

  // Now safe to release encoder and command buffer
  if (current_render_encoder_) {
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }
  if (current_command_buffer_) {
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }

  if (texture_cache_) {
    texture_cache_->Shutdown();
    texture_cache_.reset();
  }

  shader_cache_.clear();
  shared_memory_.reset();
  shader_translator_.reset();
  dxbc_to_dxil_converter_.reset();
  metal_shader_converter_.reset();

  CommandProcessor::ShutdownContext();
}

void MetalCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                      uint32_t frontbuffer_width,
                                      uint32_t frontbuffer_height) {
  XELOGI("MetalCommandProcessor::IssueSwap(ptr={:08X}, {}x{})", frontbuffer_ptr,
         frontbuffer_width, frontbuffer_height);

  // End any active render encoder
  if (current_render_encoder_) {
    current_render_encoder_->endEncoding();
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }

  // Submit and wait for command buffer
  if (current_command_buffer_) {
    current_command_buffer_->commit();
    current_command_buffer_->waitUntilCompleted();
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }

  // Push the rendered frame to the presenter's guest output mailbox
  // This is required for trace dumps to capture the output. Use the
  // MetalRenderTargetCache color target (like D3D12) rather than the
  // legacy standalone render_target_texture_.
  auto* presenter =
      static_cast<ui::metal::MetalPresenter*>(graphics_system_->presenter());
  if (presenter && render_target_cache_) {
    uint32_t output_width = render_target_width_;
    uint32_t output_height = render_target_height_;

    // Prefer the last real color target 0, fall back to current/dummy.
    MTL::Texture* source_texture =
        render_target_cache_->GetLastRealColorTarget(0);
    if (!source_texture) {
      source_texture = render_target_cache_->GetColorTarget(0);
    }
    if (!source_texture) {
      source_texture = render_target_cache_->GetDummyColorTarget();
    }

    if (source_texture) {
      ui::metal::MetalPresenter* metal_presenter = presenter;
      presenter->RefreshGuestOutput(
          output_width, output_height, 1280, 720,  // Display aspect ratio
          [source_texture, metal_presenter](
              ui::Presenter::GuestOutputRefreshContext& context) -> bool {
            auto& metal_context =
                static_cast<ui::metal::MetalGuestOutputRefreshContext&>(
                    context);
            return metal_presenter->CopyTextureToGuestOutput(
                source_texture, metal_context.resource_uav_capable());
          });
    }
  }

  // Also capture internally for backwards compatibility
  CaptureCurrentFrame();
}

void MetalCommandProcessor::CaptureCurrentFrame() {
  if (!device_) {
    return;
  }

  // Choose a color render target to capture from the cache.
  MTL::Texture* capture_texture = nullptr;
  if (render_target_cache_) {
    capture_texture = render_target_cache_->GetLastRealColorTarget(0);
    if (!capture_texture) {
      capture_texture = render_target_cache_->GetColorTarget(0);
    }
  }
  if (!capture_texture) {
    return;
  }

  // Create a staging buffer for readback
  size_t bytes_per_row = render_target_width_ * 4;  // BGRA8
  size_t buffer_size = bytes_per_row * render_target_height_;

  MTL::Buffer* staging_buffer =
      device_->newBuffer(buffer_size, MTL::ResourceStorageModeShared);

  if (!staging_buffer) {
    XELOGE("Failed to create staging buffer for frame capture");
    return;
  }

  // Create a blit command buffer
  MTL::CommandBuffer* blit_cmd = command_queue_->commandBuffer();
  MTL::BlitCommandEncoder* blit = blit_cmd->blitCommandEncoder();

  blit->copyFromTexture(
      capture_texture, 0, 0, MTL::Origin(0, 0, 0),
      MTL::Size(render_target_width_, render_target_height_, 1), staging_buffer,
      0, bytes_per_row, 0);

  blit->endEncoding();
  blit->release();  // Release the encoder after ending
  blit_cmd->commit();
  blit_cmd->waitUntilCompleted();

  // Copy data from staging buffer
  captured_width_ = render_target_width_;
  captured_height_ = render_target_height_;
  captured_frame_data_.resize(buffer_size);

  uint8_t* src = static_cast<uint8_t*>(staging_buffer->contents());

  // Convert BGRA to RGBA
  for (size_t i = 0; i < buffer_size; i += 4) {
    captured_frame_data_[i + 0] = src[i + 2];  // R from B
    captured_frame_data_[i + 1] = src[i + 1];  // G
    captured_frame_data_[i + 2] = src[i + 0];  // B from R
    captured_frame_data_[i + 3] = src[i + 3];  // A
  }

  staging_buffer->release();
  blit_cmd->release();

  has_captured_frame_ = true;
  XELOGD("Captured frame: {}x{}", captured_width_, captured_height_);
}

bool MetalCommandProcessor::GetLastCapturedFrame(uint32_t& width,
                                                 uint32_t& height,
                                                 std::vector<uint8_t>& data) {
  if (!has_captured_frame_) {
    return false;
  }
  width = captured_width_;
  height = captured_height_;
  data = captured_frame_data_;
  return true;
}

Shader* MetalCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                          uint32_t guest_address,
                                          const uint32_t* host_address,
                                          uint32_t dword_count) {
  // Create hash for caching using XXH3 (same as D3D12)
  uint64_t hash = XXH3_64bits(host_address, dword_count * sizeof(uint32_t));

  // Check cache
  auto it = shader_cache_.find(hash);
  if (it != shader_cache_.end()) {
    return it->second.get();
  }

  // Create new shader - analysis and translation happen later when the shader
  // is actually used in a draw call (matching D3D12 pattern)
  auto shader = std::make_unique<MetalShader>(shader_type, hash, host_address,
                                              dword_count);

  MetalShader* result = shader.get();
  shader_cache_[hash] = std::move(shader);

  XELOGD("Loaded {} shader at {:08X} ({} dwords, hash {:016X})",
         shader_type == xenos::ShaderType::kVertex ? "vertex" : "pixel",
         guest_address, dword_count, hash);

  return result;
}

bool MetalCommandProcessor::IssueDraw(xenos::PrimitiveType primitive_type,
                                      uint32_t index_count,
                                      IndexBufferInfo* index_buffer_info,
                                      bool major_mode_explicit) {
  const RegisterFile& regs = *register_file_;
  // Debug flags for background-related render targets.
  bool debug_bg_rt0_320x2048 = false;
  bool debug_bg_rt0_1280x2048 = false;

  // Check for copy mode
  xenos::ModeControl edram_mode = regs.Get<reg::RB_MODECONTROL>().edram_mode;
  if (edram_mode == xenos::ModeControl::kCopy) {
    return IssueCopy();
  }

  // Get shaders
  auto vertex_shader = static_cast<MetalShader*>(active_vertex_shader());
  if (!vertex_shader) {
    XELOGW("IssueDraw: No vertex shader");
    return false;
  }

  auto pixel_shader = static_cast<MetalShader*>(active_pixel_shader());
  // Pixel shader can be null for depth-only rendering

  // Analyze vertex shader ucode if needed
  if (!vertex_shader->is_ucode_analyzed()) {
    vertex_shader->AnalyzeUcode(ucode_disasm_buffer_);
  }

  // Get or create shader translations
  auto vertex_translation = static_cast<MetalShader::MetalTranslation*>(
      vertex_shader->GetOrCreateTranslation(0));

  // Translate ucode to DXBC if needed
  if (!vertex_translation->is_translated()) {
    if (!shader_translator_->TranslateAnalyzedShader(*vertex_translation)) {
      XELOGE("Failed to translate vertex shader to DXBC");
      return false;
    }
  }

  // Translate DXBC to Metal if needed
  if (!vertex_translation->is_valid()) {
    if (!vertex_translation->TranslateToMetal(device_, *dxbc_to_dxil_converter_,
                                              *metal_shader_converter_)) {
      XELOGE("Failed to translate vertex shader to Metal");
      return false;
    }
  }

  MetalShader::MetalTranslation* pixel_translation = nullptr;
  if (pixel_shader) {
    // Analyze pixel shader ucode if needed
    if (!pixel_shader->is_ucode_analyzed()) {
      pixel_shader->AnalyzeUcode(ucode_disasm_buffer_);
    }

    pixel_translation = static_cast<MetalShader::MetalTranslation*>(
        pixel_shader->GetOrCreateTranslation(0));

    // Translate ucode to DXBC if needed
    if (!pixel_translation->is_translated()) {
      if (!shader_translator_->TranslateAnalyzedShader(*pixel_translation)) {
        XELOGE("Failed to translate pixel shader to DXBC");
        return false;
      }
    }

    // Translate DXBC to Metal if needed
    if (!pixel_translation->is_valid()) {
      if (!pixel_translation->TranslateToMetal(
              device_, *dxbc_to_dxil_converter_, *metal_shader_converter_)) {
        XELOGE("Failed to translate pixel shader to Metal");
        return false;
      }
    }
  }

  // Configure render targets via MetalRenderTargetCache, similar to D3D12.
  // D3D12 passes `is_rasterization_done` when there is an actual draw to
  // perform (after primitive processing). For this Metal path we currently
  // always treat IssueDraw as doing rasterization so the render-target cache
  // will create and bind the appropriate host render targets.
  if (render_target_cache_) {
    bool is_rasterization_done = true;
    auto normalized_depth_control = draw_util::GetNormalizedDepthControl(regs);
    uint32_t normalized_color_mask =
        pixel_shader ? draw_util::GetNormalizedColorMask(
                           regs, pixel_shader->writes_color_targets())
                     : 0;
    XELOGI(
        "Metal IssueDraw: writes_color_targets=0x{:X}, "
        "normalized_color_mask=0x{:X}",
        pixel_shader ? pixel_shader->writes_color_targets() : 0,
        normalized_color_mask);
    if (!render_target_cache_->Update(is_rasterization_done,
                                      normalized_depth_control,
                                      normalized_color_mask, *vertex_shader)) {
      XELOGE(
          "MetalCommandProcessor::IssueDraw - RenderTargetCache::Update "
          "failed");
      return false;
    }

    // Log info for all draws to help debug missing background
    MTL::Texture* rt0_tex = render_target_cache_->GetColorTarget(0);
    if (rt0_tex) {
      uint32_t rt0_w = rt0_tex->width();
      uint32_t rt0_h = rt0_tex->height();
      static uint32_t draw_counter = 0;
      ++draw_counter;

      // Check texture bindings
      size_t vs_tex_count =
          vertex_shader
              ? vertex_shader->GetTextureBindingsAfterTranslation().size()
              : 0;
      size_t ps_tex_count =
          pixel_shader
              ? pixel_shader->GetTextureBindingsAfterTranslation().size()
              : 0;

      XELOGI(
          "DRAW_DEBUG: #{} RT0={}x{} index_count={} primitive={} VS_tex={} "
          "PS_tex={}",
          draw_counter, rt0_w, rt0_h, index_count,
          static_cast<int>(primitive_type), vs_tex_count, ps_tex_count);

      // Mark background-related RT sizes for additional debugging.
      if (rt0_w == 320 && rt0_h == 2048) {
        debug_bg_rt0_320x2048 = true;
        XELOGI("BG_DEBUG: draw {} into 320x2048 RT0", draw_counter);
      } else if (rt0_w == 1280 && rt0_h == 2048) {
        debug_bg_rt0_1280x2048 = true;
        XELOGI("BG_DEBUG: draw {} into 1280x2048 RT0", draw_counter);
      }
    }
  }

  // Begin command buffer if needed (will use cache-provided render targets).
  BeginCommandBuffer();

  // DEBUG TEST MODE: Use debug pipeline to verify rendering works
  // Set this to true to bypass Xbox 360 shaders and render solid magenta
  static bool use_test_pipeline =
      false;  // Use real Xbox 360 shaders by default
  static bool test_logged = false;
  if (!test_logged) {
    test_logged = true;
    XELOGI(
        "DEBUG: use_test_pipeline = {} (set to true to test with solid color "
        "shader)",
        use_test_pipeline);
  }

  MTL::RenderPipelineState* pipeline = nullptr;
  if (use_test_pipeline) {
    // Use debug pipeline for testing
    if (!debug_pipeline_) {
      CreateDebugPipeline();
    }
    pipeline = debug_pipeline_;
  } else {
    // Get or create pipeline state from Xbox 360 shaders
    pipeline = GetOrCreatePipelineState(vertex_translation, pixel_translation);
  }

  if (!pipeline) {
    XELOGE("Failed to create pipeline state");
    return false;
  }

  uint32_t used_texture_mask =
      vertex_shader->GetUsedTextureMaskAfterTranslation();
  if (pixel_shader) {
    used_texture_mask |= pixel_shader->GetUsedTextureMaskAfterTranslation();
  }
  if (texture_cache_ && used_texture_mask) {
    texture_cache_->RequestTextures(used_texture_mask);
  }

  // For the background pass (draws into 320x2048 RT0), log the pixel shader
  // texture bindings so we can see which textures are actually sampled and
  // whether they are present in the Metal texture cache.
  if (debug_bg_rt0_320x2048 && texture_cache_) {
    uint32_t vs_mask = vertex_shader->GetUsedTextureMaskAfterTranslation();
    uint32_t ps_mask =
        pixel_shader ? pixel_shader->GetUsedTextureMaskAfterTranslation() : 0;
    XELOGI("BG_DEBUG: used_texture_mask: VS=0x{:X}, PS=0x{:X}, Combined=0x{:X}",
           vs_mask, ps_mask, vs_mask | ps_mask);

    if (vertex_shader) {
      const auto& vs_tex_bindings =
          vertex_shader->GetTextureBindingsAfterTranslation();
      XELOGI("BG_DEBUG: vertex shader has {} texture bindings",
             vs_tex_bindings.size());
      for (size_t i = 0; i < vs_tex_bindings.size(); ++i) {
        const auto& binding = vs_tex_bindings[i];
        MTL::Texture* tex = texture_cache_->GetTextureForBinding(
            binding.fetch_constant, binding.dimension, binding.is_signed);
        XELOGI(
            "BG_DEBUG: VS binding[{}]: fetch_constant={}, dim={}, signed={}, "
            "texture_present={}",
            i, binding.fetch_constant, static_cast<int>(binding.dimension),
            binding.is_signed, tex != nullptr);
      }
    }

    if (pixel_shader) {
      const auto& ps_tex_bindings =
          pixel_shader->GetTextureBindingsAfterTranslation();
      XELOGI("BG_DEBUG: pixel shader has {} texture bindings",
             ps_tex_bindings.size());
      for (size_t i = 0; i < ps_tex_bindings.size(); ++i) {
        const auto& binding = ps_tex_bindings[i];
        MTL::Texture* tex = texture_cache_->GetTextureForBinding(
            binding.fetch_constant, binding.dimension, binding.is_signed);
        XELOGI(
            "BG_DEBUG: PS binding[{}]: fetch_constant={}, dim={}, signed={}, "
            "texture_present={}",
            i, binding.fetch_constant, static_cast<int>(binding.dimension),
            binding.is_signed, tex != nullptr);
      }
    }
  }

  // Sync shared memory before drawing - ensure GPU has latest data
  // This is particularly important for trace playback where memory is
  // written incrementally
  if (shared_memory_) {
    // Request entire buffer range to ensure all dirty pages are uploaded
    // In future, optimize to only request ranges actually needed by shaders
    shared_memory_->RequestRange(0, SharedMemory::kBufferSize);

    // Debug: Check vertex data AFTER sync - with endian swap
    static int draw_count = 0;
    if (draw_count < 3) {
      draw_count++;
      const RegisterFile& regs_dbg = *register_file_;
      const uint32_t* fetch_data =
          &regs_dbg.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0];
      uint32_t fc0 = fetch_data[0];
      uint32_t fc1 = fetch_data[1];
      uint32_t vertex_addr = fc0 & 0xFFFFFFFC;
      uint32_t endian_mode = fc1 & 0x3;  // bits 0-1 of dword_1
      MTL::Buffer* sm = shared_memory_->GetBuffer();
      if (sm && vertex_addr < sm->length()) {
        const uint8_t* sm_data = static_cast<const uint8_t*>(sm->contents());
        const uint32_t* raw =
            reinterpret_cast<const uint32_t*>(sm_data + vertex_addr);

        // Byteswap if endian mode is k8in32 (mode 2)
        auto bswap32 = [](uint32_t v) -> uint32_t {
          return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
                 ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
        };

        float v0x, v0y, v0z;
        if (endian_mode == 2) {
          uint32_t x = bswap32(raw[0]);
          uint32_t y = bswap32(raw[1]);
          uint32_t z = bswap32(raw[2]);
          std::memcpy(&v0x, &x, 4);
          std::memcpy(&v0y, &y, 4);
          std::memcpy(&v0z, &z, 4);
        } else {
          std::memcpy(&v0x, &raw[0], 4);
          std::memcpy(&v0y, &raw[1], 4);
          std::memcpy(&v0z, &raw[2], 4);
        }

        XELOGI("DRAW#{}: FC0=0x{:08X} FC1=0x{:08X} addr=0x{:08X} endian={}",
               draw_count, fc0, fc1, vertex_addr, endian_mode);
        XELOGI("  After byteswap: v0=({:.3f},{:.3f},{:.3f})", v0x, v0y, v0z);
      }
    }
  }

  // Set pipeline state on encoder
  current_render_encoder_->setRenderPipelineState(pipeline);

  // Bind IR Converter runtime resources
  // The Metal Shader Converter expects resources at specific bind points
  if (res_heap_ab_ && smp_heap_ab_ && uniforms_buffer_ && shared_memory_) {
    // Determine if shared memory should be UAV (for memexport)
    bool memexport_used_vertex = vertex_shader->memexport_eM_written() != 0;
    bool memexport_used_pixel =
        pixel_shader && (pixel_shader->memexport_eM_written() != 0);
    bool shared_memory_is_uav = memexport_used_vertex || memexport_used_pixel;

    // Determine primitive type characteristics
    bool primitive_polygonal = draw_util::IsPrimitivePolygonal(regs);

    // Get viewport info for NDC transform
    draw_util::ViewportInfo viewport_info;
    auto depth_control = draw_util::GetNormalizedDepthControl(regs);
    draw_util::GetHostViewportInfo(
        regs, 1, 1,             // draw resolution scale x/y
        false,                  // origin_bottom_left (Metal uses top-left)
        render_target_width_,   // x_max
        render_target_height_,  // y_max
        false,                  // allow_reverse_z
        depth_control,          // normalized_depth_control
        false,                  // convert_z_to_float24
        false,                  // full_float24_in_0_to_1
        false,                  // pixel_shader_writes_depth
        viewport_info);

    // Update full system constants from GPU registers
    // This populates flags, NDC transform, alpha test, blend constants, etc.
    UpdateSystemConstantValues(shared_memory_is_uav, primitive_polygonal,
                               0,  // line_loop_closing_index (not using line
                                   // loops currently)
                               xenos::Endian::kNone,  // index_endian (for
                                                      // non-indexed draws)
                               viewport_info, used_texture_mask);

    // Uniforms buffer layout (4KB per CBV for alignment):
    //   b0 (offset 0):     System constants (~512 bytes)
    //   b1 (offset 4096):  Float constants (256 float4s = 4KB)
    //   b2 (offset 8192):  Bool/loop constants (~256 bytes)
    //   b3 (offset 12288): Fetch constants (768 bytes)
    //   b4 (offset 16384): Descriptor indices (unused in bindful mode)
    const size_t kCBVSize = 4096;
    uint8_t* uniforms_ptr = static_cast<uint8_t*>(uniforms_buffer_->contents());

    // b0: Copy full system constants struct to uniforms buffer
    std::memcpy(uniforms_ptr, &system_constants_,
                sizeof(DxbcShaderTranslator::SystemConstants));

    // b1: Float constants at offset 4096 (1 * kCBVSize)
    // Xbox 360 float constants: 256 float4s for vertex shader (c0-c255)
    const size_t kFloatConstantOffset = 1 * kCBVSize;
    const size_t kFloatConstantsSize =
        std::min(size_t(kCBVSize), size_t(256 * 4 * sizeof(float)));
    std::memcpy(uniforms_ptr + kFloatConstantOffset,
                &regs.values[XE_GPU_REG_SHADER_CONSTANT_000_X],
                kFloatConstantsSize);

    // b2: Bool/Loop constants at offset 8192 (2 * kCBVSize)
    const size_t kBoolLoopConstantOffset = 2 * kCBVSize;
    std::memcpy(uniforms_ptr + kBoolLoopConstantOffset,
                &regs.values[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031],
                32 * sizeof(uint32_t));  // Bool constants
    std::memcpy(uniforms_ptr + kBoolLoopConstantOffset + 32 * sizeof(uint32_t),
                &regs.values[XE_GPU_REG_SHADER_CONSTANT_LOOP_00],
                32 * sizeof(uint32_t));  // Loop constants

    // b3: Fetch constants at offset 12288 (3 * kCBVSize)
    const size_t kFetchConstantOffset = 3 * kCBVSize;
    const size_t kFetchConstantCount = 32 * 6;  // 192 DWORDs = 768 bytes
    std::memcpy(uniforms_ptr + kFetchConstantOffset,
                &regs.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0],
                kFetchConstantCount * sizeof(uint32_t));

    // DEBUG: Log system constants and interpolators on first draw
    static bool logged_debug_info = false;
    if (!logged_debug_info) {
      logged_debug_info = true;

      XELOGI("=== FIRST DRAW DEBUG INFO ===");
      XELOGI("shared_memory_is_uav: {} (vs_memexport={}, ps_memexport={})",
             shared_memory_is_uav, memexport_used_vertex, memexport_used_pixel);
      XELOGI("primitive_polygonal: {}", primitive_polygonal);

      // Dump full uniforms buffer
      DumpUniformsBuffer();

      // Log interpolator info
      LogInterpolators(vertex_shader, pixel_shader);

      // Log shared memory info
      MTL::Buffer* sm = shared_memory_->GetBuffer();
      if (sm) {
        XELOGI("Shared memory GPU addr=0x{:016X} size={}MB", sm->gpuAddress(),
               sm->length() / (1024 * 1024));
      }

      // Log top-level AB layout
      XELOGI("Top-level AB entries:");
      XELOGI("  [0-3] SRV: res_heap @ 0x{:016X}", res_heap_ab_->gpuAddress());
      XELOGI("  [5-8] UAV: res_heap @ 0x{:016X}", res_heap_ab_->gpuAddress());
      XELOGI("  [9] Samplers: smp_heap @ 0x{:016X}",
             smp_heap_ab_->gpuAddress());
      XELOGI("  [10-13] CBV: cbv_heap @ 0x{:016X}", cbv_heap_ab_->gpuAddress());
      XELOGI("  CBV heap entries point to uniforms @ 0x{:016X} (stride={})",
             uniforms_buffer_->gpuAddress(), kCBVSize);
      XELOGI("=== END FIRST DRAW DEBUG INFO ===");
    }

    // Populate resource descriptor heap slot 0 with shared memory buffer
    // This allows shaders to read vertex data via vfetch instructions
    auto* res_entries =
        reinterpret_cast<IRDescriptorTableEntry*>(res_heap_ab_->contents());
    MTL::Buffer* shared_mem_buffer = shared_memory_->GetBuffer();
    if (shared_mem_buffer) {
      // Set shared memory as SRV at t0 for vertex buffer fetching
      IRDescriptorTableSetBuffer(&res_entries[0],
                                 shared_mem_buffer->gpuAddress(),
                                 shared_mem_buffer->length());

      // Make shared memory resident for GPU access
      current_render_encoder_->useResource(shared_mem_buffer,
                                           MTL::ResourceUsageRead);
    }

    std::vector<bool> srv_slot_written(kResourceHeapSlotCount, false);
    std::vector<MTL::Texture*> textures_for_encoder;
    textures_for_encoder.reserve(8);

    auto track_texture_usage = [&](MTL::Texture* texture) {
      if (!texture) {
        return;
      }
      if (std::find(textures_for_encoder.begin(), textures_for_encoder.end(),
                    texture) == textures_for_encoder.end()) {
        textures_for_encoder.push_back(texture);
      }
    };

    auto bind_shader_textures = [&](MetalShader* shader) {
      if (!shader || !texture_cache_) {
        return;
      }
      const auto& shader_texture_bindings =
          shader->GetTextureBindingsAfterTranslation();
      for (size_t binding_index = 0;
           binding_index < shader_texture_bindings.size(); ++binding_index) {
        uint32_t srv_slot = 1 + static_cast<uint32_t>(binding_index);
        if (srv_slot >= kResourceHeapSlotCount) {
          continue;
        }
        if (srv_slot_written[srv_slot]) {
          continue;
        }
        srv_slot_written[srv_slot] = true;
        const auto& binding = shader_texture_bindings[binding_index];
        MTL::Texture* texture = texture_cache_->GetTextureForBinding(
            binding.fetch_constant, binding.dimension, binding.is_signed);
        if (!texture) {
          texture = null_texture_;
          if (!logged_missing_texture_warning_) {
            XELOGW(
                "Metal: Missing texture for fetch constant {} (dimension {} "
                "signed {})",
                binding.fetch_constant, static_cast<int>(binding.dimension),
                binding.is_signed);
            logged_missing_texture_warning_ = true;
          }
        }
        if (texture) {
          IRDescriptorTableSetTexture(&res_entries[srv_slot], texture, 0.0f, 0);
          track_texture_usage(texture);
        }
      }
    };

    bind_shader_textures(vertex_shader);
    bind_shader_textures(pixel_shader);

    for (size_t slot = 1; slot < kResourceHeapSlotCount; ++slot) {
      if (!srv_slot_written[slot] && null_texture_) {
        IRDescriptorTableSetTexture(&res_entries[slot], null_texture_, 0.0f, 0);
      }
    }

    auto* sampler_entries =
        reinterpret_cast<IRDescriptorTableEntry*>(smp_heap_ab_->contents());
    std::vector<bool> sampler_slot_written(kSamplerHeapSlotCount, false);

    auto bind_shader_samplers = [&](MetalShader* shader) {
      if (!shader || !texture_cache_) {
        return;
      }
      const auto& sampler_bindings =
          shader->GetSamplerBindingsAfterTranslation();
      for (size_t sampler_index = 0; sampler_index < sampler_bindings.size();
           ++sampler_index) {
        if (sampler_index >= kSamplerHeapSlotCount) {
          continue;
        }
        if (sampler_slot_written[sampler_index]) {
          continue;
        }
        auto parameters = texture_cache_->GetSamplerParameters(
            sampler_bindings[sampler_index]);
        MTL::SamplerState* sampler_state =
            texture_cache_->GetOrCreateSampler(parameters);
        if (!sampler_state) {
          sampler_state = null_sampler_;
        }
        if (sampler_state) {
          IRDescriptorTableSetSampler(&sampler_entries[sampler_index],
                                      sampler_state, 0.0f);
          sampler_slot_written[sampler_index] = true;
        }
      }
    };

    bind_shader_samplers(vertex_shader);
    bind_shader_samplers(pixel_shader);

    for (MTL::Texture* texture : textures_for_encoder) {
      current_render_encoder_->useResource(texture, MTL::ResourceUsageRead);
    }

    // Mark all indirectly-referenced resources as used
    // The shader accesses these via GPU VA pointers in argument buffers,
    // so Metal needs to know they're in use for proper scheduling
    current_render_encoder_->useResource(null_buffer_, MTL::ResourceUsageRead);
    current_render_encoder_->useResource(null_texture_, MTL::ResourceUsageRead);
    // Note: null_sampler_ doesn't need useResource - samplers don't have GPU VA
    current_render_encoder_->useResource(res_heap_ab_, MTL::ResourceUsageRead);
    current_render_encoder_->useResource(smp_heap_ab_, MTL::ResourceUsageRead);
    current_render_encoder_->useResource(top_level_ab_, MTL::ResourceUsageRead);
    current_render_encoder_->useResource(cbv_heap_ab_, MTL::ResourceUsageRead);
    current_render_encoder_->useResource(uniforms_buffer_,
                                         MTL::ResourceUsageRead);

    // Populate top-level argument buffer with GPU addresses pointing to
    // descriptor tables IMPORTANT: The top-level AB contains uint64_t GPU
    // addresses, NOT IRDescriptorTableEntry! The layout (from
    // metal_shader_converter.cc root signature) is:
    //   0-3:  SRV spaces 0-3 (descriptor tables)
    //   4:    SRV space 10 (hull shader)
    //   5-8:  UAV spaces 0-3 (descriptor tables)
    //   9:    Samplers space 0
    //   10-13: CBV spaces 0-3
    auto* top_level_ptrs =
        reinterpret_cast<uint64_t*>(top_level_ab_->contents());

    // Clear all entries first
    std::memset(top_level_ptrs, 0, 20 * sizeof(uint64_t));

    // SRV entries 0-3: Point to resource heap (shared memory is at slot 0)
    for (int i = 0; i < 4; i++) {
      top_level_ptrs[i] = res_heap_ab_->gpuAddress();
    }

    // SRV entry 4 (hull): Also resource heap
    top_level_ptrs[4] = res_heap_ab_->gpuAddress();

    // UAV entries 5-8: Point to resource heap (for memexport)
    for (int i = 5; i < 9; i++) {
      top_level_ptrs[i] = res_heap_ab_->gpuAddress();
    }

    // Sampler entry 9: Point to sampler heap
    top_level_ptrs[9] = smp_heap_ab_->gpuAddress();

    // CBV binding: Root param 10 = CBV space 0, which contains b0, b1, b2, b3
    // We need a descriptor heap with 4 entries, one for each constant buffer
    // Populate the CBV heap with pointers to each constant buffer in uniforms
    auto* cbv_entries =
        reinterpret_cast<IRDescriptorTableEntry*>(cbv_heap_ab_->contents());

    // b0 = system constants at offset 0
    IRDescriptorTableSetBuffer(&cbv_entries[0],
                               uniforms_buffer_->gpuAddress() + 0 * kCBVSize,
                               kCBVSize);
    // b1 = float constants at offset 4096 (1 * kCBVSize)
    IRDescriptorTableSetBuffer(&cbv_entries[1],
                               uniforms_buffer_->gpuAddress() + 1 * kCBVSize,
                               kCBVSize);
    // b2 = bool/loop constants at offset 8192 (2 * kCBVSize)
    IRDescriptorTableSetBuffer(&cbv_entries[2],
                               uniforms_buffer_->gpuAddress() + 2 * kCBVSize,
                               kCBVSize);
    // b3 = fetch constants at offset 12288 (3 * kCBVSize)
    IRDescriptorTableSetBuffer(&cbv_entries[3],
                               uniforms_buffer_->gpuAddress() + 3 * kCBVSize,
                               kCBVSize);
    // b4 = descriptor indices at offset 16384 (4 * kCBVSize) - unused in
    // bindful mode but we need to set it to avoid out-of-bounds descriptor
    // access
    IRDescriptorTableSetBuffer(&cbv_entries[4],
                               uniforms_buffer_->gpuAddress() + 4 * kCBVSize,
                               kCBVSize);
    // Sentinel entry at index 5 - shader reads one past declared count
    IRDescriptorTableSetBuffer(&cbv_entries[5], null_buffer_->gpuAddress(),
                               4096);  // Point to null buffer

    // Debug: Log CBV heap setup
    static bool logged_cbv_setup = false;
    if (!logged_cbv_setup) {
      logged_cbv_setup = true;
      XELOGI("GPU DEBUG: CBV heap setup (cbv_heap_ab_ @ 0x{:016X}, {} bytes):",
             cbv_heap_ab_->gpuAddress(), cbv_heap_ab_->length());
      for (int i = 0; i < 6; i++) {
        XELOGI(
            "  CBV[{}]: gpuVA=0x{:016X}, textureID=0x{:016X}, "
            "metadata=0x{:016X}",
            i, cbv_entries[i].gpuVA, cbv_entries[i].textureViewID,
            cbv_entries[i].metadata);
      }
    }

    // Debug: Verify fetch constants in uniforms buffer
    static bool logged_cbv = false;
    if (!logged_cbv) {
      logged_cbv = true;
      const uint32_t* fc_in_uniforms = reinterpret_cast<const uint32_t*>(
          uniforms_ptr + kFetchConstantOffset);
      XELOGI("GPU DEBUG: Fetch constants in uniforms buffer at offset {}:",
             kFetchConstantOffset);
      XELOGI("  FC[0]: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
             fc_in_uniforms[0], fc_in_uniforms[1], fc_in_uniforms[2],
             fc_in_uniforms[3], fc_in_uniforms[4], fc_in_uniforms[5]);
      XELOGI("  CBV[3] GPU addr: 0x{:016X}, size: {}",
             uniforms_buffer_->gpuAddress() + 3 * kCBVSize, kCBVSize);
    }

    // Top-level entry 10 points to the CBV heap (descriptor table for CBV space
    // 0) Entry 10 = CBV space 0, containing b0-b4
    top_level_ptrs[10] = cbv_heap_ab_->gpuAddress();

    // Entries 11-13 are CBV spaces 1-3 (unused by Xenia, but point to CBV heap)
    for (int i = 11; i < 14; i++) {
      top_level_ptrs[i] = cbv_heap_ab_->gpuAddress();
    }

    // Bind argument buffer at IR Converter bind point 2
    // We use explicit resource layout mode with a root signature, so:
    // - Bind point 2 (kIRArgumentBufferBindPoint): Top-level argument buffer
    //   containing GPU addresses of descriptor tables (one per root parameter)
    // - Bind points 0 and 1 are for "dynamic resources" mode (bindless) which
    //   we don't use when we have a root signature
    current_render_encoder_->setVertexBuffer(top_level_ab_, 0,
                                             kIRArgumentBufferBindPoint);
    current_render_encoder_->setFragmentBuffer(top_level_ab_, 0,
                                               kIRArgumentBufferBindPoint);

    // Index 4 and 5 are handled by IRRuntimeDrawPrimitives which sets:
    // - Index 4: Draw arguments (vertex count, instance count, etc.)
    // - Index 5: Index type info (kIRNonIndexedDraw for non-indexed draws)
    // Note: Uniforms (constant buffers) are accessed via the top-level
    // argument buffer, NOT directly at bind point 5!

    XELOGD("Bound IR Converter resources: res_heap, smp_heap, top_level_ab");
  }

  // Convert primitive type and handle special cases
  MTL::PrimitiveType mtl_primitive;
  uint32_t draw_vertex_count = index_count;

  switch (primitive_type) {
    case xenos::PrimitiveType::kTriangleList:
      mtl_primitive = MTL::PrimitiveTypeTriangle;
      break;
    case xenos::PrimitiveType::kTriangleStrip:
      mtl_primitive = MTL::PrimitiveTypeTriangleStrip;
      break;
    case xenos::PrimitiveType::kTriangleFan:
      // Metal doesn't support triangle fans - use triangle strip as
      // approximation
      // TODO: Proper fan to strip/list conversion requires index buffer
      mtl_primitive = MTL::PrimitiveTypeTriangleStrip;
      // Keep original vertex count - shader reads via vfetch with vertex ID
      break;
    case xenos::PrimitiveType::kRectangleList:
      // Rectangle list: Xbox 360 specific primitive for 2D quads
      // Each rect is 3 vertices: top-left, bottom-left, bottom-right
      // The 4th vertex (top-right) is implied
      // For now, use triangle strip which is closest approximation
      // TODO: Proper rectangle expansion requires geometry shader or index
      // buffer
      mtl_primitive = MTL::PrimitiveTypeTriangleStrip;
      // Keep original vertex count
      break;
    case xenos::PrimitiveType::kLineList:
      mtl_primitive = MTL::PrimitiveTypeLine;
      break;
    case xenos::PrimitiveType::kLineStrip:
      mtl_primitive = MTL::PrimitiveTypeLineStrip;
      break;
    case xenos::PrimitiveType::kPointList:
      mtl_primitive = MTL::PrimitiveTypePoint;
      break;
    case xenos::PrimitiveType::kQuadList:
      // Quad list: each quad is 4 vertices, convert to 2 triangles
      mtl_primitive = MTL::PrimitiveTypeTriangle;
      draw_vertex_count = (index_count / 4) * 6;
      break;
    default:
      XELOGW("Unsupported primitive type: {}",
             static_cast<int>(primitive_type));
      mtl_primitive = MTL::PrimitiveTypeTriangle;
      break;
  }

  // Issue draw using IR Runtime helper which properly sets up draw arguments
  // The runtime expects:
  // - Bind point 4: IRRuntimeDrawParams (vertex count, instance count, etc.)
  // - Bind point 5: Index type (kIRNonIndexedDraw = 0 for non-indexed)
  // TODO: For rectangle list and triangle fan, we need to set up proper
  // vertex transformation in the shader or use index buffers to expand.
  if (index_buffer_info && index_buffer_info->guest_base) {
    // Indexed draw - use IRRuntimeDrawPrimitives for now (no index buffer yet)
    // TODO: Setup index buffer properly with IRRuntimeDrawIndexedPrimitives
    IRRuntimeDrawPrimitives(current_render_encoder_, mtl_primitive,
                            NS::UInteger(0), NS::UInteger(draw_vertex_count));
  } else {
    // Non-indexed draw
    IRRuntimeDrawPrimitives(current_render_encoder_, mtl_primitive,
                            NS::UInteger(0), NS::UInteger(draw_vertex_count));
  }

  XELOGI("IssueDraw: {} vertices (expanded to {}), primitive type {}",
         index_count, draw_vertex_count, static_cast<int>(primitive_type));

  return true;
}

bool MetalCommandProcessor::IssueCopy() {
  XELOGI("MetalCommandProcessor::IssueCopy");

  // Finish any in-flight rendering so the render target contents are
  // available to the render target cache, similar to D3D12's
  // D3D12CommandProcessor::IssueCopy.
  if (current_render_encoder_) {
    current_render_encoder_->endEncoding();
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }

  if (current_command_buffer_) {
    current_command_buffer_->commit();
    current_command_buffer_->waitUntilCompleted();
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }

  if (!render_target_cache_) {
    XELOGW("MetalCommandProcessor::IssueCopy - No render target cache");
    return true;
  }

  uint32_t written_address = 0;
  uint32_t written_length = 0;

  if (!render_target_cache_->Resolve(*memory_, written_address,
                                     written_length)) {
    XELOGE("MetalCommandProcessor::IssueCopy - Resolve failed");
    return false;
  }

  if (!written_length) {
    XELOGI("MetalCommandProcessor::IssueCopy - Resolve wrote no data");
    return true;
  }

  XELOGI("MetalCommandProcessor::IssueCopy - Completed: {} bytes at 0x{:08X}",
         written_length, written_address);

  // Track this resolved region so the trace player can avoid overwriting it
  // with stale MemoryRead commands from the trace file.
  MarkResolvedMemory(written_address, written_length);

  // Invalidate the shared memory range so the MetalSharedMemory buffer is
  // updated before textures sampling this region are uploaded.
  if (shared_memory_) {
    shared_memory_->MemoryInvalidationCallback(written_address, written_length,
                                               true);
  }

  return true;
}

void MetalCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  CommandProcessor::WriteRegister(index, value);

  if (index >= XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 &&
      index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {
    if (texture_cache_) {
      texture_cache_->TextureFetchConstantWritten(
          (index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) / 6);
    }
  }
}

void MetalCommandProcessor::BeginCommandBuffer() {
  if (current_command_buffer_) {
    return;  // Already have an active command buffer
  }

  // Note: commandBuffer() returns an autoreleased object, we must retain it
  current_command_buffer_ = command_queue_->commandBuffer();
  if (!current_command_buffer_) {
    XELOGE("Failed to create command buffer");
    return;
  }
  current_command_buffer_
      ->retain();  // Retain since we store in member variable
  current_command_buffer_->setLabel(
      NS::String::string("XeniaCommandBuffer", NS::UTF8StringEncoding));

  // Obtain the render pass descriptor. Prefer the one provided by
  // MetalRenderTargetCache (host render-target path), falling back to the
  // legacy descriptor if needed.
  MTL::RenderPassDescriptor* pass_descriptor = render_pass_descriptor_;
  if (render_target_cache_) {
    MTL::RenderPassDescriptor* cache_desc =
        render_target_cache_->GetRenderPassDescriptor(1);
    if (cache_desc) {
      pass_descriptor = cache_desc;
    }
  }

  if (!pass_descriptor) {
    XELOGE("BeginCommandBuffer: No render pass descriptor available");
    return;
  }

  // Create render encoder
  // Note: renderCommandEncoder() returns an autoreleased object, we must retain
  // it
  current_render_encoder_ =
      current_command_buffer_->renderCommandEncoder(pass_descriptor);
  if (!current_render_encoder_) {
    XELOGE("Failed to create render command encoder");
    return;
  }
  current_render_encoder_
      ->retain();  // Retain since we store in member variable
  current_render_encoder_->setLabel(
      NS::String::string("XeniaRenderEncoder", NS::UTF8StringEncoding));

  // Derive viewport/scissor from the actual bound render target rather than
  // a hard-coded 1280x720. Prefer color RT 0 from the MetalRenderTargetCache,
  // falling back to the legacy render_target_width_/height_ when needed.
  uint32_t rt_width = render_target_width_;
  uint32_t rt_height = render_target_height_;
  if (render_target_cache_) {
    MTL::Texture* rt0 = render_target_cache_->GetColorTarget(0);
    if (rt0) {
      rt_width = rt0->width();
      rt_height = rt0->height();
      XELOGI("BeginCommandBuffer: using RT0 size {}x{} for viewport/scissor",
             rt_width, rt_height);
    } else {
      XELOGI(
          "BeginCommandBuffer: no RT0 texture, falling back to {}x{} for "
          "viewport/scissor",
          rt_width, rt_height);
    }
  }

  // Set viewport
  MTL::Viewport viewport = {
      0.0, 0.0, static_cast<double>(rt_width), static_cast<double>(rt_height),
      0.0, 1.0};
  current_render_encoder_->setViewport(viewport);

  // Set scissor (must not exceed render pass dimensions)
  MTL::ScissorRect scissor = {0, 0, rt_width, rt_height};
  current_render_encoder_->setScissorRect(scissor);
}

void MetalCommandProcessor::EndCommandBuffer() {
  if (current_render_encoder_) {
    current_render_encoder_->endEncoding();
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }

  if (current_command_buffer_) {
    current_command_buffer_->commit();
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }
}

MTL::RenderPassDescriptor*
MetalCommandProcessor::GetCurrentRenderPassDescriptor() {
  return render_pass_descriptor_;
}

MTL::RenderPipelineState* MetalCommandProcessor::GetOrCreatePipelineState(
    MetalShader::MetalTranslation* vertex_translation,
    MetalShader::MetalTranslation* pixel_translation) {
  if (!vertex_translation || !vertex_translation->metal_function()) {
    XELOGE("No valid vertex shader function");
    return nullptr;
  }

  // Determine sample count and whether a depth RT is bound from the render
  // target cache, so the pipeline matches the actual render pass.
  uint32_t sample_count = 1;
  bool has_depth_rt = false;
  if (render_target_cache_) {
    MTL::Texture* rt0 = render_target_cache_->GetColorTarget(0);
    if (rt0 && rt0->sampleCount() > 0) {
      sample_count = rt0->sampleCount();
    }
    MTL::Texture* depth_tex = render_target_cache_->GetDepthTarget();
    if (depth_tex && depth_tex->sampleCount() > 0) {
      has_depth_rt = true;
      if (sample_count == 1) {
        sample_count = depth_tex->sampleCount();
      }
    }
  }

  // Create pipeline key from shader pointers and sample count so pipelines
  // differ for 1x vs MSAA configurations.
  uint64_t key = reinterpret_cast<uint64_t>(vertex_translation);
  if (pixel_translation) {
    key ^= reinterpret_cast<uint64_t>(pixel_translation) * 31;
  }
  key ^= (uint64_t(sample_count) << 48);

  // Check cache
  auto it = pipeline_cache_.find(key);
  if (it != pipeline_cache_.end()) {
    return it->second;
  }

  // Create pipeline descriptor
  MTL::RenderPipelineDescriptor* desc =
      MTL::RenderPipelineDescriptor::alloc()->init();

  desc->setVertexFunction(vertex_translation->metal_function());

  if (pixel_translation && pixel_translation->metal_function()) {
    desc->setFragmentFunction(pixel_translation->metal_function());
  }

  // Set render target formats and sample count to match bound RTs.
  desc->colorAttachments()->object(0)->setPixelFormat(
      MTL::PixelFormatBGRA8Unorm);
  MTL::PixelFormat depth_stencil_format =
      has_depth_rt ? MTL::PixelFormatDepth32Float_Stencil8
                   : MTL::PixelFormatInvalid;
  desc->setDepthAttachmentPixelFormat(depth_stencil_format);
  desc->setStencilAttachmentPixelFormat(depth_stencil_format);
  desc->setSampleCount(sample_count);

  // Create pipeline state
  NS::Error* error = nullptr;
  MTL::RenderPipelineState* pipeline =
      device_->newRenderPipelineState(desc, &error);
  desc->release();

  if (!pipeline) {
    if (error) {
      XELOGE("Failed to create pipeline state: {}",
             error->localizedDescription()->utf8String());
    } else {
      XELOGE("Failed to create pipeline state (unknown error)");
    }
    return nullptr;
  }

  pipeline_cache_[key] = pipeline;

  // Log pipeline creation for debugging
  static int pipeline_count = 0;
  pipeline_count++;
  XELOGI("GPU DEBUG: Created pipeline #{} (VS={}, PS={})", pipeline_count,
         vertex_translation->metal_function() ? "valid" : "null",
         (pixel_translation && pixel_translation->metal_function()) ? "valid"
                                                                    : "null");

  return pipeline;
}

bool MetalCommandProcessor::CreateDebugPipeline() {
  if (debug_pipeline_) {
    return true;  // Already created
  }

  // Simple vertex shader that outputs position directly
  const char* vertex_source = R"(
    #include <metal_stdlib>
    using namespace metal;
    
    struct VertexOut {
      float4 position [[position]];
      float4 color;
    };
    
    vertex VertexOut debug_vertex(uint vid [[vertex_id]]) {
      // Output a full-screen quad using 6 vertices (2 triangles)
      float2 positions[6] = {
        float2(-1.0, -1.0),
        float2( 1.0, -1.0),
        float2(-1.0,  1.0),
        float2(-1.0,  1.0),
        float2( 1.0, -1.0),
        float2( 1.0,  1.0)
      };
      
      float4 colors[6] = {
        float4(1.0, 0.0, 0.0, 1.0),  // Red
        float4(0.0, 1.0, 0.0, 1.0),  // Green
        float4(0.0, 0.0, 1.0, 1.0),  // Blue
        float4(0.0, 0.0, 1.0, 1.0),  // Blue
        float4(0.0, 1.0, 0.0, 1.0),  // Green
        float4(1.0, 1.0, 0.0, 1.0)   // Yellow
      };
      
      VertexOut out;
      out.position = float4(positions[vid], 0.0, 1.0);
      out.color = colors[vid];
      return out;
    }
  )";

  const char* fragment_source = R"(
    #include <metal_stdlib>
    using namespace metal;
    
    struct VertexOut {
      float4 position [[position]];
      float4 color;
    };
    
    fragment float4 debug_fragment(VertexOut in [[stage_in]]) {
      return in.color;
    }
  )";

  NS::Error* error = nullptr;

  // Compile vertex shader
  MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
  MTL::Library* vertex_lib = device_->newLibrary(
      NS::String::string(vertex_source, NS::UTF8StringEncoding), options,
      &error);
  if (!vertex_lib) {
    XELOGE("Failed to compile debug vertex shader: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    options->release();
    return false;
  }

  MTL::Library* fragment_lib = device_->newLibrary(
      NS::String::string(fragment_source, NS::UTF8StringEncoding), options,
      &error);
  if (!fragment_lib) {
    XELOGE("Failed to compile debug fragment shader: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    vertex_lib->release();
    options->release();
    return false;
  }
  options->release();

  MTL::Function* vertex_fn = vertex_lib->newFunction(
      NS::String::string("debug_vertex", NS::UTF8StringEncoding));
  MTL::Function* fragment_fn = fragment_lib->newFunction(
      NS::String::string("debug_fragment", NS::UTF8StringEncoding));

  if (!vertex_fn || !fragment_fn) {
    XELOGE("Failed to get debug shader functions");
    if (vertex_fn) vertex_fn->release();
    if (fragment_fn) fragment_fn->release();
    vertex_lib->release();
    fragment_lib->release();
    return false;
  }

  MTL::RenderPipelineDescriptor* desc =
      MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(vertex_fn);
  desc->setFragmentFunction(fragment_fn);
  desc->colorAttachments()->object(0)->setPixelFormat(
      MTL::PixelFormatBGRA8Unorm);
  desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float_Stencil8);
  desc->setStencilAttachmentPixelFormat(MTL::PixelFormatDepth32Float_Stencil8);

  debug_pipeline_ = device_->newRenderPipelineState(desc, &error);

  desc->release();
  vertex_fn->release();
  fragment_fn->release();
  vertex_lib->release();
  fragment_lib->release();

  if (!debug_pipeline_) {
    XELOGE("Failed to create debug pipeline: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    return false;
  }

  XELOGI("Debug pipeline created successfully");
  return true;
}

void MetalCommandProcessor::DrawDebugQuad() {
  if (!debug_pipeline_) {
    if (!CreateDebugPipeline()) {
      return;
    }
  }

  BeginCommandBuffer();

  current_render_encoder_->setRenderPipelineState(debug_pipeline_);
  current_render_encoder_->drawPrimitives(MTL::PrimitiveTypeTriangle,
                                          NS::UInteger(0), NS::UInteger(6));

  XELOGI("Drew debug quad");
}

bool MetalCommandProcessor::CreateIRConverterBuffers() {
  // Buffer creation is now done inline in SetupContext
  // This function exists for header compatibility
  return res_heap_ab_ && smp_heap_ab_ && uniforms_buffer_;
}

void MetalCommandProcessor::PopulateIRConverterBuffers() {
  if (!res_heap_ab_ || !smp_heap_ab_ || !uniforms_buffer_ || !shared_memory_) {
    return;
  }

  // Get shared memory buffer for vertex data fetching
  MTL::Buffer* shared_mem_buffer = shared_memory_->GetBuffer();
  if (!shared_mem_buffer) {
    XELOGW("PopulateIRConverterBuffers: No shared memory buffer available");
    return;
  }

  // Populate resource descriptor heap slot 0 with shared memory buffer
  // Xbox 360 shaders use vfetch instructions that read from this buffer
  IRDescriptorTableEntry* res_heap =
      static_cast<IRDescriptorTableEntry*>(res_heap_ab_->contents());

  // Set slot 0 (t0) to shared memory for vertex buffer fetching
  // The metadata encodes buffer size for bounds checking
  uint64_t shared_mem_gpu_addr = shared_mem_buffer->gpuAddress();
  uint64_t shared_mem_size = shared_mem_buffer->length();

  // Use IRDescriptorTableSetBuffer to properly encode the descriptor
  // metadata = buffer size in low 32 bits for bounds checking
  IRDescriptorTableSetBuffer(&res_heap[0], shared_mem_gpu_addr,
                             shared_mem_size);

  // Populate uniforms buffer with system constants and fetch constants
  // Layout matches DxbcShaderTranslator::SystemConstants (b0)
  uint8_t* uniforms = static_cast<uint8_t*>(uniforms_buffer_->contents());

  // For now, populate minimal system constants for passthrough rendering
  // This structure needs to match what the translated shaders expect
  struct MinimalSystemConstants {
    uint32_t flags;                      // 0x00
    float tessellation_factor_range[2];  // 0x04
    uint32_t line_loop_closing_index;    // 0x0C

    uint32_t vertex_index_endian;  // 0x10
    uint32_t vertex_index_offset;  // 0x14
    uint32_t vertex_index_min;     // 0x18
    uint32_t vertex_index_max;     // 0x1C

    float user_clip_planes[6][4];  // 0x20 - 6 clip planes * 4 floats = 96 bytes

    float ndc_scale[3];               // 0x80
    float point_vertex_diameter_min;  // 0x8C

    float ndc_offset[3];              // 0x90
    float point_vertex_diameter_max;  // 0x9C
  };

  MinimalSystemConstants* sys_const =
      reinterpret_cast<MinimalSystemConstants*>(uniforms);

  // Initialize to zero
  std::memset(sys_const, 0, sizeof(MinimalSystemConstants));

  // Set passthrough NDC transform (identity)
  sys_const->ndc_scale[0] = 1.0f;
  sys_const->ndc_scale[1] = 1.0f;
  sys_const->ndc_scale[2] = 1.0f;
  sys_const->ndc_offset[0] = 0.0f;
  sys_const->ndc_offset[1] = 0.0f;
  sys_const->ndc_offset[2] = 0.0f;

  // Set reasonable vertex index bounds
  sys_const->vertex_index_min = 0;
  sys_const->vertex_index_max = 0xFFFFFFFF;

  // Copy fetch constants from register file (b3 in DXBC)
  // Fetch constants start at register XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0
  // There are 32 fetch constants, each 6 DWORDs = 192 DWORDs total = 768 bytes
  const uint32_t* regs = register_file_->values;
  const size_t kFetchConstantOffset = 512;    // After system constants (b0)
  const size_t kFetchConstantCount = 32 * 6;  // 32 fetch constants * 6 DWORDs

  std::memcpy(uniforms + kFetchConstantOffset,
              &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0],
              kFetchConstantCount * sizeof(uint32_t));

  // Bind IR Converter runtime buffers to render encoder
  if (current_render_encoder_) {
    // Bind resource descriptor heap at index 0 (kIRDescriptorHeapBindPoint)
    current_render_encoder_->setVertexBuffer(res_heap_ab_, 0,
                                             kIRDescriptorHeapBindPoint);
    current_render_encoder_->setFragmentBuffer(res_heap_ab_, 0,
                                               kIRDescriptorHeapBindPoint);

    // Bind sampler heap at index 1 (kIRSamplerHeapBindPoint)
    current_render_encoder_->setVertexBuffer(smp_heap_ab_, 0,
                                             kIRSamplerHeapBindPoint);
    current_render_encoder_->setFragmentBuffer(smp_heap_ab_, 0,
                                               kIRSamplerHeapBindPoint);

    // Bind uniforms at index 5 (kIRArgumentBufferUniformsBindPoint)
    current_render_encoder_->setVertexBuffer(
        uniforms_buffer_, 0, kIRArgumentBufferUniformsBindPoint);
    current_render_encoder_->setFragmentBuffer(
        uniforms_buffer_, 0, kIRArgumentBufferUniformsBindPoint);

    // Make shared memory resident for GPU access
    current_render_encoder_->useResource(shared_mem_buffer,
                                         MTL::ResourceUsageRead);
  }
}

void MetalCommandProcessor::UpdateSystemConstantValues(
    bool shared_memory_is_uav, bool primitive_polygonal,
    uint32_t line_loop_closing_index, xenos::Endian index_endian,
    const draw_util::ViewportInfo& viewport_info, uint32_t used_texture_mask) {
  const RegisterFile& regs = *register_file_;
  auto pa_cl_clip_cntl = regs.Get<reg::PA_CL_CLIP_CNTL>();
  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  auto rb_alpha_ref = regs.Get<float>(XE_GPU_REG_RB_ALPHA_REF);
  auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  uint32_t vgt_indx_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  uint32_t vgt_max_vtx_indx = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  uint32_t vgt_min_vtx_indx = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;

  // Get color info for each render target
  reg::RB_COLOR_INFO color_infos[4];
  for (uint32_t i = 0; i < 4; ++i) {
    color_infos[i] = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[i]);
  }

  // Build flags
  uint32_t flags = 0;

  // Shared memory mode - determines whether shaders read from SRV (T0) or UAV
  // (U0)
  if (shared_memory_is_uav) {
    flags |= DxbcShaderTranslator::kSysFlag_SharedMemoryIsUAV;
  }

  // W0 division control from PA_CL_VTE_CNTL
  if (pa_cl_vte_cntl.vtx_xy_fmt) {
    flags |= DxbcShaderTranslator::kSysFlag_XYDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_z_fmt) {
    flags |= DxbcShaderTranslator::kSysFlag_ZDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_w0_fmt) {
    flags |= DxbcShaderTranslator::kSysFlag_WNotReciprocal;
  }

  // Primitive type flags
  if (primitive_polygonal) {
    flags |= DxbcShaderTranslator::kSysFlag_PrimitivePolygonal;
  }
  if (draw_util::IsPrimitiveLine(regs)) {
    flags |= DxbcShaderTranslator::kSysFlag_PrimitiveLine;
  }

  // Depth format
  if (rb_depth_info.depth_format == xenos::DepthRenderTargetFormat::kD24FS8) {
    flags |= DxbcShaderTranslator::kSysFlag_DepthFloat24;
  }

  // Alpha test - encode compare function in flags
  xenos::CompareFunction alpha_test_function =
      rb_colorcontrol.alpha_test_enable ? rb_colorcontrol.alpha_func
                                        : xenos::CompareFunction::kAlways;
  flags |= uint32_t(alpha_test_function)
           << DxbcShaderTranslator::kSysFlag_AlphaPassIfLess_Shift;

  // Gamma conversion flags for render targets
  for (uint32_t i = 0; i < 4; ++i) {
    if (color_infos[i].color_format ==
        xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA) {
      flags |= DxbcShaderTranslator::kSysFlag_ConvertColor0ToGamma << i;
    }
  }

  system_constants_.flags = flags;

  // Tessellation factor range
  float tessellation_factor_min =
      regs.Get<float>(XE_GPU_REG_VGT_HOS_MIN_TESS_LEVEL) + 1.0f;
  float tessellation_factor_max =
      regs.Get<float>(XE_GPU_REG_VGT_HOS_MAX_TESS_LEVEL) + 1.0f;
  system_constants_.tessellation_factor_range_min = tessellation_factor_min;
  system_constants_.tessellation_factor_range_max = tessellation_factor_max;

  // Line loop closing index
  system_constants_.line_loop_closing_index = line_loop_closing_index;

  // Vertex index configuration
  system_constants_.vertex_index_endian = index_endian;
  system_constants_.vertex_index_offset = vgt_indx_offset;
  system_constants_.vertex_index_min = vgt_min_vtx_indx;
  system_constants_.vertex_index_max = vgt_max_vtx_indx;

  // User clip planes (when not CLIP_DISABLE)
  if (!pa_cl_clip_cntl.clip_disable) {
    float* user_clip_plane_write_ptr = system_constants_.user_clip_planes[0];
    uint32_t user_clip_planes_remaining = pa_cl_clip_cntl.ucp_ena;
    uint32_t user_clip_plane_index;
    while (xe::bit_scan_forward(user_clip_planes_remaining,
                                &user_clip_plane_index)) {
      user_clip_planes_remaining &= ~(UINT32_C(1) << user_clip_plane_index);
      const float* user_clip_plane_regs = reinterpret_cast<const float*>(
          &regs.values[XE_GPU_REG_PA_CL_UCP_0_X + user_clip_plane_index * 4]);
      std::memcpy(user_clip_plane_write_ptr, user_clip_plane_regs,
                  4 * sizeof(float));
      user_clip_plane_write_ptr += 4;
    }
  }

  // NDC scale and offset from viewport info
  for (uint32_t i = 0; i < 3; ++i) {
    system_constants_.ndc_scale[i] = viewport_info.ndc_scale[i];
    system_constants_.ndc_offset[i] = viewport_info.ndc_offset[i];
  }

  // Point size parameters
  if (vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList) {
    auto pa_su_point_minmax = regs.Get<reg::PA_SU_POINT_MINMAX>();
    auto pa_su_point_size = regs.Get<reg::PA_SU_POINT_SIZE>();
    system_constants_.point_vertex_diameter_min =
        float(pa_su_point_minmax.min_size) * (2.0f / 16.0f);
    system_constants_.point_vertex_diameter_max =
        float(pa_su_point_minmax.max_size) * (2.0f / 16.0f);
    system_constants_.point_constant_diameter[0] =
        float(pa_su_point_size.width) * (2.0f / 16.0f);
    system_constants_.point_constant_diameter[1] =
        float(pa_su_point_size.height) * (2.0f / 16.0f);
    // Screen to NDC radius conversion
    system_constants_.point_screen_diameter_to_ndc_radius[0] =
        1.0f / std::max(viewport_info.xy_extent[0], uint32_t(1));
    system_constants_.point_screen_diameter_to_ndc_radius[1] =
        1.0f / std::max(viewport_info.xy_extent[1], uint32_t(1));
  }

  // Texture signedness / gamma and resolved flags (mirror D3D12 logic).
  if (texture_cache_ && used_texture_mask) {
    uint32_t textures_resolved = 0;
    uint32_t textures_remaining = used_texture_mask;
    uint32_t texture_index;
    while (xe::bit_scan_forward(textures_remaining, &texture_index)) {
      textures_remaining &= ~(uint32_t(1) << texture_index);
      uint32_t& texture_signs_uint =
          system_constants_.texture_swizzled_signs[texture_index >> 2];
      uint32_t texture_signs_shift = (texture_index & 3) * 8;
      uint8_t texture_signs =
          texture_cache_->GetActiveTextureSwizzledSigns(texture_index);
      uint32_t texture_signs_shifted = uint32_t(texture_signs)
                                       << texture_signs_shift;
      uint32_t texture_signs_mask = uint32_t(0xFF) << texture_signs_shift;
      texture_signs_uint =
          (texture_signs_uint & ~texture_signs_mask) | texture_signs_shifted;
      textures_resolved |=
          uint32_t(texture_cache_->IsActiveTextureResolved(texture_index))
          << texture_index;
    }
    system_constants_.textures_resolved = textures_resolved;
  }

  // Sample count log2 for alpha to mask
  uint32_t sample_count_log2_x =
      rb_surface_info.msaa_samples >= xenos::MsaaSamples::k4X ? 1 : 0;
  uint32_t sample_count_log2_y =
      rb_surface_info.msaa_samples >= xenos::MsaaSamples::k2X ? 1 : 0;
  system_constants_.sample_count_log2[0] = sample_count_log2_x;
  system_constants_.sample_count_log2[1] = sample_count_log2_y;

  // Alpha test reference
  system_constants_.alpha_test_reference = rb_alpha_ref;

  // Alpha to mask
  uint32_t alpha_to_mask = rb_colorcontrol.alpha_to_mask_enable
                               ? (rb_colorcontrol.value >> 24) | (1 << 8)
                               : 0;
  system_constants_.alpha_to_mask = alpha_to_mask;

  // Color exponent bias
  for (uint32_t i = 0; i < 4; ++i) {
    int32_t color_exp_bias = color_infos[i].color_exp_bias;
    auto color_exp_bias_scale = xe::memory::Reinterpret<float>(
        int32_t(0x3F800000 + (color_exp_bias << 23)));
    system_constants_.color_exp_bias[i] = color_exp_bias_scale;
  }

  // Blend constants (used by EDRAM and for host blending)
  system_constants_.edram_blend_constant[0] =
      regs.Get<float>(XE_GPU_REG_RB_BLEND_RED);
  system_constants_.edram_blend_constant[1] =
      regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN);
  system_constants_.edram_blend_constant[2] =
      regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE);
  system_constants_.edram_blend_constant[3] =
      regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA);

  system_constants_dirty_ = true;
}

void MetalCommandProcessor::DumpUniformsBuffer() {
  if (!uniforms_buffer_) {
    XELOGI("BUFFER DUMP: uniforms_buffer_ is null");
    return;
  }

  const uint8_t* data =
      static_cast<const uint8_t*>(uniforms_buffer_->contents());
  const size_t kCBVSize = 4096;

  XELOGI("=== UNIFORMS BUFFER DUMP ===");
  XELOGI("Buffer GPU address: 0x{:016X}, size: {} bytes",
         uniforms_buffer_->gpuAddress(), uniforms_buffer_->length());

  // Dump system constants (b0) - first 256 bytes
  XELOGI("--- b0: System Constants (offset 0) ---");
  const auto* sys =
      reinterpret_cast<const DxbcShaderTranslator::SystemConstants*>(data);
  XELOGI("  flags: 0x{:08X}", sys->flags);
  XELOGI("  tessellation_factor_range: [{}, {}]",
         sys->tessellation_factor_range_min,
         sys->tessellation_factor_range_max);
  XELOGI("  line_loop_closing_index: {}", sys->line_loop_closing_index);
  XELOGI("  vertex_index_endian: {}",
         static_cast<uint32_t>(sys->vertex_index_endian));
  XELOGI("  vertex_index_offset: {}", sys->vertex_index_offset);
  XELOGI("  vertex_index_min/max: [{}, {}]", sys->vertex_index_min,
         sys->vertex_index_max);
  XELOGI("  ndc_scale: [{}, {}, {}]", sys->ndc_scale[0], sys->ndc_scale[1],
         sys->ndc_scale[2]);
  XELOGI("  ndc_offset: [{}, {}, {}]", sys->ndc_offset[0], sys->ndc_offset[1],
         sys->ndc_offset[2]);
  XELOGI("  point_vertex_diameter: [{}, {}]", sys->point_vertex_diameter_min,
         sys->point_vertex_diameter_max);
  XELOGI("  alpha_test_reference: {}", sys->alpha_test_reference);
  XELOGI("  alpha_to_mask: 0x{:08X}", sys->alpha_to_mask);
  XELOGI("  color_exp_bias: [{}, {}, {}, {}]", sys->color_exp_bias[0],
         sys->color_exp_bias[1], sys->color_exp_bias[2],
         sys->color_exp_bias[3]);
  XELOGI("  edram_blend_constant: [{}, {}, {}, {}]",
         sys->edram_blend_constant[0], sys->edram_blend_constant[1],
         sys->edram_blend_constant[2], sys->edram_blend_constant[3]);

  // Flag breakdown
  XELOGI("  Flag breakdown:");
  XELOGI("    SharedMemoryIsUAV: {}",
         (sys->flags & DxbcShaderTranslator::kSysFlag_SharedMemoryIsUAV) != 0);
  XELOGI("    XYDividedByW: {}",
         (sys->flags & DxbcShaderTranslator::kSysFlag_XYDividedByW) != 0);
  XELOGI("    ZDividedByW: {}",
         (sys->flags & DxbcShaderTranslator::kSysFlag_ZDividedByW) != 0);
  XELOGI("    WNotReciprocal: {}",
         (sys->flags & DxbcShaderTranslator::kSysFlag_WNotReciprocal) != 0);
  XELOGI("    PrimitivePolygonal: {}",
         (sys->flags & DxbcShaderTranslator::kSysFlag_PrimitivePolygonal) != 0);
  XELOGI("    PrimitiveLine: {}",
         (sys->flags & DxbcShaderTranslator::kSysFlag_PrimitiveLine) != 0);
  XELOGI("    DepthFloat24: {}",
         (sys->flags & DxbcShaderTranslator::kSysFlag_DepthFloat24) != 0);

  // Dump float constants (b1) - scan for non-zero
  XELOGI("--- b1: Float Constants (offset {}) ---", kCBVSize);
  const float* float_consts = reinterpret_cast<const float*>(data + kCBVSize);
  int non_zero_count = 0;
  for (int i = 0; i < 256 && non_zero_count < 16; ++i) {
    float x = float_consts[i * 4 + 0];
    float y = float_consts[i * 4 + 1];
    float z = float_consts[i * 4 + 2];
    float w = float_consts[i * 4 + 3];
    if (x != 0.0f || y != 0.0f || z != 0.0f || w != 0.0f) {
      XELOGI("  c[{}]: [{}, {}, {}, {}]", i, x, y, z, w);
      non_zero_count++;
    }
  }
  if (non_zero_count == 0) {
    XELOGI("  (all float constants are zero)");
  }

  // Dump fetch constants (b3)
  XELOGI("--- b3: Fetch Constants (offset {}) ---", 3 * kCBVSize);
  const uint32_t* fetch_consts =
      reinterpret_cast<const uint32_t*>(data + 3 * kCBVSize);
  for (int i = 0; i < 4; ++i) {
    XELOGI("  FC[{}]: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", i,
           fetch_consts[i * 6 + 0], fetch_consts[i * 6 + 1],
           fetch_consts[i * 6 + 2], fetch_consts[i * 6 + 3],
           fetch_consts[i * 6 + 4], fetch_consts[i * 6 + 5]);
  }

  XELOGI("=== END UNIFORMS BUFFER DUMP ===");
}

void MetalCommandProcessor::LogInterpolators(MetalShader* vertex_shader,
                                             MetalShader* pixel_shader) {
  if (!vertex_shader) {
    XELOGI("INTERPOLATORS: No vertex shader");
    return;
  }

  XELOGI("=== INTERPOLATOR INFO ===");

  // Get interpolators written by vertex shader
  uint32_t vs_interpolators = vertex_shader->writes_interpolators();
  XELOGI("Vertex shader writes interpolators: 0x{:04X}", vs_interpolators);
  for (int i = 0; i < 16; ++i) {
    if (vs_interpolators & (1 << i)) {
      XELOGI("  - Interpolator {} written by VS", i);
    }
  }

  if (pixel_shader) {
    // Get interpolators read by pixel shader
    const RegisterFile& regs = *register_file_;

    // Debug flag: set when this draw targets the 320x2048 color RT0 used for
    // the A-Train background pass so we can log more detailed state.
    bool debug_bg_rt0_320x2048 = false;

    auto sq_program_cntl = regs.Get<reg::SQ_PROGRAM_CNTL>();
    auto sq_context_misc = regs.Get<reg::SQ_CONTEXT_MISC>();
    uint32_t ps_param_gen_pos = UINT32_MAX;
    uint32_t ps_interpolators = pixel_shader->GetInterpolatorInputMask(
        sq_program_cntl, sq_context_misc, ps_param_gen_pos);

    XELOGI("Pixel shader reads interpolators: 0x{:04X}", ps_interpolators);
    for (int i = 0; i < 16; ++i) {
      if (ps_interpolators & (1 << i)) {
        XELOGI("  - Interpolator {} read by PS", i);
      }
    }

    // Check for mismatches
    uint32_t used_mask = vs_interpolators & ps_interpolators;
    uint32_t missing_in_vs = ps_interpolators & ~vs_interpolators;
    uint32_t unused_from_vs = vs_interpolators & ~ps_interpolators;

    if (missing_in_vs) {
      XELOGI("WARNING: PS reads interpolators not written by VS: 0x{:04X}",
             missing_in_vs);
    }
    if (unused_from_vs) {
      XELOGI("Note: VS writes interpolators not read by PS: 0x{:04X}",
             unused_from_vs);
    }
    XELOGI("Effectively used interpolators: 0x{:04X}", used_mask);
  } else {
    XELOGI("No pixel shader - depth-only rendering");
  }

  XELOGI("=== END INTERPOLATOR INFO ===");
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
