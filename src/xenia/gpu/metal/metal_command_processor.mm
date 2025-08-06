/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_command_processor.h"

#include <cstring>
#include <ctime>

#include "third_party/stb/stb_image_write.h"
#include "xenia/ui/metal/metal_presenter.h"
#include "xenia/gpu/metal/metal_object_tracker.h"

// Forward declarations for Objective-C types
#ifdef __OBJC__
@protocol MTLTexture;
#else
typedef struct objc_object* id;
#endif

#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/primitive_processor.h"
#include "xenia/gpu/metal/metal_graphics_system.h"
#include "xenia/gpu/metal/metal_buffer_cache.h"
#include "xenia/gpu/metal/metal_pipeline_cache.h"
#include "xenia/gpu/metal/metal_primitive_processor.h"
#include "xenia/gpu/metal/metal_render_target_cache.h"
#include "xenia/gpu/metal/metal_shared_memory.h"
#include "xenia/gpu/metal/metal_texture_cache.h"

namespace xe {
namespace gpu {
namespace metal {

MetalCommandProcessor::MetalCommandProcessor(MetalGraphicsSystem* graphics_system,
                                             kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state),
      metal_graphics_system_(graphics_system) {}

MetalCommandProcessor::~MetalCommandProcessor() = default;

MTL::Device* MetalCommandProcessor::GetMetalDevice() const {
  return metal_graphics_system_->metal_device();
}

MTL::CommandQueue* MetalCommandProcessor::GetMetalCommandQueue() const {
  return metal_graphics_system_->metal_command_queue();
}

std::string MetalCommandProcessor::GetWindowTitleText() const {
  // TODO(wmarti): Add Metal device info
  return "Metal - EARLY DEVELOPMENT";
}

bool MetalCommandProcessor::SetupContext() {
  // Initialize cache systems
  pipeline_cache_ = std::make_unique<MetalPipelineCache>(
      this, register_file_, false);  // edram_rov_used = false for now
  buffer_cache_ = std::make_unique<MetalBufferCache>(
      this, register_file_, memory_);
  texture_cache_ = std::make_unique<MetalTextureCache>(
      this, register_file_, memory_);
  render_target_cache_ = std::make_unique<MetalRenderTargetCache>(
      this, register_file_, memory_);
  // Create shared memory
  shared_memory_ = std::make_unique<MetalSharedMemory>(
      *this, *memory_, trace_writer_);
  if (!shared_memory_->Initialize()) {
    XELOGE("Failed to initialize Metal shared memory");
    return false;
  }
  
  // Create primitive processor
  primitive_processor_ = std::make_unique<MetalPrimitiveProcessor>(
      *this, *register_file_, *memory_, trace_writer_, *shared_memory_);
  
  // Initialize each cache system
  if (!buffer_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal buffer cache");
    return false;
  }
  if (!texture_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal texture cache");
    return false;
  }
  if (!render_target_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal render target cache");
    return false;
  }
  if (!pipeline_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal pipeline cache");
    return false;
  }
  if (!primitive_processor_->Initialize()) {
    XELOGE("Failed to initialize Metal primitive processor");
    return false;
  }
  
  XELOGI("Metal cache systems initialized successfully");
  
  return CommandProcessor::SetupContext();
}

void MetalCommandProcessor::ShutdownContext() {
  // Force flush any pending commands
  if (submission_open_) {
    XELOGI("Metal ShutdownContext: Forcing EndSubmission before shutdown");
    EndSubmission(false);
  }
  
  // Clean up any remaining command buffer
  if (current_command_buffer_) {
    XELOGI("Metal ShutdownContext: Found uncommitted command buffer - this shouldn't happen");
    // Don't track release here - EndSubmission should have handled it
    current_command_buffer_ = nullptr;
  }
  
  // Generate PNG before shutting down to capture final render output
  XELOGI("Metal ShutdownContext: Generating PNG before shutdown...");
  
  ui::Presenter* presenter = graphics_system_->presenter();
  if (presenter) {
    ui::RawImage raw_image;
    if (presenter->CaptureGuestOutput(raw_image)) {
      // Save PNG with timestamp to avoid overwrites
      auto now = std::time(nullptr);
      std::string filename = "xenia_metal_output_" + std::to_string(now) + ".png";
      FILE* file = std::fopen(filename.c_str(), "wb");
      if (file) {
        auto callback = [](void* context, void* data, int size) {
          std::fwrite(data, 1, size, (FILE*)context);
        };
        stbi_write_png_to_func(callback, file, static_cast<int>(raw_image.width),
                               static_cast<int>(raw_image.height), 4,
                               raw_image.data.data(),
                               static_cast<int>(raw_image.stride));
        std::fclose(file);
        XELOGI("Metal ShutdownContext: PNG saved as {}", filename);
      } else {
        XELOGE("Metal ShutdownContext: Failed to create PNG file {}", filename);
      }
    } else {
      XELOGW("Metal ShutdownContext: Failed to capture guest output for PNG");
    }
  } else {
    XELOGW("Metal ShutdownContext: No presenter available for PNG generation");
  }
  
  // Shutdown cache systems in reverse order
  if (primitive_processor_) {
    primitive_processor_->Shutdown();
    primitive_processor_.reset();
  }
  if (pipeline_cache_) {
    pipeline_cache_->Shutdown();
    pipeline_cache_.reset();
  }
  if (render_target_cache_) {
    render_target_cache_->Shutdown();
    render_target_cache_.reset();
  }
  if (texture_cache_) {
    texture_cache_->Shutdown();
    texture_cache_.reset();
  }
  if (buffer_cache_) {
    buffer_cache_->Shutdown();
    buffer_cache_.reset();
  }
  if (shared_memory_) {
    shared_memory_->Shutdown();
    shared_memory_.reset();
  }
  
  XELOGI("Metal cache systems shutdown");
  
  // Print Metal object tracking report
  MetalObjectTracker::Instance().PrintReport();
  
  CommandProcessor::ShutdownContext();
}

void MetalCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                      uint32_t frontbuffer_width,
                                      uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");
  
  XELOGI("Metal IssueSwap: frontbuffer 0x{:08X}, {}x{}", 
         frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
  
  // End the current submission and mark it as a swap
  if (!EndSubmission(true)) {
    XELOGE("Metal IssueSwap: Failed to end submission");
    return;
  }
  
  // Track frame boundaries
  frame_count_++;
  XELOGI("Metal IssueSwap: Frame {} complete", frame_count_);
  
  // Update presenter with guest output for PNG capture
  ui::Presenter* presenter = graphics_system_->presenter();
  if (presenter) {
    // For now, just refresh with basic dimensions
    // TODO: Get actual frontbuffer dimensions and data from EDRAM or guest memory
    presenter->RefreshGuestOutput(
        frontbuffer_width ? frontbuffer_width : 1280,
        frontbuffer_height ? frontbuffer_height : 720,
        1280, 720,  // Display dimensions
        [this, frontbuffer_width, frontbuffer_height](ui::Presenter::GuestOutputRefreshContext& context) -> bool {
          // Get current color render target from render target cache
          MTL::Texture* color_target = render_target_cache_->GetColorTarget(0);
          if (!color_target) {
            XELOGW("Metal IssueSwap: No color render target available for guest output");
            return false;
          }
          
          // Cast to Metal-specific context to get the destination texture
          auto* metal_context = static_cast<xe::ui::metal::MetalGuestOutputRefreshContext*>(&context);
          id dest_texture = metal_context->resource_uav_capable();
          
          // Get MetalPresenter to handle the texture copy (since it can use Objective-C)
          auto* metal_presenter = static_cast<xe::ui::metal::MetalPresenter*>(graphics_system_->presenter());
          if (!metal_presenter) {
            XELOGE("Metal IssueSwap: Failed to get MetalPresenter for texture copy");
            return false;
          }
          
          // Use the helper method to copy the texture
          return metal_presenter->CopyTextureToGuestOutput(color_target, dest_texture);
        });
    
    // Generate PNG after successful guest output refresh
    ui::RawImage raw_image;
    if (presenter->CaptureGuestOutput(raw_image)) {
      // Save PNG with frame number for trace analysis
      std::string filename = "xenia_metal_frame_" + std::to_string(frame_count_) + ".png";
      FILE* file = std::fopen(filename.c_str(), "wb");
      if (file) {
        auto callback = [](void* context, void* data, int size) {
          std::fwrite(data, 1, size, (FILE*)context);
        };
        stbi_write_png_to_func(callback, file, static_cast<int>(raw_image.width),
                               static_cast<int>(raw_image.height), 4,
                               raw_image.data.data(),
                               static_cast<int>(raw_image.stride));
        std::fclose(file);
        XELOGI("Metal IssueSwap: PNG saved as {} ({}x{})", filename, raw_image.width, raw_image.height);
      } else {
        XELOGE("Metal IssueSwap: Failed to create PNG file {}", filename);
      }
    } else {
      XELOGW("Metal IssueSwap: Failed to capture guest output for PNG");
    }
  }
  
  // Begin a new submission for the next frame
  BeginSubmission(true);
}

void MetalCommandProcessor::OnGammaRamp256EntryTableValueWritten() {
  // TODO(wmarti): Handle gamma ramp changes
}

void MetalCommandProcessor::OnGammaRampPWLValueWritten() {
  // TODO(wmarti): Handle gamma ramp changes
}

void MetalCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  // TODO(wmarti): Handle Metal-specific register writes
  CommandProcessor::WriteRegister(index, value);
}

void MetalCommandProcessor::TracePlaybackWroteMemory(uint32_t base_ptr,
                                                     uint32_t length) {
  // TODO(wmarti): Handle trace playback memory writes
}

void MetalCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {
  // TODO(wmarti): Restore EDRAM state from snapshot
  XELOGW("Metal RestoreEdramSnapshot not implemented");
}

Shader* MetalCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                          uint32_t guest_address,
                                          const uint32_t* host_address,
                                          uint32_t dword_count) {
  // Delegate to the pipeline cache for shader loading
  return pipeline_cache_->LoadShader(shader_type, host_address, dword_count);
}

// Helper function to swap vertex element endianness
void SwapVertexElement(uint8_t* data, xenos::VertexFormat format, xenos::Endian endian) {
  switch (format) {
    case xenos::VertexFormat::k_32_FLOAT:
    case xenos::VertexFormat::k_32:
      // Single 32-bit value
      *(uint32_t*)data = xenos::GpuSwap(*(uint32_t*)data, endian);
      break;
      
    case xenos::VertexFormat::k_16_16_FLOAT:
    case xenos::VertexFormat::k_16_16:
      // Two 16-bit values - need to handle different endian modes
      if (endian == xenos::Endian::k8in16 || endian == xenos::Endian::kNone) {
        // Can use 16-bit swap directly
        *(uint16_t*)(data + 0) = xenos::GpuSwap(*(uint16_t*)(data + 0), endian);
        *(uint16_t*)(data + 2) = xenos::GpuSwap(*(uint16_t*)(data + 2), endian);
      } else {
        // For k8in32 or k16in32, treat as 32-bit value
        *(uint32_t*)data = xenos::GpuSwap(*(uint32_t*)data, endian);
      }
      break;
      
    case xenos::VertexFormat::k_32_32_FLOAT:
    case xenos::VertexFormat::k_32_32:
      // Two 32-bit values
      *(uint32_t*)(data + 0) = xenos::GpuSwap(*(uint32_t*)(data + 0), endian);
      *(uint32_t*)(data + 4) = xenos::GpuSwap(*(uint32_t*)(data + 4), endian);
      break;
      
    case xenos::VertexFormat::k_32_32_32_FLOAT:
      // Three 32-bit values
      *(uint32_t*)(data + 0) = xenos::GpuSwap(*(uint32_t*)(data + 0), endian);
      *(uint32_t*)(data + 4) = xenos::GpuSwap(*(uint32_t*)(data + 4), endian);
      *(uint32_t*)(data + 8) = xenos::GpuSwap(*(uint32_t*)(data + 8), endian);
      break;
      
    case xenos::VertexFormat::k_32_32_32_32_FLOAT:
    case xenos::VertexFormat::k_32_32_32_32:
      // Four 32-bit values
      *(uint32_t*)(data + 0) = xenos::GpuSwap(*(uint32_t*)(data + 0), endian);
      *(uint32_t*)(data + 4) = xenos::GpuSwap(*(uint32_t*)(data + 4), endian);
      *(uint32_t*)(data + 8) = xenos::GpuSwap(*(uint32_t*)(data + 8), endian);
      *(uint32_t*)(data + 12) = xenos::GpuSwap(*(uint32_t*)(data + 12), endian);
      break;
      
    case xenos::VertexFormat::k_8_8_8_8:
    case xenos::VertexFormat::k_2_10_10_10:
    case xenos::VertexFormat::k_10_11_11:
    case xenos::VertexFormat::k_11_11_10:
      // Single 32-bit packed value
      *(uint32_t*)data = xenos::GpuSwap(*(uint32_t*)data, endian);
      break;
      
    case xenos::VertexFormat::k_16_16_16_16_FLOAT:
    case xenos::VertexFormat::k_16_16_16_16:
      // Four 16-bit values - need to handle different endian modes
      if (endian == xenos::Endian::k8in16 || endian == xenos::Endian::kNone) {
        // Can use 16-bit swap directly
        *(uint16_t*)(data + 0) = xenos::GpuSwap(*(uint16_t*)(data + 0), endian);
        *(uint16_t*)(data + 2) = xenos::GpuSwap(*(uint16_t*)(data + 2), endian);
        *(uint16_t*)(data + 4) = xenos::GpuSwap(*(uint16_t*)(data + 4), endian);
        *(uint16_t*)(data + 6) = xenos::GpuSwap(*(uint16_t*)(data + 6), endian);
      } else {
        // For k8in32 or k16in32, treat as two 32-bit values
        *(uint32_t*)(data + 0) = xenos::GpuSwap(*(uint32_t*)(data + 0), endian);
        *(uint32_t*)(data + 4) = xenos::GpuSwap(*(uint32_t*)(data + 4), endian);
      }
      break;
      
    default:
      XELOGW("SwapVertexElement: Unhandled vertex format {}", static_cast<uint32_t>(format));
      break;
  }
}

bool MetalCommandProcessor::IssueDraw(xenos::PrimitiveType prim_type,
                                      uint32_t index_count,
                                      IndexBufferInfo* index_buffer_info,
                                      bool major_mode_explicit) {
  // Phase B Step 2: Pipeline state object creation and caching
  
  const char* prim_type_name = "unknown";
  switch (prim_type) {
    case xenos::PrimitiveType::kNone:
      prim_type_name = "none";
      break;
    case xenos::PrimitiveType::kPointList:
      prim_type_name = "point_list";
      break;
    case xenos::PrimitiveType::kLineList:
      prim_type_name = "line_list";
      break;
    case xenos::PrimitiveType::kLineStrip:
      prim_type_name = "line_strip";
      break;
    case xenos::PrimitiveType::kTriangleList:
      prim_type_name = "triangle_list";
      break;
    case xenos::PrimitiveType::kTriangleFan:
      prim_type_name = "triangle_fan";
      break;
    case xenos::PrimitiveType::kTriangleStrip:
      prim_type_name = "triangle_strip";
      break;
    case xenos::PrimitiveType::kTriangleWithWFlags:
      prim_type_name = "triangle_with_w_flags";
      break;
    case xenos::PrimitiveType::kRectangleList:
      prim_type_name = "rectangle_list";
      break;
    case xenos::PrimitiveType::kLineLoop:
      prim_type_name = "line_loop";
      break;
    case xenos::PrimitiveType::kQuadList:
      prim_type_name = "quad_list";
      break;
    case xenos::PrimitiveType::kQuadStrip:
      prim_type_name = "quad_strip";
      break;
    case xenos::PrimitiveType::kPolygon:
      prim_type_name = "polygon";
      break;
    default:
      // TODO: Phase D - Add support for explicit major mode primitive types
      prim_type_name = "unknown";
      XELOGW("Metal IssueDraw: Unsupported primitive type: 0x{:02X}", static_cast<uint32_t>(prim_type));
      break;
  }
  
  XELOGI("Metal IssueDraw: prim_type={}, index_count={}, major_mode_explicit={}",
         prim_type_name, index_count, major_mode_explicit);
  
  if (index_buffer_info) {
    XELOGI("Metal IssueDraw: index_buffer guest_base=0x{:08X}, format={}, endianness={}",
           index_buffer_info->guest_base, 
           static_cast<int>(index_buffer_info->format),
           static_cast<int>(index_buffer_info->endianness));
  }

  // Begin submission for this draw
  if (!BeginSubmission(true)) {
    XELOGE("Metal IssueDraw: Failed to begin submission");
    return false;
  }
  
  // Process primitives through the primitive processor
  // This handles primitive restart, primitive type conversion, etc.
  XELOGI("Metal IssueDraw: About to call primitive processor Process()");
  PrimitiveProcessor::ProcessingResult primitive_processing_result;
  if (!primitive_processor_->Process(primitive_processing_result)) {
    XELOGE("Metal IssueDraw: Primitive processing failed");
    return false;
  }
  
  // Log primitive restart status for testing
  const auto& pa_su_sc_mode_cntl = register_file_->Get<reg::PA_SU_SC_MODE_CNTL>();
  XELOGI("Metal IssueDraw: Primitive restart enabled: {}, reset index enabled: {}", 
         pa_su_sc_mode_cntl.multi_prim_ib_ena ? "YES" : "NO",
         primitive_processing_result.host_primitive_reset_enabled ? "YES" : "NO");
  
  if (primitive_processing_result.index_buffer_type == 
      PrimitiveProcessor::ProcessedIndexBufferType::kHostConverted) {
    XELOGI("Metal IssueDraw: Index buffer was CONVERTED by primitive processor (likely for restart handling)");
  }
  
  XELOGI("Metal IssueDraw: Primitive processing result - "
         "type: {}, index_count: {}, index_buffer_type: {}",
         static_cast<uint32_t>(primitive_processing_result.host_primitive_type),
         primitive_processing_result.host_draw_vertex_count,
         static_cast<uint32_t>(primitive_processing_result.index_buffer_type));
  
  // Check if there's anything to draw
  if (!primitive_processing_result.host_draw_vertex_count) {
    XELOGI("Metal IssueDraw: Nothing to draw after primitive processing");
    return true;
  }
  
  XELOGI("Metal IssueDraw: Primitive processing result - "
         "type: {}, index_count: {}, index_buffer_type: {}",
         static_cast<uint32_t>(primitive_processing_result.host_primitive_type),
         primitive_processing_result.host_draw_vertex_count,
         static_cast<uint32_t>(primitive_processing_result.index_buffer_type));

  // Phase B Step 2: Create pipeline state for this draw call
  // This demonstrates the shader→pipeline integration
  XELOGI("Metal IssueDraw: Attempting pipeline state creation...");
  
  // Phase D.1: Get real Xbox 360 shaders from register state
  // Use active shaders loaded from Xbox 360 game data instead of hardcoded values
  Shader* vertex_shader = active_vertex_shader();
  Shader* pixel_shader = active_pixel_shader();
  
  if (!vertex_shader || !pixel_shader) {
    XELOGE("Metal IssueDraw: Missing active shaders - vertex: {}, pixel: {}",
           vertex_shader ? "present" : "missing", 
           pixel_shader ? "present" : "missing");
    return false;
  }
  
  XELOGI("Metal IssueDraw: Using real Xbox 360 shaders - vertex: {:016x}, pixel: {:016x}",
         vertex_shader->ucode_data_hash(), pixel_shader->ucode_data_hash());
  
  // IMPORTANT: Update render targets BEFORE creating pipeline so we get the correct formats
  // Parse RB_SURFACE_INFO and RB_COLOR_INFO registers to determine active render targets
  uint32_t rb_surface_info = register_file_->values[XE_GPU_REG_RB_SURFACE_INFO];
  uint32_t rb_color_info[4] = {
    register_file_->values[XE_GPU_REG_RB_COLOR_INFO],
    register_file_->values[XE_GPU_REG_RB_COLOR1_INFO],
    register_file_->values[XE_GPU_REG_RB_COLOR2_INFO],
    register_file_->values[XE_GPU_REG_RB_COLOR3_INFO]
  };
  uint32_t rb_depth_info = register_file_->values[XE_GPU_REG_RB_DEPTH_INFO];
  
  // Log all render target info for debugging
  XELOGI("Metal IssueDraw: RB_SURFACE_INFO = 0x{:08X}", rb_surface_info);
  XELOGI("Metal IssueDraw: RB_COLOR_INFO[0-3] = 0x{:08X}, 0x{:08X}, 0x{:08X}, 0x{:08X}",
         rb_color_info[0], rb_color_info[1], rb_color_info[2], rb_color_info[3]);
  XELOGI("Metal IssueDraw: RB_DEPTH_INFO = 0x{:08X}", rb_depth_info);
  
  // Process all color render targets - Xbox 360 allows multiple RTs at same address
  // This is used for effects like deferred lighting (4D5307E6) where different
  // shader outputs write to the same EDRAM location
  uint32_t rt_count = 0;
  uint32_t color_targets[4] = {0};
  uint32_t duplicate_rt_mask = 0;  // Track which RTs share addresses
  
  // First pass: collect all active RTs
  for (uint32_t i = 0; i < 4; ++i) {
    uint32_t base_address = rb_color_info[i] & 0xFFFFF000;
    if (base_address != 0) {
      color_targets[rt_count++] = rb_color_info[i];
    }
  }
  
  // Second pass: detect duplicates for special handling
  for (uint32_t i = 0; i < rt_count; ++i) {
    uint32_t addr_i = color_targets[i] & 0xFFFFF000;
    for (uint32_t j = i + 1; j < rt_count; ++j) {
      uint32_t addr_j = color_targets[j] & 0xFFFFF000;
      if (addr_i == addr_j) {
        duplicate_rt_mask |= (1u << i) | (1u << j);
        XELOGW("Metal IssueDraw: RT{} and RT{} share address 0x{:08X}", 
               i, j, addr_i);
      }
    }
  }
  
  // For Metal, we must skip duplicates to avoid validation errors
  // TODO: Implement proper handling via shader modification or multiple passes
  if (duplicate_rt_mask) {
    // Keep only the first RT at each address
    uint32_t filtered_count = 0;
    uint32_t seen_addresses[4] = {0};
    uint32_t seen_count = 0;
    
    for (uint32_t i = 0; i < rt_count; ++i) {
      uint32_t addr = color_targets[i] & 0xFFFFF000;
      bool is_duplicate = false;
      for (uint32_t j = 0; j < seen_count; ++j) {
        if (seen_addresses[j] == addr) {
          is_duplicate = true;
          break;
        }
      }
      if (!is_duplicate) {
        color_targets[filtered_count++] = color_targets[i];
        seen_addresses[seen_count++] = addr;
      }
    }
    rt_count = filtered_count;
    XELOGW("Metal IssueDraw: Filtered {} duplicate RTs, {} remain", 
           4 - rt_count, rt_count);
  }
  
  // Set render targets in cache BEFORE creating pipeline
  // Check if depth is actually used - need to check RB_DEPTHCONTROL, not RB_DEPTH_INFO bit 0
  // RB_DEPTHCONTROL bit 1 = depth test enable, bit 2 = depth write enable
  uint32_t rb_depthcontrol = register_file_->values[XE_GPU_REG_RB_DEPTHCONTROL];
  bool depth_enabled = (rb_depthcontrol & 0x00000002) || (rb_depthcontrol & 0x00000004);
  uint32_t depth_target_info = depth_enabled ? rb_depth_info : 0;
  render_target_cache_->SetRenderTargets(rt_count, color_targets, depth_target_info);
  
  // Create pipeline description with real shader hashes
  MetalPipelineCache::RenderPipelineDescription pipeline_desc = {};
  pipeline_desc.primitive_type = prim_type;
  pipeline_desc.vertex_shader_hash = vertex_shader->ucode_data_hash();
  pipeline_desc.pixel_shader_hash = pixel_shader->ucode_data_hash();
  
  // TODO: Set shader modifications based on register state
  pipeline_desc.vertex_shader_modification = 0;
  pipeline_desc.pixel_shader_modification = 0;
  
  // Get render target formats from render target cache
  // Initialize all formats to invalid
  for (int i = 0; i < 4; ++i) {
    pipeline_desc.color_formats[i] = MTL::PixelFormatInvalid;
  }
  pipeline_desc.depth_format = MTL::PixelFormatInvalid;
  pipeline_desc.num_color_attachments = 0;
  pipeline_desc.sample_count = 1;  // Default to no MSAA
  
  // Query the render target cache for the current formats
  for (uint32_t i = 0; i < 4; ++i) {
    MTL::Texture* color_target = render_target_cache_->GetColorTarget(i);
    if (color_target) {
      pipeline_desc.color_formats[i] = color_target->pixelFormat();
      pipeline_desc.num_color_attachments = i + 1;
      // Get sample count from the first render target
      if (i == 0) {
        pipeline_desc.sample_count = color_target->sampleCount();
      }
    }
  }
  
  // Get depth format
  MTL::Texture* depth_target = render_target_cache_->GetDepthTarget();
  if (depth_target) {
    pipeline_desc.depth_format = depth_target->pixelFormat();
    // If no color targets, get sample count from depth target
    if (pipeline_desc.num_color_attachments == 0) {
      pipeline_desc.sample_count = depth_target->sampleCount();
    }
  }
  
  // If no render targets are bound (neither color nor depth), use a default format for the dummy target
  if (pipeline_desc.num_color_attachments == 0 && pipeline_desc.depth_format == MTL::PixelFormatInvalid) {
    // Get MSAA samples from RB_SURFACE_INFO for consistency
    uint32_t msaa_samples = (rb_surface_info >> 16) & 0x3;
    uint32_t sample_count = msaa_samples ? (1 << msaa_samples) : 1;
    
    pipeline_desc.color_formats[0] = MTL::PixelFormatBGRA8Unorm;
    pipeline_desc.num_color_attachments = 1;
    pipeline_desc.sample_count = sample_count;
  }
  
  // Log pipeline descriptor details for debugging
  XELOGI("Metal IssueDraw: Pipeline descriptor - {} color attachments, sample count: {}",
         pipeline_desc.num_color_attachments, pipeline_desc.sample_count);
  for (uint32_t i = 0; i < pipeline_desc.num_color_attachments; ++i) {
    XELOGI("  Color attachment {}: format {}", i, static_cast<uint32_t>(pipeline_desc.color_formats[i]));
  }
  
  // Request pipeline state from cache (this will create it if needed)
  MTL::RenderPipelineState* pipeline_state = 
      pipeline_cache_->GetRenderPipelineState(pipeline_desc);
  
  if (pipeline_state) {
    XELOGI("Metal IssueDraw: Successfully obtained pipeline state");
    
    // Phase C Step 1: Metal Command Buffer and Render Pass Encoding
    MTL::CommandQueue* command_queue = GetMetalCommandQueue();
    if (command_queue) {
      MTL::CommandBuffer* command_buffer = current_command_buffer_;
      if (!command_buffer) {
        // Create a new command buffer if we don't have one
        BeginSubmission(true);
        command_buffer = current_command_buffer_;
      }
      if (command_buffer) {
        XELOGI("Metal IssueDraw: Using command buffer for rendering");
        
        // Phase C Step 3: Enhanced Metal frame debugging support
        char draw_label[256];
        snprintf(draw_label, sizeof(draw_label), "Xbox360Draw_%s_idx%u", 
                prim_type_name, index_count);
        command_buffer->setLabel(NS::String::string(draw_label, NS::UTF8StringEncoding));
        
        // Render targets were already set up before pipeline creation
        
        // Get render pass descriptor from cache
        // For now, if no descriptor available, create a dummy one
        XELOGI("Metal IssueDraw: Getting render pass descriptor from cache...");
        MTL::RenderPassDescriptor* render_pass = render_target_cache_->GetRenderPassDescriptor();
        XELOGI("Metal IssueDraw: Got render pass: {}", render_pass ? "valid" : "null");
        
        if (!render_pass) {
          // Fallback: Create a dummy render pass for testing
          XELOGW("Metal IssueDraw: No render pass from cache, creating dummy render target");
          render_pass = MTL::RenderPassDescriptor::alloc()->init();
          TRACK_METAL_OBJECT(render_pass, "MTL::RenderPassDescriptor");
          
          MTL::TextureDescriptor* texture_desc = MTL::TextureDescriptor::alloc()->init();
          TRACK_METAL_OBJECT(texture_desc, "MTL::TextureDescriptor");
          texture_desc->setWidth(1280);
          texture_desc->setHeight(720);
          texture_desc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
          texture_desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
          
          MTL::Texture* render_target = GetMetalDevice()->newTexture(texture_desc);
          render_pass->colorAttachments()->object(0)->setTexture(render_target);
          render_pass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
          render_pass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
          render_pass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor(0.1, 0.0, 0.2, 1.0));
          
          TRACK_METAL_RELEASE(texture_desc);
          texture_desc->release();
          // Note: render_target will be released automatically when render pass is released
        }
        
        // Create render command encoder
        XELOGI("Metal IssueDraw: Creating render command encoder...");
        MTL::RenderCommandEncoder* encoder = command_buffer->renderCommandEncoder(render_pass);
        XELOGI("Metal IssueDraw: Render encoder created: {}", encoder ? "valid" : "null");
        if (encoder) {
          // Track encoder creation
          TRACK_METAL_OBJECT(encoder, "MTL::RenderCommandEncoder");
          char encoder_label[256];
          snprintf(encoder_label, sizeof(encoder_label), "Xbox360RenderPass_%s_hash_0x%llx_0x%llx", 
                  prim_type_name, pipeline_desc.vertex_shader_hash, pipeline_desc.pixel_shader_hash);
          encoder->setLabel(NS::String::string(encoder_label, NS::UTF8StringEncoding));
          
          // Phase C Step 2: Pipeline State and Draw Call Encoding
          encoder->setRenderPipelineState(pipeline_state);
          
          XELOGI("Metal IssueDraw: Set pipeline state and created render encoder");
          XELOGI("Metal IssueDraw: Render pass label: {}", encoder_label);
          
          // Phase C Step 2: Enhanced debugging - Push debug group
          encoder->pushDebugGroup(NS::String::string("Xbox360DrawCommands", NS::UTF8StringEncoding));
          
          // Phase C Step 3: Vertex Buffer Support from Xbox 360 guest memory
          // Process vertex bindings from the active vertex shader
          std::vector<MTL::Buffer*> vertex_buffers_to_release;
          
          if (vertex_shader) {
            const auto& vertex_bindings = vertex_shader->vertex_bindings();
          XELOGI("Metal IssueDraw: Processing {} vertex bindings", vertex_bindings.size());
            
            for (const auto& binding : vertex_bindings) {
              // Get the vertex fetch constant for this binding
              xenos::xe_gpu_vertex_fetch_t vfetch_constant = 
                  register_file_->GetVertexFetch(binding.fetch_constant);
              
              if (vfetch_constant.type != xenos::FetchConstantType::kVertex) {
              XELOGW("Metal IssueDraw: Vertex fetch constant {} is not a vertex type", 
                       binding.fetch_constant);
                continue;
              }
              
              // Calculate the actual address and size
              uint32_t address = vfetch_constant.address << 2;  // Convert from dwords to bytes
              uint32_t size = vfetch_constant.size << 2;        // Convert from words to bytes
              
            XELOGI("Metal IssueDraw: Vertex binding {} - fetch constant {}, address 0x{:08X}, size {} bytes, stride {} words",
                     binding.binding_index, binding.fetch_constant, address, size, binding.stride_words);
              
              // Map guest memory to get vertex data
              const void* vertex_data = memory_->TranslatePhysical(address);
              if (!vertex_data) {
              XELOGW("Metal IssueDraw: Failed to translate vertex buffer address 0x{:08X}", address);
                continue;
              }
              
              // Check if we need endian conversion
              MTL::Buffer* vertex_buffer = nullptr;
              
              if (vfetch_constant.endian != xenos::Endian::kNone) {
                // Need to swap endianness - create a copy with swapped data
                void* swapped_data = std::malloc(size);
                if (swapped_data) {
                  std::memcpy(swapped_data, vertex_data, size);
                  
                  // Get vertex format info from the binding
                  // For now, log the endian type and handle common cases
                  XELOGI("Metal IssueDraw: Vertex buffer needs endian swap - endian type: {}", 
                         static_cast<uint32_t>(vfetch_constant.endian));
                  
                  // Swap based on the endian type and stride
                  uint32_t stride_bytes = binding.stride_words * 4;
                  uint32_t vertex_count = size / stride_bytes;
                  
                  for (uint32_t v = 0; v < vertex_count; v++) {
                    uint8_t* vertex_start = static_cast<uint8_t*>(swapped_data) + (v * stride_bytes);
                    
                    // For each element in the vertex
                    for (const auto& attribute : binding.attributes) {
                      const auto& fetch = attribute.fetch_instr;
                      uint8_t* element_data = vertex_start + fetch.attributes.offset * 4;
                      
                      // Swap based on element format
                      SwapVertexElement(element_data, fetch.attributes.data_format, vfetch_constant.endian);
                    }
                  }
                  
                  vertex_buffer = GetMetalDevice()->newBuffer(
                      swapped_data, 
                      size, 
                      MTL::ResourceStorageModeShared);
                  TRACK_METAL_OBJECT(vertex_buffer, "MTL::Buffer[VertexSwapped]");
                  
                  std::free(swapped_data);
                }
              } else {
                // No endian swap needed - use data directly
                vertex_buffer = GetMetalDevice()->newBuffer(
                    vertex_data, 
                    size, 
                    MTL::ResourceStorageModeShared);
                  TRACK_METAL_OBJECT(vertex_buffer, "MTL::Buffer[VertexDirect]");
              }
              
              if (vertex_buffer) {
                // Label the buffer for debugging
                char label[256];
                snprintf(label, sizeof(label), "Xbox360VertexBuffer_%d_0x%08X", 
                        binding.binding_index, address);
                vertex_buffer->setLabel(NS::String::string(label, NS::UTF8StringEncoding));
                
                // Bind vertex buffer at the appropriate index
                // TODO: Map shader binding index to Metal buffer index properly
                encoder->setVertexBuffer(vertex_buffer, 0, binding.binding_index);
                
                XELOGI("Metal IssueDraw: Bound vertex buffer {} ({} bytes) from guest address 0x{:08X}", 
                       binding.binding_index, size, address);
                
                // Log vertex attributes for debugging
                if (!binding.attributes.empty() && size >= binding.stride_words * 4) {
                  XELOGI("Metal IssueDraw: Vertex has {} attributes with stride {} bytes", 
                         binding.attributes.size(), binding.stride_words * 4);
                  
                  // Only log raw data if we have a reasonable stride
                  if (binding.stride_words > 0 && binding.stride_words < 64) {
                    const uint32_t* vertex_data_u32 = static_cast<const uint32_t*>(vertex_data);
                    XELOGI("Metal IssueDraw: First vertex raw data (hex): 0x{:08X} 0x{:08X} 0x{:08X} 0x{:08X}",
                           xe::byte_swap(vertex_data_u32[0]), xe::byte_swap(vertex_data_u32[1]), 
                           xe::byte_swap(vertex_data_u32[2]), xe::byte_swap(vertex_data_u32[3]));
                    
                    // If we did endian swapping, show the swapped values as floats
                    if (vfetch_constant.endian != xenos::Endian::kNone && binding.attributes.size() > 0) {
                      const auto& first_attr = binding.attributes[0].fetch_instr;
                      if (first_attr.attributes.data_format == xenos::VertexFormat::k_32_32_FLOAT ||
                          first_attr.attributes.data_format == xenos::VertexFormat::k_32_32_32_FLOAT ||
                          first_attr.attributes.data_format == xenos::VertexFormat::k_32_32_32_32_FLOAT) {
                        // Show the data after endian swap as floats
                        float swapped_floats[4];
                        for (int i = 0; i < 4; i++) {
                          uint32_t swapped = xenos::GpuSwap(vertex_data_u32[i], vfetch_constant.endian);
                          swapped_floats[i] = *reinterpret_cast<float*>(&swapped);
                        }
                        XELOGI("Metal IssueDraw: After endian swap as floats: ({:.3f}, {:.3f}, {:.3f}, {:.3f})",
                               swapped_floats[0], swapped_floats[1], swapped_floats[2], swapped_floats[3]);
                      }
                    }
                  }
                }
                
                // Track for cleanup
                vertex_buffers_to_release.push_back(vertex_buffer);
              } else {
              XELOGW("Metal IssueDraw: Failed to create vertex buffer for binding {}", 
                       binding.binding_index);
              }
          }
          }
          
          // If no vertex buffers were bound, create a fallback triangle
          if (vertex_buffers_to_release.empty()) {
            XELOGW("Metal IssueDraw: No vertex buffers bound, using fallback triangle");
            float triangle_vertices[] = {
                // position (x, y, z)
                -0.5f, -0.5f, 0.0f,   // bottom left
                 0.5f, -0.5f, 0.0f,   // bottom right  
                 0.0f,  0.5f, 0.0f    // top center
          };
            
          MTL::Buffer* vertex_buffer = GetMetalDevice()->newBuffer(
                triangle_vertices, sizeof(triangle_vertices), MTL::ResourceStorageModeShared);
          TRACK_METAL_OBJECT(vertex_buffer, "MTL::Buffer[TriangleTest]");
            
            if (vertex_buffer) {
              vertex_buffer->setLabel(NS::String::string("FallbackTriangleBuffer", NS::UTF8StringEncoding));
            encoder->setVertexBuffer(vertex_buffer, 0, 0);
            XELOGI("Metal IssueDraw: Created and bound fallback triangle buffer ({} bytes)", 
                     sizeof(triangle_vertices));
              vertex_buffers_to_release.push_back(vertex_buffer);
          }
          }
          
          // Phase D: Bind required shader resource buffers
          // The converted shaders expect these buffers to be bound:
          // - Buffer at index 2: struct.top_level_global_ab[0] (global register state)
          // - Buffer at index 4: drawArguments[0] (draw parameters)
          // - Buffer at index 5: uniforms[0] (shader constants)
          
          // Create dummy buffers to satisfy shader requirements
          // In a full implementation, these would contain actual Xbox 360 GPU state
          
          // Buffer 2: Global register state from actual Xbox 360 GPU
          // The Xbox 360 GPU has a large register file that contains all GPU state
          // We'll pass a subset of important registers that shaders might need
          struct GlobalRegisters {
              // Viewport and scissor registers
              float viewport_scale[3];      // PA_CL_VPORT_XSCALE, YSCALE, ZSCALE
              float viewport_offset[3];     // PA_CL_VPORT_XOFFSET, YOFFSET, ZOFFSET
              
              // Screen extent registers
              uint32_t screen_scissor_tl;  // PA_SC_SCREEN_SCISSOR_TL
              uint32_t screen_scissor_br;  // PA_SC_SCREEN_SCISSOR_BR
              
              // Render target configuration
              uint32_t rb_surface_info;     // RB_SURFACE_INFO
              uint32_t rb_color_info;       // RB_COLOR_INFO
              uint32_t rb_depth_info;       // RB_DEPTH_INFO
              
              // Alpha test and blend state
              uint32_t rb_colorcontrol;     // RB_COLORCONTROL
              uint32_t rb_blendcontrol[4];  // RB_BLENDCONTROL_0 through 3
              
              // Padding to align to 256 bytes for now
              uint32_t padding[256 - 19];
          } global_registers = {};
          
          // Read actual register values from the Xbox 360 GPU register file
          const uint32_t* regs = register_file_->values;
            
          // Viewport registers (0x20D8 - 0x20DD)
          // Note: The register file already stores values in the correct endianness
          global_registers.viewport_scale[0] = *reinterpret_cast<const float*>(&regs[XE_GPU_REG_PA_CL_VPORT_XSCALE]);
          global_registers.viewport_scale[1] = *reinterpret_cast<const float*>(&regs[XE_GPU_REG_PA_CL_VPORT_YSCALE]);
          global_registers.viewport_scale[2] = *reinterpret_cast<const float*>(&regs[XE_GPU_REG_PA_CL_VPORT_ZSCALE]);
          global_registers.viewport_offset[0] = *reinterpret_cast<const float*>(&regs[XE_GPU_REG_PA_CL_VPORT_XOFFSET]);
          global_registers.viewport_offset[1] = *reinterpret_cast<const float*>(&regs[XE_GPU_REG_PA_CL_VPORT_YOFFSET]);
          global_registers.viewport_offset[2] = *reinterpret_cast<const float*>(&regs[XE_GPU_REG_PA_CL_VPORT_ZOFFSET]);
            
          // Screen scissor registers
          global_registers.screen_scissor_tl = regs[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL];
          global_registers.screen_scissor_br = regs[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR];
            
          // Render target configuration
          global_registers.rb_surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO];
          global_registers.rb_color_info = regs[XE_GPU_REG_RB_COLOR_INFO];
          global_registers.rb_depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO];
            
          // Alpha test and blend state
          global_registers.rb_colorcontrol = regs[XE_GPU_REG_RB_COLORCONTROL];
          global_registers.rb_blendcontrol[0] = regs[XE_GPU_REG_RB_BLENDCONTROL0];
          global_registers.rb_blendcontrol[1] = regs[XE_GPU_REG_RB_BLENDCONTROL1];
          global_registers.rb_blendcontrol[2] = regs[XE_GPU_REG_RB_BLENDCONTROL2];
          global_registers.rb_blendcontrol[3] = regs[XE_GPU_REG_RB_BLENDCONTROL3];
            
          XELOGI("Metal IssueDraw: Read GPU registers - viewport scale: ({:.2f}, {:.2f}, {:.2f})",
                   global_registers.viewport_scale[0], global_registers.viewport_scale[1], 
                   global_registers.viewport_scale[2]);
            
          MTL::Buffer* global_buffer = GetMetalDevice()->newBuffer(
                &global_registers, sizeof(global_registers), MTL::ResourceStorageModeShared);
          TRACK_METAL_OBJECT(global_buffer, "MTL::Buffer[GlobalRegisters]");
          if (global_buffer) {
              global_buffer->setLabel(NS::String::string("Xbox360GlobalRegisters", NS::UTF8StringEncoding));
            encoder->setVertexBuffer(global_buffer, 0, 2);
            encoder->setFragmentBuffer(global_buffer, 0, 2);
            XELOGI("Metal IssueDraw: Bound global register buffer at index 2 with actual GPU state");
          }
            
          // Buffer 4: Draw arguments from actual draw call
          struct DrawArguments {
              uint32_t vertex_count;
              uint32_t instance_count;
              uint32_t first_vertex;
              uint32_t first_instance;
          } draw_args = {
              index_count,  // Use actual index count from draw call
              1,            // Instance count (Xbox 360 doesn't support instancing)
              0,            // First vertex
              0             // First instance
          };
            
          MTL::Buffer* draw_args_buffer = GetMetalDevice()->newBuffer(
                &draw_args, sizeof(draw_args), MTL::ResourceStorageModeShared);
          TRACK_METAL_OBJECT(draw_args_buffer, "MTL::Buffer[DrawArgs]");
          if (draw_args_buffer) {
              draw_args_buffer->setLabel(NS::String::string("Xbox360DrawArguments", NS::UTF8StringEncoding));
            encoder->setVertexBuffer(draw_args_buffer, 0, 4);
            XELOGI("Metal IssueDraw: Bound draw arguments buffer at index 4");
          }
            
          // Buffer 5: Shader uniforms/constants from Xbox 360 GPU
            // The Xbox 360 has 512 float constants (c0-c511), each is a float4
            // For now, we'll pass the first 64 constants which are most commonly used
          const int kNumConstants = 64;
          float shader_constants[kNumConstants * 4]; // 64 float4 constants
            
          // Copy shader constants from the register file
          // Constants start at XE_GPU_REG_SHADER_CONSTANT_000_X (0x4000)
          // Note: The register file already stores values in the correct endianness
          for (int i = 0; i < kNumConstants; i++) {
            // Each constant is 4 consecutive registers (XYZW)
            int base_reg = XE_GPU_REG_SHADER_CONSTANT_000_X + (i * 4);
            shader_constants[i * 4 + 0] = *reinterpret_cast<const float*>(&regs[base_reg + 0]); // X
            shader_constants[i * 4 + 1] = *reinterpret_cast<const float*>(&regs[base_reg + 1]); // Y
            shader_constants[i * 4 + 2] = *reinterpret_cast<const float*>(&regs[base_reg + 2]); // Z
            shader_constants[i * 4 + 3] = *reinterpret_cast<const float*>(&regs[base_reg + 3]); // W
          }
            
          XELOGI("Metal IssueDraw: Read {} shader constants from GPU registers", kNumConstants);
          if (kNumConstants > 0) {
            XELOGI("Metal IssueDraw: Constant c0 = ({:.2f}, {:.2f}, {:.2f}, {:.2f})",
                     shader_constants[0], shader_constants[1], shader_constants[2], shader_constants[3]);
          }
            
          MTL::Buffer* uniforms_buffer = GetMetalDevice()->newBuffer(
                shader_constants, sizeof(shader_constants), MTL::ResourceStorageModeShared);
          TRACK_METAL_OBJECT(uniforms_buffer, "MTL::Buffer[Uniforms]");
          if (uniforms_buffer) {
              uniforms_buffer->setLabel(NS::String::string("Xbox360ShaderConstants", NS::UTF8StringEncoding));
            encoder->setVertexBuffer(uniforms_buffer, 0, 5);
            encoder->setFragmentBuffer(uniforms_buffer, 0, 5);
            XELOGI("Metal IssueDraw: Bound shader constants buffer at index 5 with actual GPU constants");
          }
            
          // TODO: Phase C Step 2 - Bind index buffers from Xbox 360 data
          // TODO: Phase C Step 2 - Use actual Xbox 360 vertex data
            
          // Encode the actual draw call
          // Convert processed primitive type to Metal primitive type
          MTL::PrimitiveType metal_prim_type = MTL::PrimitiveTypeTriangle; // Default
          switch (primitive_processing_result.host_primitive_type) {
            case xenos::PrimitiveType::kPointList:
              metal_prim_type = MTL::PrimitiveTypePoint;
              break;
            case xenos::PrimitiveType::kLineList:
              metal_prim_type = MTL::PrimitiveTypeLine;
              break;
            case xenos::PrimitiveType::kLineStrip:
              metal_prim_type = MTL::PrimitiveTypeLineStrip;
              break;
            case xenos::PrimitiveType::kTriangleList:
              metal_prim_type = MTL::PrimitiveTypeTriangle;
              break;
            case xenos::PrimitiveType::kTriangleFan:
            case xenos::PrimitiveType::kTriangleStrip:
              metal_prim_type = MTL::PrimitiveTypeTriangleStrip;
              break;
            default:
              XELOGW("Metal IssueDraw: Unsupported primitive type {}, using triangles", 
                       static_cast<uint32_t>(prim_type));
              break;
          }
            
          if (primitive_processing_result.index_buffer_type != 
              PrimitiveProcessor::ProcessedIndexBufferType::kNone) {
            // Indexed draw
            if (primitive_processing_result.index_buffer_type == 
                PrimitiveProcessor::ProcessedIndexBufferType::kHostConverted) {
              // Use pre-processed index buffer from primitive processor
              MTL::Buffer* index_buffer = reinterpret_cast<MTL::Buffer*>(
                  primitive_processing_result.host_index_buffer_handle);
              
              MTL::IndexType index_type = 
                  primitive_processing_result.host_index_format == xenos::IndexFormat::kInt32
                      ? MTL::IndexTypeUInt32 
                      : MTL::IndexTypeUInt16;
              
              encoder->drawIndexedPrimitives(
                  metal_prim_type, 
                  NS::UInteger(primitive_processing_result.host_draw_vertex_count),
                  index_type,
                  index_buffer,
                  NS::UInteger(0));  // offset
                  
              XELOGI("Metal IssueDraw: Drew indexed primitives with converted buffer");
            } else {
              // Handle guest DMA index buffers
              XELOGI("Metal IssueDraw: Processing guest DMA index buffer from 0x{:08X}",
                     primitive_processing_result.guest_index_base);
              
              // Map guest memory for index buffer
              const void* guest_indices = memory_->TranslatePhysical(
                  primitive_processing_result.guest_index_base);
              
              if (!guest_indices) {
                XELOGE("Metal IssueDraw: Failed to translate guest index buffer address 0x{:08X}",
                       primitive_processing_result.guest_index_base);
                encoder->drawPrimitives(metal_prim_type, NS::UInteger(0), 
                                      NS::UInteger(primitive_processing_result.host_draw_vertex_count));
              } else {
                // Calculate index buffer size
                uint32_t index_size = 
                    primitive_processing_result.host_index_format == xenos::IndexFormat::kInt32 ? 4 : 2;
                uint32_t index_buffer_size = 
                    primitive_processing_result.host_draw_vertex_count * index_size;
                
                XELOGI("Metal IssueDraw: Creating Metal index buffer - format: {}, count: {}, size: {} bytes",
                       primitive_processing_result.host_index_format == xenos::IndexFormat::kInt32 ? "32-bit" : "16-bit",
                       primitive_processing_result.host_draw_vertex_count,
                       index_buffer_size);
                
                // Check for restart indices in the data for testing
                bool has_restart_values = false;
                if (primitive_processing_result.host_index_format == xenos::IndexFormat::kInt16) {
                  const uint16_t* indices16 = static_cast<const uint16_t*>(guest_indices);
                  for (uint32_t i = 0; i < primitive_processing_result.host_draw_vertex_count && i < 10; ++i) {
                    uint16_t index = xenos::GpuSwap(indices16[i], primitive_processing_result.host_shader_index_endian);
                    if (index == 0xFFFF) {
                      has_restart_values = true;
                      XELOGI("Metal IssueDraw: Found 0xFFFF at index {}", i);
                    }
                  }
                } else {
                  const uint32_t* indices32 = static_cast<const uint32_t*>(guest_indices);
                  for (uint32_t i = 0; i < primitive_processing_result.host_draw_vertex_count && i < 10; ++i) {
                    uint32_t index = xenos::GpuSwap(indices32[i], primitive_processing_result.host_shader_index_endian);
                    if (index == 0xFFFFFFFF) {
                      has_restart_values = true;
                      XELOGI("Metal IssueDraw: Found 0xFFFFFFFF at index {}", i);
                    }
                  }
                }
                
                if (has_restart_values && !pa_su_sc_mode_cntl.multi_prim_ib_ena) {
                  XELOGI("Metal IssueDraw: WARNING - Found restart values with primitive restart DISABLED!");
                  XELOGI("Metal IssueDraw: This should have been caught by primitive processor!");
                }
                
                // Create Metal index buffer from guest data
                // NOTE: The primitive processor already handles primitive restart pre-processing
                // when needed. If primitive restart is disabled but the indices contain values
                // that Metal would treat as restart (0xFFFF/0xFFFFFFFF), the primitive processor
                // will have already converted them via ProcessedIndexBufferType::kHostConverted.
                // For guest DMA buffers, we can use them directly since the processor determined
                // they don't need conversion.
                MTL::Buffer* guest_index_buffer = GetMetalDevice()->newBuffer(
                    guest_indices,
                    index_buffer_size,
                    MTL::ResourceStorageModeShared);
                TRACK_METAL_OBJECT(guest_index_buffer, "MTL::Buffer[GuestIndex]");
                
                if (guest_index_buffer) {
                  // Label the buffer for debugging
                  char label[256];
                  snprintf(label, sizeof(label), "Xbox360GuestDMAIndexBuffer_0x%08X", 
                          primitive_processing_result.guest_index_base);
                  guest_index_buffer->setLabel(NS::String::string(label, NS::UTF8StringEncoding));
                  
                  MTL::IndexType index_type = 
                      primitive_processing_result.host_index_format == xenos::IndexFormat::kInt32
                          ? MTL::IndexTypeUInt32 
                          : MTL::IndexTypeUInt16;
                  
                  // Draw with guest DMA index buffer
                  encoder->drawIndexedPrimitives(
                      metal_prim_type, 
                      NS::UInteger(primitive_processing_result.host_draw_vertex_count),
                      index_type,
                      guest_index_buffer,
                      NS::UInteger(0));  // offset
                  
                  XELOGI("Metal IssueDraw: Drew indexed primitives with guest DMA buffer");
                  
                  // Clean up - add to release list
                  vertex_buffers_to_release.push_back(guest_index_buffer);
                } else {
                  XELOGE("Metal IssueDraw: Failed to create Metal buffer for guest indices");
                  encoder->drawPrimitives(metal_prim_type, NS::UInteger(0), 
                                        NS::UInteger(primitive_processing_result.host_draw_vertex_count));
                }
              }
            }
          } else {
            // Non-indexed draw
            encoder->drawPrimitives(metal_prim_type, NS::UInteger(0), 
                                  NS::UInteger(primitive_processing_result.host_draw_vertex_count));
          }
          
          XELOGI("Metal IssueDraw: Encoded draw primitives call ({} vertices, primitive type {})", 
                 index_count, static_cast<uint32_t>(metal_prim_type));
          
          // Clean up buffers
          for (auto* buffer : vertex_buffers_to_release) {
            TRACK_METAL_RELEASE(buffer);
            buffer->release();
          }
          if (global_buffer) {
            TRACK_METAL_RELEASE(global_buffer);
            global_buffer->release();
          }
          if (draw_args_buffer) {
            TRACK_METAL_RELEASE(draw_args_buffer);
            draw_args_buffer->release();
          }
          if (uniforms_buffer) {
            TRACK_METAL_RELEASE(uniforms_buffer);
            uniforms_buffer->release();
          }
          
          // Pop debug group
          encoder->popDebugGroup();
          
          encoder->endEncoding();
          
          // Track encoder release
          TRACK_METAL_RELEASE(encoder);
          encoder->release();
          
          XELOGI("Metal IssueDraw: Metal frame debugging - Command buffer '{}' ready for capture", draw_label);
        }
        
        // Don't commit here - let EndSubmission handle it
        // The command buffer will be committed when:
        // 1. IssueSwap is called (for swap)
        // 2. OnPrimaryBufferEnd is called (for primary buffer completion)
        // 3. ShutdownContext is called (for cleanup)
        XELOGI("Metal IssueDraw: Draw commands added to command buffer");
        
        // Cleanup render pass if it was allocated here
        if (render_pass) {
          TRACK_METAL_RELEASE(render_pass);
          render_pass->release();
        }
      }
    }
  } else {
    XELOGW("Metal IssueDraw: Failed to create pipeline state");
  }

  // Phase C: Return true to indicate successful Metal command encoding
  return true;
}

bool MetalCommandProcessor::IssueCopy() {
  SCOPE_profile_cpu_f("gpu");
  
  if (!BeginSubmission(true)) {
    return false;
  }
  
  // Call render target cache to perform the resolve operation
  uint32_t written_address, written_length;
  if (!render_target_cache_->Resolve(*memory_, written_address, written_length)) {
    XELOGE("Metal command processor: Failed to resolve render targets");
    return false;
  }
  
  // TODO: Implement CPU readback for resolved data if needed
  
  return true;
}

bool MetalCommandProcessor::BeginSubmission(bool is_guest_command) {
  XELOGI("Metal BeginSubmission called: is_guest_command={}, submission_open={}",
         is_guest_command, submission_open_);
  
  if (submission_open_) {
    return true;
  }
  

  // Mark frame as open if needed
  if (is_guest_command && !frame_open_) {
    frame_open_ = true;
    
    // Notify cache systems of new frame
    if (primitive_processor_) {
      primitive_processor_->BeginFrame();
    }
    if (buffer_cache_) {
      // buffer_cache_->BeginFrame();  // Will add when implementing buffer cache
    }
    if (texture_cache_) {
      // texture_cache_->BeginFrame();  // Will add when implementing texture cache
    }
  }

  // Create new command buffer
  auto* command_queue = GetMetalCommandQueue();
  if (!command_queue) {
    XELOGE("No Metal command queue available for submission");
    return false;
  }

  current_command_buffer_ = command_queue->commandBuffer();
  if (!current_command_buffer_) {
    XELOGE("Failed to create Metal command buffer");
    return false;
  }
  
  // Track command buffer creation
  TRACK_METAL_OBJECT(current_command_buffer_, "MTL::CommandBuffer");

  submission_open_ = true;
  
  // Notify primitive processor of new submission
  if (primitive_processor_) {
    primitive_processor_->BeginSubmission();
  }
  
  return true;
}

bool MetalCommandProcessor::EndSubmission(bool is_swap) {
  if (!submission_open_) {
    return true;
  }

  bool is_closing_frame = is_swap && frame_open_;
  
  if (is_swap) {
    XELOGI("Metal EndSubmission: SWAP detected, is_closing_frame={}", is_closing_frame);
  }

  if (is_closing_frame) {
    // Notify cache systems of frame end
    if (primitive_processor_) {
      primitive_processor_->EndFrame();
    }
    if (texture_cache_) {
      // texture_cache_->EndFrame();  // Will add when implementing texture cache
    }
    frame_open_ = false;
    
    // Generate PNG after frame completion
    XELOGI("Metal EndSubmission: Generating PNG for completed frame {}", frame_count_);
    
    ui::Presenter* presenter = graphics_system_->presenter();
    if (presenter) {
      ui::RawImage raw_image;
      // For now, just use the fallback test pattern until we get real render target data
      if (presenter->CaptureGuestOutput(raw_image)) {
        std::string filename = "xenia_metal_frame_" + std::to_string(frame_count_) + ".png";
        FILE* file = std::fopen(filename.c_str(), "wb");
        if (file) {
          auto callback = [](void* context, void* data, int size) {
            std::fwrite(data, 1, size, (FILE*)context);
          };
          stbi_write_png_to_func(callback, file, static_cast<int>(raw_image.width),
                                 static_cast<int>(raw_image.height), 4,
                                 raw_image.data.data(),
                                 static_cast<int>(raw_image.stride));
          std::fclose(file);
          XELOGI("Metal EndSubmission: PNG saved as {} ({}x{}) for frame {}", 
                 filename, raw_image.width, raw_image.height, frame_count_);
        } else {
          XELOGE("Metal EndSubmission: Failed to create PNG file {} for frame {}", filename, frame_count_);
        }
      } else {
        XELOGW("Metal EndSubmission: Failed to capture guest output for PNG for frame {}", frame_count_);
      }
    } else {
      XELOGW("Metal EndSubmission: No presenter available for PNG generation for frame {}", frame_count_);
    }
  }

  // Commit and submit the command buffer
  if (current_command_buffer_) {
#if defined(DEBUG) || defined(_DEBUG) || defined(CHECKED)
    // Add completion handler for debugging
    current_command_buffer_->addCompletedHandler(^(MTL::CommandBuffer* buffer) {
      if (buffer->error()) {
        NS::Error* error = buffer->error();
        XELOGE("Metal command buffer error: {} (code: {})",
               error->localizedDescription()->utf8String(),
               error->code());
        
        // Check command buffer status
        MTL::CommandBufferStatus status = buffer->status();
        const char* status_str = "Unknown";
        switch (status) {
          case MTL::CommandBufferStatusNotEnqueued: status_str = "NotEnqueued"; break;
          case MTL::CommandBufferStatusEnqueued: status_str = "Enqueued"; break;
          case MTL::CommandBufferStatusCommitted: status_str = "Committed"; break;
          case MTL::CommandBufferStatusScheduled: status_str = "Scheduled"; break;
          case MTL::CommandBufferStatusCompleted: status_str = "Completed"; break;
          case MTL::CommandBufferStatusError: status_str = "Error"; break;
        }
        XELOGE("Command buffer status: {}", status_str);
      }
    });
#endif
    
    current_command_buffer_->commit();
    
    // Track command buffer release before nulling
    TRACK_METAL_RELEASE(current_command_buffer_);
    current_command_buffer_ = nullptr;
  }

  submission_open_ = false;
  return true;
}

bool MetalCommandProcessor::CanEndSubmissionImmediately() const {
  // For now, always allow immediate submission
  // Later we may want to check for pipeline compilation, etc.
  return true;
}

void MetalCommandProcessor::OnPrimaryBufferEnd() {
  // Submit on primary buffer end to reduce latency and handle traces without SWAP
  if (submission_open_ && CanEndSubmissionImmediately()) {
    XELOGI("Metal OnPrimaryBufferEnd: Submitting command buffer");
    EndSubmission(false);
  }
}

void MetalCommandProcessor::WaitForIdle() {
  // First call base implementation to wait for command processing to complete
  CommandProcessor::WaitForIdle();
  
  // Then flush any pending Metal submissions
  if (submission_open_) {
    XELOGI("Metal WaitForIdle: Flushing pending submission");
    EndSubmission(false);
  }
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
