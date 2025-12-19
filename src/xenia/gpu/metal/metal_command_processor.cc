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
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
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

DEFINE_bool(metal_draw_debug_quad, false,
            "Draw a full-screen debug quad instead of guest draws (Metal "
            "backend bring-up).",
            "GPU");

namespace {

MTL::ColorWriteMask ToMetalColorWriteMask(uint32_t write_mask) {
  MTL::ColorWriteMask mtl_mask = MTL::ColorWriteMaskNone;
  if (write_mask & 0x1) {
    mtl_mask |= MTL::ColorWriteMaskRed;
  }
  if (write_mask & 0x2) {
    mtl_mask |= MTL::ColorWriteMaskGreen;
  }
  if (write_mask & 0x4) {
    mtl_mask |= MTL::ColorWriteMaskBlue;
  }
  if (write_mask & 0x8) {
    mtl_mask |= MTL::ColorWriteMaskAlpha;
  }
  return mtl_mask;
}

MTL::BlendOperation ToMetalBlendOperation(xenos::BlendOp blend_op) {
  // 8 entries for safety since 3 bits from the guest are passed directly.
  static const MTL::BlendOperation kBlendOpMap[8] = {
      MTL::BlendOperationAdd,              // 0
      MTL::BlendOperationSubtract,         // 1
      MTL::BlendOperationMin,              // 2
      MTL::BlendOperationMax,              // 3
      MTL::BlendOperationReverseSubtract,  // 4
      MTL::BlendOperationAdd,              // 5
      MTL::BlendOperationAdd,              // 6
      MTL::BlendOperationAdd,              // 7
  };
  return kBlendOpMap[uint32_t(blend_op) & 0x7];
}

MTL::BlendFactor ToMetalBlendFactorRgb(xenos::BlendFactor blend_factor) {
  // 32 because of 0x1F mask, for safety (all unknown to zero).
  static const MTL::BlendFactor kBlendFactorMap[32] = {
      /*  0 */ MTL::BlendFactorZero,
      /*  1 */ MTL::BlendFactorOne,
      /*  2 */ MTL::BlendFactorZero,  // ?
      /*  3 */ MTL::BlendFactorZero,  // ?
      /*  4 */ MTL::BlendFactorSourceColor,
      /*  5 */ MTL::BlendFactorOneMinusSourceColor,
      /*  6 */ MTL::BlendFactorSourceAlpha,
      /*  7 */ MTL::BlendFactorOneMinusSourceAlpha,
      /*  8 */ MTL::BlendFactorDestinationColor,
      /*  9 */ MTL::BlendFactorOneMinusDestinationColor,
      /* 10 */ MTL::BlendFactorDestinationAlpha,
      /* 11 */ MTL::BlendFactorOneMinusDestinationAlpha,
      /* 12 */ MTL::BlendFactorBlendColor,  // CONSTANT_COLOR
      /* 13 */ MTL::BlendFactorOneMinusBlendColor,
      /* 14 */ MTL::BlendFactorBlendColor,  // CONSTANT_ALPHA
      /* 15 */ MTL::BlendFactorOneMinusBlendColor,
      /* 16 */ MTL::BlendFactorSourceAlphaSaturated,
  };
  return kBlendFactorMap[uint32_t(blend_factor) & 0x1F];
}

MTL::BlendFactor ToMetalBlendFactorAlpha(xenos::BlendFactor blend_factor) {
  // Like the RGB map, but with color modes changed to alpha.
  static const MTL::BlendFactor kBlendFactorAlphaMap[32] = {
      /*  0 */ MTL::BlendFactorZero,
      /*  1 */ MTL::BlendFactorOne,
      /*  2 */ MTL::BlendFactorZero,  // ?
      /*  3 */ MTL::BlendFactorZero,  // ?
      /*  4 */ MTL::BlendFactorSourceAlpha,
      /*  5 */ MTL::BlendFactorOneMinusSourceAlpha,
      /*  6 */ MTL::BlendFactorSourceAlpha,
      /*  7 */ MTL::BlendFactorOneMinusSourceAlpha,
      /*  8 */ MTL::BlendFactorDestinationAlpha,
      /*  9 */ MTL::BlendFactorOneMinusDestinationAlpha,
      /* 10 */ MTL::BlendFactorDestinationAlpha,
      /* 11 */ MTL::BlendFactorOneMinusDestinationAlpha,
      /* 12 */ MTL::BlendFactorBlendAlpha,
      /* 13 */ MTL::BlendFactorOneMinusBlendAlpha,
      /* 14 */ MTL::BlendFactorBlendAlpha,
      /* 15 */ MTL::BlendFactorOneMinusBlendAlpha,
      /* 16 */ MTL::BlendFactorSourceAlphaSaturated,
  };
  return kBlendFactorAlphaMap[uint32_t(blend_factor) & 0x1F];
}

}  // namespace

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
  if (primitive_processor_) {
    primitive_processor_->MemoryInvalidationCallback(base_ptr, length, true);
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
  XELOGI("MetalCommandProcessor::SetupContext: Starting");
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Failed to initialize base command processor context");
    return false;
  }
  XELOGI("MetalCommandProcessor::SetupContext: Base context initialized");

  const ui::metal::MetalProvider& provider = GetMetalProvider();
  device_ = provider.GetDevice();
  command_queue_ = provider.GetCommandQueue();

  if (!device_ || !command_queue_) {
    XELOGE("MetalCommandProcessor: No Metal device or command queue available");
    return false;
  }
  XELOGI("MetalCommandProcessor::SetupContext: Got device and queue");

  // Initialize shared memory
  XELOGI("MetalCommandProcessor::SetupContext: Creating shared memory");
  shared_memory_ = std::make_unique<MetalSharedMemory>(*this, *memory_);
  if (!shared_memory_->Initialize()) {
    XELOGE("Failed to initialize shared memory");
    return false;
  }
  XELOGI("MetalCommandProcessor::SetupContext: Shared memory initialized");

  // Initialize primitive processor (index/primitive conversion like D3D12).
  XELOGI("MetalCommandProcessor::SetupContext: Creating primitive processor");
  primitive_processor_ = std::make_unique<MetalPrimitiveProcessor>(
      *this, *register_file_, *memory_, trace_writer_, *shared_memory_);
  if (!primitive_processor_->Initialize()) {
    XELOGE("Failed to initialize Metal primitive processor");
    return false;
  }
  XELOGI(
      "MetalCommandProcessor::SetupContext: Primitive processor initialized");

  XELOGI("MetalCommandProcessor::SetupContext: Creating texture cache");
  texture_cache_ = std::make_unique<MetalTextureCache>(this, *register_file_,
                                                       *shared_memory_, 1, 1);
  if (!texture_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal texture cache");
    return false;
  }
  XELOGI("MetalCommandProcessor::SetupContext: Texture cache initialized");

  // Initialize render target cache
  XELOGI("MetalCommandProcessor::SetupContext: Creating render target cache");
  render_target_cache_ = std::make_unique<MetalRenderTargetCache>(
      *register_file_, *memory_, &trace_writer_, 1, 1, *this);
  if (!render_target_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal render target cache");
    return false;
  }
  XELOGI(
      "MetalCommandProcessor::SetupContext: Render target cache initialized");

  // Initialize shader translation pipeline
  XELOGI(
      "MetalCommandProcessor::SetupContext: Initializing shader translation");
  if (!InitializeShaderTranslation()) {
    XELOGE("Failed to initialize shader translation");
    return false;
  }
  XELOGI("MetalCommandProcessor::SetupContext: Shader translation initialized");

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

  // Create IR Converter runtime descriptor heap buffers.
  // These are arrays of IRDescriptorTableEntry used by Metal Shader Converter.
  const size_t kDescriptorTableCount = kStageCount * kDrawRingCount;
  const size_t kResourceHeapSlots =
      kResourceHeapSlotsPerTable * kDescriptorTableCount;
  const size_t kSamplerHeapSlots =
      kSamplerHeapSlotsPerTable * kDescriptorTableCount;
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

  // Initialize all tables:
  // - Slot 0: null buffer (will be replaced with shared memory per draw).
  // - Slots 1+: null texture (safe default for any accidental access).
  for (size_t table = 0; table < kDescriptorTableCount; ++table) {
    IRDescriptorTableEntry* table_entries =
        res_entries + table * kResourceHeapSlotsPerTable;
    IRDescriptorTableSetBuffer(&table_entries[0], null_buffer_->gpuAddress(),
                               kNullBufferSize);
    for (size_t i = 1; i < kResourceHeapSlotsPerTable; ++i) {
      IRDescriptorTableSetTexture(&table_entries[i], null_texture_, 0.0f, 0);
    }
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
  for (size_t i = 0; i < kSamplerHeapSlots; ++i) {
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
  const size_t kUniformsBufferSize =
      kUniformsBytesPerTable * kDescriptorTableCount;
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
  // sampler + 4 CBV). Each entry is sizeof(uint64_t) = 8 bytes.
  // We allocate a ring buffer with one 20-entry region per draw so that
  // descriptor table pointers are not overwritten before earlier draws execute
  // on the GPU.
  const size_t kTopLevelABTotalBytes =
      kTopLevelABBytesPerTable * kDescriptorTableCount;
  top_level_ab_ =
      device_->newBuffer(kTopLevelABTotalBytes, MTL::ResourceStorageModeShared);
  if (!top_level_ab_) {
    XELOGE("Failed to create top-level argument buffer");
    return false;
  }
  top_level_ab_->setLabel(
      NS::String::string("TopLevelArgumentBuffer", NS::UTF8StringEncoding));
  std::memset(top_level_ab_->contents(), 0, kTopLevelABTotalBytes);

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
  const size_t kCBVHeapSlots = kCbvHeapSlotsPerTable * kDescriptorTableCount;
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
      kTopLevelABTotalBytes, kDrawArgsSize);

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

void MetalCommandProcessor::PrepareForWait() {
  XELOGI("MetalCommandProcessor::PrepareForWait: enter (encoder={}, cb={})",
         current_render_encoder_ ? "yes" : "no",
         current_command_buffer_ ? "yes" : "no");

  // Flush any pending Metal command buffers before entering wait state.
  // This is critical because:
  // 1. The worker thread's autorelease pool will be drained when it exits
  // 2. Metal objects in that pool might still be referenced by in-flight
  // commands
  // 3. Releasing those objects during pool drain can hang waiting for GPU
  // completion
  //
  // By submitting and waiting for all GPU work now, we ensure clean pool
  // drainage.

  if (current_render_encoder_) {
    XELOGI("MetalCommandProcessor::PrepareForWait: ending render encoder");
    current_render_encoder_->endEncoding();
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }

  if (current_command_buffer_) {
    XELOGI(
        "MetalCommandProcessor::PrepareForWait: submitting and waiting for "
        "command buffer");
    current_command_buffer_->commit();
    current_command_buffer_->waitUntilCompleted();
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
    XELOGI("MetalCommandProcessor::PrepareForWait: command buffer completed");
  }

  // Even if we have no active command buffer, there might be GPU work from
  // previously submitted command buffers that autoreleased objects depend on.
  // Submit and wait for a dummy command buffer to ensure ALL GPU work
  // completes.
  if (command_queue_) {
    XELOGI(
        "MetalCommandProcessor::PrepareForWait: submitting sync command "
        "buffer");
    // Note: commandBuffer() returns an autoreleased object per metal-cpp docs.
    // We do NOT call release() since we didn't retain() it.
    // The autorelease pool will handle cleanup.
    MTL::CommandBuffer* sync_cmd = command_queue_->commandBuffer();
    if (sync_cmd) {
      sync_cmd->commit();
      sync_cmd->waitUntilCompleted();
      // Don't release - it's autoreleased and will be cleaned up by the pool
      XELOGI("MetalCommandProcessor::PrepareForWait: sync complete");
    }
  }

  // Also call the base class to flush trace writer
  CommandProcessor::PrepareForWait();
}

void MetalCommandProcessor::ShutdownContext() {
  XELOGI("MetalCommandProcessor::ShutdownContext: begin");
  fflush(stdout);
  fflush(stderr);

  // End any active render encoder before shutdown
  if (current_render_encoder_) {
    XELOGI("MetalCommandProcessor::ShutdownContext: ending render encoder");
    fflush(stdout);
    fflush(stderr);
    current_render_encoder_->endEncoding();
    // Don't release yet - wait until command buffer completes
  }

  // Submit and wait for any pending command buffer
  if (current_command_buffer_) {
    XELOGI("MetalCommandProcessor::ShutdownContext: committing command buffer");
    fflush(stdout);
    fflush(stderr);
    current_command_buffer_->commit();
    XELOGI("MetalCommandProcessor::ShutdownContext: waiting for completion");
    fflush(stdout);
    fflush(stderr);
    current_command_buffer_->waitUntilCompleted();
    XELOGI("MetalCommandProcessor::ShutdownContext: command buffer completed");
    fflush(stdout);
    fflush(stderr);
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }

  // Now safe to release encoder and command buffer
  if (current_render_encoder_) {
    XELOGI("MetalCommandProcessor::ShutdownContext: releasing render encoder");
    fflush(stdout);
    fflush(stderr);
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }
  if (current_command_buffer_) {
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }
  XELOGI("MetalCommandProcessor::ShutdownContext: encoder/cb cleanup done");
  fflush(stdout);
  fflush(stderr);

  if (texture_cache_) {
    texture_cache_->Shutdown();
    texture_cache_.reset();
  }

  if (primitive_processor_) {
    primitive_processor_->Shutdown();
    primitive_processor_.reset();
  }
  frame_open_ = false;

  shader_cache_.clear();
  shared_memory_.reset();
  shader_translator_.reset();
  dxbc_to_dxil_converter_.reset();
  metal_shader_converter_.reset();

  CommandProcessor::ShutdownContext();
  XELOGI("MetalCommandProcessor::ShutdownContext: end");
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

  if (primitive_processor_ && frame_open_) {
    primitive_processor_->EndFrame();
    frame_open_ = false;
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
  // Note: blitCommandEncoder() returns autoreleased object - don't release
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
  // Note: blit_cmd is from commandBuffer() which is autoreleased - don't
  // release

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

  // Vertex shader analysis.
  auto vertex_shader = static_cast<MetalShader*>(active_vertex_shader());
  if (!vertex_shader) {
    XELOGW("IssueDraw: No vertex shader");
    return false;
  }
  if (!vertex_shader->is_ucode_analyzed()) {
    vertex_shader->AnalyzeUcode(ucode_disasm_buffer_);
  }
  bool memexport_used_vertex = vertex_shader->memexport_eM_written() != 0;

  // Pixel shader analysis.
  bool primitive_polygonal = draw_util::IsPrimitivePolygonal(regs);
  bool is_rasterization_done =
      draw_util::IsRasterizationPotentiallyDone(regs, primitive_polygonal);
  MetalShader* pixel_shader = nullptr;
  if (is_rasterization_done) {
    if (edram_mode == xenos::ModeControl::kColorDepth) {
      pixel_shader = static_cast<MetalShader*>(active_pixel_shader());
      if (pixel_shader) {
        if (!pixel_shader->is_ucode_analyzed()) {
          pixel_shader->AnalyzeUcode(ucode_disasm_buffer_);
        }
        if (!draw_util::IsPixelShaderNeededWithRasterization(*pixel_shader,
                                                             regs)) {
          pixel_shader = nullptr;
        }
      }
    }
  } else {
    if (!memexport_used_vertex) {
      return true;
    }
  }
  bool memexport_used_pixel =
      pixel_shader && (pixel_shader->memexport_eM_written() != 0);
  bool memexport_used = memexport_used_vertex || memexport_used_pixel;

  // Primitive/index processing (like D3D12/Vulkan).
  PrimitiveProcessor::ProcessingResult primitive_processing_result;
  if (!primitive_processor_) {
    XELOGE("IssueDraw: primitive processor is not initialized");
    return false;
  }
  if (!primitive_processor_->Process(primitive_processing_result)) {
    XELOGE("IssueDraw: primitive processing failed");
    return false;
  }
  if (!primitive_processing_result.host_draw_vertex_count) {
    return true;
  }

  // Log the first few draws to identify primitive processing / shader
  // modification issues quickly when bringing up the backend.
  static uint32_t draw_debug_log_count = 0;
  if (draw_debug_log_count < 8) {
    ++draw_debug_log_count;
    auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
    auto rb_modecontrol = regs.Get<reg::RB_MODECONTROL>();
    XELOGI(
        "DRAW_PROC_DEBUG[{}]: guest_prim={} host_prim={} host_vs_type={} "
        "src_select={} idx_fmt={} host_idx_fmt={} ib_type={} "
        "edram_mode={}",
        draw_debug_log_count,
        uint32_t(primitive_processing_result.guest_primitive_type),
        uint32_t(primitive_processing_result.host_primitive_type),
        uint32_t(primitive_processing_result.host_vertex_shader_type),
        uint32_t(vgt_draw_initiator.source_select),
        uint32_t(vgt_draw_initiator.index_size),
        uint32_t(primitive_processing_result.host_index_format),
        uint32_t(primitive_processing_result.index_buffer_type),
        uint32_t(rb_modecontrol.edram_mode));
  }

  if (primitive_processing_result.IsTessellated()) {
    // TODO(Metal): Implement tessellation support (hull/domain pipeline) for
    // Metal. Until then, skip tessellated draws to allow non-tessellated draws
    // in trace dumps to execute.
    XELOGW(
        "Metal: Tessellated draw not supported yet (host_vs_type={}, "
        "tess_mode={}, host_prim={}), skipping",
        uint32_t(primitive_processing_result.host_vertex_shader_type),
        uint32_t(primitive_processing_result.tessellation_mode),
        uint32_t(primitive_processing_result.host_primitive_type));
    return true;
  }

  // The DXBC->DXIL->Metal pipeline currently only supports the "normal" guest
  // vertex shader path (and domain shaders are skipped above). Some
  // PrimitiveProcessor fallback host vertex shader types require additional
  // translator/runtime support and can't be executed yet.
  if (primitive_processing_result.host_vertex_shader_type ==
          Shader::HostVertexShaderType::kMemExportCompute ||
      primitive_processing_result.host_vertex_shader_type ==
          Shader::HostVertexShaderType::kPointListAsTriangleStrip ||
      primitive_processing_result.host_vertex_shader_type ==
          Shader::HostVertexShaderType::kRectangleListAsTriangleStrip) {
    // TODO(Metal): Implement primitive expansion / memexport host vertex shader
    // types for the Metal backend (likely requiring additional translation
    // support or a geometry-stage emulation path).
    XELOGW(
        "Metal: Host vertex shader type {} is not supported yet, skipping "
        "draw (guest_prim={}, host_prim={})",
        uint32_t(primitive_processing_result.host_vertex_shader_type),
        uint32_t(primitive_processing_result.guest_primitive_type),
        uint32_t(primitive_processing_result.host_primitive_type));
    return true;
  }

  // Configure render targets via MetalRenderTargetCache, similar to D3D12.
  // D3D12 passes `is_rasterization_done` when there is an actual draw to
  // perform (after primitive processing). For this Metal path we currently
  // always treat IssueDraw as doing rasterization so the render-target cache
  // will create and bind the appropriate host render targets.
  if (render_target_cache_) {
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

  if (cvars::metal_draw_debug_quad) {
    if (!debug_pipeline_) {
      if (!CreateDebugPipeline()) {
        return false;
      }
    }
    current_render_encoder_->setRenderPipelineState(debug_pipeline_);
    // Override viewport/scissor to the full render target.
    uint32_t rt_width = render_target_width_;
    uint32_t rt_height = render_target_height_;
    if (render_target_cache_) {
      if (MTL::Texture* rt0_tex = render_target_cache_->GetColorTarget(0)) {
        rt_width = uint32_t(rt0_tex->width());
        rt_height = uint32_t(rt0_tex->height());
      }
    }
    MTL::Viewport viewport;
    viewport.originX = 0.0;
    viewport.originY = 0.0;
    viewport.width = double(rt_width);
    viewport.height = double(rt_height);
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    current_render_encoder_->setViewport(viewport);
    MTL::ScissorRect scissor;
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = rt_width;
    scissor.height = rt_height;
    current_render_encoder_->setScissorRect(scissor);
    current_render_encoder_->drawPrimitives(MTL::PrimitiveTypeTriangle,
                                            NS::UInteger(0), NS::UInteger(6));
    ++current_draw_index_;
    return true;
  }

  MTL::RenderPipelineState* pipeline = nullptr;
  // Select per-draw shader modifications (mirrors D3D12 PipelineCache).
  uint32_t ps_param_gen_pos = UINT32_MAX;
  uint32_t interpolator_mask = 0;
  if (pixel_shader) {
    interpolator_mask = vertex_shader->writes_interpolators() &
                        pixel_shader->GetInterpolatorInputMask(
                            regs.Get<reg::SQ_PROGRAM_CNTL>(),
                            regs.Get<reg::SQ_CONTEXT_MISC>(), ps_param_gen_pos);
  }

  auto normalized_depth_control = draw_util::GetNormalizedDepthControl(regs);
  DxbcShaderTranslator::Modification vertex_shader_modification =
      GetCurrentVertexShaderModification(
          *vertex_shader, primitive_processing_result.host_vertex_shader_type,
          interpolator_mask);
  DxbcShaderTranslator::Modification pixel_shader_modification =
      pixel_shader ? GetCurrentPixelShaderModification(
                         *pixel_shader, interpolator_mask, ps_param_gen_pos,
                         normalized_depth_control)
                   : DxbcShaderTranslator::Modification(0);

  // Get or create shader translations for the selected modifications.
  auto vertex_translation = static_cast<MetalShader::MetalTranslation*>(
      vertex_shader->GetOrCreateTranslation(vertex_shader_modification.value));
  if (!vertex_translation->is_translated()) {
    if (!shader_translator_->TranslateAnalyzedShader(*vertex_translation)) {
      XELOGE("Failed to translate vertex shader to DXBC");
      return false;
    }
  }
  if (!vertex_translation->is_valid()) {
    if (!vertex_translation->TranslateToMetal(device_, *dxbc_to_dxil_converter_,
                                              *metal_shader_converter_)) {
      XELOGE("Failed to translate vertex shader to Metal");
      return false;
    }
  }

  MetalShader::MetalTranslation* pixel_translation = nullptr;
  if (pixel_shader) {
    pixel_translation = static_cast<MetalShader::MetalTranslation*>(
        pixel_shader->GetOrCreateTranslation(pixel_shader_modification.value));
    if (!pixel_translation->is_translated()) {
      if (!shader_translator_->TranslateAnalyzedShader(*pixel_translation)) {
        XELOGE("Failed to translate pixel shader to DXBC");
        return false;
      }
    }
    if (!pixel_translation->is_valid()) {
      if (!pixel_translation->TranslateToMetal(
              device_, *dxbc_to_dxil_converter_, *metal_shader_converter_)) {
        XELOGE("Failed to translate pixel shader to Metal");
        return false;
      }
    }
  }

  // Get or create pipeline state matching current RT formats.
  pipeline =
      GetOrCreatePipelineState(vertex_translation, pixel_translation, regs);

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

    // Get viewport info for NDC transform. Use the actual RT0 dimensions
    // when available so system constants match the current render target.
    uint32_t vp_width = render_target_width_;
    uint32_t vp_height = render_target_height_;
    if (render_target_cache_) {
      MTL::Texture* rt0_tex = render_target_cache_->GetColorTarget(0);
      if (rt0_tex) {
        vp_width = rt0_tex->width();
        vp_height = rt0_tex->height();
      }
    }
    draw_util::ViewportInfo viewport_info;
    auto depth_control = draw_util::GetNormalizedDepthControl(regs);
    draw_util::GetHostViewportInfo(
        regs, 1, 1,     // draw resolution scale x/y
        true,           // origin_bottom_left (Metal NDC Y is up, like D3D)
        vp_width,       // x_max
        vp_height,      // y_max
        false,          // allow_reverse_z
        depth_control,  // normalized_depth_control
        false,          // convert_z_to_float24
        false,          // full_float24_in_0_to_1
        false,          // pixel_shader_writes_depth
        viewport_info);

    // Apply per-draw viewport and scissor so the Metal viewport
    // matches the guest viewport computed by draw_util.
    draw_util::Scissor scissor;
    draw_util::GetScissor(regs, scissor, true);

    // Clamp scissor to actual render target bounds (Metal requires this).
    if (scissor.offset[0] + scissor.extent[0] > vp_width) {
      scissor.extent[0] =
          (scissor.offset[0] < vp_width) ? (vp_width - scissor.offset[0]) : 0;
    }
    if (scissor.offset[1] + scissor.extent[1] > vp_height) {
      scissor.extent[1] =
          (scissor.offset[1] < vp_height) ? (vp_height - scissor.offset[1]) : 0;
    }

    MTL::Viewport mtl_viewport;
    mtl_viewport.originX = static_cast<double>(viewport_info.xy_offset[0]);
    mtl_viewport.originY = static_cast<double>(viewport_info.xy_offset[1]);
    mtl_viewport.width = static_cast<double>(viewport_info.xy_extent[0]);
    mtl_viewport.height = static_cast<double>(viewport_info.xy_extent[1]);
    mtl_viewport.znear = viewport_info.z_min;
    mtl_viewport.zfar = viewport_info.z_max;
    current_render_encoder_->setViewport(mtl_viewport);

    MTL::ScissorRect mtl_scissor;
    mtl_scissor.x = scissor.offset[0];
    mtl_scissor.y = scissor.offset[1];
    mtl_scissor.width = scissor.extent[0];
    mtl_scissor.height = scissor.extent[1];
    current_render_encoder_->setScissorRect(mtl_scissor);

    // Debug: Log viewport/scissor setup for early draws
    static int vp_log_count = 0;
    if (vp_log_count < 8) {
      vp_log_count++;
      XELOGI(
          "VP_DEBUG: draw={} RT={}x{} viewport=({},{} {}x{}) scissor=({},{} "
          "{}x{}) ndc_scale=({:.3f},{:.3f},{:.3f}) "
          "ndc_offset=({:.3f},{:.3f},{:.3f})",
          vp_log_count, vp_width, vp_height,
          static_cast<int>(mtl_viewport.originX),
          static_cast<int>(mtl_viewport.originY),
          static_cast<int>(mtl_viewport.width),
          static_cast<int>(mtl_viewport.height), mtl_scissor.x, mtl_scissor.y,
          mtl_scissor.width, mtl_scissor.height, viewport_info.ndc_scale[0],
          viewport_info.ndc_scale[1], viewport_info.ndc_scale[2],
          viewport_info.ndc_offset[0], viewport_info.ndc_offset[1],
          viewport_info.ndc_offset[2]);
    }

    // Update full system constants from GPU registers
    // This populates flags, NDC transform, alpha test, blend constants, etc.
    UpdateSystemConstantValues(
        shared_memory_is_uav, primitive_polygonal,
        primitive_processing_result.line_loop_closing_index,
        primitive_processing_result.host_shader_index_endian, viewport_info,
        used_texture_mask);

    float blend_constants[] = {
        regs.Get<float>(XE_GPU_REG_RB_BLEND_RED),
        regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN),
        regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE),
        regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA),
    };
    bool blend_factor_update_needed =
        !ff_blend_factor_valid_ ||
        std::memcmp(ff_blend_factor_, blend_constants, sizeof(float) * 4) != 0;
    if (blend_factor_update_needed) {
      std::memcpy(ff_blend_factor_, blend_constants, sizeof(float) * 4);
      ff_blend_factor_valid_ = true;
      current_render_encoder_->setBlendColor(
          blend_constants[0], blend_constants[1], blend_constants[2],
          blend_constants[3]);
    }

    constexpr size_t kStageVertex = 0;
    constexpr size_t kStagePixel = 1;
    uint32_t ring_index = current_draw_index_ % uint32_t(kDrawRingCount);
    size_t table_index_vertex = size_t(ring_index) * kStageCount + kStageVertex;
    size_t table_index_pixel = size_t(ring_index) * kStageCount + kStagePixel;

    // Uniforms buffer layout (4KB per CBV for alignment):
    //   b0 (offset 0):     System constants (~512 bytes)
    //   b1 (offset 4096):  Float constants (256 float4s = 4KB)
    //   b2 (offset 8192):  Bool/loop constants (~256 bytes)
    //   b3 (offset 12288): Fetch constants (768 bytes)
    //   b4 (offset 16384): Descriptor indices (unused in bindful mode)
    const size_t kCBVSize = kCbvSizeBytes;
    uint8_t* uniforms_base =
        static_cast<uint8_t*>(uniforms_buffer_->contents());
    uint8_t* uniforms_vertex =
        uniforms_base + table_index_vertex * kUniformsBytesPerTable;
    uint8_t* uniforms_pixel =
        uniforms_base + table_index_pixel * kUniformsBytesPerTable;

    // b0: System constants.
    std::memcpy(uniforms_vertex, &system_constants_,
                sizeof(DxbcShaderTranslator::SystemConstants));
    std::memcpy(uniforms_pixel, &system_constants_,
                sizeof(DxbcShaderTranslator::SystemConstants));

    // b1: Float constants at offset 4096 (1 * kCBVSize).
    const size_t kFloatConstantOffset = 1 * kCBVSize;
    // DxbcShaderTranslator uses packed float constants, mirroring the D3D12
    // backend behavior: only the constants actually used by the shader are
    // written sequentially based on Shader::ConstantRegisterMap.
    auto write_packed_float_constants = [&](uint8_t* dst, const Shader& shader,
                                            uint32_t regs_base) {
      std::memset(dst, 0, kCBVSize);
      const Shader::ConstantRegisterMap& map = shader.constant_register_map();
      if (!map.float_count) {
        return;
      }
      uint8_t* out = dst;
      for (uint32_t i = 0; i < 4; ++i) {
        uint64_t bits = map.float_bitmap[i];
        uint32_t constant_index;
        while (xe::bit_scan_forward(bits, &constant_index)) {
          bits &= ~(uint64_t(1) << constant_index);
          if (out + 4 * sizeof(uint32_t) > dst + kCBVSize) {
            return;
          }
          std::memcpy(
              out, &regs.values[regs_base + (i << 8) + (constant_index << 2)],
              4 * sizeof(uint32_t));
          out += 4 * sizeof(uint32_t);
        }
      }
    };

    // Vertex shader uses c0-c255, pixel shader uses c256-c511.
    write_packed_float_constants(uniforms_vertex + kFloatConstantOffset,
                                 *vertex_shader,
                                 XE_GPU_REG_SHADER_CONSTANT_000_X);
    if (pixel_shader) {
      write_packed_float_constants(uniforms_pixel + kFloatConstantOffset,
                                   *pixel_shader,
                                   XE_GPU_REG_SHADER_CONSTANT_256_X);
    } else {
      std::memset(uniforms_pixel + kFloatConstantOffset, 0, kCBVSize);
    }

    // b2: Bool/Loop constants at offset 8192 (2 * kCBVSize).
    const size_t kBoolLoopConstantOffset = 2 * kCBVSize;
    constexpr size_t kBoolLoopConstantsSize = (8 + 32) * sizeof(uint32_t);
    std::memcpy(uniforms_vertex + kBoolLoopConstantOffset,
                &regs.values[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031],
                kBoolLoopConstantsSize);
    std::memcpy(uniforms_pixel + kBoolLoopConstantOffset,
                &regs.values[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031],
                kBoolLoopConstantsSize);

    // b3: Fetch constants at offset 12288 (3 * kCBVSize)
    // Xbox 360 has 96 vertex fetch constants (indices 0-95), each 6 DWORDs
    const size_t kFetchConstantOffset = 3 * kCBVSize;
    const size_t kFetchConstantCount = 96 * 6;  // 576 DWORDs = 2304 bytes
    std::memcpy(uniforms_vertex + kFetchConstantOffset,
                &regs.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0],
                kFetchConstantCount * sizeof(uint32_t));
    std::memcpy(uniforms_pixel + kFetchConstantOffset,
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

      // Dump fetch constant 95 from b3 (at offset 760 in fetch constants
      // buffer) FC95 is at register 47 (95/2=47), offset = 47*16 = 752 bytes
      // Plus 8 bytes for zw components = 760 bytes
      uint8_t* fc_buffer = uniforms_vertex + kFetchConstantOffset;
      const uint32_t* fc95 = reinterpret_cast<const uint32_t*>(fc_buffer + 760);
      XELOGI("Fetch constant 95 (at b3 offset 760):");
      XELOGI("  Raw: 0x{:08X} 0x{:08X}", fc95[0], fc95[1]);
      uint32_t vb_addr = fc95[0] & 0xFFFFFFFC;
      uint32_t vb_size = (fc95[1] >> 2) & 0x3FFFFF;  // size field
      XELOGI("  VB addr=0x{:08X} size={}", vb_addr, vb_size * 4);

      // Check if vertex data exists at this address in shared memory
      if (sm && vb_addr < sm->length()) {
        const uint32_t* raw_data = reinterpret_cast<const uint32_t*>(
            static_cast<const uint8_t*>(sm->contents()) + vb_addr);

        // Log raw data first
        XELOGI(
            "  Raw vertex data: 0x{:08X} 0x{:08X} 0x{:08X} 0x{:08X} 0x{:08X}",
            raw_data[0], raw_data[1], raw_data[2], raw_data[3], raw_data[4]);

        // Apply 8-in-32 byteswap (endian mode 2)
        auto bswap32 = [](uint32_t v) -> uint32_t {
          return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
                 ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
        };

        uint32_t swapped[5];
        for (int i = 0; i < 5; i++) {
          swapped[i] = bswap32(raw_data[i]);
        }

        float pos[3];
        std::memcpy(&pos[0], &swapped[0], 4);
        std::memcpy(&pos[1], &swapped[1], 4);
        std::memcpy(&pos[2], &swapped[2], 4);

        XELOGI("  After byteswap: ({:.3f}, {:.3f}, {:.3f})", pos[0], pos[1],
               pos[2]);

        // Show all 4 vertices (stride is 20 bytes = 5 DWORDs)
        for (int v = 0; v < 4; v++) {
          const uint32_t* vraw = raw_data + v * 5;
          uint32_t vswapped[3];
          for (int i = 0; i < 3; i++) {
            vswapped[i] = bswap32(vraw[i]);
          }
          float vpos[3];
          std::memcpy(&vpos[0], &vswapped[0], 4);
          std::memcpy(&vpos[1], &vswapped[1], 4);
          std::memcpy(&vpos[2], &vswapped[2], 4);
          XELOGI("  Vertex[{}]: ({:.3f}, {:.3f}, {:.3f})", v, vpos[0], vpos[1],
                 vpos[2]);
        }
      }
      XELOGI("=== END FIRST DRAW DEBUG INFO ===");
    }

    auto* res_entries_all =
        reinterpret_cast<IRDescriptorTableEntry*>(res_heap_ab_->contents());
    auto* smp_entries_all =
        reinterpret_cast<IRDescriptorTableEntry*>(smp_heap_ab_->contents());
    auto* cbv_entries_all =
        reinterpret_cast<IRDescriptorTableEntry*>(cbv_heap_ab_->contents());

    IRDescriptorTableEntry* res_entries_vertex =
        res_entries_all + table_index_vertex * kResourceHeapSlotsPerTable;
    IRDescriptorTableEntry* res_entries_pixel =
        res_entries_all + table_index_pixel * kResourceHeapSlotsPerTable;
    IRDescriptorTableEntry* smp_entries_vertex =
        smp_entries_all + table_index_vertex * kSamplerHeapSlotsPerTable;
    IRDescriptorTableEntry* smp_entries_pixel =
        smp_entries_all + table_index_pixel * kSamplerHeapSlotsPerTable;
    IRDescriptorTableEntry* cbv_entries_vertex =
        cbv_entries_all + table_index_vertex * kCbvHeapSlotsPerTable;
    IRDescriptorTableEntry* cbv_entries_pixel =
        cbv_entries_all + table_index_pixel * kCbvHeapSlotsPerTable;

    uint64_t res_heap_gpu_base_vertex =
        res_heap_ab_->gpuAddress() + table_index_vertex *
                                         kResourceHeapSlotsPerTable *
                                         sizeof(IRDescriptorTableEntry);
    uint64_t res_heap_gpu_base_pixel =
        res_heap_ab_->gpuAddress() + table_index_pixel *
                                         kResourceHeapSlotsPerTable *
                                         sizeof(IRDescriptorTableEntry);
    uint64_t smp_heap_gpu_base_vertex =
        smp_heap_ab_->gpuAddress() + table_index_vertex *
                                         kSamplerHeapSlotsPerTable *
                                         sizeof(IRDescriptorTableEntry);
    uint64_t smp_heap_gpu_base_pixel =
        smp_heap_ab_->gpuAddress() + table_index_pixel *
                                         kSamplerHeapSlotsPerTable *
                                         sizeof(IRDescriptorTableEntry);

    uint64_t uniforms_gpu_base_vertex =
        uniforms_buffer_->gpuAddress() +
        table_index_vertex * kUniformsBytesPerTable;
    uint64_t uniforms_gpu_base_pixel =
        uniforms_buffer_->gpuAddress() +
        table_index_pixel * kUniformsBytesPerTable;

    MTL::Buffer* shared_mem_buffer = shared_memory_->GetBuffer();
    if (shared_mem_buffer) {
      IRDescriptorTableSetBuffer(&res_entries_vertex[0],
                                 shared_mem_buffer->gpuAddress(),
                                 shared_mem_buffer->length());
      IRDescriptorTableSetBuffer(&res_entries_pixel[0],
                                 shared_mem_buffer->gpuAddress(),
                                 shared_mem_buffer->length());
      current_render_encoder_->useResource(shared_mem_buffer,
                                           MTL::ResourceUsageRead);
    }

    std::vector<MTL::Texture*> textures_for_encoder;
    textures_for_encoder.reserve(16);
    auto track_texture_usage = [&](MTL::Texture* texture) {
      if (!texture) {
        return;
      }
      if (std::find(textures_for_encoder.begin(), textures_for_encoder.end(),
                    texture) == textures_for_encoder.end()) {
        textures_for_encoder.push_back(texture);
      }
    };

    auto bind_shader_textures = [&](MetalShader* shader,
                                    IRDescriptorTableEntry* stage_res_entries) {
      if (!shader || !texture_cache_) {
        return;
      }
      const auto& shader_texture_bindings =
          shader->GetTextureBindingsAfterTranslation();
      for (size_t binding_index = 0;
           binding_index < shader_texture_bindings.size(); ++binding_index) {
        uint32_t srv_slot = 1 + static_cast<uint32_t>(binding_index);
        if (srv_slot >= kResourceHeapSlotsPerTable) {
          break;
        }
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
          IRDescriptorTableSetTexture(&stage_res_entries[srv_slot], texture,
                                      0.0f, 0);
          track_texture_usage(texture);
        }
      }
    };

    auto bind_shader_samplers = [&](MetalShader* shader,
                                    IRDescriptorTableEntry* stage_smp_entries) {
      if (!shader || !texture_cache_) {
        return;
      }
      const auto& sampler_bindings =
          shader->GetSamplerBindingsAfterTranslation();
      for (size_t sampler_index = 0; sampler_index < sampler_bindings.size();
           ++sampler_index) {
        if (sampler_index >= kSamplerHeapSlotsPerTable) {
          break;
        }
        auto parameters = texture_cache_->GetSamplerParameters(
            sampler_bindings[sampler_index]);
        MTL::SamplerState* sampler_state =
            texture_cache_->GetOrCreateSampler(parameters);
        if (!sampler_state) {
          sampler_state = null_sampler_;
        }
        if (sampler_state) {
          IRDescriptorTableSetSampler(&stage_smp_entries[sampler_index],
                                      sampler_state, 0.0f);
        }
      }
    };

    bind_shader_textures(vertex_shader, res_entries_vertex);
    bind_shader_textures(pixel_shader, res_entries_pixel);
    bind_shader_samplers(vertex_shader, smp_entries_vertex);
    bind_shader_samplers(pixel_shader, smp_entries_pixel);

    for (MTL::Texture* texture : textures_for_encoder) {
      current_render_encoder_->useResource(texture, MTL::ResourceUsageRead);
    }

    current_render_encoder_->useResource(null_buffer_, MTL::ResourceUsageRead);
    current_render_encoder_->useResource(null_texture_, MTL::ResourceUsageRead);
    current_render_encoder_->useResource(res_heap_ab_, MTL::ResourceUsageRead);
    current_render_encoder_->useResource(smp_heap_ab_, MTL::ResourceUsageRead);
    current_render_encoder_->useResource(top_level_ab_, MTL::ResourceUsageRead);
    current_render_encoder_->useResource(cbv_heap_ab_, MTL::ResourceUsageRead);
    current_render_encoder_->useResource(uniforms_buffer_,
                                         MTL::ResourceUsageRead);

    auto write_top_level_and_cbvs = [&](size_t table_index,
                                        uint64_t res_table_gpu_base,
                                        uint64_t smp_table_gpu_base,
                                        IRDescriptorTableEntry* cbv_entries,
                                        uint64_t uniforms_gpu_base) {
      size_t top_level_offset = table_index * kTopLevelABBytesPerTable;
      auto* top_level_ptrs = reinterpret_cast<uint64_t*>(
          static_cast<uint8_t*>(top_level_ab_->contents()) + top_level_offset);
      std::memset(top_level_ptrs, 0, kTopLevelABBytesPerTable);

      for (int i = 0; i < 4; ++i) {
        top_level_ptrs[i] = res_table_gpu_base;
      }
      top_level_ptrs[4] = res_table_gpu_base;
      for (int i = 5; i < 9; ++i) {
        top_level_ptrs[i] = res_table_gpu_base;
      }
      top_level_ptrs[9] = smp_table_gpu_base;

      IRDescriptorTableSetBuffer(&cbv_entries[0],
                                 uniforms_gpu_base + 0 * kCbvSizeBytes,
                                 kCbvSizeBytes);
      IRDescriptorTableSetBuffer(&cbv_entries[1],
                                 uniforms_gpu_base + 1 * kCbvSizeBytes,
                                 kCbvSizeBytes);
      IRDescriptorTableSetBuffer(&cbv_entries[2],
                                 uniforms_gpu_base + 2 * kCbvSizeBytes,
                                 kCbvSizeBytes);
      IRDescriptorTableSetBuffer(&cbv_entries[3],
                                 uniforms_gpu_base + 3 * kCbvSizeBytes,
                                 kCbvSizeBytes);
      IRDescriptorTableSetBuffer(&cbv_entries[4],
                                 uniforms_gpu_base + 4 * kCbvSizeBytes,
                                 kCbvSizeBytes);
      IRDescriptorTableSetBuffer(&cbv_entries[5], null_buffer_->gpuAddress(),
                                 kCbvSizeBytes);
      IRDescriptorTableSetBuffer(&cbv_entries[6], null_buffer_->gpuAddress(),
                                 kCbvSizeBytes);

      uint64_t cbv_table_gpu_base =
          cbv_heap_ab_->gpuAddress() +
          table_index * kCbvHeapSlotsPerTable * sizeof(IRDescriptorTableEntry);
      top_level_ptrs[10] = cbv_table_gpu_base;
      top_level_ptrs[11] = cbv_table_gpu_base;
      top_level_ptrs[12] = cbv_table_gpu_base;
      top_level_ptrs[13] = cbv_table_gpu_base;
    };

    write_top_level_and_cbvs(table_index_vertex, res_heap_gpu_base_vertex,
                             smp_heap_gpu_base_vertex, cbv_entries_vertex,
                             uniforms_gpu_base_vertex);
    write_top_level_and_cbvs(table_index_pixel, res_heap_gpu_base_pixel,
                             smp_heap_gpu_base_pixel, cbv_entries_pixel,
                             uniforms_gpu_base_pixel);

    current_render_encoder_->setVertexBuffer(
        top_level_ab_, table_index_vertex * kTopLevelABBytesPerTable,
        kIRArgumentBufferBindPoint);
    current_render_encoder_->setFragmentBuffer(
        top_level_ab_, table_index_pixel * kTopLevelABBytesPerTable,
        kIRArgumentBufferBindPoint);

    current_render_encoder_->setVertexBuffer(res_heap_ab_, 0,
                                             kIRDescriptorHeapBindPoint);
    current_render_encoder_->setFragmentBuffer(res_heap_ab_, 0,
                                               kIRDescriptorHeapBindPoint);
    current_render_encoder_->setVertexBuffer(smp_heap_ab_, 0,
                                             kIRSamplerHeapBindPoint);
    current_render_encoder_->setFragmentBuffer(smp_heap_ab_, 0,
                                               kIRSamplerHeapBindPoint);

    XELOGD("Bound IR Converter resources: res_heap, smp_heap, top_level_ab");
  }

  // Bind vertex buffers at kIRVertexBufferBindPoint (index 6+) for stage-in.
  // The pipeline's vertex descriptor expects buffers at these indices,
  // populated from the vertex fetch constants. The buffer addresses come from
  // shared memory.
  const auto& vb_bindings = vertex_shader->vertex_bindings();
  if (shared_memory_ && !vb_bindings.empty()) {
    MTL::Buffer* shared_mem_buffer = shared_memory_->GetBuffer();
    if (shared_mem_buffer) {
      // Mark shared memory as used for reading
      current_render_encoder_->useResource(shared_mem_buffer,
                                           MTL::ResourceUsageRead);

      // Bind vertex buffers for each binding
      static int vb_log_count = 0;
      for (const auto& binding : vb_bindings) {
        xenos::xe_gpu_vertex_fetch_t vfetch =
            regs.GetVertexFetch(binding.fetch_constant);

        // Calculate buffer offset within shared memory
        uint32_t buffer_offset = vfetch.address << 2;  // address is in dwords

        // Bind at kIRVertexBufferBindPoint + binding_index
        uint64_t buffer_index =
            kIRVertexBufferBindPoint + uint64_t(binding.binding_index);

        current_render_encoder_->setVertexBuffer(shared_mem_buffer,
                                                 buffer_offset, buffer_index);

        // Debug logging for first few draws
        if (vb_log_count < 5) {
          XELOGI(
              "VB_DEBUG: binding={} fetch_const={} addr=0x{:08X} size={} "
              "stride={} -> buffer_index={}",
              binding.binding_index, binding.fetch_constant, buffer_offset,
              vfetch.size << 2, binding.stride_words * 4, buffer_index);
        }
      }
      if (vb_log_count < 5) {
        vb_log_count++;
      }
    }
  } else if (shared_memory_) {
    // No vertex bindings, but still mark shared memory as resident
    if (MTL::Buffer* shared_mem_buffer = shared_memory_->GetBuffer()) {
      current_render_encoder_->useResource(shared_mem_buffer,
                                           MTL::ResourceUsageRead);
    }
  }

  // Primitive topology - from primitive processor, like D3D12.
  MTL::PrimitiveType mtl_primitive = MTL::PrimitiveTypeTriangle;
  switch (primitive_processing_result.host_primitive_type) {
    case xenos::PrimitiveType::kPointList:
      mtl_primitive = MTL::PrimitiveTypePoint;
      break;
    case xenos::PrimitiveType::kLineList:
      mtl_primitive = MTL::PrimitiveTypeLine;
      break;
    case xenos::PrimitiveType::kLineStrip:
      mtl_primitive = MTL::PrimitiveTypeLineStrip;
      break;
    case xenos::PrimitiveType::kTriangleList:
    case xenos::PrimitiveType::kRectangleList:
      mtl_primitive = MTL::PrimitiveTypeTriangle;
      break;
    case xenos::PrimitiveType::kTriangleStrip:
      mtl_primitive = MTL::PrimitiveTypeTriangleStrip;
      break;
    default:
      XELOGE(
          "Host primitive type {} returned by the primitive processor is not "
          "supported by the Metal command processor",
          uint32_t(primitive_processing_result.host_primitive_type));
      return false;
  }

  // Draw using primitive processor output.
  if (primitive_processing_result.index_buffer_type ==
      PrimitiveProcessor::ProcessedIndexBufferType::kNone) {
    IRRuntimeDrawPrimitives(
        current_render_encoder_, mtl_primitive, NS::UInteger(0),
        NS::UInteger(primitive_processing_result.host_draw_vertex_count));
  } else {
    MTL::IndexType index_type =
        (primitive_processing_result.host_index_format ==
         xenos::IndexFormat::kInt16)
            ? MTL::IndexTypeUInt16
            : MTL::IndexTypeUInt32;
    MTL::Buffer* index_buffer = nullptr;
    uint64_t index_offset = 0;
    switch (primitive_processing_result.index_buffer_type) {
      case PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA:
        index_buffer = shared_memory_ ? shared_memory_->GetBuffer() : nullptr;
        index_offset = primitive_processing_result.guest_index_base;
        break;
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostConverted:
        if (primitive_processor_) {
          index_buffer = primitive_processor_->GetConvertedIndexBuffer(
              primitive_processing_result.host_index_buffer_handle,
              index_offset);
        }
        break;
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostBuiltinForAuto:
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostBuiltinForDMA:
        if (primitive_processor_) {
          index_buffer = primitive_processor_->GetBuiltinIndexBuffer();
          index_offset = primitive_processing_result.host_index_buffer_handle;
        }
        break;
      default:
        XELOGE("Unsupported index buffer type {}",
               uint32_t(primitive_processing_result.index_buffer_type));
        return false;
    }
    if (!index_buffer) {
      XELOGE("IssueDraw: index buffer is null for type {}",
             uint32_t(primitive_processing_result.index_buffer_type));
      return false;
    }
    IRRuntimeDrawIndexedPrimitives(
        current_render_encoder_, mtl_primitive,
        NS::UInteger(primitive_processing_result.host_draw_vertex_count),
        index_type, index_buffer, index_offset, NS::UInteger(1), 0, 0);
  }

  XELOGI(
      "IssueDraw: guest_prim={} guest_count={} ib_info={} host_prim={} "
      "host_count={} idx_type={}",
      uint32_t(primitive_type), index_count, index_buffer_info != nullptr,
      uint32_t(primitive_processing_result.host_primitive_type),
      primitive_processing_result.host_draw_vertex_count,
      uint32_t(primitive_processing_result.index_buffer_type));

  // Advance ring-buffer indices for descriptor and argument buffers.
  ++current_draw_index_;

  return true;
}

bool MetalCommandProcessor::IssueCopy() {
  XELOGI("MetalCommandProcessor::IssueCopy");
  XELOGI("MetalCommandProcessor::IssueCopy: encoder={} command_buffer={}",
         current_render_encoder_ ? "yes" : "no",
         current_command_buffer_ ? "yes" : "no");

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

  if (primitive_processor_ && !frame_open_) {
    primitive_processor_->BeginFrame();
    frame_open_ = true;
  }

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
  ff_blend_factor_valid_ = false;

  // Derive viewport/scissor from the actual bound render target rather than
  // a hard-coded 1280x720. Prefer color RT 0 from the MetalRenderTargetCache,
  // falling back to the legacy render_target_width_/height_ when needed.
  uint32_t rt_width = render_target_width_;
  uint32_t rt_height = render_target_height_;
  if (render_target_cache_) {
    MTL::Texture* rt0 = render_target_cache_->GetColorTarget(0);
    if (rt0) {
      rt_width = static_cast<uint32_t>(rt0->width());
      rt_height = static_cast<uint32_t>(rt0->height());
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
    MetalShader::MetalTranslation* pixel_translation,
    const RegisterFile& regs) {
  if (!vertex_translation || !vertex_translation->metal_function()) {
    XELOGE("No valid vertex shader function");
    return nullptr;
  }

  // Determine attachment formats and sample count from the render target cache
  // so the pipeline matches the actual render pass. If no real RT is bound,
  // fall back to the dummy RT0 format used by the cache.
  uint32_t sample_count = 1;
  MTL::PixelFormat color_formats[4] = {
      MTL::PixelFormatInvalid, MTL::PixelFormatInvalid, MTL::PixelFormatInvalid,
      MTL::PixelFormatInvalid};
  MTL::PixelFormat depth_format = MTL::PixelFormatInvalid;
  MTL::PixelFormat stencil_format = MTL::PixelFormatInvalid;
  if (render_target_cache_) {
    for (uint32_t i = 0; i < 4; ++i) {
      if (MTL::Texture* rt = render_target_cache_->GetColorTarget(i)) {
        color_formats[i] = rt->pixelFormat();
        if (rt->sampleCount() > 0) {
          sample_count = std::max<uint32_t>(
              sample_count, static_cast<uint32_t>(rt->sampleCount()));
        }
      }
    }
    if (color_formats[0] == MTL::PixelFormatInvalid) {
      if (MTL::Texture* dummy = render_target_cache_->GetDummyColorTarget()) {
        color_formats[0] = dummy->pixelFormat();
        if (dummy->sampleCount() > 0) {
          sample_count = std::max<uint32_t>(
              sample_count, static_cast<uint32_t>(dummy->sampleCount()));
        }
      }
    }
    if (MTL::Texture* depth_tex = render_target_cache_->GetDepthTarget()) {
      depth_format = depth_tex->pixelFormat();
      switch (depth_format) {
        case MTL::PixelFormatDepth32Float_Stencil8:
        case MTL::PixelFormatDepth24Unorm_Stencil8:
        case MTL::PixelFormatX32_Stencil8:
          stencil_format = depth_format;
          break;
        default:
          stencil_format = MTL::PixelFormatInvalid;
          break;
      }
      if (depth_tex->sampleCount() > 0) {
        sample_count = std::max<uint32_t>(
            sample_count, static_cast<uint32_t>(depth_tex->sampleCount()));
      }
    }
  }

  struct PipelineKey {
    const void* vs;
    const void* ps;
    uint32_t sample_count;
    uint32_t depth_format;
    uint32_t stencil_format;
    uint32_t color_formats[4];
    uint32_t normalized_color_mask;
    uint32_t alpha_to_mask_enable;
    uint32_t blendcontrol[4];
  } key_data = {};
  key_data.vs = vertex_translation;
  key_data.ps = pixel_translation;
  key_data.sample_count = sample_count;
  key_data.depth_format = uint32_t(depth_format);
  key_data.stencil_format = uint32_t(stencil_format);
  for (uint32_t i = 0; i < 4; ++i) {
    key_data.color_formats[i] = uint32_t(color_formats[i]);
  }
  uint32_t pixel_shader_writes_color_targets =
      pixel_translation ? pixel_translation->shader().writes_color_targets()
                        : 0;
  key_data.normalized_color_mask =
      pixel_shader_writes_color_targets
          ? draw_util::GetNormalizedColorMask(regs,
                                              pixel_shader_writes_color_targets)
          : 0;
  auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  key_data.alpha_to_mask_enable = rb_colorcontrol.alpha_to_mask_enable ? 1 : 0;
  for (uint32_t i = 0; i < 4; ++i) {
    key_data.blendcontrol[i] =
        regs.Get<reg::RB_BLENDCONTROL>(
                reg::RB_BLENDCONTROL::rt_register_indices[i])
            .value;
  }
  uint64_t key = XXH3_64bits(&key_data, sizeof(key_data));

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
  for (uint32_t i = 0; i < 4; ++i) {
    desc->colorAttachments()->object(i)->setPixelFormat(color_formats[i]);
  }
  desc->setDepthAttachmentPixelFormat(depth_format);
  desc->setStencilAttachmentPixelFormat(stencil_format);
  desc->setSampleCount(sample_count);
  desc->setAlphaToCoverageEnabled(key_data.alpha_to_mask_enable != 0);

  // Fixed-function blending and color write masks.
  // These are part of the render pipeline state, so the cache key must include
  // the relevant register-derived values (mask, RB_BLENDCONTROL, A2C).
  for (uint32_t i = 0; i < 4; ++i) {
    auto* color_attachment = desc->colorAttachments()->object(i);
    if (color_formats[i] == MTL::PixelFormatInvalid) {
      color_attachment->setWriteMask(MTL::ColorWriteMaskNone);
      color_attachment->setBlendingEnabled(false);
      continue;
    }

    uint32_t rt_write_mask = (key_data.normalized_color_mask >> (i * 4)) & 0xF;
    color_attachment->setWriteMask(ToMetalColorWriteMask(rt_write_mask));
    if (!rt_write_mask) {
      color_attachment->setBlendingEnabled(false);
      continue;
    }

    auto blendcontrol = regs.Get<reg::RB_BLENDCONTROL>(
        reg::RB_BLENDCONTROL::rt_register_indices[i]);
    MTL::BlendFactor src_rgb =
        ToMetalBlendFactorRgb(blendcontrol.color_srcblend);
    MTL::BlendFactor dst_rgb =
        ToMetalBlendFactorRgb(blendcontrol.color_destblend);
    MTL::BlendOperation op_rgb =
        ToMetalBlendOperation(blendcontrol.color_comb_fcn);
    MTL::BlendFactor src_alpha =
        ToMetalBlendFactorAlpha(blendcontrol.alpha_srcblend);
    MTL::BlendFactor dst_alpha =
        ToMetalBlendFactorAlpha(blendcontrol.alpha_destblend);
    MTL::BlendOperation op_alpha =
        ToMetalBlendOperation(blendcontrol.alpha_comb_fcn);

    bool blending_enabled =
        src_rgb != MTL::BlendFactorOne || dst_rgb != MTL::BlendFactorZero ||
        op_rgb != MTL::BlendOperationAdd || src_alpha != MTL::BlendFactorOne ||
        dst_alpha != MTL::BlendFactorZero || op_alpha != MTL::BlendOperationAdd;
    color_attachment->setBlendingEnabled(blending_enabled);
    if (blending_enabled) {
      color_attachment->setSourceRGBBlendFactor(src_rgb);
      color_attachment->setDestinationRGBBlendFactor(dst_rgb);
      color_attachment->setRgbBlendOperation(op_rgb);
      color_attachment->setSourceAlphaBlendFactor(src_alpha);
      color_attachment->setDestinationAlphaBlendFactor(dst_alpha);
      color_attachment->setAlphaBlendOperation(op_alpha);
    }
  }

  // Configure vertex fetch layout for MSC stage-in.
  // NOTE: The translated shaders use vfetch (buffer load) to read vertices
  // directly from shared memory via SRV descriptors, NOT stage-in attributes.
  // This vertex descriptor may be unnecessary.
  const Shader& vertex_shader_ref = vertex_translation->shader();
  const auto& vertex_bindings = vertex_shader_ref.vertex_bindings();
  if (!vertex_bindings.empty()) {
    auto map_vertex_format =
        [](const ParsedVertexFetchInstruction::Attributes& attrs)
        -> MTL::VertexFormat {
      using xenos::VertexFormat;
      switch (attrs.data_format) {
        case VertexFormat::k_8_8_8_8:
          if (attrs.is_integer) {
            return attrs.is_signed ? MTL::VertexFormatChar4
                                   : MTL::VertexFormatUChar4;
          }
          return attrs.is_signed ? MTL::VertexFormatChar4Normalized
                                 : MTL::VertexFormatUChar4Normalized;
        case VertexFormat::k_2_10_10_10:
          // Metal only supports normalized variants of 10:10:10:2.
          return attrs.is_signed ? MTL::VertexFormatInt1010102Normalized
                                 : MTL::VertexFormatUInt1010102Normalized;
        case VertexFormat::k_10_11_11:
        case VertexFormat::k_11_11_10:
          return MTL::VertexFormatFloatRG11B10;
        case VertexFormat::k_16_16:
          if (attrs.is_integer) {
            return attrs.is_signed ? MTL::VertexFormatShort2
                                   : MTL::VertexFormatUShort2;
          }
          return attrs.is_signed ? MTL::VertexFormatShort2Normalized
                                 : MTL::VertexFormatUShort2Normalized;
        case VertexFormat::k_16_16_16_16:
          if (attrs.is_integer) {
            return attrs.is_signed ? MTL::VertexFormatShort4
                                   : MTL::VertexFormatUShort4;
          }
          return attrs.is_signed ? MTL::VertexFormatShort4Normalized
                                 : MTL::VertexFormatUShort4Normalized;
        case VertexFormat::k_16_16_FLOAT:
          return MTL::VertexFormatHalf2;
        case VertexFormat::k_16_16_16_16_FLOAT:
          return MTL::VertexFormatHalf4;
        case VertexFormat::k_32:
          if (attrs.is_integer) {
            return attrs.is_signed ? MTL::VertexFormatInt
                                   : MTL::VertexFormatUInt;
          }
          return MTL::VertexFormatFloat;
        case VertexFormat::k_32_32:
          if (attrs.is_integer) {
            return attrs.is_signed ? MTL::VertexFormatInt2
                                   : MTL::VertexFormatUInt2;
          }
          return MTL::VertexFormatFloat2;
        case VertexFormat::k_32_32_32_FLOAT:
          return MTL::VertexFormatFloat3;
        case VertexFormat::k_32_32_32_32:
          if (attrs.is_integer) {
            return attrs.is_signed ? MTL::VertexFormatInt4
                                   : MTL::VertexFormatUInt4;
          }
          return MTL::VertexFormatFloat4;
        case VertexFormat::k_32_32_32_32_FLOAT:
          return MTL::VertexFormatFloat4;
        default:
          return MTL::VertexFormatInvalid;
      }
    };

    MTL::VertexDescriptor* vertex_desc = MTL::VertexDescriptor::alloc()->init();

    uint32_t attr_index = static_cast<uint32_t>(kIRStageInAttributeStartIndex);
    for (const auto& binding : vertex_bindings) {
      uint64_t buffer_index =
          kIRVertexBufferBindPoint + uint64_t(binding.binding_index);
      auto layout = vertex_desc->layouts()->object(buffer_index);
      layout->setStride(binding.stride_words * 4);
      layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
      layout->setStepRate(1);

      for (const auto& attr : binding.attributes) {
        MTL::VertexFormat fmt = map_vertex_format(attr.fetch_instr.attributes);
        if (fmt == MTL::VertexFormatInvalid) {
          ++attr_index;
          continue;
        }
        auto attr_desc = vertex_desc->attributes()->object(attr_index);
        attr_desc->setFormat(fmt);
        attr_desc->setOffset(
            static_cast<NS::UInteger>(attr.fetch_instr.attributes.offset * 4));
        attr_desc->setBufferIndex(static_cast<NS::UInteger>(buffer_index));
        ++attr_index;
      }
    }

    desc->setVertexDescriptor(vertex_desc);
    vertex_desc->release();
  }

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

DxbcShaderTranslator::Modification
MetalCommandProcessor::GetCurrentVertexShaderModification(
    const Shader& shader, Shader::HostVertexShaderType host_vertex_shader_type,
    uint32_t interpolator_mask) const {
  const auto& regs = *register_file_;

  DxbcShaderTranslator::Modification modification(
      shader_translator_->GetDefaultVertexShaderModification(
          shader.GetDynamicAddressableRegisterCount(
              regs.Get<reg::SQ_PROGRAM_CNTL>().vs_num_reg),
          host_vertex_shader_type));

  modification.vertex.interpolator_mask = interpolator_mask;

  auto pa_cl_clip_cntl = regs.Get<reg::PA_CL_CLIP_CNTL>();
  uint32_t user_clip_planes =
      pa_cl_clip_cntl.clip_disable ? 0 : pa_cl_clip_cntl.ucp_ena;
  modification.vertex.user_clip_plane_count = xe::bit_count(user_clip_planes);
  modification.vertex.user_clip_plane_cull =
      uint32_t(user_clip_planes && pa_cl_clip_cntl.ucp_cull_only_ena);
  modification.vertex.vertex_kill_and =
      uint32_t((shader.writes_point_size_edge_flag_kill_vertex() & 0b100) &&
               !pa_cl_clip_cntl.vtx_kill_or);

  modification.vertex.output_point_size =
      uint32_t((shader.writes_point_size_edge_flag_kill_vertex() & 0b001) &&
               regs.Get<reg::VGT_DRAW_INITIATOR>().prim_type ==
                   xenos::PrimitiveType::kPointList);

  return modification;
}

DxbcShaderTranslator::Modification
MetalCommandProcessor::GetCurrentPixelShaderModification(
    const Shader& shader, uint32_t interpolator_mask, uint32_t param_gen_pos,
    reg::RB_DEPTHCONTROL normalized_depth_control) const {
  const auto& regs = *register_file_;

  DxbcShaderTranslator::Modification modification(
      shader_translator_->GetDefaultPixelShaderModification(
          shader.GetDynamicAddressableRegisterCount(
              regs.Get<reg::SQ_PROGRAM_CNTL>().ps_num_reg)));

  modification.pixel.interpolator_mask = interpolator_mask;
  modification.pixel.interpolators_centroid =
      interpolator_mask &
      ~xenos::GetInterpolatorSamplingPattern(
          regs.Get<reg::RB_SURFACE_INFO>().msaa_samples,
          regs.Get<reg::SQ_CONTEXT_MISC>().sc_sample_cntl,
          regs.Get<reg::SQ_INTERPOLATOR_CNTL>().sampling_pattern);

  if (param_gen_pos < xenos::kMaxInterpolators) {
    modification.pixel.param_gen_enable = 1;
    modification.pixel.param_gen_interpolator = param_gen_pos;
    modification.pixel.param_gen_point =
        uint32_t(regs.Get<reg::VGT_DRAW_INITIATOR>().prim_type ==
                 xenos::PrimitiveType::kPointList);
  } else {
    modification.pixel.param_gen_enable = 0;
    modification.pixel.param_gen_interpolator = 0;
    modification.pixel.param_gen_point = 0;
  }

  using DepthStencilMode = DxbcShaderTranslator::Modification::DepthStencilMode;
  if (shader.implicit_early_z_write_allowed() &&
      (!shader.writes_color_target(0) ||
       !draw_util::DoesCoverageDependOnAlpha(
           regs.Get<reg::RB_COLORCONTROL>()))) {
    modification.pixel.depth_stencil_mode = DepthStencilMode::kEarlyHint;
  } else {
    modification.pixel.depth_stencil_mode = DepthStencilMode::kNoModifiers;
  }

  return modification;
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
