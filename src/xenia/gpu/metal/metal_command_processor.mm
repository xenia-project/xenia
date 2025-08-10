/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_command_processor.h"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <thread>

#include "third_party/stb/stb_image_write.h"
#include "xenia/ui/metal/metal_presenter.h"
#include "xenia/gpu/metal/metal_object_tracker.h"
#include "xenia/gpu/metal/metal_debug_utils.h"
#include "xenia/gpu/draw_util.h"

// Forward declarations for Objective-C types
#ifdef __OBJC__
@protocol MTLTexture;
#else
typedef struct objc_object* id;
#endif

#include "xenia/base/autorelease_pool.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/primitive_processor.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/metal/metal_graphics_system.h"
#include "xenia/gpu/metal/metal_buffer_cache.h"
#include "xenia/gpu/metal/metal_pipeline_cache.h"
#include "xenia/gpu/metal/metal_primitive_processor.h"
#include "xenia/gpu/metal/metal_render_target_cache.h"
#include "xenia/gpu/metal/metal_shared_memory.h"
#include "xenia/gpu/metal/metal_texture_cache.h"

// Include Metal IR Converter Runtime for proper resource binding
#define IR_RUNTIME_METALCPP
#include "/usr/local/include/metal_irconverter_runtime/metal_irconverter_runtime.h"

// IRDescriptorTableEntry is defined in metal_irconverter_runtime.h
// No need to redefine it

// Make sure we have access to the MSC runtime functions
extern "C" {
  // These are defined in ir_runtime_impl.mm
  extern const uint64_t kIRArgumentBufferBindPoint;
  extern const uint64_t kIRArgumentBufferDrawArgumentsBindPoint;
  extern const uint64_t kIRArgumentBufferUniformsBindPoint;
  extern const uint64_t kIRDescriptorHeapBindPoint;
  extern const uint64_t kIRSamplerHeapBindPoint;
  extern const uint64_t kIRVertexBufferBindPoint;
}

namespace xe {
namespace gpu {
namespace metal {

MetalCommandProcessor::MetalCommandProcessor(MetalGraphicsSystem* graphics_system,
                                             kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state),
      metal_graphics_system_(graphics_system) {}

MetalCommandProcessor::~MetalCommandProcessor() {
  // Clean up stored render pass descriptor
  if (last_render_pass_descriptor_) {
    last_render_pass_descriptor_->release();
    last_render_pass_descriptor_ = nullptr;
  }
}

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
  XE_SCOPED_AUTORELEASE_POOL("SetupContext");
  
  // Initialize cache systems
  pipeline_cache_ = std::make_unique<MetalPipelineCache>(
      this, register_file_, false);  // edram_rov_used = false for now
  buffer_cache_ = std::make_unique<MetalBufferCache>(
      this, register_file_, memory_);
  texture_cache_ = std::make_unique<MetalTextureCache>(
      this, register_file_, memory_);
  // Create render target cache with new signature
  render_target_cache_ = std::make_unique<MetalRenderTargetCache>(
      *register_file_, *memory_, &trace_writer_,
      1, 1,  // No resolution scaling for now
      *this);
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
  
  // Initialize debug utilities for trace dumps
  bool is_trace_dump = !graphics_system_->presenter();
  if (is_trace_dump) {
    debug_utils_ = std::make_unique<MetalDebugUtils>(this);
    debug_utils_->Initialize(GetMetalCommandQueue());
    debug_utils_->SetValidationEnabled(true);
    debug_utils_->SetCaptureModeEnabled(true);
    debug_utils_->SetDumpStateEnabled(true);
    debug_utils_->SetLabelResourcesEnabled(true);
    XELOGI("Metal debug utilities initialized for trace dump");
  }
  
  // Create debug red pipeline for testing
  CreateDebugRedPipeline();
  
  // Allocate staging buffers for shader constants
  // These get populated by WriteRegister when PM4 packets load constants
  cb0_staging_ = new float[256 * 4];  // VS constants (256 vec4s)
  cb1_staging_ = new float[256 * 4];  // PS constants (256 vec4s)
  memset(cb0_staging_, 0, 256 * 4 * sizeof(float));
  memset(cb1_staging_, 0, 256 * 4 * sizeof(float));
  XELOGI("Metal SetupContext: Allocated CB staging buffers");
  
  // Create empty descriptor heap buffers used by MSC (arrays of IRDescriptorTableEntry)
  // We fill them per-draw via IRDescriptorTableSet* and bind at the MSC heap bind points.
  {
    XE_SCOPED_AUTORELEASE_POOL("CreateDescriptorHeaps");

    const size_t kHeapSlots = 64;
    const size_t kHeapBytes = kHeapSlots * sizeof(IRDescriptorTableEntry);

    // Resource heap: textures
    res_heap_ab_ = GetMetalDevice()->newBuffer(kHeapBytes, MTL::ResourceStorageModeShared);
    res_heap_ab_->setLabel(NS::String::string("ResourceDescriptorHeap", NS::UTF8StringEncoding));
    std::memset(res_heap_ab_->contents(), 0, kHeapBytes);

    // Sampler heap
    smp_heap_ab_ = GetMetalDevice()->newBuffer(kHeapBytes, MTL::ResourceStorageModeShared);
    smp_heap_ab_->setLabel(NS::String::string("SamplerDescriptorHeap", NS::UTF8StringEncoding));
    std::memset(smp_heap_ab_->contents(), 0, kHeapBytes);

    // These encoders are not used with the MSC runtime path
    res_heap_encoder_ = nullptr;
    smp_heap_encoder_ = nullptr;

    XELOGI("Metal SetupContext: Created MSC heaps ({} entries)", (int)kHeapSlots);
  }
  
  return CommandProcessor::SetupContext();
}

void MetalCommandProcessor::ShutdownContext() {
  // NOTE: Do NOT use @autoreleasepool here - it causes crashes when destroyed
  // Metal-cpp manages its own autorelease pools internally
  
  // End any active render encoder before shutting down
  if (current_render_encoder_) {
    XELOGI("Metal ShutdownContext: Ending active render encoder");
    current_render_encoder_->endEncoding();
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }
  
  // Release render pass reference
  if (current_render_pass_) {
    current_render_pass_->release();
    current_render_pass_ = nullptr;
  }
  
  // Commit any outstanding draws before shutting down
  if (submission_open_ && draws_in_current_batch_ > 0) {
    XELOGI("Metal ShutdownContext: Committing final batch of {} draws", draws_in_current_batch_);
    EndSubmission(false);
    draws_in_current_batch_ = 0;
  }
  
  // Wait for all command buffers to complete before ending capture
  if (GetMetalCommandQueue()) {
    XELOGI("Metal ShutdownContext: Waiting for GPU to finish...");
    // Create a command buffer and commit it to ensure all previous work completes
    MTL::CommandBuffer* sync_buffer = GetMetalCommandQueue()->commandBuffer();
    if (sync_buffer) {
      sync_buffer->commit();
      sync_buffer->waitUntilCompleted();
    }
  }
  
  // End GPU capture if we started one
  if (debug_utils_) {
    debug_utils_->EndProgrammaticCapture();
    XELOGI("Ended programmatic GPU capture at shutdown");
    
    // Give the capture system time to finish writing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  // Clean up staging buffers
  delete[] cb0_staging_;
  delete[] cb1_staging_;
  cb0_staging_ = nullptr;
  cb1_staging_ = nullptr;
  
  // Clean up descriptor heaps
  if (res_heap_encoder_) {
    res_heap_encoder_->release();
    res_heap_encoder_ = nullptr;
  }
  if (res_heap_ab_) {
    res_heap_ab_->release();
    res_heap_ab_ = nullptr;
  }
  if (smp_heap_encoder_) {
    smp_heap_encoder_->release();
    smp_heap_encoder_ = nullptr;
  }
  if (smp_heap_ab_) {
    smp_heap_ab_->release();
    smp_heap_ab_ = nullptr;
  }
  XELOGI("MetalCommandProcessor: Beginning clean shutdown");
  
  // Force flush any pending commands
  if (submission_open_) {
    XELOGI("Metal ShutdownContext: Forcing EndSubmission before shutdown");
    EndSubmission(false);
  }
  
  // Clean up any remaining command buffer
  if (current_command_buffer_) {
    XELOGI("Metal ShutdownContext: Found uncommitted command buffer");
    // Commit it first to ensure it completes
    current_command_buffer_->commit();
    // Wait for it to complete before releasing
    current_command_buffer_->waitUntilCompleted();
    XELOGI("Metal ShutdownContext: Command buffer completed, releasing");
    // Release our retained reference
    TRACK_METAL_RELEASE(current_command_buffer_);
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }
  
  // Wait for all command buffers to complete before continuing
  // This prevents crashes from async completion handlers
  MTL::CommandQueue* command_queue = GetMetalCommandQueue();
  if (command_queue) {
    XELOGI("Metal ShutdownContext: Waiting for command queue to finish all work");
    // Create a dummy command buffer to act as a fence
    // Note: commandBuffer() returns an autoreleased object, so we don't need to release it
    MTL::CommandBuffer* fence = command_queue->commandBuffer();
    if (fence) {
      fence->commit();
      fence->waitUntilCompleted();
      // Don't call release() - fence is autoreleased
      XELOGI("Metal ShutdownContext: All GPU work completed");
    }
  }
  
  // PNG generation moved to TraceDump::Run to avoid shutdown deadlock
  // The main thread handles PNG capture before shutdown begins
  
  // Clean up debug pipeline
  if (debug_red_pipeline_) {
    debug_red_pipeline_->release();
    debug_red_pipeline_ = nullptr;
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
  
  XELOGI("MetalCommandProcessor: Clean shutdown complete");
}

void MetalCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                      uint32_t frontbuffer_width,
                                      uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");
  
  XELOGI("Metal IssueSwap: frontbuffer 0x{:08X}, {}x{}", 
         frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
  
  // Frame boundary - any pending draws should be committed
  // Reset draw batch counter for next frame
  draws_in_current_batch_ = 0;
  
  // End the current submission and mark it as a swap
  if (!EndSubmission(true)) {
    XELOGE("Metal IssueSwap: Failed to end submission");
    return;
  }
  
  // Track frame boundaries
  frame_count_++;
  XELOGI("Metal IssueSwap: Frame {} complete", frame_count_);
  
  // Reset dummy target clear flag for new frame
  if (render_target_cache_) {
    render_target_cache_->BeginFrame();
  }
  
  // Capture the current frame for trace dumps (do this before presenter operations)
  // Note: CaptureColorTarget has its own autorelease pool management
  uint32_t capture_width, capture_height;
  std::vector<uint8_t> capture_data;
  if (CaptureColorTarget(0, capture_width, capture_height, capture_data)) {
    last_captured_frame_data_ = std::move(capture_data);
    last_captured_frame_width_ = capture_width;
    last_captured_frame_height_ = capture_height;
    XELOGI("Metal IssueSwap: Captured frame {}x{} for trace dump", capture_width, capture_height);
  }
  
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
            // Try to get depth target if no color target is available
            MTL::Texture* depth_target = render_target_cache_->GetDepthTarget();
            if (depth_target) {
              XELOGI("Metal IssueSwap: Using depth target for guest output (no color target available)");
              color_target = depth_target;
            } else {
              XELOGW("Metal IssueSwap: No render targets available for guest output");
              return false;
            }
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

void MetalCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  // Check if this is a shader constant register (0x4000 - 0x4FFF range)
  // Constants are at 0x4000 + (constant_index * 4) for each component
  const uint32_t kShaderConstantBase = 0x4000;
  const uint32_t kShaderConstantEnd = 0x4800;  // 512 vec4 constants
  
  if (index >= kShaderConstantBase && index < kShaderConstantEnd) {
    uint32_t constant_offset = index - kShaderConstantBase;
    uint32_t constant_index = constant_offset / 4;  // Which vec4
    uint32_t component = constant_offset % 4;       // Which component (x,y,z,w)
    
    // Update our staging buffers when constants are written
    // This is crucial for trace replay where LOAD_ALU_CONSTANT calls WriteRegister
    float float_value = *reinterpret_cast<const float*>(&value);
    
    // Determine which bank (VS or PS) and update staging
    if (constant_index < 256) {
      // Vertex shader constants (c0-c255)
      if (cb0_staging_) {
        cb0_staging_[constant_index * 4 + component] = float_value;
      }
    } else {
      // Pixel shader constants (c256-c511)
      uint32_t ps_index = constant_index - 256;
      if (cb1_staging_ && ps_index < 256) {
        cb1_staging_[ps_index * 4 + component] = float_value;
      }
    }
    
    // Log first few constant writes for debugging
    if (constant_index < 8) {
      XELOGI("WriteRegister: Shader constant c{}[{}] = {:.3f} (0x{:08X}) - Updated staging",
             constant_index, component, float_value, value);
    }
  }
  
  // Call base implementation
  CommandProcessor::WriteRegister(index, value);
}

void MetalCommandProcessor::OnGammaRamp256EntryTableValueWritten() {
  // TODO(wmarti): Handle gamma ramp changes
}

void MetalCommandProcessor::OnGammaRampPWLValueWritten() {
  // TODO(wmarti): Handle gamma ramp changes
}

void MetalCommandProcessor::CreateDebugRedPipeline() {
  XELOGI("Creating debug red shader pipeline");
  
  MTL::Device* device = GetMetalDevice();
  if (!device) {
    XELOGE("No Metal device available for debug pipeline");
    return;
  }
  
  // Simple vertex shader that passes through positions
  const char* vertex_source = R"(
    #include <metal_stdlib>
    using namespace metal;
    
    struct VertexOut {
      float4 position [[position]];
    };
    
    vertex VertexOut debug_vertex_main(uint vertex_id [[vertex_id]]) {
      // Create a triangle that covers the screen
      float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
      };
      
      VertexOut out;
      out.position = float4(positions[vertex_id], 0.0, 1.0);
      return out;
    }
  )";
  
  // Fragment shader that outputs solid red
  const char* fragment_source = R"(
    #include <metal_stdlib>
    using namespace metal;
    
    struct VertexOut {
      float4 position [[position]];
    };
    
    fragment float4 debug_fragment_main(VertexOut in [[stage_in]]) {
      return float4(1.0, 0.0, 0.0, 1.0);  // Solid red
    }
  )";
  
  // Compile shaders
  NS::Error* error = nullptr;
  MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
  
  MTL::Library* library = device->newLibrary(
    NS::String::string(vertex_source, NS::UTF8StringEncoding),
    options,
    &error
  );
  
  if (!library) {
    XELOGE("Failed to compile debug vertex shader: {}", 
           error ? error->localizedDescription()->utf8String() : "unknown error");
    if (error) error->release();
    options->release();
    return;
  }
  
  MTL::Function* vertex_func = library->newFunction(
    NS::String::string("debug_vertex_main", NS::UTF8StringEncoding)
  );
  
  // Compile fragment shader
  MTL::Library* frag_library = device->newLibrary(
    NS::String::string(fragment_source, NS::UTF8StringEncoding),
    options,
    &error
  );
  
  if (!frag_library) {
    XELOGE("Failed to compile debug fragment shader: {}", 
           error ? error->localizedDescription()->utf8String() : "unknown error");
    if (error) error->release();
    options->release();
    library->release();
    if (vertex_func) vertex_func->release();
    return;
  }
  
  MTL::Function* fragment_func = frag_library->newFunction(
    NS::String::string("debug_fragment_main", NS::UTF8StringEncoding)
  );
  
  // Create pipeline descriptor
  MTL::RenderPipelineDescriptor* pipeline_desc = MTL::RenderPipelineDescriptor::alloc()->init();
  pipeline_desc->setVertexFunction(vertex_func);
  pipeline_desc->setFragmentFunction(fragment_func);
  
  // Set up color attachment - assume BGRA8
  pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  pipeline_desc->colorAttachments()->object(0)->setBlendingEnabled(false);
  
  // Create pipeline state
  debug_red_pipeline_ = device->newRenderPipelineState(pipeline_desc, &error);
  
  if (!debug_red_pipeline_) {
    XELOGE("Failed to create debug red pipeline: {}", 
           error ? error->localizedDescription()->utf8String() : "unknown error");
  } else {
    XELOGI("Debug red pipeline created successfully");
  }
  
  // Clean up
  if (error) error->release();
  options->release();
  pipeline_desc->release();
  vertex_func->release();
  fragment_func->release();
  library->release();
  frag_library->release();
}


void MetalCommandProcessor::TracePlaybackWroteMemory(uint32_t base_ptr,
                                                     uint32_t length) {
  // Critical: Tell memory subsystem and caches that guest memory was written
  // This is called during trace playback when memory writes are replayed
  
  // 1) Tell shared memory someone changed guest memory (using same API as D3D12)
  if (shared_memory_) {
    shared_memory_->MemoryInvalidationCallback(base_ptr, length, true);
  }

  // 2) Tell primitive processor about memory changes
  if (primitive_processor_) {
    primitive_processor_->MemoryInvalidationCallback(base_ptr, length, true);
  }

  // 3) Texture and buffer caches in Metal don't have memory callbacks yet
  // but they should be notified when implemented
  // TODO: Add memory invalidation to Metal texture/buffer caches
  
  XELOGI("TracePlaybackWroteMemory: base=0x{:08X}, length=0x{:X}", base_ptr, length);
}

void MetalCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {
  if (!snapshot) {
    XELOGW("Metal RestoreEdramSnapshot: No snapshot provided");
    return;
  }
  
  // Copy snapshot data to EDRAM buffer via render target cache
  if (render_target_cache_) {
    render_target_cache_->RestoreEdram(snapshot);
    XELOGI("Metal RestoreEdramSnapshot: Restored EDRAM state to render target cache");
  } else {
    XELOGE("Metal RestoreEdramSnapshot: No render target cache available");
  }
}

bool MetalCommandProcessor::CaptureColorTarget(uint32_t index, uint32_t& width, uint32_t& height,
                                              std::vector<uint8_t>& data) {
  SCOPE_profile_cpu_f("gpu");
  
  // Create autorelease pool for Metal object management
  void* pool = AutoreleasePoolTracker::Push("CaptureColorTarget");
  
  XELOGI("Metal CaptureColorTarget: Starting capture for index {}", index);
  
  // First try to get the texture from the last render pass descriptor
  MTL::Texture* texture = nullptr;
  
  // Try to get from our stored last render pass descriptor
  if (last_render_pass_descriptor_ && 
      last_render_pass_descriptor_->colorAttachments()->object(index)->texture()) {
    texture = last_render_pass_descriptor_->colorAttachments()->object(index)->texture();
    XELOGI("Metal CaptureColorTarget: Got texture from last render pass ({}x{}, format {}, ptr: 0x{:016x})",
           texture->width(), texture->height(), static_cast<uint32_t>(texture->pixelFormat()),
           reinterpret_cast<uintptr_t>(texture));
  }
  
  // Fall back to the regular method
  if (!texture) {
    texture = render_target_cache_->GetColorTarget(index);
  }
  
  if (!texture) {
    XELOGI("Metal CaptureColorTarget: No current color target {}, trying last REAL render target", index);
    // Try to get the last REAL render target that was rendered to
    texture = render_target_cache_->GetLastRealColorTarget(index);
    if (texture) {
      XELOGI("Metal CaptureColorTarget: Using last REAL color target {} ({}x{}, format {})", 
             index, texture->width(), texture->height(), static_cast<uint32_t>(texture->pixelFormat()));
    }
  }
  
  if (!texture) {
    XELOGI("Metal CaptureColorTarget: No real targets, trying dummy color target from cache");
    // Try the dummy color target from the render target cache
    texture = render_target_cache_->GetDummyColorTarget();
    if (!texture) {
      XELOGI("Metal CaptureColorTarget: No dummy color target from cache, trying last real depth");
      // Try last real depth target before giving up
      texture = render_target_cache_->GetLastRealDepthTarget();
      if (!texture) {
        XELOGI("Metal CaptureColorTarget: No last real depth, trying current depth");
        texture = render_target_cache_->GetDepthTarget();
        if (!texture) {
          XELOGW("Metal CaptureColorTarget: No render targets available at all");
          AutoreleasePoolTracker::Pop(pool, "CaptureColorTarget");
          return false;
        }
      }
      XELOGI("Metal CaptureColorTarget: Using depth target ({}x{}, format {})", 
             texture->width(), texture->height(), static_cast<uint32_t>(texture->pixelFormat()));
    } else {
      XELOGI("Metal CaptureColorTarget: Using render cache dummy target ({}x{}, format {}) - texture ptr: 0x{:016x}", 
             texture->width(), texture->height(), static_cast<uint32_t>(texture->pixelFormat()),
             reinterpret_cast<uintptr_t>(texture));
    }
  } else {
    XELOGI("Metal CaptureColorTarget: Found current color target ({}x{}, format {})", 
           texture->width(), texture->height(), static_cast<uint32_t>(texture->pixelFormat()));
  }
  
  width = static_cast<uint32_t>(texture->width());
  height = static_cast<uint32_t>(texture->height());
  
  XELOGI("Metal CaptureColorTarget: About to capture from texture ptr: 0x{:016x}",
         reinterpret_cast<uintptr_t>(texture));
  
  // Handle different texture formats  
  MTL::PixelFormat pixel_format = texture->pixelFormat();
  bool is_depth_stencil = (pixel_format == MTL::PixelFormatDepth32Float_Stencil8 ||
                          pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8);
  bool is_depth_only = (pixel_format == MTL::PixelFormatDepth32Float ||
                       pixel_format == MTL::PixelFormatDepth16Unorm);
  
  // Calculate bytes per pixel based on format
  size_t bytes_per_pixel = 4; // Default for RGBA8
  size_t source_bytes_per_pixel = 4;
  
  uint32_t sample_count = texture->sampleCount();
  XELOGI("Metal CaptureColorTarget: Texture format = {}, width = {}, height = {}, sampleCount = {}",
         static_cast<uint32_t>(pixel_format), width, height, sample_count);
  
  if (pixel_format == MTL::PixelFormatDepth32Float_Stencil8) {
    source_bytes_per_pixel = 8; // 4 bytes depth + 1 byte stencil + 3 bytes padding = 8
  } else if (pixel_format == MTL::PixelFormatDepth32Float) {
    source_bytes_per_pixel = 4; // 4 bytes depth
  } else if (pixel_format == MTL::PixelFormatDepth16Unorm) {
    source_bytes_per_pixel = 2; // 2 bytes depth
  } else if (pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
    source_bytes_per_pixel = 4; // 3 bytes depth + 1 byte stencil = 4
  }
  
  size_t data_size = width * height * bytes_per_pixel; // Output buffer size (RGBA8)
  size_t source_data_size = width * height * source_bytes_per_pixel; // Input texture size
  data.resize(data_size);
  
  // Get Metal device and command queue
  MTL::Device* device = GetMetalDevice();
  MTL::CommandQueue* command_queue = GetMetalCommandQueue();
  if (!device || !command_queue) {
    XELOGE("Metal CaptureColorTarget: No device or command queue");
    AutoreleasePoolTracker::Pop(pool, "CaptureColorTarget");
    return false;
  }
  
  // Create a command buffer to read the texture
  MTL::CommandBuffer* command_buffer = command_queue->commandBuffer();
  if (!command_buffer) {
    XELOGE("Metal CaptureColorTarget: Failed to create command buffer");
    AutoreleasePoolTracker::Pop(pool, "CaptureColorTarget");
    return false;
  }
  
  // Handle MSAA textures - need to resolve first
  MTL::Texture* capture_texture = texture;
  MTL::Texture* resolved_texture = nullptr;
  
  if (sample_count > 1) {
    XELOGI("Metal CaptureColorTarget: MSAA texture detected, creating resolve texture");
    MTL::TextureDescriptor* td = MTL::TextureDescriptor::alloc()->init();
    td->setTextureType(MTL::TextureType2D);
    td->setPixelFormat(texture->pixelFormat());
    td->setWidth(texture->width());
    td->setHeight(texture->height());
    td->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget);
    td->setSampleCount(1);
    resolved_texture = device->newTexture(td);
    td->release();
    
    if (!resolved_texture) {
      XELOGE("Metal CaptureColorTarget: Failed to create resolve texture for MSAA");
      AutoreleasePoolTracker::Pop(pool, "CaptureColorTarget");
      return false;
    }
    
    capture_texture = resolved_texture;  // We'll read from the resolved texture
    XELOGI("Metal CaptureColorTarget: Created resolve texture for MSAA");
  }
  
  // Create a blit encoder to copy the texture to a buffer
  MTL::BlitCommandEncoder* blit_encoder = command_buffer->blitCommandEncoder();
  if (!blit_encoder) {
    XELOGE("Metal CaptureColorTarget: Failed to create blit encoder");
    if (resolved_texture) resolved_texture->release();
    // command_buffer is autoreleased, don't release manually
    AutoreleasePoolTracker::Pop(pool, "CaptureColorTarget");
    return false;
  }
  
  // Resolve MSAA if needed
  if (sample_count > 1) {
    XELOGI("Metal CaptureColorTarget: Resolving MSAA texture");
    // For color textures, use resolveTexture which handles MSAA resolve
    // Note: This only works for color textures, not depth
    if (!is_depth_stencil) {
      // Create a render pass to resolve (Metal doesn't have direct MSAA resolve in blit encoder)
      MTL::RenderPassDescriptor* resolve_pass = MTL::RenderPassDescriptor::alloc()->init();
      resolve_pass->colorAttachments()->object(0)->setTexture(resolved_texture);
      resolve_pass->colorAttachments()->object(0)->setResolveTexture(texture);
      resolve_pass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
      resolve_pass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionMultisampleResolve);
      
      // End blit encoder and create render encoder for resolve
      blit_encoder->endEncoding();
      MTL::RenderCommandEncoder* resolve_encoder = command_buffer->renderCommandEncoder(resolve_pass);
      resolve_encoder->endEncoding();
      resolve_pass->release();
      
      // Create new blit encoder for the copy
      blit_encoder = command_buffer->blitCommandEncoder();
      if (!blit_encoder) {
        XELOGE("Metal CaptureColorTarget: Failed to create blit encoder after resolve");
        resolved_texture->release();
        AutoreleasePoolTracker::Pop(pool, "CaptureColorTarget");
        return false;
      }
    }
  }
  
  // Create a temporary buffer to read the texture data (use source data size for reading)
  MTL::Buffer* read_buffer = device->newBuffer(source_data_size, MTL::ResourceStorageModeShared);
  if (!read_buffer) {
    XELOGE("Metal CaptureColorTarget: Failed to create read buffer");
    blit_encoder->endEncoding();
    // blit_encoder and command_buffer are autoreleased, don't release manually
    AutoreleasePoolTracker::Pop(pool, "CaptureColorTarget");
    return false;
  }
  
  
  if (is_depth_stencil) {
    // For depth+stencil textures, we cannot easily capture them with blit operations
    // due to Metal validation restrictions. For now, create a gray placeholder
    // that indicates a depth buffer was present.
    XELOGI("Metal CaptureColorTarget: Skipping depth+stencil texture capture (format {})", 
           static_cast<uint32_t>(pixel_format));
    
    // End the current blit encoder
    blit_encoder->endEncoding();
    // Clean up allocated buffer
    read_buffer->release();
    
    XELOGW("Metal CaptureColorTarget: Creating gray placeholder for depth+stencil texture");
    
    // Create a gray RGBA image to indicate depth buffer presence
    data.resize(width * height * 4);
    for (size_t i = 0; i < data.size(); i += 4) {
      data[i + 0] = 128;  // R
      data[i + 1] = 128;  // G  
      data[i + 2] = 128;  // B
      data[i + 3] = 255;  // A
    }
    
    XELOGI("Metal CaptureColorTarget: Created {}x{} gray depth placeholder", width, height);
    AutoreleasePoolTracker::Pop(pool, "CaptureColorTarget");
    return true;
  } else if (is_depth_only) {
    XELOGI("Metal CaptureColorTarget: Capturing depth-only texture (format {})", 
           static_cast<uint32_t>(pixel_format));
    
    // Depth-only textures can be copied directly  
    blit_encoder->copyFromTexture(
        capture_texture, 0, 0,  // sourceSlice, sourceLevel
        MTL::Origin(0, 0, 0),  // sourceOrigin
        MTL::Size(width, height, 1),  // sourceSize
        read_buffer,  // destinationBuffer
        0,  // destinationOffset
        width * source_bytes_per_pixel,  // destinationBytesPerRow (use source format size)
        source_data_size  // destinationBytesPerImage (total size)
    );
  } else {
    // Color texture - copy directly
    XELOGI("Metal CaptureColorTarget: Copying color texture - width={}, height={}, source_bytes_per_pixel={}, bytesPerRow={}, totalSize={}",
           width, height, source_bytes_per_pixel, width * source_bytes_per_pixel, source_data_size);
    blit_encoder->copyFromTexture(
        capture_texture, 0, 0,  // sourceSlice, sourceLevel
        MTL::Origin(0, 0, 0),  // sourceOrigin
        MTL::Size(width, height, 1),  // sourceSize
        read_buffer,  // destinationBuffer
        0,  // destinationOffset
        width * source_bytes_per_pixel,  // destinationBytesPerRow (use source format size)
        source_data_size  // destinationBytesPerImage (total size)
    );
  }
  
  blit_encoder->endEncoding();
  // blit_encoder is autoreleased, don't release manually
  
  // Submit and wait for completion
  command_buffer->commit();
  command_buffer->waitUntilCompleted();
  // command_buffer is autoreleased, don't release manually
  
  // Handle data conversion based on format
  if (is_depth_stencil || is_depth_only) {
    XELOGI("Metal CaptureColorTarget: Converting depth values to RGBA for visualization");
    
    // Create temporary vector with source data
    std::vector<uint8_t> source_data(source_data_size);
    memcpy(source_data.data(), read_buffer->contents(), source_data_size);
    
    // Convert depth values to RGBA for visualization
    ConvertDepthToRGBA(source_data, width, height, pixel_format);
    
    // Copy converted data to output
    data = std::move(source_data);
  } else {
    // Direct copy for color textures
    memcpy(data.data(), read_buffer->contents(), std::min(data_size, source_data_size));
    
    // Check if the data is all zeros (black)
    bool all_zeros = true;
    for (size_t i = 0; i < std::min(data_size, size_t(1000)); i++) {
      if (data[i] != 0) {
        all_zeros = false;
        break;
      }
    }
    if (all_zeros) {
      XELOGW("Metal CaptureColorTarget: WARNING - Captured data is all zeros (black)!");
    } else {
      XELOGI("Metal CaptureColorTarget: Data contains non-zero values (not all black)");
      // Log first few pixels for debugging
      if (data.size() >= 16) {
        XELOGI("  First 4 pixels (RGBA): [{},{},{},{}] [{},{},{},{}] [{},{},{},{}] [{},{},{},{}]",
               data[0], data[1], data[2], data[3],
               data[4], data[5], data[6], data[7],
               data[8], data[9], data[10], data[11],
               data[12], data[13], data[14], data[15]);
      }
    }
  }
  
  // Clean up
  read_buffer->release();
  if (resolved_texture) {
    resolved_texture->release();
  }
  
  XELOGI("Metal CaptureColorTarget: Captured {}x{} texture", width, height);
  AutoreleasePoolTracker::Pop(pool, "CaptureColorTarget");
  return true;
}

bool MetalCommandProcessor::GetLastCapturedFrame(uint32_t& width, uint32_t& height, std::vector<uint8_t>& data) {
  if (last_captured_frame_data_.empty()) {
    XELOGW("Metal GetLastCapturedFrame: No captured frame available");
    return false;
  }
  
  width = last_captured_frame_width_;
  height = last_captured_frame_height_;
  data = last_captured_frame_data_;
  
  XELOGI("Metal GetLastCapturedFrame: Returning {}x{} frame", width, height);
  return true;
}

void MetalCommandProcessor::ConvertDepthToRGBA(std::vector<uint8_t>& data, uint32_t width, uint32_t height, MTL::PixelFormat format) {
  // Convert depth values to RGBA for visualization
  // Depth values are typically float32 in range [0,1], we'll map to grayscale
  
  if (format == MTL::PixelFormatDepth32Float || format == MTL::PixelFormatDepth32Float_Stencil8) {
    // Each depth value is a 32-bit float
    const float* depth_data = reinterpret_cast<const float*>(data.data());
    size_t pixel_count = width * height;
    
    // Create RGBA data (4 bytes per pixel)
    std::vector<uint8_t> rgba_data(pixel_count * 4);
    
    for (size_t i = 0; i < pixel_count; ++i) {
      float depth = depth_data[i];
      
      // Clamp depth to [0,1] and convert to [0,255]
      depth = std::max(0.0f, std::min(1.0f, depth));
      uint8_t gray = static_cast<uint8_t>(depth * 255.0f);
      
      // Set RGBA to grayscale (RGB same value, A=255)
      rgba_data[i * 4 + 0] = gray;  // R
      rgba_data[i * 4 + 1] = gray;  // G
      rgba_data[i * 4 + 2] = gray;  // B
      rgba_data[i * 4 + 3] = 255;   // A
    }
    
    // Replace original data
    data = std::move(rgba_data);
    XELOGI("Metal ConvertDepthToRGBA: Converted {}x{} depth32f to RGBA", width, height);
    
  } else if (format == MTL::PixelFormatDepth16Unorm) {
    // Each depth value is a 16-bit unsigned normalized integer
    const uint16_t* depth_data = reinterpret_cast<const uint16_t*>(data.data());
    size_t pixel_count = width * height;
    
    // Create RGBA data (4 bytes per pixel)  
    std::vector<uint8_t> rgba_data(pixel_count * 4);
    
    for (size_t i = 0; i < pixel_count; ++i) {
      uint16_t depth_u16 = depth_data[i];
      
      // Convert 16-bit [0,65535] to 8-bit [0,255]
      uint8_t gray = static_cast<uint8_t>(depth_u16 >> 8);
      
      // Set RGBA to grayscale
      rgba_data[i * 4 + 0] = gray;  // R
      rgba_data[i * 4 + 1] = gray;  // G  
      rgba_data[i * 4 + 2] = gray;  // B
      rgba_data[i * 4 + 3] = 255;   // A
    }
    
    // Replace original data
    data = std::move(rgba_data);
    XELOGI("Metal ConvertDepthToRGBA: Converted {}x{} depth16 to RGBA", width, height);
  }
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

// Apply Xbox 360 vertex component swizzle
void ApplyVertexSwizzle(uint8_t* element_data, 
                        xenos::VertexFormat format,
                        const SwizzleSource* components) {
  uint32_t component_count = xenos::GetVertexFormatComponentCount(format);
  if (component_count == 0) return;
  
  // Read original float values
  float original[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  
  // Only handle float formats for now (most common case)
  switch (format) {
    case xenos::VertexFormat::k_32_FLOAT:
      original[0] = *(float*)(element_data);
      break;
    case xenos::VertexFormat::k_32_32_FLOAT:
      original[0] = *(float*)(element_data + 0);
      original[1] = *(float*)(element_data + 4);
      break;
    case xenos::VertexFormat::k_32_32_32_FLOAT:
      original[0] = *(float*)(element_data + 0);
      original[1] = *(float*)(element_data + 4);
      original[2] = *(float*)(element_data + 8);
      break;
    case xenos::VertexFormat::k_32_32_32_32_FLOAT:
      original[0] = *(float*)(element_data + 0);
      original[1] = *(float*)(element_data + 4);
      original[2] = *(float*)(element_data + 8);
      original[3] = *(float*)(element_data + 12);
      break;
    default:
      // Non-float formats not implemented yet
      return;
  }
  
  // Apply swizzle to create output
  float swizzled[4];
  for (uint32_t i = 0; i < 4; i++) {
    switch (components[i]) {
      case SwizzleSource::kX:
        swizzled[i] = original[0];
        break;
      case SwizzleSource::kY:
        swizzled[i] = (component_count > 1) ? original[1] : 0.0f;
        break;
      case SwizzleSource::kZ:
        swizzled[i] = (component_count > 2) ? original[2] : 0.0f;
        break;
      case SwizzleSource::kW:
        swizzled[i] = (component_count > 3) ? original[3] : 1.0f;
        break;
      case SwizzleSource::k0:
        swizzled[i] = 0.0f;
        break;
      case SwizzleSource::k1:
        swizzled[i] = 1.0f;
        break;
    }
  }
  
  // Log swizzle application for first vertex
  static bool logged_swizzle = false;
  if (!logged_swizzle) {
    XELOGI("Vertex swizzle: [{:.3f},{:.3f},{:.3f},{:.3f}] -> [{:.3f},{:.3f},{:.3f},{:.3f}]",
           original[0], original[1], original[2], original[3],
           swizzled[0], swizzled[1], swizzled[2], swizzled[3]);
    logged_swizzle = true;
  }
  
  // Write back swizzled data (only write the actual component count)
  switch (format) {
    case xenos::VertexFormat::k_32_FLOAT:
      *(float*)(element_data) = swizzled[0];
      break;
    case xenos::VertexFormat::k_32_32_FLOAT:
      *(float*)(element_data + 0) = swizzled[0];
      *(float*)(element_data + 4) = swizzled[1];
      break;
    case xenos::VertexFormat::k_32_32_32_FLOAT:
      *(float*)(element_data + 0) = swizzled[0];
      *(float*)(element_data + 4) = swizzled[1];
      *(float*)(element_data + 8) = swizzled[2];
      break;
    case xenos::VertexFormat::k_32_32_32_32_FLOAT:
      *(float*)(element_data + 0) = swizzled[0];
      *(float*)(element_data + 4) = swizzled[1];
      *(float*)(element_data + 8) = swizzled[2];
      *(float*)(element_data + 12) = swizzled[3];
      break;
  }
}

bool MetalCommandProcessor::IssueDraw(xenos::PrimitiveType prim_type,
                                      uint32_t index_count,
                                      IndexBufferInfo* index_buffer_info,
                                      bool major_mode_explicit) {
  // NOTE: No autorelease pool here - too broad, causes accumulation
  // We'll add focused pools around specific Metal operations instead
  
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
  XELOGI("Metal IssueDraw: Calling BeginSubmission, submission_open_={}", submission_open_);
  if (!BeginSubmission(true)) {
    XELOGE("Metal IssueDraw: Failed to begin submission");
    return false;
  }
  XELOGI("Metal IssueDraw: BeginSubmission succeeded, current_command_buffer_={}", 
         current_command_buffer_ != nullptr);
  
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
  
  // REFACTOR: Use base RenderTargetCache infrastructure
  // Calculate normalized color mask based on pixel shader outputs
  uint32_t normalized_color_mask = pixel_shader ? 
      draw_util::GetNormalizedColorMask(*register_file_, pixel_shader->writes_color_targets()) : 0;
  
  // Get normalized depth control
  reg::RB_DEPTHCONTROL normalized_depth_control = 
      draw_util::GetNormalizedDepthControl(*register_file_);
  
  // Determine if rasterization is done (not a memexport-only draw)
  bool is_rasterization_done = true;  // For now, assume rasterization is always done
  
  // Call base RenderTargetCache::Update() - this handles:
  // - EDRAM snapshot interpretation when registers are zero
  // - Render target creation from EDRAM contents
  // - Complex render target resolution
  if (!render_target_cache_->Update(is_rasterization_done,
                                    normalized_depth_control,
                                    normalized_color_mask,
                                    *vertex_shader)) {
    XELOGE("Metal IssueDraw: RenderTargetCache::Update failed");
    return false;
  }
  
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
  
  // The render target cache will automatically create a dummy color target
  // when rt_count == 0, so we don't need to create a fake configuration here
  
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
  // NOTE: The base RenderTargetCache::Update() already set up render targets
  // We don't need to call SetRenderTargets anymore
  bool depth_enabled = (rb_depthcontrol & 0x00000002) || (rb_depthcontrol & 0x00000004);
  
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
        pipeline_desc.sample_count = static_cast<uint32_t>(color_target->sampleCount());
      }
    }
  }
  
  // Get depth format
  MTL::Texture* depth_target = render_target_cache_->GetDepthTarget();
  if (depth_target) {
    pipeline_desc.depth_format = depth_target->pixelFormat();
    // If no color targets, get sample count from depth target
    if (pipeline_desc.num_color_attachments == 0) {
      pipeline_desc.sample_count = static_cast<uint32_t>(depth_target->sampleCount());
    }
  }
  
  // Check if the pixel shader writes color output
  bool shader_writes_color = false;
  if (pixel_shader) {
    // The shader analyzer determines which render targets are written to
    shader_writes_color = pixel_shader->writes_color_targets() != 0;
  }
  
  // If the shader writes color but we have no color attachments, we need a dummy target
  // This is required by Metal - if the fragment shader writes color, there must be at least one color attachment
  if (shader_writes_color && pipeline_desc.num_color_attachments == 0) {
    XELOGW("Metal IssueDraw: Shader writes color but no color targets bound - adding dummy target");
    // Use the depth target's sample count if available, otherwise from RB_SURFACE_INFO
    uint32_t sample_count = pipeline_desc.sample_count;
    if (sample_count == 1 && depth_target) {
      sample_count = static_cast<uint32_t>(depth_target->sampleCount());
    }
    
    pipeline_desc.color_formats[0] = MTL::PixelFormatBGRA8Unorm;
    pipeline_desc.num_color_attachments = 1;
    pipeline_desc.sample_count = sample_count;
  }
  
  // If no render targets are bound at all (neither color nor depth), use a default format for the dummy target
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
  
  // Get viewport information and NDC transformation values
  // This needs to be calculated here so it's available for the encoder setup
  // 
  // Metal Coordinate Systems (per Metal Specification):
  // - NDC: Left-handed, (-1,-1) at bottom-left, (1,1) at top-right, Z: 0 to 1
  // - Viewport: Origin at top-left, Y+ down, measured in pixels
  // 
  // Xbox 360 outputs vertices in screen space (0 to width/height)
  // We need to transform these to Metal's NDC (-1 to 1)
  
  // Get actual render target dimensions for proper NDC scaling
  uint32_t max_rt_width = 1280;
  uint32_t max_rt_height = 720;
  
  // Use the first color target if available
  MTL::Texture* primary_color_target = render_target_cache_->GetColorTarget(0);
  if (primary_color_target) {
    max_rt_width = static_cast<uint32_t>(primary_color_target->width());
    max_rt_height = static_cast<uint32_t>(primary_color_target->height());
  } else if (depth_target) {
    // Fall back to depth target dimensions if no color target
    max_rt_width = static_cast<uint32_t>(depth_target->width());
    max_rt_height = static_cast<uint32_t>(depth_target->height());
  }
  
  draw_util::ViewportInfo viewport_info;
  draw_util::GetHostViewportInfo(
      *register_file_, 1, 1, true,  // origin_bottom_left=true for Metal's NDC Y-up system
      max_rt_width, max_rt_height,  // Use actual RT dimensions, not conservative max
      true,  // Allow reverse Z
      normalized_depth_control,
      false,  // Don't convert Z to float24
      true,   // Host render targets used
      pixel_shader && pixel_shader->writes_depth(),  // Check if pixel shader writes depth
      viewport_info);
  
  XELOGI("Metal IssueDraw: Viewport NDC transform - scale: [{}, {}, {}], offset: [{}, {}, {}]",
         viewport_info.ndc_scale[0], viewport_info.ndc_scale[1], viewport_info.ndc_scale[2],
         viewport_info.ndc_offset[0], viewport_info.ndc_offset[1], viewport_info.ndc_offset[2]);
  
  // Request pipeline state from cache (this will create it if needed)
  MTL::RenderPipelineState* pipeline_state = nullptr;
  {
    // Scoped pool for pipeline creation (may trigger shader compilation)
    XE_SCOPED_AUTORELEASE_POOL("GetRenderPipelineState");
    pipeline_state = pipeline_cache_->GetRenderPipelineState(pipeline_desc);
  }
  
  if (pipeline_state) {
    XELOGI("Metal IssueDraw: Successfully obtained pipeline state");
    
    // Get shader translations for reflection data
    auto [vertex_translation, pixel_translation] = 
        pipeline_cache_->GetShaderTranslations(pipeline_desc);
    
    if (!vertex_translation || !pixel_translation) {
      XELOGW("Metal IssueDraw: Failed to get shader translations for reflection");
    }
    
    // Phase C Step 1: Metal Command Buffer and Render Pass Encoding
    MTL::CommandQueue* command_queue = GetMetalCommandQueue();
    if (command_queue) {
      // The command buffer should already be created by BeginSubmission called earlier
      MTL::CommandBuffer* command_buffer = current_command_buffer_;
      if (!command_buffer) {
        XELOGE("Metal IssueDraw: current_command_buffer_ is null even after BeginSubmission!");
        return false;
      }
      
      // Validate the command buffer is actually a command buffer
      XELOGI("Metal IssueDraw: Validating command buffer at address {}", (void*)command_buffer);
      
      if (command_buffer) {
        XELOGI("Metal IssueDraw: Using command buffer for rendering");
        
        // Phase C Step 3: Enhanced Metal frame debugging support
        char draw_label[256];
        snprintf(draw_label, sizeof(draw_label), "Xbox360Draw_%s_idx%u", 
                prim_type_name, index_count);
        // Skip label setting for now to avoid potential crashes
        XELOGI("Metal IssueDraw: Skipping command buffer label (would be: {})", draw_label);
        
        // Render targets were already set up before pipeline creation
        
        // Get render pass descriptor from cache
        // IMPORTANT: We need to ensure the render pass uses the same sample count as the pipeline
        // Store the pipeline's sample count so we can use it if we need to create a dummy target
        uint32_t pipeline_sample_count = pipeline_desc.sample_count;
        
        XELOGI("Metal IssueDraw: Getting render pass descriptor from cache (pipeline expects {} samples)...", 
               pipeline_sample_count);
        
        // Pass the pipeline's sample count to ensure dummy targets match
        MTL::RenderPassDescriptor* render_pass = render_target_cache_->GetRenderPassDescriptor(pipeline_sample_count);
        if (render_pass) {
          // Retain to prevent autorelease
          render_pass->retain();
        }
        XELOGI("Metal IssueDraw: Got render pass: {}", render_pass ? "valid" : "null");
        
        if (!render_pass) {
          XELOGE("Metal IssueDraw: Failed to get render pass from cache!");
          return false;
        }
        
        // Check if we need to create a new render encoder or can reuse the existing one
        // Only create a new encoder if:
        // 1. We don't have one yet
        // 2. The render pass has changed (different render targets)
        // 3. The command buffer has changed
        
        bool need_new_encoder = false;
        if (!current_render_encoder_) {
          XELOGI("Metal IssueDraw: Need new encoder - no current encoder");
          need_new_encoder = true;
        } else if (current_render_pass_ != render_pass) {
          XELOGI("Metal IssueDraw: Need new encoder - render pass changed");
          need_new_encoder = true;
        } else if (active_draw_command_buffer_ != command_buffer) {
          XELOGI("Metal IssueDraw: Need new encoder - command buffer changed");
          need_new_encoder = true;
        }
        
        MTL::RenderCommandEncoder* encoder = current_render_encoder_;
        
        if (need_new_encoder) {
          // End the previous encoder if it exists
          if (current_render_encoder_) {
            XELOGI("Metal IssueDraw: Ending previous render encoder");
            current_render_encoder_->endEncoding();
            current_render_encoder_->release();
            current_render_encoder_ = nullptr;
          }
          
          // Release old render pass reference
          if (current_render_pass_) {
            current_render_pass_->release();
            current_render_pass_ = nullptr;
          }
          
          XELOGI("Metal IssueDraw: Creating new render command encoder...");
          XELOGI("Metal IssueDraw: Command buffer valid: {}, render pass valid: {}", 
                 command_buffer != nullptr, render_pass != nullptr);
          
          if (!command_buffer) {
            XELOGE("Metal IssueDraw: Command buffer is null!");
            return false;
          }
          if (!render_pass) {
            XELOGE("Metal IssueDraw: Render pass is null!");
            return false;
          }
          
          // Store the render pass descriptor for potential capture fallback
          if (last_render_pass_descriptor_) {
            last_render_pass_descriptor_->release();
            last_render_pass_descriptor_ = nullptr;
          }
          last_render_pass_descriptor_ = render_pass->retain();
          
          // Wrap in Objective-C exception handling since Metal throws NSExceptions
          @try {
            encoder = command_buffer->renderCommandEncoder(render_pass);
            XELOGI("Metal IssueDraw: renderCommandEncoder returned: {}", encoder ? "valid" : "null");
            if (encoder) {
              // Retain the encoder to prevent it from being autoreleased prematurely
              encoder->retain();
              current_render_encoder_ = encoder;
              current_render_pass_ = render_pass->retain();
              active_draw_command_buffer_ = command_buffer;
              XELOGI("Metal IssueDraw: New encoder created and stored for reuse");
            }
          }
          @catch (NSException* exception) {
            // Log the Objective-C exception details
            XELOGE("Metal IssueDraw: NSException creating render encoder - Name: {}, Reason: {}",
                   [[exception name] UTF8String],
                   [[exception reason] UTF8String]);
            encoder = nullptr;
          }
          @catch (...) {
            XELOGE("Metal IssueDraw: Unknown exception creating render encoder");
            encoder = nullptr;
          }
        } else {
          XELOGI("Metal IssueDraw: Reusing existing render encoder");
        }
        
        XELOGI("Metal IssueDraw: Render encoder {}: {}", 
               need_new_encoder ? "created" : "reused", encoder ? "valid" : "null");
        
        if (encoder) {
          // Track encoder creation
          TRACK_METAL_OBJECT(encoder, "MTL::RenderCommandEncoder");
          char encoder_label[256];
          snprintf(encoder_label, sizeof(encoder_label), "Xbox360RenderPass_%s_hash_0x%llx_0x%llx", 
                  prim_type_name, pipeline_desc.vertex_shader_hash, pipeline_desc.pixel_shader_hash);
          encoder->setLabel(NS::String::string(encoder_label, NS::UTF8StringEncoding));
          
          // TEMPORARY: Force disable culling and depth test to prove fragments can hit the RT
          encoder->setCullMode(MTL::CullModeNone);
          
          MTL::DepthStencilDescriptor* ds_desc = MTL::DepthStencilDescriptor::alloc()->init();
          ds_desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
          ds_desc->setDepthWriteEnabled(false);
          MTL::DepthStencilState* ds = GetMetalDevice()->newDepthStencilState(ds_desc);
          ds_desc->release();
          encoder->setDepthStencilState(ds);
          ds->release();
          XELOGI("Metal IssueDraw: Forced CullNone + DepthAlways for testing");
          
          // Phase C Step 2: Pipeline State and Draw Call Encoding
          
          // TEST NDC TRANSFORMATION - Enable this to test NDC transformation
          const bool test_ndc_transform = false; // Set to true to enable NDC test
          
          if (test_ndc_transform) {
            // Replace the pipeline with our NDC test pipeline
            MTL::RenderPipelineState* ndc_test_pipeline = 
                pipeline_cache_->GetRenderPipelineStateWithNDCTransform(pipeline_desc);
            
            if (ndc_test_pipeline) {
              XELOGI("Metal IssueDraw: TESTING NDC TRANSFORMATION");
              XELOGI("  - Using test pipeline that transforms screen space to NDC");
              XELOGI("  - Should render a colored triangle if transformation works");
              encoder->setRenderPipelineState(ndc_test_pipeline);
              pipeline_state = ndc_test_pipeline; // Update for later use
            } else {
              XELOGE("Metal IssueDraw: Failed to create NDC test pipeline, using original");
              encoder->setRenderPipelineState(pipeline_state);
            }
          } else {
            encoder->setRenderPipelineState(pipeline_state);
          }
          
          // Set viewport to match our render target dimensions
          // Get viewport from Xbox 360 registers
          auto& rf = *register_file_;
          float viewport_scale_x = rf.Get<float>(XE_GPU_REG_PA_CL_VPORT_XSCALE);
          float viewport_scale_y = rf.Get<float>(XE_GPU_REG_PA_CL_VPORT_YSCALE);
          float viewport_offset_x = rf.Get<float>(XE_GPU_REG_PA_CL_VPORT_XOFFSET);
          float viewport_offset_y = rf.Get<float>(XE_GPU_REG_PA_CL_VPORT_YOFFSET);
          
          // Get actual render target dimensions
          const uint32_t rt_width = render_pass->colorAttachments()->object(0)->texture()
                                   ? static_cast<uint32_t>(render_pass->colorAttachments()->object(0)->texture()->width())
                                   : (render_pass->depthAttachment()->texture()
                                        ? static_cast<uint32_t>(render_pass->depthAttachment()->texture()->width())
                                        : 1280);
          const uint32_t rt_height = render_pass->colorAttachments()->object(0)->texture()
                                    ? static_cast<uint32_t>(render_pass->colorAttachments()->object(0)->texture()->height())
                                    : (render_pass->depthAttachment()->texture()
                                         ? static_cast<uint32_t>(render_pass->depthAttachment()->texture()->height())
                                         : 720);
          
          // Xbox 360 provides center + half-extent. Y half-extent is often negative (top-left style).
          // Convert to top-left rect first, then flip to Metal's bottom-left viewport coords.
          float vx_center = viewport_offset_x;
          float vy_center = viewport_offset_y;
          float vx_half = viewport_scale_x;
          float vy_half = std::abs(viewport_scale_y);  // half extents are positive sizes
          
          float rect_left = vx_center - vx_half;
          float rect_topTL = vy_center - vy_half;  // top in top-left space
          float rect_width = std::max(0.0f, vx_half * 2.0f);
          float rect_height = std::max(0.0f, vy_half * 2.0f);
          
          // Clamp to RT bounds in top-left space to avoid degenerate viewports
          rect_left = std::max(0.0f, std::min(rect_left, static_cast<float>(rt_width)));
          rect_topTL = std::max(0.0f, std::min(rect_topTL, static_cast<float>(rt_height)));
          if (rect_left + rect_width > rt_width) rect_width = rt_width - rect_left;
          if (rect_topTL + rect_height > rt_height) rect_height = rt_height - rect_topTL;
          
          // Convert top-left rect to Metal's bottom-left viewport coords
          float originX = rect_left;
          float originY = static_cast<float>(rt_height) - (rect_topTL + rect_height);
          
          MTL::Viewport viewport;
          // Since the shader transforms vertices from screen space to NDC,
          // we need to set the viewport to map NDC (-1 to 1) to screen pixels
          // The shader has already done the transformation, so we just map NDC to screen
          viewport.originX = 0;
          viewport.originY = 0;
          viewport.width = static_cast<double>(rt_width);
          viewport.height = static_cast<double>(rt_height);
          viewport.znear = static_cast<double>(viewport_info.z_min);
          viewport.zfar = static_cast<double>(viewport_info.z_max);
          
          encoder->setViewport(viewport);
          XELOGI("Metal IssueDraw: Set viewport to full framebuffer (0, 0, {}, {}, znear={}, zfar={})",
                 rt_width, rt_height, viewport.znear, viewport.zfar);
          
          // Set scissor rect from Xbox 360 registers
          uint32_t window_scissor_tl = rf.values[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL];
          uint32_t window_scissor_br = rf.values[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR];
          
          MTL::ScissorRect scissor;
          scissor.x = window_scissor_tl & 0x7FFF;  // TL X coordinate
          scissor.y = (window_scissor_tl >> 16) & 0x7FFF;  // TL Y coordinate
          uint32_t br_x = window_scissor_br & 0x7FFF;  // BR X coordinate
          uint32_t br_y = (window_scissor_br >> 16) & 0x7FFF;  // BR Y coordinate
          scissor.width = br_x > scissor.x ? br_x - scissor.x : 0;
          scissor.height = br_y > scissor.y ? br_y - scissor.y : 0;
          
          // Prevent degenerate scissor rects
          if (scissor.width == 0 || scissor.height == 0) {
            // Fallback to full target if the guest set a degenerate scissor
            scissor.x = 0;
            scissor.y = 0;
            scissor.width = rt_width;
            scissor.height = rt_height;
            XELOGW("Metal IssueDraw: Degenerate scissor detected, using full target");
          }
          
          if (scissor.x + scissor.width > rt_width) scissor.width = rt_width - scissor.x;
          if (scissor.y + scissor.height > rt_height) scissor.height = rt_height - scissor.y;
          
          encoder->setScissorRect(scissor);
          XELOGI("Metal IssueDraw: Set scissor rect ({}, {}, {}, {})",
                 scissor.x, scissor.y, scissor.width, scissor.height);
          
          XELOGI("Metal IssueDraw: Set pipeline state and created render encoder");
          XELOGI("Metal IssueDraw: Render pass label: {}", encoder_label);
          
          // Phase C Step 2: Enhanced debugging - Push debug group
          encoder->pushDebugGroup(NS::String::string("Xbox360DrawCommands", NS::UTF8StringEncoding));
          
          // Phase C Step 3: Vertex Buffer Support from Xbox 360 guest memory
          // Process vertex bindings from the active vertex shader
          std::vector<MTL::Buffer*> vertex_buffers_to_release;
          MTL::Buffer* first_bound_vertex_buffer = nullptr; // Track first VB for T0/U0 binding
          
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
              
              // Create pool for vertex buffer operations
              {
                XE_SCOPED_AUTORELEASE_POOL("CreateVertexBuffer");
                
                // Check if any vertex processing is needed
                bool needs_endian_swap = (vfetch_constant.endian != xenos::Endian::kNone);
                bool needs_swizzle = false;
                
                // Check all attributes for non-standard swizzles
                for (const auto& attribute : binding.attributes) {
                  if (!attribute.fetch_instr.result.IsStandardSwizzle()) {
                    needs_swizzle = true;
                    break;
                  }
                }
                
                if (needs_endian_swap || needs_swizzle) {
                  // Need to process vertices - create a working copy
                  void* processed_data = std::malloc(size);
                  if (processed_data) {
                    std::memcpy(processed_data, vertex_data, size);
                    
                    uint32_t stride_bytes = binding.stride_words * 4;
                    uint32_t vertex_count = size / stride_bytes;
                    
                    if (needs_endian_swap) {
                      XELOGI("Metal IssueDraw: Vertex buffer needs endian swap - type: {}", 
                             static_cast<uint32_t>(vfetch_constant.endian));
                    }
                    
                    for (uint32_t v = 0; v < vertex_count; v++) {
                      uint8_t* vertex_start = static_cast<uint8_t*>(processed_data) + (v * stride_bytes);
                      
                      // Process each attribute in the vertex
                      for (const auto& attribute : binding.attributes) {
                        const auto& fetch = attribute.fetch_instr;
                        uint8_t* element_data = vertex_start + fetch.attributes.offset * 4;
                        
                        // Step 1: Apply endian swap if needed
                        if (needs_endian_swap) {
                          SwapVertexElement(element_data, fetch.attributes.data_format, vfetch_constant.endian);
                        }
                        
                        // Log all attributes for first vertex (for debugging)
                        if (v == 0) {
                          // Find attribute index
                          size_t attr_idx = 0;
                          for (size_t i = 0; i < binding.attributes.size(); i++) {
                            if (&binding.attributes[i] == &attribute) {
                              attr_idx = i;
                              break;
                            }
                          }
                          XELOGI("Vertex attr {}: [{}{}{}{}] offset={} format={} {}",
                                 attr_idx,
                                 GetCharForSwizzle(fetch.result.components[0]),
                                 GetCharForSwizzle(fetch.result.components[1]),
                                 GetCharForSwizzle(fetch.result.components[2]),
                                 GetCharForSwizzle(fetch.result.components[3]),
                                 fetch.attributes.offset,
                                 static_cast<uint32_t>(fetch.attributes.data_format),
                                 fetch.result.IsStandardSwizzle() ? "(standard)" : "(swizzled)");
                        }
                        
                        // Step 2: Apply component swizzle if not standard XYZW
                        if (!fetch.result.IsStandardSwizzle()) {
                          ApplyVertexSwizzle(element_data, fetch.attributes.data_format, fetch.result.components);
                        }
                      }
                    }
                    
                    vertex_buffer = GetMetalDevice()->newBuffer(
                        processed_data, 
                        size, 
                        MTL::ResourceStorageModeShared);
                    TRACK_METAL_OBJECT(vertex_buffer, "MTL::Buffer[VertexProcessed]");
                    
                    std::free(processed_data);
                  }
                } else {
                  // No processing needed - use data directly
                  vertex_buffer = GetMetalDevice()->newBuffer(
                      vertex_data, 
                      size, 
                      MTL::ResourceStorageModeShared);
                  TRACK_METAL_OBJECT(vertex_buffer, "MTL::Buffer[VertexDirect]");
                }
              } // End autorelease pool for vertex buffer creation
              
              if (vertex_buffer) {
                // Label the buffer for debugging
                char label[256];
                snprintf(label, sizeof(label), "Xbox360VertexBuffer_%d_0x%08X", 
                        binding.binding_index, address);
                vertex_buffer->setLabel(NS::String::string(label, NS::UTF8StringEncoding));
                
                // Bind vertex buffer at the appropriate index
                // CRITICAL FIX: The DXIL→Metal converter expects vertex buffers at specific indices
                // After analyzing the shader conversion, vertex buffers should be at:
                // - Index 30: Primary vertex buffer (v0)
                // - Index 31: Secondary vertex buffer (v1) 
                // - Index 0-29: Reserved for constant buffers and other resources
                // The huge offset (506099730477023245) suggests the shader is looking for vertex data
                // at a buffer index that's not bound (likely index 30+)
                
                // Map Xbox 360 binding index to Metal buffer index
                // Metal IR Converter expects vertex buffers starting at index 6
                uint32_t metal_buffer_index = static_cast<uint32_t>(kIRVertexBufferBindPoint) + binding.binding_index;
                
                // Debug: Log vertex data from the processed buffer
                if (binding.stride_words > 0 && size >= binding.stride_words * 4) {
                  float* vertex_data = static_cast<float*>(vertex_buffer->contents());
                  uint32_t stride_floats = binding.stride_words;
                  
                  // Log first vertex with full stride
                  std::string vertex_str;
                  for (uint32_t i = 0; i < stride_floats && i < 8; i++) {
                    if (i > 0) vertex_str += ", ";
                    vertex_str += fmt::format("{:.3f}", vertex_data[i]);
                  }
                  XELOGI("  First vertex (processed): [{}]", vertex_str);
                  
                  // Log second vertex if available
                  if (size >= binding.stride_words * 8) {
                    vertex_str.clear();
                    for (uint32_t i = 0; i < stride_floats && i < 8; i++) {
                      if (i > 0) vertex_str += ", ";
                      vertex_str += fmt::format("{:.3f}", vertex_data[stride_floats + i]);
                    }
                    XELOGI("  Second vertex (processed): [{}]", vertex_str);
                  }
                }
                
                // --- DEBUG: CPU-side viewport transform from screen-pixel -> clip ---
                // Enable with a local flag while validating
                static const bool kCPUViewportTransformTest = false;  // DISABLED - let shader handle NDC transform
                
                if (kCPUViewportTransformTest && vertex_buffer) {
                  void* transformed_data = nullptr;  // Track if we need to free this
                  // Get RT size (we computed these earlier for viewport/scissor)
                  uint32_t rt_width = 1280, rt_height = 720;
                  if (render_pass && render_pass->colorAttachments()->object(0)->texture()) {
                    MTL::Texture* ct = render_pass->colorAttachments()->object(0)->texture();
                    rt_width = static_cast<uint32_t>(ct->width());
                    rt_height = static_cast<uint32_t>(ct->height());
                  } else if (render_pass && render_pass->depthAttachment()->texture()) {
                    MTL::Texture* dt = render_pass->depthAttachment()->texture();
                    rt_width = static_cast<uint32_t>(dt->width());
                    rt_height = static_cast<uint32_t>(dt->height());
                  }
                  
                  const float inv_w = rt_width > 1 ? 1.0f / float(rt_width) : 1.0f;
                  const float inv_h = rt_height > 1 ? 1.0f / float(rt_height) : 1.0f;
                  
                  // Find POSITION attribute in this binding
                  int pos_attr_index = -1;
                  xenos::VertexFormat pos_fmt = xenos::VertexFormat::kUndefined;
                  uint32_t pos_offset_words = 0;
                  
                  for (size_t ai = 0; ai < binding.attributes.size(); ++ai) {
                    const auto& attr = binding.attributes[ai];
                    const auto& fi = attr.fetch_instr;
                    // Check if this is a position attribute (usually offset 0, float format)
                    // We'll detect by checking if it's the first float2/3/4 attribute
                    if (fi.attributes.offset == 0 && 
                        (fi.attributes.data_format == xenos::VertexFormat::k_32_32_FLOAT ||
                         fi.attributes.data_format == xenos::VertexFormat::k_32_32_32_FLOAT ||
                         fi.attributes.data_format == xenos::VertexFormat::k_32_32_32_32_FLOAT)) {
                      pos_attr_index = static_cast<int>(ai);
                      pos_fmt = fi.attributes.data_format;
                      pos_offset_words = fi.attributes.offset;
                      XELOGI("CPU viewport transform: Found POSITION at offset {} with format {}", 
                             pos_offset_words, static_cast<uint32_t>(pos_fmt));
                      break;
                    }
                  }
                  
                  if (pos_attr_index >= 0) {
                    uint32_t stride_bytes = binding.stride_words * 4;
                    uint32_t vertex_count = size / stride_bytes;
                    
                    // Get the data to transform - either swapped or original
                    void* data_to_transform = vertex_buffer ? vertex_buffer->contents() : nullptr;
                    if (!data_to_transform) {
                      XELOGW("CPU viewport transform: No vertex data to transform");
                    } else {
                      // If we need to transform in-place, make a copy first
                      transformed_data = std::malloc(size);
                      if (transformed_data) {
                        std::memcpy(transformed_data, data_to_transform, size);
                        
                        XELOGI("CPU viewport transform: Transforming {} vertices from screen [{}x{}] to clip space",
                               vertex_count, rt_width, rt_height);
                        
                        for (uint32_t v = 0; v < vertex_count; ++v) {
                          uint8_t* vertex_start = static_cast<uint8_t*>(transformed_data) + (v * stride_bytes);
                          uint8_t* elem = vertex_start + pos_offset_words * 4;
                      
                      // Handle common float formats
                      auto to_clip = [&](float& x, float& y) {
                        float orig_x = x, orig_y = y;
                        // Map 0..W → -1..+1 and 0..H → +1..-1 (Metal clip Y up is positive)
                        x = x * (2.0f * inv_w) - 1.0f;
                        y = 1.0f - (y * (2.0f * inv_h));
                        if (v == 0) {  // Log first vertex transformation
                          XELOGI("CPU viewport transform: Vertex[0] ({:.2f}, {:.2f}) -> ({:.2f}, {:.2f})",
                                 orig_x, orig_y, x, y);
                        }
                      };
                      
                      switch (pos_fmt) {
                        case xenos::VertexFormat::k_32_32_FLOAT: {
                          float* p = reinterpret_cast<float*>(elem);
                          to_clip(p[0], p[1]);
                          break;
                        }
                        case xenos::VertexFormat::k_32_32_32_FLOAT: {
                          float* p = reinterpret_cast<float*>(elem);
                          // Quick test: Just transform the first two components normally
                          // regardless of what's in Z
                          float x = p[0];
                          float y = p[1];
                          float z = p[2];
                          
                          // Log what we're seeing
                          if (v == 0) {
                            XELOGI("CPU viewport transform: Raw vertex[0] = ({:.2f}, {:.2f}, {:.2f})",
                                   x, y, z);
                          }
                          
                          // If Z looks like a screen coordinate (> 100), maybe it's X?
                          if (std::abs(z) > 100.0f) {
                            // Z contains screen X coordinate
                            float screen_x = z;
                            float screen_y = y;
                            to_clip(screen_x, screen_y);
                            p[0] = screen_x;
                            p[1] = screen_y;
                            p[2] = 0.5f;
                            if (v == 0) {
                              XELOGI("CPU viewport transform: Interpreted as swizzled (Z->X)");
                            }
                          } else {
                            // Normal layout
                            to_clip(x, y);
                            p[0] = x;
                            p[1] = y;
                            // Leave Z as-is
                            if (v == 0) {
                              XELOGI("CPU viewport transform: Interpreted as normal layout");
                            }
                          }
                          break;
                        }
                        case xenos::VertexFormat::k_32_32_32_32_FLOAT: {
                          float* p = reinterpret_cast<float*>(elem);
                          to_clip(p[0], p[1]);
                          // If w is 0, set to 1
                          if (p[3] == 0.0f) p[3] = 1.0f;
                          break;
                        }
                        default:
                          XELOGW("CPU viewport transform: Unhandled position format {}", 
                                 static_cast<uint32_t>(pos_fmt));
                          break;
                        }
                      }
                      
                      // Replace the vertex buffer with transformed data
                      vertex_buffer->release();
                      vertex_buffer = GetMetalDevice()->newBuffer(transformed_data, size, 
                                                                  MTL::ResourceStorageModeShared);
                      XELOGI("CPU viewport transform: Created new vertex buffer with transformed data");
                      
                      // Clean up temporary transformed data
                      std::free(transformed_data);
                    }
                  }
                } else {
                  XELOGI("CPU viewport transform: No POSITION attribute found in binding");
                }
              }
                // --- end DEBUG viewport transform ---
                
                // NOW bind the (possibly transformed) vertex buffer to the encoder
                encoder->setVertexBuffer(vertex_buffer, 0, metal_buffer_index);
                XELOGI("Metal IssueDraw: Bound vertex buffer at Metal indices {} and 0 (Xbox binding {}, {} bytes) from guest address 0x{:08X}", 
                       metal_buffer_index, binding.binding_index, size, address);
                
                // Log vertex attributes for debugging
                if (!binding.attributes.empty() && size >= binding.stride_words * 4) {
                  XELOGI("Metal IssueDraw: Vertex has {} attributes with stride {} bytes", 
                         binding.attributes.size(), binding.stride_words * 4);
                  
                  // Only log raw data if we have a reasonable stride
                  if (binding.stride_words > 0 && binding.stride_words < 64) {
                    const uint32_t* vertex_data_u32 = static_cast<const uint32_t*>(vertex_data);
                    // Log all dwords in the vertex (up to stride)
                    std::string hex_str;
                    for (uint32_t i = 0; i < binding.stride_words && i < 8; i++) {
                      if (i > 0) hex_str += " ";
                      hex_str += fmt::format("0x{:08X}", xe::byte_swap(vertex_data_u32[i]));
                    }
                    XELOGI("Metal IssueDraw: First vertex raw data (hex): {}", hex_str);
                    
                    // If we did endian swapping, show the swapped values as floats
                    if (vfetch_constant.endian != xenos::Endian::kNone && binding.attributes.size() > 0) {
                      const auto& first_attr = binding.attributes[0].fetch_instr;
                      if (first_attr.attributes.data_format == xenos::VertexFormat::k_32_32_FLOAT ||
                          first_attr.attributes.data_format == xenos::VertexFormat::k_32_32_32_FLOAT ||
                          first_attr.attributes.data_format == xenos::VertexFormat::k_32_32_32_32_FLOAT) {
                        // Show the data after endian swap as floats
                        std::string float_str;
                        for (uint32_t i = 0; i < binding.stride_words && i < 8; i++) {
                          if (i > 0) float_str += ", ";
                          uint32_t swapped = xenos::GpuSwap(vertex_data_u32[i], vfetch_constant.endian);
                          float val = *reinterpret_cast<float*>(&swapped);
                          float_str += fmt::format("{:.3f}", val);
                        }
                        XELOGI("Metal IssueDraw: After endian swap as floats: [{}]", float_str);
                      }
                    }
                  }
                }
                
                // Track for cleanup
                vertex_buffers_to_release.push_back(vertex_buffer);
                // Save first vertex buffer for T0/U0 binding
                if (!first_bound_vertex_buffer) {
                  first_bound_vertex_buffer = vertex_buffer;
                }
              } else {
                XELOGW("Metal IssueDraw: Failed to create vertex buffer for binding {}", 
                       binding.binding_index);
              }
            }
          }
          
          // Prepare textures and samplers for argument buffer
          // Use maps indexed by heap slot number to support sparse indices
          // IMPORTANT: Declare these outside the if block so they're accessible later
          std::unordered_map<uint32_t, MTL::Texture*> bound_textures_by_heap_slot;
          std::unordered_map<uint32_t, MTL::SamplerState*> bound_samplers_by_heap_slot;
          
          // Process textures from BOTH vertex and pixel shaders
          // Check vertex shader first
          if (vertex_shader && vertex_translation) {
            const auto& vs_heap_texture_indices = vertex_translation->GetTextureSlots();
            const auto& vs_heap_sampler_indices = vertex_translation->GetSamplerSlots();
            XELOGI("Metal IssueDraw: Vertex shader uses {} texture heap indices from reflection", vs_heap_texture_indices.size());
            XELOGI("Metal IssueDraw: Vertex shader uses {} sampler heap indices from reflection", vs_heap_sampler_indices.size());
            
            // Use the shader's texture bindings to get the actual fetch constants
            const auto& vs_texture_bindings = vertex_shader->texture_bindings();
            XELOGI("Metal IssueDraw: Vertex shader has {} texture bindings", vs_texture_bindings.size());
            
            // Process vertex shader textures
            size_t texture_binding_index = 0;
            for (size_t i = 0; i < vs_heap_texture_indices.size(); ++i) {
              uint32_t heap_slot = vs_heap_texture_indices[i];
              
              // Skip sentinel values (framebuffer fetch)
              if (heap_slot == 0xFFFF) {
                XELOGI("Metal IssueDraw: Skipping framebuffer fetch sentinel at index {}", i);
                continue;
              }
              
              // Get fetch constant for this texture
              uint32_t fetch_constant;
              if (texture_binding_index < vs_texture_bindings.size()) {
                const auto& binding = vs_texture_bindings[texture_binding_index];
                fetch_constant = binding.fetch_constant;
                texture_binding_index++;
              } else {
                // No texture bindings - try to find a valid texture by scanning fetch constants
                XELOGI("Metal IssueDraw: No texture bindings for VS, scanning fetch constants for heap slot {}", heap_slot);
                fetch_constant = 0xFFFF; // Invalid by default
                
                // Try common fetch constant indices (0-31)
                for (uint32_t fc = 0; fc < 32; fc++) {
                  xenos::xe_gpu_texture_fetch_t test_fetch = register_file_->GetTextureFetch(fc);
                  if (test_fetch.base_address) {
                    XELOGI("Metal IssueDraw: Found valid texture at fetch constant {} for VS heap slot {}", fc, heap_slot);
                    fetch_constant = fc;
                    break;
                  }
                }
                
                if (fetch_constant == 0xFFFF) {
                  XELOGW("Metal IssueDraw: No valid texture found for VS heap slot {}", heap_slot);
                  continue;
                }
              }
              
              // Get texture fetch constant
              xenos::xe_gpu_texture_fetch_t fetch = register_file_->GetTextureFetch(fetch_constant);
              if (!fetch.base_address) {
                XELOGW("Metal IssueDraw: VS texture fetch constant {} has no base address", fetch_constant);
                continue;
              }
              
              // Parse texture info
              TextureInfo texture_info;
              if (!TextureInfo::Prepare(fetch, &texture_info)) {
                XELOGW("Metal IssueDraw: Failed to prepare VS texture info for fetch constant {}", fetch_constant);
                continue;
              }
              
              // Get Metal texture based on dimension
              MTL::Texture* metal_texture = nullptr;
              if (texture_info.dimension == xenos::DataDimension::kCube) {
                // Upload cube texture first, then get it
                if (!texture_cache_->UploadTextureCube(texture_info)) {
                  XELOGW("Metal IssueDraw: Failed to upload VS cube texture for heap slot {}", heap_slot);
                  continue;
                }
                metal_texture = texture_cache_->GetTextureCube(texture_info);
              } else {
                // Upload 2D texture first, then get it (same as pixel shader path)
                if (!texture_cache_->UploadTexture2D(texture_info)) {
                  XELOGW("Metal IssueDraw: Failed to upload VS 2D texture for heap slot {}", heap_slot);
                  continue;
                }
                metal_texture = texture_cache_->GetTexture2D(texture_info);
              }
              
              if (metal_texture) {
                bound_textures_by_heap_slot[heap_slot] = metal_texture;
                XELOGI("Metal IssueDraw: Bound VS texture at heap slot {} for argument buffer", heap_slot);
              }
            }
            
            // Process vertex shader samplers - for now just skip
            // TODO: Implement proper sampler support
            XELOGI("Metal IssueDraw: Skipping VS sampler processing for now");
          }
          
          // Then check pixel shader (keeping existing code)
          if (pixel_shader && pixel_translation) {
            // Get heap indices from the Metal translation's reflection data
            const auto& heap_texture_indices = pixel_translation->GetTextureSlots();
            const auto& heap_sampler_indices = pixel_translation->GetSamplerSlots();
            XELOGI("Metal IssueDraw: Pixel shader uses {} texture heap indices from reflection", heap_texture_indices.size());
            XELOGI("Metal IssueDraw: Pixel shader uses {} sampler heap indices from reflection", heap_sampler_indices.size());
            
            // Use the shader's texture bindings to get the actual fetch constants
            const auto& texture_bindings = pixel_shader->texture_bindings();
            XELOGI("Metal IssueDraw: Pixel shader has {} texture bindings", texture_bindings.size());
            
            // Log all texture bindings for debugging
            for (size_t i = 0; i < texture_bindings.size(); ++i) {
              XELOGI("  Texture binding[{}]: fetch_constant={}", 
                     i, texture_bindings[i].fetch_constant);
            }
            
            // Process textures - map from binding index to heap slot
            // We need to match non-sentinel heap indices with texture bindings
            size_t texture_binding_index = 0;
            for (size_t i = 0; i < heap_texture_indices.size(); ++i) {
              uint32_t heap_slot = heap_texture_indices[i];
              
              // Skip sentinel values (framebuffer fetch)
              if (heap_slot == 0xFFFF) {
                XELOGI("Metal IssueDraw: Skipping framebuffer fetch sentinel at index {}", i);
                continue;
              }
              
              // Check if we have a texture binding for this non-sentinel heap slot
              uint32_t fetch_constant;
              if (texture_binding_index < texture_bindings.size()) {
                const auto& binding = texture_bindings[texture_binding_index];
                fetch_constant = binding.fetch_constant;
                texture_binding_index++; // Move to next texture binding for next non-sentinel heap slot
              } else {
                // No texture binding available, try various fallback strategies
                XELOGW("Metal IssueDraw: No texture binding for heap slot {}, trying fallbacks", heap_slot);
                
                // Strategy 1: Try using heap slot as fetch constant
                fetch_constant = heap_slot;
                if (fetch_constant >= 32) {
                  XELOGW("Metal IssueDraw: Heap slot {} is out of range for fetch constants", heap_slot);
                  fetch_constant = 0; // Fall through to try fetch constant 0
                }
                
                // Check if this fetch constant has valid data first
                xenos::xe_gpu_texture_fetch_t test_fetch = register_file_->GetTextureFetch(fetch_constant);
                if (!test_fetch.base_address) {
                  // No valid texture at this fetch constant
                  // DO NOT reuse textures - each slot needs its own texture or nothing
                  XELOGW("Metal IssueDraw: Fetch constant {} has no base address for heap slot {}, skipping", 
                         fetch_constant, heap_slot);
                  
                  // DEBUG: Comprehensive fetch constant scan to find any available textures
                  if (heap_slot == 0 || heap_slot == 1) {
                    XELOGI("===== COMPREHENSIVE FETCH CONSTANT SCAN FOR HEAP SLOT {} =====", heap_slot);
                    for (uint32_t scan_i = 0; scan_i < 32; scan_i++) {
                      auto scan_fetch = register_file_->GetTextureFetch(scan_i);
                      if (scan_fetch.base_address) {
                        XELOGI("Fetch constant {}: base_address=0x{:08X}, type={}, format={}, size={}x{}x{}", 
                               scan_i, scan_fetch.base_address, scan_fetch.type, scan_fetch.format,
                               scan_fetch.size_2d.width + 1, scan_fetch.size_2d.height + 1, 
                               scan_fetch.size_3d.depth + 1);
                      }
                    }
                    XELOGI("No texture found at fetch_constant={} for heap_slot={}", fetch_constant, heap_slot);
                    XELOGI("===== END FETCH CONSTANT SCAN =====");
                  }
                  
                  // Leave this heap slot empty - the shader will handle missing textures
                  continue;
                }
              }
              
              // Ensure fetch constant is valid
              if (fetch_constant >= 32) {
                XELOGW("Metal IssueDraw: Invalid fetch constant {} for texture binding {}", fetch_constant, i);
                continue;
              }
              
              // Get texture fetch constant for this slot
              xenos::xe_gpu_texture_fetch_t fetch = register_file_->GetTextureFetch(fetch_constant);
              
              // Skip if no texture address
              if (!fetch.base_address) {
                XELOGW("Metal IssueDraw: Texture fetch constant {} has no base address", fetch_constant);
                continue;
              }
              
              // Parse texture info from fetch constant
              TextureInfo texture_info;
              if (!TextureInfo::Prepare(fetch, &texture_info)) {
                XELOGW("Metal IssueDraw: Failed to prepare texture info for fetch constant {}", fetch_constant);
                continue;
              }
              
              // Xbox 360 stores dimensions as (actual_size - 1), so add 1 for display
              uint32_t actual_width = texture_info.width + 1;
              uint32_t actual_height = texture_info.height + 1;
              
              XELOGI("Metal IssueDraw: Texture heap slot {} (fetch {}) - {}x{} format {} @ 0x{:08X}",
                     heap_slot, fetch_constant, actual_width, actual_height,
                     static_cast<uint32_t>(texture_info.format_info()->format),
                     texture_info.memory.base_address);
              
              // Upload texture if not in cache
              if (texture_info.dimension == xenos::DataDimension::k2DOrStacked) {
                if (!texture_cache_->UploadTexture2D(texture_info)) {
                  XELOGW("Metal IssueDraw: Failed to upload 2D texture for heap slot {} (fetch {})", heap_slot, fetch_constant);
                  continue;
                }
                
                // Get Metal texture from cache
                MTL::Texture* metal_texture = texture_cache_->GetTexture2D(texture_info);
                if (metal_texture) {
                  // Store texture at its heap slot index
                  bound_textures_by_heap_slot[heap_slot] = metal_texture;
                  XELOGI("Metal IssueDraw: Got texture ptr 0x{:016X} at heap slot {} for argument buffer", 
                         reinterpret_cast<uintptr_t>(metal_texture), heap_slot);
                }
              } else if (texture_info.dimension == xenos::DataDimension::kCube) {
                if (!texture_cache_->UploadTextureCube(texture_info)) {
                  XELOGW("Metal IssueDraw: Failed to upload cube texture for heap slot {} (fetch {})", heap_slot, fetch_constant);
                  continue;
                }
                
                MTL::Texture* metal_texture = texture_cache_->GetTextureCube(texture_info);
                if (metal_texture) {
                  // Store texture at its heap slot index
                  bound_textures_by_heap_slot[heap_slot] = metal_texture;
                  XELOGI("Metal IssueDraw: Prepared cube texture at heap slot {} for argument buffer", heap_slot);
                }
              }
            }
            
            // Process samplers - use same texture bindings since Xbox 360 combines them
            // Match sampler heap indices with texture bindings
            for (size_t i = 0; i < heap_sampler_indices.size() && i < texture_bindings.size(); ++i) {
              uint32_t heap_slot = heap_sampler_indices[i];
              const auto& binding = texture_bindings[i];
              uint32_t fetch_constant = binding.fetch_constant;
              
              // Ensure fetch constant is valid
              if (fetch_constant >= 32) {
                XELOGW("Metal IssueDraw: Invalid fetch constant {} for sampler binding {}", fetch_constant, i);
                continue;
              }
              
              // Get sampler info from register
              xenos::xe_gpu_texture_fetch_t fetch = register_file_->GetTextureFetch(fetch_constant);
              
              // Create sampler state based on fetch settings
              MTL::SamplerDescriptor* sampler_desc = MTL::SamplerDescriptor::alloc()->init();
              sampler_desc->setMinFilter(MTL::SamplerMinMagFilterLinear);
              sampler_desc->setMagFilter(MTL::SamplerMinMagFilterLinear);
              sampler_desc->setSAddressMode(MTL::SamplerAddressModeRepeat);
              sampler_desc->setTAddressMode(MTL::SamplerAddressModeRepeat);
              sampler_desc->setSupportArgumentBuffers(true);  // Required for AB access
              MTL::SamplerState* sampler = GetMetalDevice()->newSamplerState(sampler_desc);
              sampler_desc->release();
              
              // Store sampler at its heap slot
              bound_samplers_by_heap_slot[heap_slot] = sampler;
              XELOGI("Metal IssueDraw: Prepared sampler at heap slot {} for argument buffer", heap_slot);
            }
            
            XELOGI("Metal IssueDraw: Prepared {} textures and {} samplers for argument buffer", 
                   bound_textures_by_heap_slot.size(), bound_samplers_by_heap_slot.size());
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
          
          // Phase D: Create and bind argument buffers with proper descriptor heaps
          // Metal shader converter expects:
          // [[buffer(2)]] - Top-level descriptor table
          // [[buffer(3)]] - Resource descriptor heap (textures)
          // [[buffer(4)]] - Sampler descriptor heap
          // [[buffer(5)]] - Draw arguments (handled by IRRuntime)
          // [[buffer(6)]] - Uniforms buffer (CBVs)
          {
            // First, create uniforms buffer with all constants
            const uint32_t* regs = register_file_->values;
            const int total_constants = 512;
            size_t uniforms_size = total_constants * 4 * sizeof(float);
            MTL::Buffer* uniforms_buffer = GetMetalDevice()->newBuffer(uniforms_size, MTL::ResourceStorageModeShared);
            TRACK_METAL_OBJECT(uniforms_buffer, "MTL::Buffer[Uniforms]");
            
            if (uniforms_buffer) {
              uniforms_buffer->setLabel(NS::String::string("UniformsBuffer", NS::UTF8StringEncoding));
              float* uniforms_data = (float*)uniforms_buffer->contents();
              
              // Copy constants - ALWAYS read from register file like D3D12/Vulkan do
              XELOGI("Metal IssueDraw: Reading VS constants from register file");
              for (int i = 0; i < 256; i++) {
                int base_reg = XE_GPU_REG_SHADER_CONSTANT_000_X + (i * 4);
                uniforms_data[i * 4 + 0] = *reinterpret_cast<const float*>(&regs[base_reg + 0]);
                uniforms_data[i * 4 + 1] = *reinterpret_cast<const float*>(&regs[base_reg + 1]);
                uniforms_data[i * 4 + 2] = *reinterpret_cast<const float*>(&regs[base_reg + 2]);
                uniforms_data[i * 4 + 3] = *reinterpret_cast<const float*>(&regs[base_reg + 3]);
              }
              
              // Debug: check if register file has non-zero values
              bool found_nonzero_in_regs = false;
              for (int i = 0; i < 8 && !found_nonzero_in_regs; i++) {
                int base_reg = XE_GPU_REG_SHADER_CONSTANT_000_X + (i * 4);
                for (int j = 0; j < 4; j++) {
                  if (regs[base_reg + j] != 0) {
                    found_nonzero_in_regs = true;
                    XELOGI("  Found non-zero in register file at c{}[{}]: 0x{:08X} = {:.3f}", 
                           i, j, regs[base_reg + j], 
                           *reinterpret_cast<const float*>(&regs[base_reg + j]));
                    break;
                  }
                }
              }
              
              // Copy PS constants - ALWAYS read from register file like D3D12/Vulkan do
              XELOGI("Metal IssueDraw: Reading PS constants from register file");
              for (int i = 256; i < 512; i++) {
                int base_reg = XE_GPU_REG_SHADER_CONSTANT_000_X + (i * 4);
                uniforms_data[i * 4 + 0] = *reinterpret_cast<const float*>(&regs[base_reg + 0]);
                uniforms_data[i * 4 + 1] = *reinterpret_cast<const float*>(&regs[base_reg + 1]);
                uniforms_data[i * 4 + 2] = *reinterpret_cast<const float*>(&regs[base_reg + 2]);
                uniforms_data[i * 4 + 3] = *reinterpret_cast<const float*>(&regs[base_reg + 3]);
              }
              
              // Check if all constants are zero
              int nonzero_count = 0;
              for (int i = 0; i < total_constants * 4; i++) {
                if (uniforms_data[i] != 0.0f) {
                  nonzero_count++;
                }
              }
              
              // CRITICAL FIX: ALWAYS inject system constants for viewport transformation
              // Xbox 360 shaders output screen space, we need to transform to Metal NDC
              // Use the properly calculated viewport_info values
              {
                XELOGI("Injecting NDC transformation constants into CB0");
                
                // Use the viewport_info we calculated with GetHostViewportInfo
                // These are the CORRECT values for Metal's coordinate system
                float ndc_scale_x = viewport_info.ndc_scale[0];
                float ndc_scale_y = viewport_info.ndc_scale[1];
                float ndc_scale_z = viewport_info.ndc_scale[2];
                float ndc_offset_x = viewport_info.ndc_offset[0];
                float ndc_offset_y = viewport_info.ndc_offset[1];
                float ndc_offset_z = viewport_info.ndc_offset[2];
                
                // System constants layout (matching D3D12/Vulkan approach)
                // IMPORTANT: These go in SYSTEM CONSTANT slots, not user constants
                // We reserve CB0[0-3] for system constants
                // CB0[0]: ndc_scale (x, y, z, unused)
                // CB0[1]: ndc_offset (x, y, z, unused)
                // CB0[2]: viewport info (width, height, 1/width, 1/height)
                
                // Write NDC transformation to the CORRECT offsets where shader expects them
                // The shader reads NDC constants from CB0 at specific byte offsets:
                // - Bytes 128-136: NDC scale (floats 32, 33, 34)
                // - Bytes 144-152: NDC offset (floats 36, 37, 38)
                uniforms_data[32] = ndc_scale_x;   // Byte 128
                uniforms_data[33] = ndc_scale_y;   // Byte 132
                uniforms_data[34] = ndc_scale_z;   // Byte 136
                uniforms_data[35] = 0.0f;          // Padding
                
                uniforms_data[36] = ndc_offset_x;  // Byte 144
                uniforms_data[37] = ndc_offset_y;  // Byte 148
                uniforms_data[38] = ndc_offset_z;  // Byte 152
                uniforms_data[39] = 0.0f;          // Padding
                
                // Keep viewport dimensions at original location for now
                uniforms_data[8] = float(rt_width);
                uniforms_data[9] = float(rt_height);
                uniforms_data[10] = 1.0f / float(rt_width);
                uniforms_data[11] = 1.0f / float(rt_height);
                
                XELOGI("Injected NDC transformation constants at CORRECT offsets:");
                XELOGI("  CB0[32-34] (bytes 128-136): ndc_scale = [{:.6f}, {:.6f}, {:.6f}]", 
                       uniforms_data[32], uniforms_data[33], uniforms_data[34]);
                XELOGI("  CB0[36-38] (bytes 144-152): ndc_offset = [{:.6f}, {:.6f}, {:.6f}]",
                       uniforms_data[36], uniforms_data[37], uniforms_data[38]);
                XELOGI("  CB0[2]: viewport_dims = [{:.0f}, {:.0f}, {:.6f}, {:.6f}]",
                       uniforms_data[8], uniforms_data[9], uniforms_data[10], uniforms_data[11]);
              }
              
              // Log if we found non-zero constants from the game
              if (nonzero_count > 0) {
                XELOGI("Found {} non-zero constants from game - preserved after system constants", nonzero_count);
                // Log ALL non-zero constants to understand what we're getting
                XELOGI("Non-zero constants from trace:");
                for (int i = 0; i < 512; i++) {
                  if (uniforms_data[i * 4] != 0.0f || uniforms_data[i * 4 + 1] != 0.0f ||
                      uniforms_data[i * 4 + 2] != 0.0f || uniforms_data[i * 4 + 3] != 0.0f) {
                    XELOGI("  C{}[{}]: [{:.6f}, {:.6f}, {:.6f}, {:.6f}]", 
                           i < 256 ? i : i - 256,
                           i < 256 ? "(VS)" : "(PS)",
                           uniforms_data[i*4], uniforms_data[i*4+1], 
                           uniforms_data[i*4+2], uniforms_data[i*4+3]);
                  }
                }
              }
              
              // Don't bind uniforms buffer directly - MSC accesses it through the argument buffer
              // encoder->setVertexBuffer(uniforms_buffer, 0, 6);  // REMOVED - incorrect
              // encoder->setFragmentBuffer(uniforms_buffer, 0, 6);  // REMOVED - incorrect
              XELOGI("Uniforms buffer will be accessed through argument buffer CBV entries");
            }
            
            // --- Populate and bind MSC descriptor heaps (arrays of IRDescriptorTableEntry) ---
            // CRITICAL: Write at the EXACT indices from reflection, not packed sequentially
            if (res_heap_ab_) {
              std::memset(res_heap_ab_->contents(), 0, res_heap_ab_->length());
              auto* res_entries = reinterpret_cast<IRDescriptorTableEntry*>(res_heap_ab_->contents());
              
              XELOGI("Metal IssueDraw: About to populate resource heap with {} textures at buffer {:p}", 
                     bound_textures_by_heap_slot.size(), (void*)res_heap_ab_);
              
              // Debug: Check if we actually have textures
              if (bound_textures_by_heap_slot.empty()) {
                XELOGW("Metal IssueDraw: bound_textures_by_heap_slot is EMPTY! No textures to populate.");
              }
              
              // Write textures at their exact heap indices from reflection
              for (const auto& [heap_slot, texture] : bound_textures_by_heap_slot) {
                if (texture && heap_slot < 64) { // Ensure within heap bounds
                  ::IRDescriptorTableSetTexture(&res_entries[heap_slot], texture, /*minLODClamp*/0.0f, /*flags*/0);
                  encoder->useResource(texture, MTL::ResourceUsageRead,
                                       MTL::RenderStageVertex | MTL::RenderStageFragment);
                  
                  // Debug: Verify descriptor was written
                  auto* desc_bytes = reinterpret_cast<uint8_t*>(&res_entries[heap_slot]);
                  auto resID = texture->gpuResourceID();
                  XELOGI("  MSC res-heap: setTexture 0x{:016X} at heap slot {} (gpuResID: impl=0x{:x})", 
                         reinterpret_cast<uintptr_t>(texture), heap_slot,
                         resID._impl);
                  XELOGI("    Descriptor bytes [0-23]: {:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x} {:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x} {:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
                         desc_bytes[0], desc_bytes[1], desc_bytes[2], desc_bytes[3],
                         desc_bytes[4], desc_bytes[5], desc_bytes[6], desc_bytes[7],
                         desc_bytes[8], desc_bytes[9], desc_bytes[10], desc_bytes[11],
                         desc_bytes[12], desc_bytes[13], desc_bytes[14], desc_bytes[15],
                         desc_bytes[16], desc_bytes[17], desc_bytes[18], desc_bytes[19],
                         desc_bytes[20], desc_bytes[21], desc_bytes[22], desc_bytes[23]);
                  
                  // Verify the descriptor was actually written
                  uint64_t* entry_data = reinterpret_cast<uint64_t*>(&res_entries[heap_slot]);
                  XELOGI("    Descriptor contents: gpuVA=0x{:016X}, textureViewID=0x{:016X}, metadata=0x{:016X}",
                         entry_data[0], entry_data[1], entry_data[2]);
                }
              }
              
              // Final validation: Check if heap has any valid entries
              bool has_any_valid = false;
              for (int i = 0; i < 32; i++) {
                if (res_entries[i].textureViewID != 0) {
                  has_any_valid = true;
                  XELOGI("  Heap slot {} has valid texture ID: 0x{:016X}", i, res_entries[i].textureViewID);
                }
              }
              if (!has_any_valid) {
                XELOGE("Metal IssueDraw: Resource heap has NO valid entries after population!");
              }
            }

            if (smp_heap_ab_) {
              std::memset(smp_heap_ab_->contents(), 0, smp_heap_ab_->length());
              auto* smp_entries = reinterpret_cast<IRDescriptorTableEntry*>(smp_heap_ab_->contents());
              
              // Write samplers at their exact heap indices from reflection
              for (const auto& [heap_slot, sampler] : bound_samplers_by_heap_slot) {
                if (sampler && heap_slot < 64) { // Ensure within heap bounds
                  ::IRDescriptorTableSetSampler(&smp_entries[heap_slot], sampler, /*lodBias*/0.0f);
                  
                  // Debug: Verify sampler descriptor was written
                  auto* smp_desc_bytes = reinterpret_cast<uint8_t*>(&smp_entries[heap_slot]);
                  XELOGI("  MSC smp-heap: setSampler at heap slot {} (sampler: 0x{:x})", 
                         heap_slot, reinterpret_cast<uintptr_t>(sampler));
                  XELOGI("    Sampler descriptor bytes [0-23]: {:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x} {:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x} {:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
                         smp_desc_bytes[0], smp_desc_bytes[1], smp_desc_bytes[2], smp_desc_bytes[3],
                         smp_desc_bytes[4], smp_desc_bytes[5], smp_desc_bytes[6], smp_desc_bytes[7],
                         smp_desc_bytes[8], smp_desc_bytes[9], smp_desc_bytes[10], smp_desc_bytes[11],
                         smp_desc_bytes[12], smp_desc_bytes[13], smp_desc_bytes[14], smp_desc_bytes[15],
                         smp_desc_bytes[16], smp_desc_bytes[17], smp_desc_bytes[18], smp_desc_bytes[19],
                         smp_desc_bytes[20], smp_desc_bytes[21], smp_desc_bytes[22], smp_desc_bytes[23]);
                }
              }
            }

            // Bind the populated heaps at the MSC-defined heap bind points for BOTH stages
            if (res_heap_ab_) {
              encoder->setVertexBuffer(res_heap_ab_, 0, (NS::UInteger)kIRDescriptorHeapBindPoint);
              encoder->setFragmentBuffer(res_heap_ab_, 0, (NS::UInteger)kIRDescriptorHeapBindPoint);
              encoder->useResource(res_heap_ab_, MTL::ResourceUsageRead,
                                   MTL::RenderStageVertex | MTL::RenderStageFragment);
            }
            if (smp_heap_ab_) {
              encoder->setVertexBuffer(smp_heap_ab_, 0, (NS::UInteger)kIRSamplerHeapBindPoint);
              encoder->setFragmentBuffer(smp_heap_ab_, 0, (NS::UInteger)kIRSamplerHeapBindPoint);
              encoder->useResource(smp_heap_ab_, MTL::ResourceUsageRead,
                                   MTL::RenderStageVertex | MTL::RenderStageFragment);
            }

            XELOGI("Metal IssueDraw: Bound MSC descriptor heaps (res/smp) at indices {}, {}",
                   (uint32_t)kIRDescriptorHeapBindPoint, (uint32_t)kIRSamplerHeapBindPoint);
            
            // --- Build & bind top-level argument buffers (MSC) ---
            // Use reflection-based layout for proper resource binding
            const size_t kEntry = sizeof(IRDescriptorTableEntry);
            const uint32_t kCBBytes = 256u * 4u * sizeof(float);
            
            // Build VS top-level AB from reflection layout
            MTL::Buffer* top_level_vs_ab = nullptr;
            if (vertex_translation) {
              XELOGI("Building VS top-level AB using reflection layout");
              const auto& vs_layout = vertex_translation->GetTopLevelABLayout();
              size_t vs_ab_size = kEntry * 4; // Default size if no layout
              if (!vs_layout.empty()) {
                // Get actual size from last entry
                vs_ab_size = vs_layout.back().elt_offset + kEntry;
              }
              
              top_level_vs_ab = GetMetalDevice()->newBuffer(vs_ab_size, MTL::ResourceStorageModeShared);
              top_level_vs_ab->setLabel(NS::String::string("IR_TopLevelAB_VS", NS::UTF8StringEncoding));
              auto* vs_entries = reinterpret_cast<uint8_t*>(top_level_vs_ab->contents());
              std::memset(vs_entries, 0, vs_ab_size);
              
              if (!vs_layout.empty()) {
                // Use reflection layout
                for (const auto& entry : vs_layout) {
                  auto* dst = reinterpret_cast<IRDescriptorTableEntry*>(vs_entries + entry.elt_offset);
                  
                  switch (entry.kind) {
                    case MetalShader::ABEntry::Kind::SRV:
                      // CRITICAL: Shaders expect DIRECT texture pointers, not heap pointers!
                      // VS typically uses T0/U0 for vertex data which we handle separately
                      // But if there are other texture slots, handle them here
                      {
                        MTL::Texture* texture_for_slot = nullptr;
                        for (const auto& [heap_slot, texture] : bound_textures_by_heap_slot) {
                          if (heap_slot == entry.slot) {
                            texture_for_slot = texture;
                            break;
                          }
                        }
                        if (texture_for_slot) {
                          ::IRDescriptorTableSetTexture(dst, texture_for_slot, /*minLODClamp*/0.0f, /*flags*/0);
                          XELOGI("VS top-level AB: Set texture directly at offset {} (slot {})",
                                 entry.elt_offset, entry.slot);
                        } else if (res_heap_ab_) {
                          // Fallback to heap pointer if no specific texture
                          ::IRDescriptorTableSetBuffer(dst, res_heap_ab_->gpuAddress(), 
                                                      (uint64_t)res_heap_ab_->length());
                          XELOGI("VS top-level AB: Set heap pointer at offset {} (no texture for slot {})",
                                 entry.elt_offset, entry.slot);
                        }
                      }
                      break;
                    case MetalShader::ABEntry::Kind::UAV:
                      // UAV heap (unused in most traces)
                      break;
                    case MetalShader::ABEntry::Kind::CBV:
                      // Constant buffer
                      if (uniforms_buffer) {
                        if (entry.slot == 0) {
                          ::IRDescriptorTableSetBuffer(dst, uniforms_buffer->gpuAddress(), kCBBytes);
                        } else if (entry.slot == 3) {
                          ::IRDescriptorTableSetBuffer(dst, uniforms_buffer->gpuAddress() + kCBBytes, kCBBytes);
                        }
                      }
                      break;
                    case MetalShader::ABEntry::Kind::Sampler:
                      // Point to sampler heap
                      if (smp_heap_ab_) {
                        ::IRDescriptorTableSetBuffer(dst, smp_heap_ab_->gpuAddress(),
                                                    (uint64_t)smp_heap_ab_->length());
                      }
                      break;
                    default:
                      break;
                  }
                }
                
                // Override T0/U0 with vertex buffer for programmatic vertex fetching
                if (first_bound_vertex_buffer) {
                  auto* vs_entries_typed = reinterpret_cast<IRDescriptorTableEntry*>(vs_entries);
                  // T0 is typically at index 0, U0 at index 1 in the top-level AB
                  ::IRDescriptorTableSetBuffer(&vs_entries_typed[0], 
                                              first_bound_vertex_buffer->gpuAddress(),
                                              (uint64_t)first_bound_vertex_buffer->length());
                  ::IRDescriptorTableSetBuffer(&vs_entries_typed[1], 
                                              first_bound_vertex_buffer->gpuAddress(),
                                              (uint64_t)first_bound_vertex_buffer->length());
                  XELOGI("Overrode T0/U0 with vertex buffer in VS reflection layout (addr: 0x{:x}, size: {})",
                         first_bound_vertex_buffer->gpuAddress(), first_bound_vertex_buffer->length());
                  
                  // Debug: Dump first few floats
                  if (first_bound_vertex_buffer->contents()) {
                    float* vb_data = reinterpret_cast<float*>(first_bound_vertex_buffer->contents());
                    XELOGI("  T0/U0 vertex data: [{:.6f}, {:.6f}, {:.6f}]", 
                           vb_data[0], vb_data[1], vb_data[2]);
                  }
                }
              } else {
                // Fallback to hardcoded layout
                XELOGI("Using hardcoded VS top-level AB layout (no reflection data)");
                // The shader expects vertex buffers as T0/U0 in the top-level AB
                auto* vs_entries_typed = reinterpret_cast<IRDescriptorTableEntry*>(vs_entries);
                
                // T0/U0: Bind vertex buffers as texture buffers
                if (first_bound_vertex_buffer) {
                  // T0: First vertex buffer
                  ::IRDescriptorTableSetBuffer(&vs_entries_typed[0], first_bound_vertex_buffer->gpuAddress(), 
                                              (uint64_t)first_bound_vertex_buffer->length());
                  // U0/T1: Same buffer for now (shaders use both T0 and U0 to access vertex data)
                  ::IRDescriptorTableSetBuffer(&vs_entries_typed[1], first_bound_vertex_buffer->gpuAddress(), 
                                              (uint64_t)first_bound_vertex_buffer->length());
                  XELOGI("Metal: Bound vertex buffer as T0/U0 in top-level AB (addr: 0x{:x}, size: {})", 
                         first_bound_vertex_buffer->gpuAddress(), first_bound_vertex_buffer->length());
                  
                  // Debug: Dump first few floats of vertex data to verify format
                  if (first_bound_vertex_buffer->contents()) {
                    float* vb_data = reinterpret_cast<float*>(first_bound_vertex_buffer->contents());
                    XELOGI("  T0/U0 first 3 floats: [{:.6f}, {:.6f}, {:.6f}]", 
                           vb_data[0], vb_data[1], vb_data[2]);
                  }
                } else if (res_heap_ab_) {
                  // Fallback to resource heap if no vertex buffer
                  XELOGI("Metal: No vertex buffer found, using resource heap for T0");
                  ::IRDescriptorTableSetBuffer(&vs_entries_typed[0], res_heap_ab_->gpuAddress(), 
                                              (uint64_t)res_heap_ab_->length());
                }
                
                if (uniforms_buffer) {
                  ::IRDescriptorTableSetBuffer(&vs_entries_typed[2], uniforms_buffer->gpuAddress(), kCBBytes);
                  ::IRDescriptorTableSetBuffer(&vs_entries_typed[3], uniforms_buffer->gpuAddress() + kCBBytes, kCBBytes);
                }
              }
            }
            
            // Build PS top-level AB from reflection layout
            MTL::Buffer* top_level_ps_ab = nullptr;
            if (pixel_translation) {
              const auto& ps_layout = pixel_translation->GetTopLevelABLayout();
              size_t ps_ab_size = kEntry; // Default size if no layout
              if (!ps_layout.empty()) {
                // Calculate total size needed for all entries
                size_t max_offset = 0;
                for (const auto& entry : ps_layout) {
                  max_offset = std::max(max_offset, static_cast<size_t>(entry.elt_offset + kEntry));
                }
                ps_ab_size = max_offset;
                XELOGI("PS top-level AB size: {} bytes for {} entries", ps_ab_size, ps_layout.size());
              }
              
              top_level_ps_ab = GetMetalDevice()->newBuffer(ps_ab_size, MTL::ResourceStorageModeShared);
              top_level_ps_ab->setLabel(NS::String::string("IR_TopLevelAB_PS", NS::UTF8StringEncoding));
              auto* ps_entries = reinterpret_cast<uint8_t*>(top_level_ps_ab->contents());
              std::memset(ps_entries, 0, ps_ab_size);
              
              if (!ps_layout.empty()) {
                // Use reflection layout - CRITICAL: Must populate ALL entries
                for (const auto& entry : ps_layout) {
                  auto* dst = reinterpret_cast<IRDescriptorTableEntry*>(ps_entries + entry.elt_offset);
                  
                  XELOGI("Metal IssueDraw: PS AB entry '{}' at offset {} (slot {}, kind {})",
                         entry.name, entry.elt_offset, entry.slot,
                         entry.kind == MetalShader::ABEntry::Kind::SRV ? "SRV" :
                         entry.kind == MetalShader::ABEntry::Kind::Sampler ? "Sampler" :
                         entry.kind == MetalShader::ABEntry::Kind::CBV ? "CBV" : "Other");
                  
                  switch (entry.kind) {
                    case MetalShader::ABEntry::Kind::SRV:
                      // CRITICAL: Shaders expect DIRECT texture pointers, not heap pointers!
                      // Find the texture for this slot and write it directly
                      {
                        // Log all available textures for debugging
                        XELOGI("Metal IssueDraw: Looking for PS texture at slot {} (name: {}). Available textures:", 
                               entry.slot, entry.name);
                        for (const auto& [heap_slot, texture] : bound_textures_by_heap_slot) {
                          XELOGI("  - Heap slot {}: texture 0x{:x}", heap_slot, (uintptr_t)texture);
                        }
                        
                        MTL::Texture* texture_for_slot = nullptr;
                        
                        // Map shader texture names to heap slots
                        // T0 (typically at shader slot 1) -> heap slot 0
                        // T1 (typically at shader slot 2) -> heap slot 1
                        // This handles the off-by-one difference between shader reflection and heap indices
                        
                        if (entry.name == "T0") {
                          // T0 should come from heap slot 0
                          auto it = bound_textures_by_heap_slot.find(0);
                          if (it != bound_textures_by_heap_slot.end()) {
                            texture_for_slot = it->second;
                            XELOGI("Metal IssueDraw: Mapped T0 to heap slot 0 texture");
                          }
                        } else if (entry.name == "T1") {
                          // T1 should come from heap slot 1
                          auto it = bound_textures_by_heap_slot.find(1);
                          if (it != bound_textures_by_heap_slot.end()) {
                            texture_for_slot = it->second;
                            XELOGI("Metal IssueDraw: Mapped T1 to heap slot 1 texture");
                          } else {
                            // Use the same texture as T0 since game only provides one texture
                            it = bound_textures_by_heap_slot.find(0);
                            if (it != bound_textures_by_heap_slot.end()) {
                              texture_for_slot = it->second;
                              XELOGI("Metal IssueDraw: No texture at heap slot 1 for T1, using same texture as T0 (heap slot 0)");
                            } else {
                              // Create a debug texture if even T0 is missing
                              XELOGI("Metal IssueDraw: No textures available, creating debug texture for T1");
                              texture_for_slot = texture_cache_->CreateDebugTexture(256, 256);
                              if (texture_for_slot) {
                                XELOGI("Metal IssueDraw: Using debug checkerboard texture for T1");
                                // Store it so we can use it consistently
                                bound_textures_by_heap_slot[1] = texture_for_slot;
                              }
                            }
                          }
                        } else if (entry.name.size() > 0 && entry.name[0] == 'T') {
                          // Generic texture mapping - extract number from name
                          try {
                            int tex_num = std::stoi(entry.name.substr(1));
                            auto it = bound_textures_by_heap_slot.find(tex_num);
                            if (it != bound_textures_by_heap_slot.end()) {
                              texture_for_slot = it->second;
                              XELOGI("Metal IssueDraw: Mapped {} to heap slot {}", entry.name, tex_num);
                            }
                          } catch (...) {
                            XELOGW("Metal IssueDraw: Failed to parse texture number from {}", entry.name);
                          }
                        }
                        
                        // Fallback to first available texture if nothing matched
                        if (!texture_for_slot && !bound_textures_by_heap_slot.empty()) {
                          texture_for_slot = bound_textures_by_heap_slot.begin()->second;
                          XELOGI("Metal IssueDraw: WARNING - No mapping for {}, using first texture as fallback", 
                                 entry.name);
                        }
                        
                        if (texture_for_slot) {
                          ::IRDescriptorTableSetTexture(dst, texture_for_slot, /*minLODClamp*/0.0f, /*flags*/0);
                          // Mark texture as resident for indirect access through argument buffer
                          encoder->useResource(texture_for_slot, MTL::ResourceUsageRead, MTL::RenderStageFragment);
                          
                          // Debug: Verify texture properties
                          auto resID = texture_for_slot->gpuResourceID();
                          XELOGI("Metal IssueDraw: Set PS texture {} at offset {} (slot {}), gpuResourceID=0x{:x}, size={}x{}", 
                                 entry.name, entry.elt_offset, entry.slot, resID._impl,
                                 texture_for_slot->width(), texture_for_slot->height());
                        } else {
                          XELOGE("ERROR - No texture available for PS {} at slot {} - THIS WILL CAUSE PINK OUTPUT!", entry.name, entry.slot);
                        }
                      }
                      break;
                    case MetalShader::ABEntry::Kind::UAV:
                      // UAV heap (rarely used in PS)
                      break;
                    case MetalShader::ABEntry::Kind::CBV:
                      // Constant buffer - PS CB0 is typically after VS CB0
                      if (uniforms_buffer) {
                        ::IRDescriptorTableSetBuffer(dst, uniforms_buffer->gpuAddress() + kCBBytes, kCBBytes);
                        XELOGI("Metal IssueDraw: Set PS CBV at offset {}", entry.elt_offset);
                      }
                      break;
                    case MetalShader::ABEntry::Kind::Sampler:
                      // CRITICAL: Shaders expect DIRECT sampler pointers, not heap pointers!
                      // Find the sampler for this slot and write it directly
                      {
                        XELOGI("Metal IssueDraw: Looking for PS sampler at slot {} (name: {})", 
                               entry.slot, entry.name);
                        
                        MTL::SamplerState* sampler_for_slot = nullptr;
                        
                        // Map shader sampler names to heap slots
                        // S0 (typically at shader slot 0) -> heap slot 4 or 5 (common sampler slots)
                        // S1 -> heap slot 5 or 6
                        
                        if (entry.name == "S0") {
                          // Try common sampler heap slots (4, 5)
                          auto it = bound_samplers_by_heap_slot.find(4);
                          if (it != bound_samplers_by_heap_slot.end()) {
                            sampler_for_slot = it->second;
                            XELOGI("Metal IssueDraw: Mapped S0 to heap slot 4 sampler");
                          } else {
                            it = bound_samplers_by_heap_slot.find(5);
                            if (it != bound_samplers_by_heap_slot.end()) {
                              sampler_for_slot = it->second;
                              XELOGI("Metal IssueDraw: Mapped S0 to heap slot 5 sampler");
                            }
                          }
                        } else if (entry.name.size() > 0 && entry.name[0] == 'S') {
                          // Generic sampler mapping - try to find any available sampler
                          if (!bound_samplers_by_heap_slot.empty()) {
                            sampler_for_slot = bound_samplers_by_heap_slot.begin()->second;
                            XELOGI("Metal IssueDraw: Using first available sampler for {}", entry.name);
                          }
                        }
                        
                        // Fallback: use first available sampler
                        if (!sampler_for_slot && !bound_samplers_by_heap_slot.empty()) {
                          sampler_for_slot = bound_samplers_by_heap_slot.begin()->second;
                          XELOGI("Metal IssueDraw: WARNING - No mapping for {}, using first sampler as fallback", 
                                 entry.name);
                        }
                        
                        if (sampler_for_slot) {
                          ::IRDescriptorTableSetSampler(dst, sampler_for_slot, /*lodBias*/0.0f);
                          // Note: Samplers don't need useResource as they're not MTLResource objects
                          XELOGI("Metal IssueDraw: Set PS sampler {} at offset {} (slot {})", 
                                 entry.name, entry.elt_offset, entry.slot);
                        } else {
                          XELOGW("ERROR - No sampler available for PS {} at slot {}", entry.name, entry.slot);
                        }
                      }
                      break;
                    default:
                      break;
                  }
                }
              } else {
                // Fallback to hardcoded layout
                auto* ps_entries_typed = reinterpret_cast<IRDescriptorTableEntry*>(ps_entries);
                if (uniforms_buffer) {
                  ::IRDescriptorTableSetBuffer(&ps_entries_typed[0], uniforms_buffer->gpuAddress() + kCBBytes, kCBBytes);
                }
              }
            }

            // Bind the top-level ABs at the MSC bind point [[buffer(2)]] for VS and PS
            if (top_level_vs_ab) {
              encoder->setVertexBuffer(top_level_vs_ab, 0, (NS::UInteger)kIRArgumentBufferBindPoint);
              encoder->useResource(top_level_vs_ab, MTL::ResourceUsageRead,
                                   MTL::RenderStageVertex | MTL::RenderStageFragment);
            }
            if (top_level_ps_ab) {
              encoder->setFragmentBuffer(top_level_ps_ab, 0, (NS::UInteger)kIRArgumentBufferBindPoint);
              encoder->useResource(top_level_ps_ab, MTL::ResourceUsageRead,
                                   MTL::RenderStageVertex | MTL::RenderStageFragment);
            }

            XELOGI("Metal IssueDraw: Bound MSC top-level ABs at buffer[{}] (VS slots: 4, PS slots: 1)",
                   (uint32_t)kIRArgumentBufferBindPoint);
            
            // Create MSC DrawArguments and Uniforms buffers
            static MTL::Buffer* ir_draw_args_buffer = nullptr;
            static MTL::Buffer* ir_uniforms_buffer = nullptr;
            
            if (!ir_draw_args_buffer) {
              ir_draw_args_buffer = GetMetalDevice()->newBuffer(256, MTL::ResourceStorageModeShared);
              TRACK_METAL_OBJECT(ir_draw_args_buffer, "MTL::Buffer[IR_DrawArguments]");
              ir_draw_args_buffer->setLabel(NS::String::string("IR_DrawArguments", NS::UTF8StringEncoding));
            }
            if (!ir_uniforms_buffer) {
              // Need at least 160 bytes to hold NDC constants at offsets 128-152 (floats 32-38)
              // Use 512 bytes for safety and alignment
              ir_uniforms_buffer = GetMetalDevice()->newBuffer(512, MTL::ResourceStorageModeShared);
              TRACK_METAL_OBJECT(ir_uniforms_buffer, "MTL::Buffer[IR_Uniforms]");
              ir_uniforms_buffer->setLabel(NS::String::string("IR_Uniforms", NS::UTF8StringEncoding));
            }
            
            // Zero-init MSC runtime buffers (safe defaults)
            std::memset(ir_draw_args_buffer->contents(), 0, ir_draw_args_buffer->length());
            std::memset(ir_uniforms_buffer->contents(), 0, ir_uniforms_buffer->length());
            
            // Copy NDC constants from uniforms_buffer to ir_uniforms_buffer
            // The shader expects them at bytes 128-152 (floats 32-38)
            if (uniforms_buffer) {
              float* src_uniforms = (float*)uniforms_buffer->contents();
              float* dst_uniforms = (float*)ir_uniforms_buffer->contents();
              
              // Copy NDC scale (floats 32-34)
              dst_uniforms[32] = src_uniforms[32];  // ndc_scale_x
              dst_uniforms[33] = src_uniforms[33];  // ndc_scale_y
              dst_uniforms[34] = src_uniforms[34];  // ndc_scale_z
              dst_uniforms[35] = 0.0f;              // padding
              
              // Copy NDC offset (floats 36-38)
              dst_uniforms[36] = src_uniforms[36];  // ndc_offset_x
              dst_uniforms[37] = src_uniforms[37];  // ndc_offset_y
              dst_uniforms[38] = src_uniforms[38];  // ndc_offset_z
              dst_uniforms[39] = 0.0f;              // padding
              
              XELOGI("Copied NDC constants to ir_uniforms_buffer");
              XELOGI("  NDC scale: [{}, {}, {}]", dst_uniforms[32], dst_uniforms[33], dst_uniforms[34]);
              XELOGI("  NDC offset: [{}, {}, {}]", dst_uniforms[36], dst_uniforms[37], dst_uniforms[38]);
            }
            
            // Bind MSC runtime buffers at their expected indices
            encoder->setVertexBuffer(ir_draw_args_buffer, 0, (NS::UInteger)kIRArgumentBufferDrawArgumentsBindPoint);
            encoder->setFragmentBuffer(ir_draw_args_buffer, 0, (NS::UInteger)kIRArgumentBufferDrawArgumentsBindPoint);
            encoder->setVertexBuffer(ir_uniforms_buffer, 0, (NS::UInteger)kIRArgumentBufferUniformsBindPoint);
            encoder->setFragmentBuffer(ir_uniforms_buffer, 0, (NS::UInteger)kIRArgumentBufferUniformsBindPoint);
            
            // Mark MSC runtime buffers as resident
            encoder->useResource(ir_draw_args_buffer, MTL::ResourceUsageRead,
                               MTL::RenderStageVertex | MTL::RenderStageFragment);
            encoder->useResource(ir_uniforms_buffer, MTL::ResourceUsageRead,
                               MTL::RenderStageVertex | MTL::RenderStageFragment);
            
            XELOGI("Metal IssueDraw: Bound MSC runtime buffers (DrawArgs and Uniforms)");
            
            // Old duplicate code removed - top-level ABs are created above
            
            // CRITICAL: Mark ALL resources as resident for indirect access
            // This is required by Metal Shader Converter - without this, textures show as black
            if (uniforms_buffer) {
              encoder->useResource(uniforms_buffer, MTL::ResourceUsageRead, 
                                 MTL::RenderStageVertex | MTL::RenderStageFragment);
              XELOGI("Marked uniforms buffer as resident");
            }
            
            // Mark all textures as resident
            for (const auto& [heap_slot, texture] : bound_textures_by_heap_slot) {
              if (texture) {
                encoder->useResource(texture, MTL::ResourceUsageRead, 
                                   MTL::RenderStageVertex | MTL::RenderStageFragment);
                XELOGI("Marked texture at heap slot {} as resident", heap_slot);
              }
            }
            
            // Note: Samplers don't need useResource calls - they're not MTL::Resource objects
            // Only textures and buffers need to be marked as resident
            XELOGI("Created {} samplers (samplers don't need residency marking)", bound_samplers_by_heap_slot.size());
            
            // Mark the descriptor heap buffers themselves as resident
            if (res_heap_ab_) {
              encoder->useResource(res_heap_ab_, MTL::ResourceUsageRead,
                                 MTL::RenderStageVertex | MTL::RenderStageFragment);
              XELOGI("Marked resource heap argument buffer as resident");
            }
            if (smp_heap_ab_) {
              encoder->useResource(smp_heap_ab_, MTL::ResourceUsageRead,
                                 MTL::RenderStageVertex | MTL::RenderStageFragment);
              XELOGI("Marked sampler heap argument buffer as resident");
            }
            
            // Clean up samplers after marking as resident
            for (const auto& [heap_slot, sampler] : bound_samplers_by_heap_slot) {
              if (sampler) {
                sampler->release();
              }
            }
            
            // Clean up top-level argument buffers
            if (top_level_vs_ab) {
              TRACK_METAL_RELEASE(top_level_vs_ab);
              top_level_vs_ab->release();
            }
            if (top_level_ps_ab) {
              TRACK_METAL_RELEASE(top_level_ps_ab);
              top_level_ps_ab->release();
            }
            
            // Note: We don't release res_heap_ab_ and smp_heap_ab_ here as they're reused across draws
            
            // Note: buffers 0-1 are not used in our configuration
            // buffer 5 is handled by IRRuntimeDrawPrimitives/IRRuntimeDrawIndexedPrimitives
          }          // Phase E: Draw parameters are now handled by IRRuntimeDrawPrimitives/IRRuntimeDrawIndexedPrimitives
          // which automatically bind the correct structures at indices 4 (draw args) and 5 (uniforms)
            
          // Encode the actual draw call
          // Convert processed primitive type to Metal primitive type
          MTL::PrimitiveType metal_prim_type = MTL::PrimitiveTypeTriangle; // Default
          MTL::IndexType draw_index_type = MTL::IndexTypeUInt16; // Default for reuse in debug draw
          MTL::Buffer* draw_index_buffer = nullptr; // For reuse in debug draw
          uint32_t draw_index_count = 0; // For reuse in debug draw
          
          // If we're testing NDC transformation, just draw a simple triangle
          if (test_ndc_transform) {
            XELOGI("Metal IssueDraw: Drawing NDC test triangle (3 vertices, non-indexed)");
            ::IRRuntimeDrawPrimitives(encoder, MTL::PrimitiveTypeTriangle, 
                                     uint64_t(0),  // vertexStart
                                     uint64_t(3));  // vertexCount - just a triangle
            
            // Skip the normal draw path
            goto after_normal_draw;
          }
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
              
              draw_index_type = 
                  primitive_processing_result.host_index_format == xenos::IndexFormat::kInt32
                      ? MTL::IndexTypeUInt32 
                      : MTL::IndexTypeUInt16;
              draw_index_buffer = index_buffer;
              
              // NOTE: drawIndexedPrimitives takes (primitiveType, indexCount, indexType, indexBuffer, indexBufferOffset)
              draw_index_count = primitive_processing_result.host_draw_vertex_count;
              XELOGI("Metal IssueDraw: Drawing {} indices as {}", draw_index_count, 
                     primitive_processing_result.host_primitive_type == xenos::PrimitiveType::kTriangleList ? "triangles" : "other");
              
              // Use IRRuntime helper to bind draw params at indices 4-5
              IRRuntimeDrawIndexedPrimitives(
                  encoder,
                  metal_prim_type, 
                  NS::UInteger(draw_index_count),
                  draw_index_type,
                  draw_index_buffer,
                  NS::UInteger(0));  // offset
                  
              XELOGI("Metal IssueDraw: Drew indexed primitives with converted buffer - {} indices", draw_index_count);
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
                  
                  // Draw with guest DMA index buffer using MSC runtime helper
                  ::IRRuntimeDrawIndexedPrimitives(
                      encoder,
                      metal_prim_type, 
                      primitive_processing_result.host_draw_vertex_count,  // indexCount
                      index_type,
                      guest_index_buffer,
                      0);  // indexBufferOffset
                  
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
            // Use MSC runtime helper to set draw params and issue draw
            ::IRRuntimeDrawPrimitives(encoder, metal_prim_type, 
                                     uint64_t(0),  // vertexStart
                                     uint64_t(primitive_processing_result.host_draw_vertex_count));  // vertexCount
          }
          
          XELOGI("Metal IssueDraw: Encoded draw primitives call ({} vertices, primitive type {})", 
                 index_count, static_cast<uint32_t>(metal_prim_type));
          
after_normal_draw:
          // DEBUG TEST: Draw with real VS + green PS to test if PS is running
          // DISABLED: This test was preventing real textures from showing
          if (false) {
            auto* green_ps_pipeline = pipeline_cache_->GetRenderPipelineStateWithGreenPS(pipeline_desc);
            if (green_ps_pipeline) {
              XELOGI("Metal IssueDraw: Testing real VS + green PS (no texture dependencies)");
              encoder->setRenderPipelineState(green_ps_pipeline);
              
              // Simply repeat the same draw with green PS
              if (primitive_processing_result.index_buffer_type != PrimitiveProcessor::ProcessedIndexBufferType::kNone 
                  && draw_index_buffer != nullptr) {
              // Indexed draw
              ::IRRuntimeDrawIndexedPrimitives(
                  encoder,
                  metal_prim_type,
                  draw_index_count,
                  draw_index_type,
                  draw_index_buffer,
                  0);
              XELOGI("Metal IssueDraw: Drew real VS + green PS indexed ({} indices)", draw_index_count);
            } else {
              // Non-indexed draw
              ::IRRuntimeDrawPrimitives(encoder, metal_prim_type, 
                                       uint64_t(0),
                                       uint64_t(primitive_processing_result.host_draw_vertex_count));
              XELOGI("Metal IssueDraw: Drew real VS + green PS non-indexed ({} vertices)", 
                     primitive_processing_result.host_draw_vertex_count);
            }
            
            green_ps_pipeline->release();
            }
          }
          
          
          
          
          // DEBUG: Add a red triangle overlay to test if the pipeline works
          /*
          if (debug_red_pipeline_) {
            XELOGI("Metal IssueDraw: Adding debug red triangle overlay");
            encoder->setRenderPipelineState(debug_red_pipeline_);
            // Draw fallback triangle using IRRuntime helper
            IRRuntimeDrawPrimitives(encoder, MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
            XELOGI("Metal IssueDraw: Debug red triangle drawn");
          }
          */
          
          // Clean up buffers
          for (auto* buffer : vertex_buffers_to_release) {
            TRACK_METAL_RELEASE(buffer);
            buffer->release();
          }
          // No manual cleanup needed - IRRuntimeDraw* handles draw params automatically
          // CB0 buffer will be released when it goes out of scope
          
          // Pop debug group
          encoder->popDebugGroup();
          
          // DON'T end the encoder here - we'll reuse it for the next draw!
          // The encoder will be ended when:
          // 1. A new render pass is needed (different render targets)
          // 2. The command buffer is being committed
          // 3. ShutdownContext is called
          XELOGI("Metal IssueDraw: Keeping encoder open for potential reuse");
          
          XELOGI("Metal IssueDraw: Metal frame debugging - Command buffer '{}' ready for capture", draw_label);
        }
        
        // Batch draws to avoid hitting Metal's 64 in-flight command buffer limit
        // Only commit after every N draws or when explicitly needed
        draws_in_current_batch_++;
        
        // REMOVED: Mid-frame capture was causing command buffer issues
        // Capture will now happen at end of frame in trace dump
        // This allows all draws to accumulate in one command buffer
        
        // Add debug validation and labeling
        if (debug_utils_) {
          debug_utils_->ValidateDrawState(draws_in_current_batch_, register_file_);
          debug_utils_->ValidateDescriptorHeap(res_heap_ab_, "Resource");
          debug_utils_->ValidateDescriptorHeap(smp_heap_ab_, "Sampler");
          debug_utils_->LabelCommandEncoder(encoder, 
                                           draws_in_current_batch_, 0);
        }
        
        // Batch multiple draws in one command buffer for better GPU capture
        // For trace dumps, don't commit until explicitly requested to allow GPU capture
        // Note: trace dumps have a MetalTraceDumpPresenter, so check the actual type
        bool is_trace_dump = true;  // Always treat as trace dump for now to ensure commits
        
        if (is_trace_dump) {
          // In trace dump mode, accumulate all draws in a single command buffer
          // They will be committed at the end of the trace playback
          XELOGI("Metal IssueDraw: Trace dump mode - accumulated {} draws", draws_in_current_batch_);
        } else {
          // Normal mode: batch 20 draws together
          if (draws_in_current_batch_ >= 20) {  // Batch 20 draws together
            XELOGI("Metal IssueDraw: Committing command buffer after {} draws", draws_in_current_batch_);
            EndSubmission(false);
            draws_in_current_batch_ = 0;
            // Start a new submission for the next batch of draws
            BeginSubmission(true);
          } else {
            XELOGI("Metal IssueDraw: Draw {} added to batch", draws_in_current_batch_);
          }
        }
        
        // Cleanup render pass
        if (render_pass) {
          TRACK_METAL_RELEASE(render_pass);
          // We retained the render pass from the cache or created it ourselves
          // Either way, we need to release it
          render_pass->release();
        }
      }
    }
  } else {
    XELOGW("Metal IssueDraw: Failed to create pipeline state");
  }

  // Store render target contents back to EDRAM buffer after draw completion
  // DISABLED FOR TRACE DUMPS: EDRAM emulation not needed, causes validation errors
  // TODO: Re-enable when proper render target dimensions are calculated
  // render_target_cache_->StoreRenderTargetsToEDRAM(current_command_buffer_);

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
  XE_SCOPED_AUTORELEASE_POOL("BeginSubmission");
  
  XELOGI("Metal BeginSubmission called: is_guest_command={}, submission_open={}, current_command_buffer_={}",
         is_guest_command, submission_open_, (void*)current_command_buffer_);
  
  // Start GPU capture for first submission in trace dump mode
  static bool first_submission = true;
  bool is_trace_dump = debug_utils_ && debug_utils_->IsCaptureEnabled();
  XELOGI("GPU capture check: first={}, debug_utils={}, is_trace_dump={}", 
         first_submission, (bool)debug_utils_, is_trace_dump);
  if (is_trace_dump && first_submission) {
    debug_utils_->BeginProgrammaticCapture();
    first_submission = false;
    XELOGI("Started programmatic GPU capture for trace dump");
  }
  
  if (submission_open_) {
    XELOGI("Metal BeginSubmission: Already open, returning existing command buffer {}", 
           (void*)current_command_buffer_);
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
  
  XELOGI("Metal BeginSubmission: Created new command buffer {}", (void*)current_command_buffer_);
  
  // Retain the command buffer to keep it alive (commandBuffer returns autoreleased object)
  current_command_buffer_->retain();
  XELOGI("Metal BeginSubmission: Retained command buffer");
  
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
  XE_SCOPED_AUTORELEASE_POOL("EndSubmission");
  
  if (!submission_open_) {
    return true;
  }
  
  // End any active render encoder before committing
  if (current_render_encoder_) {
    XELOGI("Metal EndSubmission: Ending render encoder before commit");
    current_render_encoder_->endEncoding();
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }
  
  // Release render pass reference
  if (current_render_pass_) {
    current_render_pass_->release();
    current_render_pass_ = nullptr;
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
    XELOGI("Metal EndSubmission: About to commit command buffer");
#if 0 // DISABLED - completion handler causing hang
    // Add completion handler for debugging
    XELOGI("Metal EndSubmission: Adding completion handler");
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
    XELOGI("Metal EndSubmission: Completion handler added");
#endif
    
    XELOGI("Metal EndSubmission: About to commit command buffer {}", (void*)current_command_buffer_);
    current_command_buffer_->commit();
    XELOGI("Metal EndSubmission: Command buffer {} committed successfully", (void*)current_command_buffer_);
    
    // For trace dumps, wait synchronously to ensure completion
    // This prevents crashes from async completion handlers after shutdown
    if (!graphics_system_->presenter()) {
      XELOGI("Metal EndSubmission: No presenter (trace dump mode) - waiting for completion");
      current_command_buffer_->waitUntilCompleted();
      XELOGI("Metal EndSubmission: Command buffer completed");
    }
    
    // Track command buffer release before nulling
    XELOGI("Metal EndSubmission: Releasing command buffer {}", (void*)current_command_buffer_);
    TRACK_METAL_RELEASE(current_command_buffer_);
    // Release our retained reference
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
    XELOGI("Metal EndSubmission: Command buffer released and nulled");
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
  XELOGI("Metal OnPrimaryBufferEnd: Called, submission_open_={}, draws_in_batch={}", 
         submission_open_, draws_in_current_batch_);
         
  // For trace dumps, only submit if we have accumulated draws
  bool is_trace_dump = !graphics_system_->presenter();
  
  if (is_trace_dump) {
    // In trace dump mode, only commit if we have draws to preserve for GPU capture
    if (submission_open_ && draws_in_current_batch_ > 0) {
      XELOGI("Metal OnPrimaryBufferEnd: Trace dump mode - Submitting {} accumulated draws", 
             draws_in_current_batch_);
      EndSubmission(false);
      draws_in_current_batch_ = 0;
    } else {
      XELOGI("Metal OnPrimaryBufferEnd: Trace dump mode - No draws to submit");
    }
  } else {
    // Normal mode: Submit on primary buffer end to reduce latency
    if (submission_open_ && CanEndSubmissionImmediately()) {
      XELOGI("Metal OnPrimaryBufferEnd: Submitting command buffer");
      EndSubmission(false);
    } else if (submission_open_) {
      XELOGI("Metal OnPrimaryBufferEnd: Submission open but cannot end immediately");
    } else {
      XELOGI("Metal OnPrimaryBufferEnd: No submission open to end");
    }
  }
}

void MetalCommandProcessor::WaitForIdle() {
  // First call base implementation to wait for command processing to complete
  CommandProcessor::WaitForIdle();
  
  // End any active render encoder before flushing
  if (current_render_encoder_) {
    XELOGI("Metal WaitForIdle: Ending active render encoder");
    current_render_encoder_->endEncoding();
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }
  
  // Release render pass reference
  if (current_render_pass_) {
    current_render_pass_->release();
    current_render_pass_ = nullptr;
  }
  
  // Then flush any pending Metal submissions
  if (submission_open_) {
    XELOGI("Metal WaitForIdle: Flushing pending submission with {} draws", draws_in_current_batch_);
    EndSubmission(false);
    draws_in_current_batch_ = 0;
  }
}

void MetalCommandProcessor::CommitPendingCommandBuffer() {
  SCOPE_profile_cpu_f("gpu");
  XELOGI("MetalCommandProcessor::CommitPendingCommandBuffer called");
  
  // End any active render encoder before committing
  if (current_render_encoder_) {
    XELOGI("MetalCommandProcessor: Ending active render encoder before commit");
    current_render_encoder_->endEncoding();
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }
  
  // Release render pass reference
  if (current_render_pass_) {
    current_render_pass_->release();
    current_render_pass_ = nullptr;
  }
  
  // Commit the current command buffer if there is one
  if (current_command_buffer_) {
    XELOGI("MetalCommandProcessor: Committing current command buffer");
    current_command_buffer_->commit();
    current_command_buffer_->waitUntilCompleted();
    
    // Release and clear
    TRACK_METAL_RELEASE(current_command_buffer_);
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
    submission_open_ = false;
  } else {
    XELOGI("MetalCommandProcessor: No pending command buffer to commit");
  }
  
  XELOGI("MetalCommandProcessor::CommitPendingCommandBuffer completed");
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
