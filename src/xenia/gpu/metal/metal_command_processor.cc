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
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <unordered_set>
#include <sstream>
#include <utility>
#include <vector>

#include "third_party/metal-cpp/Foundation/NSProcessInfo.hpp"

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/xxhash.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/metal/metal_shader_converter.h"
#include "xenia/gpu/metal/metal_graphics_system.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/xenos.h"
using BYTE = uint8_t;
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/adaptive_quad_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/adaptive_triangle_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/continuous_quad_1cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/continuous_quad_4cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/continuous_triangle_1cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/continuous_triangle_3cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/discrete_quad_1cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/discrete_quad_4cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/discrete_triangle_1cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/discrete_triangle_3cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/tessellation_adaptive_vs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/tessellation_indexed_vs.h"
#include "xenia/ui/metal/metal_presenter.h"


// Metal IR Converter Runtime - defines IRDescriptorTableEntry and bind points
#define IR_RUNTIME_METALCPP
#include "third_party/metal-shader-converter/include/metal_irconverter_runtime.h"

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
DEFINE_bool(metal_capture_frame, false,
            "Capture Metal swap frames for readback (debug).", "GPU");
DEFINE_bool(metal_log_memexport, false,
            "Log Metal memexport draws and ranges (debug).", "GPU");
DEFINE_bool(metal_readback_memexport, false,
            "Read back Metal memexport ranges on command buffer completion "
            "(debug).",
            "GPU");
DEFINE_bool(metal_disable_swap_dest_swap, false,
            "Disable forcing RB swap based on resolve dest_swap (debug).",
            "GPU");

namespace {

void LogMetalErrorDetails(const char* label, NS::Error* error) {
  if (!error) {
    return;
  }
  const char* desc = error->localizedDescription()
                         ? error->localizedDescription()->utf8String()
                         : nullptr;
  const char* failure = error->localizedFailureReason()
                            ? error->localizedFailureReason()->utf8String()
                            : nullptr;
  const char* recovery =
      error->localizedRecoverySuggestion()
          ? error->localizedRecoverySuggestion()->utf8String()
          : nullptr;
  const char* domain =
      error->domain() ? error->domain()->utf8String() : nullptr;
  int64_t code = error->code();
  XELOGE("{}: domain={} code={} desc='{}' failure='{}' recovery='{}'", label,
         domain ? domain : "<null>", code, desc ? desc : "<null>",
         failure ? failure : "<null>", recovery ? recovery : "<null>");
  NS::Dictionary* user_info = error->userInfo();
  if (user_info) {
    auto* info_desc = user_info->description();
    XELOGE("{}: userInfo={}", label,
           info_desc ? info_desc->utf8String() : "<null>");
  }
}

const char* GetGeometryShaderTypeName(PipelineGeometryShader type) {
  switch (type) {
    case PipelineGeometryShader::kPointList:
      return "point";
    case PipelineGeometryShader::kRectangleList:
      return "rect";
    case PipelineGeometryShader::kQuadList:
      return "quad";
    default:
      return "unknown";
  }
}

bool ShouldDumpGeometryShaders() {
  const char* env = std::getenv("XENIA_GEOMETRY_SHADER_DUMP");
  if (!env || !*env) {
    return false;
  }
  return std::strcmp(env, "0") != 0;
}

const char* GetShaderDumpDir() {
  const char* dump_dir = std::getenv("XENIA_SHADER_DUMP_DIR");
  if (dump_dir && *dump_dir) {
    return dump_dir;
  }
  return "/tmp";
}

bool WriteDumpFile(const std::string& path, const void* data, size_t size) {
  if (!data || !size) {
    return false;
  }
  std::filesystem::path fs_path = xe::to_path(path);
  if (!xe::filesystem::CreateParentFolder(fs_path)) {
    return false;
  }
  FILE* file = xe::filesystem::OpenFile(fs_path, "wb");
  if (!file) {
    return false;
  }
  fwrite(data, 1, size, file);
  fclose(file);
  return true;
}

bool WriteDumpText(const std::string& path, const std::string& text) {
  if (text.empty()) {
    return false;
  }
  std::filesystem::path fs_path = xe::to_path(path);
  if (!xe::filesystem::CreateParentFolder(fs_path)) {
    return false;
  }
  FILE* file = xe::filesystem::OpenFile(fs_path, "wb");
  if (!file) {
    return false;
  }
  fwrite(text.data(), 1, text.size(), file);
  fclose(file);
  return true;
}

bool ShaderUsesVertexFetch(const Shader& shader) {
  if (!shader.vertex_bindings().empty()) {
    return true;
  }
  const Shader::ConstantRegisterMap& constant_map =
      shader.constant_register_map();
  for (uint32_t i = 0; i < xe::countof(constant_map.vertex_fetch_bitmap); ++i) {
    if (constant_map.vertex_fetch_bitmap[i] != 0) {
      return true;
    }
  }
  return false;
}

void DumpGeometryArtifact(const char* dump_dir, int dump_id,
                          PipelineGeometryShader type, uint32_t key,
                          const char* suffix, const void* data, size_t size) {
  if (!dump_dir || !suffix) {
    return;
  }
  char filename[512];
  std::snprintf(filename, sizeof(filename), "%s/geometry_%d_%s_%08X_%s",
                dump_dir, dump_id, GetGeometryShaderTypeName(type), key, suffix);
  WriteDumpFile(filename, data, size);
}

void DumpGeometryText(const char* dump_dir, int dump_id,
                      PipelineGeometryShader type, uint32_t key,
                      const char* suffix, const std::string& text) {
  if (!dump_dir || !suffix) {
    return;
  }
  char filename[512];
  std::snprintf(filename, sizeof(filename), "%s/geometry_%d_%s_%08X_%s",
                dump_dir, dump_id, GetGeometryShaderTypeName(type), key, suffix);
  WriteDumpText(filename, text);
}

uint64_t HashBytes(const void* data, size_t size) {
  if (!data || !size) {
    return 0;
  }
  return XXH3_64bits(data, size);
}

bool ReadFileToVector(const char* path, std::vector<uint8_t>& out_data) {
  if (!path || !*path) {
    return false;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file.seekg(0, std::ios::end);
  std::streamsize size = file.tellg();
  if (size <= 0) {
    return false;
  }
  file.seekg(0, std::ios::beg);
  out_data.resize(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char*>(out_data.data()), size)) {
    out_data.clear();
    return false;
  }
  return true;
}

void ProbeGeometryMetallib(MTL::Device* device) {
  const char* path = std::getenv("XENIA_GEOMETRY_METALLIB_PROBE");
  if (!path || !*path || !device) {
    return;
  }
  std::vector<uint8_t> data;
  if (!ReadFileToVector(path, data)) {
    XELOGE("Geometry probe: failed to read metallib {}", path);
    return;
  }
  NS::Error* error = nullptr;
  dispatch_data_t lib_data = dispatch_data_create(
      data.data(), data.size(), nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  MTL::Library* library = device->newLibrary(lib_data, &error);
  dispatch_release(lib_data);
  if (!library) {
    LogMetalErrorDetails("Geometry probe: failed to create library", error);
    return;
  }
  NS::String* fn_name = NS::String::string("main", NS::UTF8StringEncoding);
  MTL::Function* fn_no_constants = library->newFunction(fn_name);
  if (fn_no_constants) {
    XELOGI("Geometry probe: newFunction(main) succeeded without constants");
    fn_no_constants->release();
  } else {
    XELOGE("Geometry probe: newFunction(main) failed without constants");
  }

  int32_t output_size = 64;
  if (const char* env = std::getenv("XENIA_GEOMETRY_OUTPUT_SIZE")) {
    output_size = std::atoi(env);
  }
  bool tessellation_enabled = false;
  MTL::FunctionConstantValues* fc =
      MTL::FunctionConstantValues::alloc()->init();
  fc->setConstantValue(&tessellation_enabled, MTL::DataTypeBool,
                       NS::String::string("tessellationEnabled",
                                          NS::UTF8StringEncoding));
  fc->setConstantValue(&output_size, MTL::DataTypeInt,
                       NS::String::string("vertex_shader_output_size_fc",
                                          NS::UTF8StringEncoding));
  error = nullptr;
  MTL::Function* fn_constants = library->newFunction(fn_name, fc, &error);
  if (fn_constants) {
    XELOGI(
        "Geometry probe: newFunction(main) succeeded with constants "
        "(output_size={})",
        output_size);
    fn_constants->release();
  } else {
    LogMetalErrorDetails(
        "Geometry probe: newFunction(main) failed with constants", error);
  }
  fc->release();
  library->release();
}


MTL::CompareFunction ToMetalCompareFunction(xenos::CompareFunction compare) {
  static const MTL::CompareFunction kCompareMap[8] = {
      MTL::CompareFunctionNever,         // 0
      MTL::CompareFunctionLess,          // 1
      MTL::CompareFunctionEqual,         // 2
      MTL::CompareFunctionLessEqual,     // 3
      MTL::CompareFunctionGreater,       // 4
      MTL::CompareFunctionNotEqual,      // 5
      MTL::CompareFunctionGreaterEqual,  // 6
      MTL::CompareFunctionAlways,        // 7
  };
  return kCompareMap[uint32_t(compare) & 0x7];
}

MTL::StencilOperation ToMetalStencilOperation(xenos::StencilOp op) {
  static const MTL::StencilOperation kStencilOpMap[8] = {
      MTL::StencilOperationKeep,           // 0
      MTL::StencilOperationZero,           // 1
      MTL::StencilOperationReplace,        // 2
      MTL::StencilOperationIncrementClamp, // 3
      MTL::StencilOperationDecrementClamp, // 4
      MTL::StencilOperationInvert,         // 5
      MTL::StencilOperationIncrementWrap,  // 6
      MTL::StencilOperationDecrementWrap,  // 7
  };
  return kStencilOpMap[uint32_t(op) & 0x7];
}

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
      /* 14 */ MTL::BlendFactorBlendAlpha,  // CONSTANT_ALPHA
      /* 15 */ MTL::BlendFactorOneMinusBlendAlpha,
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

  for (auto& pair : geometry_pipeline_cache_) {
    if (pair.second.pipeline) {
      pair.second.pipeline->release();
    }
  }
  geometry_pipeline_cache_.clear();

  for (auto& pair : geometry_vertex_stage_cache_) {
    if (pair.second.library) {
      pair.second.library->release();
    }
    if (pair.second.stage_in_library) {
      pair.second.stage_in_library->release();
    }
  }
  geometry_vertex_stage_cache_.clear();

  for (auto& pair : geometry_shader_stage_cache_) {
    if (pair.second.library) {
      pair.second.library->release();
    }
  }
  geometry_shader_stage_cache_.clear();

  for (auto& pair : depth_stencil_state_cache_) {
    if (pair.second) {
      pair.second->release();
    }
  }
  depth_stencil_state_cache_.clear();

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
  {
    std::lock_guard<std::mutex> lock(draw_ring_mutex_);
    active_draw_ring_.reset();
    draw_ring_pool_.clear();
    command_buffer_draw_rings_.clear();
  }
  res_heap_ab_ = nullptr;
  smp_heap_ab_ = nullptr;
  cbv_heap_ab_ = nullptr;
  uniforms_buffer_ = nullptr;
  top_level_ab_ = nullptr;
  draw_args_buffer_ = nullptr;
}

MetalCommandProcessor::DrawRingBuffers::~DrawRingBuffers() {
  if (res_heap_ab) {
    res_heap_ab->release();
    res_heap_ab = nullptr;
  }
  if (smp_heap_ab) {
    smp_heap_ab->release();
    smp_heap_ab = nullptr;
  }
  if (cbv_heap_ab) {
    cbv_heap_ab->release();
    cbv_heap_ab = nullptr;
  }
  if (uniforms_buffer) {
    uniforms_buffer->release();
    uniforms_buffer = nullptr;
  }
  if (top_level_ab) {
    top_level_ab->release();
    top_level_ab = nullptr;
  }
  if (draw_args_buffer) {
    draw_args_buffer->release();
    draw_args_buffer = nullptr;
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
  if (saw_swap_) {
    return;
  }
  IssueSwap(0, render_target_width_, render_target_height_);
}

void MetalCommandProcessor::SetSwapDestSwap(uint32_t dest_base, bool swap) {
  if (!dest_base) {
    return;
  }
  if (swap_dest_swaps_by_base_.size() > 256) {
    swap_dest_swaps_by_base_.clear();
  }
  swap_dest_swaps_by_base_[dest_base] = swap;
}

bool MetalCommandProcessor::ConsumeSwapDestSwap(uint32_t dest_base,
                                                bool* swap_out) {
  if (!swap_out || !dest_base) {
    return false;
  }
  auto it = swap_dest_swaps_by_base_.find(dest_base);
  if (it == swap_dest_swaps_by_base_.end()) {
    return false;
  }
  *swap_out = it->second;
  swap_dest_swaps_by_base_.erase(it);
  return true;
}

bool MetalCommandProcessor::SetupContext() {
  XELOGI("MetalCommandProcessor::SetupContext: Starting");
  XELOGI(
      "MetalCommandProcessor::SetupContext: metal_edram_compute_fallback={}",
      ::cvars::metal_edram_compute_fallback);
  saw_swap_ = false;
  last_swap_ptr_ = 0;
  last_swap_width_ = 0;
  last_swap_height_ = 0;
  swap_dest_swaps_by_base_.clear();
  gamma_ramp_256_entry_table_up_to_date_ = false;
  gamma_ramp_pwl_up_to_date_ = false;
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

  bool supports_apple7 = device_->supportsFamily(MTL::GPUFamilyApple7);
  bool supports_mac2 = device_->supportsFamily(MTL::GPUFamilyMac2);
  bool supports_metal3 = device_->supportsFamily(MTL::GPUFamilyMetal3);
  bool supports_metal4 = device_->supportsFamily(MTL::GPUFamilyMetal4);
  XELOGI(
      "MetalCommandProcessor::SetupContext: GPU family support Apple7={} "
      "Mac2={} Metal3={} Metal4={}",
      supports_apple7, supports_mac2, supports_metal3, supports_metal4);
  mesh_shader_supported_ = supports_apple7 || supports_mac2;
  XELOGI("MetalCommandProcessor::SetupContext: Mesh shaders supported={}",
         mesh_shader_supported_);

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
  ProbeGeometryMetallib(device_);

  if (mesh_shader_supported_) {
    uint64_t tess_tables_size = IRRuntimeTessellatorTablesSize();
    tessellator_tables_buffer_ =
        device_->newBuffer(tess_tables_size, MTL::ResourceStorageModeShared);
    if (!tessellator_tables_buffer_) {
      XELOGE("Failed to allocate tessellator tables buffer ({} bytes)",
             tess_tables_size);
      return false;
    }
    tessellator_tables_buffer_->setLabel(
        NS::String::string("XeniaTessellatorTables", NS::UTF8StringEncoding));
    IRRuntimeLoadTessellatorTables(tessellator_tables_buffer_);
    XELOGI("MetalCommandProcessor::SetupContext: Loaded tessellator tables");
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

  // Create a null buffer for unused descriptor entries
  // This prevents shader validation errors when accessing unpopulated
  // descriptors
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

  auto ring = CreateDrawRingBuffers();
  if (!ring) {
    return false;
  }
  SetActiveDrawRing(ring);

  XELOGI("MetalCommandProcessor::SetupContext() completed successfully");
  return true;
}

bool MetalCommandProcessor::InitializeShaderTranslation() {
  // Initialize DXBC shader translator (use Apple vendor ID for Metal)
  // Parameters: vendor_id, bindless_resources_used, edram_rov_used,
  //             gamma_render_target_as_srgb, msaa_2x_supported,
  //             draw_resolution_scale_x, draw_resolution_scale_y
  const bool edram_rov_used = ::cvars::metal_edram_rov;
  shader_translator_ = std::make_unique<DxbcShaderTranslator>(
      ui::GraphicsProvider::GpuVendorID::kApple,
      false,  // bindless_resources_used - not using bindless for now
      edram_rov_used,
      ::cvars::gamma_render_target_as_srgb,
      true,   // msaa_2x_supported - Metal supports MSAA
      1,      // draw_resolution_scale_x - 1x for now
      1);     // draw_resolution_scale_y - 1x for now
  XELOGI("MetalCommandProcessor: edram_rov_used={}", edram_rov_used);

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

  // Configure MSC minimum targets to avoid materialization failures on older
  // GPUs/OS versions.
  if (device_) {
    IRGPUFamily min_family = IRGPUFamilyMetal3;
    if (device_->supportsFamily(MTL::GPUFamilyApple10)) {
      min_family = IRGPUFamilyApple10;
    } else if (device_->supportsFamily(MTL::GPUFamilyApple9)) {
      min_family = IRGPUFamilyApple9;
    } else if (device_->supportsFamily(MTL::GPUFamilyApple8)) {
      min_family = IRGPUFamilyApple8;
    } else if (device_->supportsFamily(MTL::GPUFamilyApple7)) {
      min_family = IRGPUFamilyApple7;
    } else if (device_->supportsFamily(MTL::GPUFamilyApple6)) {
      min_family = IRGPUFamilyApple6;
    } else if (device_->supportsFamily(MTL::GPUFamilyMac2) ||
               device_->supportsFamily(MTL::GPUFamilyMetal4) ||
               device_->supportsFamily(MTL::GPUFamilyMetal3)) {
      min_family = IRGPUFamilyMetal3;
    }

    NS::OperatingSystemVersion os_version =
        NS::ProcessInfo::processInfo()->operatingSystemVersion();
    std::ostringstream version_stream;
    version_stream << os_version.majorVersion << "." << os_version.minorVersion
                   << "." << os_version.patchVersion;
    XELOGI("MetalShaderConverter: minimum target gpu_family={} os={}",
           static_cast<int>(min_family), version_stream.str());
    metal_shader_converter_->SetMinimumTarget(
        min_family, IROperatingSystem_macOS, version_stream.str());
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
    ScheduleDrawRingRelease(current_command_buffer_);
    current_command_buffer_->commit();
    current_command_buffer_->waitUntilCompleted();
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
    SetActiveDrawRing(nullptr);
    current_draw_index_ = 0;
    XELOGI("MetalCommandProcessor::PrepareForWait: command buffer completed");
  }

  FlushEdramFromHostRenderTargetsIfEnabled("PrepareForWait");

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
    ScheduleDrawRingRelease(current_command_buffer_);
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
    SetActiveDrawRing(nullptr);
    current_draw_index_ = 0;
  }

  FlushEdramFromHostRenderTargetsIfEnabled("ShutdownContext");

  // Even if we have no active command buffer at this point, there may be
  // previously committed command buffers still in flight. Submit and wait for
  // a dummy command buffer to ensure all GPU work on this queue has completed
  // before tearing down resources on thread exit.
  if (command_queue_) {
    XELOGI(
        "MetalCommandProcessor::ShutdownContext: submitting sync command "
        "buffer");
    MTL::CommandBuffer* sync_cmd = command_queue_->commandBuffer();
    if (sync_cmd) {
      sync_cmd->commit();
      sync_cmd->waitUntilCompleted();
      XELOGI("MetalCommandProcessor::ShutdownContext: sync complete");
    }
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

  {
    std::lock_guard<std::mutex> lock(draw_ring_mutex_);
    active_draw_ring_.reset();
    draw_ring_pool_.clear();
    command_buffer_draw_rings_.clear();
  }

  if (texture_cache_) {
    texture_cache_->Shutdown();
    texture_cache_.reset();
  }

  if (primitive_processor_) {
    primitive_processor_->Shutdown();
    primitive_processor_.reset();
  }
  if (tessellator_tables_buffer_) {
    tessellator_tables_buffer_->release();
    tessellator_tables_buffer_ = nullptr;
  }
  if (depth_only_pixel_library_) {
    depth_only_pixel_library_->release();
    depth_only_pixel_library_ = nullptr;
  }
  depth_only_pixel_function_name_.clear();
  frame_open_ = false;

  shader_cache_.clear();
  shared_memory_.reset();
  shader_translator_.reset();
  dxbc_to_dxil_converter_.reset();
  metal_shader_converter_.reset();

  XELOGI(
      "MetalCommandProcessor::ShutdownContext: tessellation draws: "
      "path_select={} tessellated={}",
      tessellation_enabled_draws_, tessellated_draws_);

  CommandProcessor::ShutdownContext();
  XELOGI("MetalCommandProcessor::ShutdownContext: end");
}

void MetalCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                      uint32_t frontbuffer_width,
                                      uint32_t frontbuffer_height) {
  XELOGI("MetalCommandProcessor::IssueSwap(ptr={:08X}, {}x{})", frontbuffer_ptr,
         frontbuffer_width, frontbuffer_height);
  saw_swap_ = true;
  last_swap_ptr_ = frontbuffer_ptr;
  last_swap_width_ = frontbuffer_width;
  last_swap_height_ = frontbuffer_height;

  // End any active render encoder
  if (current_render_encoder_) {
    current_render_encoder_->endEncoding();
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
  }

  // Submit and wait for command buffer
  if (current_command_buffer_) {
    ScheduleDrawRingRelease(current_command_buffer_);
    current_command_buffer_->commit();
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
    SetActiveDrawRing(nullptr);
    current_draw_index_ = 0;
  }

  FlushEdramFromHostRenderTargetsIfEnabled("IssueSwap");

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
    uint32_t output_width =
        frontbuffer_width ? frontbuffer_width : render_target_width_;
    uint32_t output_height =
        frontbuffer_height ? frontbuffer_height : render_target_height_;

    MTL::Texture* source_texture = nullptr;
    bool use_pwl_gamma_ramp = false;
    if (texture_cache_) {
      uint32_t swap_width = 0;
      uint32_t swap_height = 0;
      xenos::TextureFormat swap_format = xenos::TextureFormat::k_8_8_8_8;
      source_texture =
          texture_cache_->RequestSwapTexture(swap_width, swap_height,
                                             swap_format);
      if (source_texture) {
        output_width = swap_width;
        output_height = swap_height;
        use_pwl_gamma_ramp =
            swap_format == xenos::TextureFormat::k_2_10_10_10 ||
            swap_format ==
                xenos::TextureFormat::k_2_10_10_10_AS_16_16_16_16;
        static MTL::PixelFormat last_format = MTL::PixelFormatInvalid;
        static uint32_t last_samples = 0;
        static uint32_t last_width = 0;
        static uint32_t last_height = 0;
        static int last_swap_format = -1;
        MTL::PixelFormat src_format = source_texture->pixelFormat();
        uint32_t src_samples = source_texture->sampleCount();
        uint32_t src_width = uint32_t(source_texture->width());
        uint32_t src_height = uint32_t(source_texture->height());
        int swap_format_int = static_cast<int>(swap_format);
        if (src_format != last_format || src_samples != last_samples ||
            src_width != last_width || src_height != last_height ||
            swap_format_int != last_swap_format) {
          XELOGI(
              "Metal IssueSwap: source fmt={} samples={} size={}x{} "
              "swap_format={} output={}x{}",
              int(src_format), src_samples, src_width, src_height,
              swap_format_int, output_width, output_height);
          last_format = src_format;
          last_samples = src_samples;
          last_width = src_width;
          last_height = src_height;
          last_swap_format = swap_format_int;
        }
        if (presenter) {
          if (!gamma_ramp_256_entry_table_up_to_date_ ||
              !gamma_ramp_pwl_up_to_date_) {
            constexpr size_t kGammaRampTableBytes =
                sizeof(reg::DC_LUT_30_COLOR) * 256;
            constexpr size_t kGammaRampPwlBytes =
                sizeof(reg::DC_LUT_PWL_DATA) * 128 * 3;
            if (presenter->UpdateGammaRamp(
                    gamma_ramp_256_entry_table(), kGammaRampTableBytes,
                    gamma_ramp_pwl_rgb(), kGammaRampPwlBytes)) {
              gamma_ramp_256_entry_table_up_to_date_ = true;
              gamma_ramp_pwl_up_to_date_ = true;
            } else {
              XELOGW("Metal IssueSwap: gamma ramp upload failed");
            }
          }
        }
      }
    }

    bool swap_dest_swap = false;
    const bool has_swap_dest_swap =
        ConsumeSwapDestSwap(frontbuffer_ptr, &swap_dest_swap);
    if (!has_swap_dest_swap && frontbuffer_ptr) {
      static uint32_t swap_dest_miss_count = 0;
      if (swap_dest_miss_count < 8) {
        ++swap_dest_miss_count;
        XELOGI(
            "Metal IssueSwap: no dest_swap record for swap ptr 0x{:08X}",
            frontbuffer_ptr);
      }
    }
    bool force_swap_rb = has_swap_dest_swap && swap_dest_swap;
    if (force_swap_rb && cvars::metal_disable_swap_dest_swap) {
      static uint32_t swap_dest_disable_log_count = 0;
      if (swap_dest_disable_log_count < 8) {
        ++swap_dest_disable_log_count;
        XELOGI(
            "Metal IssueSwap: dest_swap RB swap disabled by "
            "metal_disable_swap_dest_swap");
      }
      force_swap_rb = false;
    }
    if (force_swap_rb) {
      XELOGI("Metal IssueSwap: forcing RB swap due to dest_swap");
    }

    if (!source_texture) {
      static bool missing_swap_logged = false;
      if (!missing_swap_logged) {
        missing_swap_logged = true;
        XELOGW(
            "MetalCommandProcessor::IssueSwap: swap texture unavailable; "
            "presenting inactive (black) output");
      }
      presenter->RefreshGuestOutput(
          0, 0, 0, 0,
          [](ui::Presenter::GuestOutputRefreshContext&) -> bool {
            return false;
          });
      if (cvars::metal_capture_frame) {
        CaptureCurrentFrame();
      }
      return;
    }

    if (source_texture) {
      ui::metal::MetalPresenter* metal_presenter = presenter;
      uint32_t source_width = output_width;
      uint32_t source_height = output_height;
      bool force_swap_rb_copy = force_swap_rb;
      bool use_pwl_gamma_ramp_copy = use_pwl_gamma_ramp;
      presenter->RefreshGuestOutput(
          output_width, output_height, 1280, 720,  // Display aspect ratio
          [source_texture, metal_presenter, source_width, source_height,
           force_swap_rb_copy, use_pwl_gamma_ramp_copy](
              ui::Presenter::GuestOutputRefreshContext& context) -> bool {
            auto& metal_context =
                static_cast<ui::metal::MetalGuestOutputRefreshContext&>(
                    context);
            context.SetIs8bpc(!use_pwl_gamma_ramp_copy);
            return metal_presenter->CopyTextureToGuestOutput(
                source_texture, metal_context.resource_uav_capable(),
                source_width, source_height, force_swap_rb_copy,
                use_pwl_gamma_ramp_copy);
          });
    }
  }

  // Also capture internally for backwards compatibility
  if (cvars::metal_capture_frame) {
    CaptureCurrentFrame();
  }
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

  // Only capture common 32bpp color targets for now.
  const MTL::PixelFormat capture_format = capture_texture->pixelFormat();
  if (capture_format != MTL::PixelFormatBGRA8Unorm &&
      capture_format != MTL::PixelFormatRGBA8Unorm) {
    XELOGD("CaptureCurrentFrame: skipping capture of pixel format {}",
           static_cast<uint32_t>(capture_format));
    return;
  }

  const uint32_t capture_width = static_cast<uint32_t>(capture_texture->width());
  const uint32_t capture_height =
      static_cast<uint32_t>(capture_texture->height());

  // Create a staging buffer for readback
  size_t bytes_per_row = size_t(capture_width) * 4;  // 32bpp
  size_t buffer_size = bytes_per_row * size_t(capture_height);

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
      MTL::Size(capture_width, capture_height, 1), staging_buffer, 0,
      bytes_per_row, 0);

  blit->endEncoding();
  // Note: blitCommandEncoder() returns autoreleased object - don't release
  blit_cmd->commit();
  blit_cmd->waitUntilCompleted();

  // Copy data from staging buffer
  captured_width_ = capture_width;
  captured_height_ = capture_height;
  captured_frame_data_.resize(buffer_size);

  uint8_t* src = static_cast<uint8_t*>(staging_buffer->contents());

  if (capture_format == MTL::PixelFormatBGRA8Unorm) {
    // Convert BGRA to RGBA.
    for (size_t i = 0; i < buffer_size; i += 4) {
      captured_frame_data_[i + 0] = src[i + 2];  // R from B
      captured_frame_data_[i + 1] = src[i + 1];  // G
      captured_frame_data_[i + 2] = src[i + 0];  // B from R
      captured_frame_data_[i + 3] = src[i + 3];  // A
    }
  } else {
    std::memcpy(captured_frame_data_.data(), src, buffer_size);
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
  uint32_t normalized_color_mask = 0;
  uint32_t ordered_blend_mask = 0;
  bool use_ordered_blend_fallback = false;
  bool ordered_blend_coverage = false;
  draw_util::Scissor guest_scissor = {};
  bool guest_scissor_valid = false;

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
  memexport_ranges_.clear();
  if (memexport_used_vertex) {
    draw_util::AddMemExportRanges(regs, *vertex_shader, memexport_ranges_);
  }
  if (memexport_used_pixel) {
    draw_util::AddMemExportRanges(regs, *pixel_shader, memexport_ranges_);
  }
  static std::unordered_set<uint64_t> logged_memexport_vs;
  static std::unordered_set<uint64_t> logged_memexport_ps;
  if (memexport_used_vertex) {
    uint64_t vs_hash = vertex_shader->ucode_data_hash();
    if (logged_memexport_vs.insert(vs_hash).second) {
      XELOGI("Metal memexport VS: hash={:016X} eM=0x{:02X}", vs_hash,
             vertex_shader->memexport_eM_written());
    }
  }
  if (memexport_used_pixel && pixel_shader) {
    uint64_t ps_hash = pixel_shader->ucode_data_hash();
    if (logged_memexport_ps.insert(ps_hash).second) {
      XELOGI("Metal memexport PS: hash={:016X} eM=0x{:02X}", ps_hash,
             pixel_shader->memexport_eM_written());
    }
  }
  static uint32_t memexport_log_count = 0;
  if (cvars::metal_log_memexport && memexport_used && memexport_log_count < 16) {
    ++memexport_log_count;
    XELOGI(
        "Metal memexport draw: vs_memexport={} ps_memexport={} ranges={}",
        memexport_used_vertex ? 1 : 0, memexport_used_pixel ? 1 : 0,
        memexport_ranges_.size());
    if (memexport_ranges_.empty()) {
      XELOGW("Metal memexport draw has no ranges (missing stream constants?)");
    }
    for (const auto& range : memexport_ranges_) {
      XELOGI("  memexport range: base=0x{:08X} size={}",
             range.base_address_dwords << 2, range.size_bytes);
    }
  }

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
  if (primitive_processing_result.host_vertex_shader_type ==
      Shader::HostVertexShaderType::kMemExportCompute) {
    primitive_processing_result.host_vertex_shader_type =
        Shader::HostVertexShaderType::kVertex;
    static bool logged_memexport_compute = false;
    if (!logged_memexport_compute) {
      logged_memexport_compute = true;
      XELOGI(
          "Metal: memexport-only draws use the vertex path (MSC supports UAV "
          "writes in vertex shaders)");
    }
  }

  // Log the first few draws to identify primitive processing / shader
  // modification issues quickly when bringing up the backend.
  static uint32_t draw_debug_log_count = 0;
  if (draw_debug_log_count < 8) {
    ++draw_debug_log_count;
    auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
    auto vgt_output_path_cntl = regs.Get<reg::VGT_OUTPUT_PATH_CNTL>();
    auto vgt_hos_cntl = regs.Get<reg::VGT_HOS_CNTL>();
    auto rb_modecontrol = regs.Get<reg::RB_MODECONTROL>();
    XELOGI(
        "DRAW_PROC_DEBUG[{}]: guest_prim={} host_prim={} host_vs_type={} "
        "tessellated={} tess_mode={} path_select={} hos_tess_mode={} "
        "src_select={} idx_fmt={} host_idx_fmt={} ib_type={} edram_mode={}",
        draw_debug_log_count,
        uint32_t(primitive_processing_result.guest_primitive_type),
        uint32_t(primitive_processing_result.host_primitive_type),
        uint32_t(primitive_processing_result.host_vertex_shader_type),
        primitive_processing_result.IsTessellated(),
        uint32_t(primitive_processing_result.tessellation_mode),
        uint32_t(vgt_output_path_cntl.path_select),
        uint32_t(vgt_hos_cntl.tess_mode),
        uint32_t(vgt_draw_initiator.source_select),
        uint32_t(vgt_draw_initiator.index_size),
        uint32_t(primitive_processing_result.host_index_format),
        uint32_t(primitive_processing_result.index_buffer_type),
        uint32_t(rb_modecontrol.edram_mode));
  }

  auto vgt_output_path_cntl = regs.Get<reg::VGT_OUTPUT_PATH_CNTL>();
  bool tessellation_enabled =
      vgt_output_path_cntl.path_select ==
      xenos::VGTOutputPath::kTessellationEnable;
  if (tessellation_enabled) {
    ++tessellation_enabled_draws_;
    if (!logged_tessellation_enable_) {
      logged_tessellation_enable_ = true;
      auto vgt_hos_cntl = regs.Get<reg::VGT_HOS_CNTL>();
      XELOGI(
          "Metal: tessellation enabled in draw (hos_tess_mode={}, "
          "guest_prim={}, host_prim={})",
          uint32_t(vgt_hos_cntl.tess_mode),
          uint32_t(primitive_processing_result.guest_primitive_type),
          uint32_t(primitive_processing_result.host_primitive_type));
    }
  }
  if (primitive_processing_result.IsTessellated()) {
    ++tessellated_draws_;
    if (!logged_tessellated_draw_) {
      logged_tessellated_draw_ = true;
      XELOGI(
          "Metal: tessellation emulation active in draw (host_vs_type={}, "
          "tess_mode={})",
          uint32_t(primitive_processing_result.host_vertex_shader_type),
          uint32_t(primitive_processing_result.tessellation_mode));
    }
  }

  bool use_tessellation_emulation = false;
  if (primitive_processing_result.IsTessellated()) {
    if (!mesh_shader_supported_) {
      static bool tess_mesh_logged = false;
      if (!tess_mesh_logged) {
        tess_mesh_logged = true;
        XELOGW(
            "Metal: Tessellation emulation requested but mesh shaders are not "
            "supported on this device");
      }
      return true;
    }
    if (!pixel_shader) {
      static bool tess_no_ps_logged = false;
      if (!tess_no_ps_logged) {
        tess_no_ps_logged = true;
        XELOGW(
            "Metal: Tessellation emulation requested without a pixel shader; "
            "using depth-only PS fallback");
      }
    }
    use_tessellation_emulation = true;
  }

  // The DXBC->DXIL->Metal pipeline currently only supports the "normal" guest
  // vertex shader path (and domain shaders are skipped above). Some
  // PrimitiveProcessor fallback host vertex shader types require additional
  // translator/runtime support and can't be executed yet.
  // Configure render targets via MetalRenderTargetCache, similar to D3D12.
  // D3D12 passes `is_rasterization_done` when there is an actual draw to
  // perform (after primitive processing). For this Metal path we currently
  // always treat IssueDraw as doing rasterization so the render-target cache
  // will create and bind the appropriate host render targets.
  if (render_target_cache_) {
    auto normalized_depth_control = draw_util::GetNormalizedDepthControl(regs);
    normalized_color_mask =
        pixel_shader ? draw_util::GetNormalizedColorMask(
                           regs, pixel_shader->writes_color_targets())
                     : 0;
    XELOGI(
        "Metal IssueDraw: writes_color_targets=0x{:X}, "
        "normalized_color_mask=0x{:X}",
        pixel_shader ? pixel_shader->writes_color_targets() : 0,
        normalized_color_mask);
    const bool edram_psi_used =
        render_target_cache_->GetPath() ==
        RenderTargetCache::Path::kPixelShaderInterlock;
    {
      static int edram_blend_log_count = 0;
      ordered_blend_mask = 0;
      if (edram_psi_used && normalized_color_mask) {
        for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
          uint32_t rt_write_mask =
              (normalized_color_mask >> (i * 4)) & 0xF;
          if (!rt_write_mask) {
            continue;
          }
          auto blendcontrol = regs.Get<reg::RB_BLENDCONTROL>(
              reg::RB_BLENDCONTROL::rt_register_indices[i]);
          bool blending_enabled =
              blendcontrol.color_srcblend != xenos::BlendFactor::kOne ||
              blendcontrol.color_destblend != xenos::BlendFactor::kZero ||
              blendcontrol.color_comb_fcn != xenos::BlendOp::kAdd ||
              blendcontrol.alpha_srcblend != xenos::BlendFactor::kOne ||
              blendcontrol.alpha_destblend != xenos::BlendFactor::kZero ||
              blendcontrol.alpha_comb_fcn != xenos::BlendOp::kAdd;
          float rt_clamp[4];
          uint32_t rt_keep_mask[2];
          RenderTargetCache::GetPSIColorFormatInfo(
              regs.Get<reg::RB_COLOR_INFO>(
                  reg::RB_COLOR_INFO::rt_register_indices[i])
                  .color_format,
              rt_write_mask, rt_clamp[0], rt_clamp[1], rt_clamp[2],
              rt_clamp[3], rt_keep_mask[0], rt_keep_mask[1]);
          bool rt_disabled =
              rt_keep_mask[0] == UINT32_MAX && rt_keep_mask[1] == UINT32_MAX;
          bool keep_mask_nonempty =
              !rt_disabled && (rt_keep_mask[0] != 0 || rt_keep_mask[1] != 0);
          if (!rt_disabled && (blending_enabled || keep_mask_nonempty)) {
            ordered_blend_mask |= (1u << i);
          }
        }
      }
      if (edram_blend_log_count < 16 && ordered_blend_mask) {
        ++edram_blend_log_count;
        XELOGI("EDRAM blend: ordered blending likely required (mask=0x{:X})",
               ordered_blend_mask);
      }
    }
    const bool allow_compute_fallback =
        edram_psi_used && ::cvars::metal_edram_rov &&
        ::cvars::metal_edram_compute_fallback;
    use_ordered_blend_fallback = ordered_blend_mask && allow_compute_fallback;
    ordered_blend_coverage = use_ordered_blend_fallback && pixel_shader;
    draw_util::GetScissor(regs, guest_scissor);
    guest_scissor_valid = true;
    if (!render_target_cache_->Update(is_rasterization_done,
                                      normalized_depth_control,
                                      normalized_color_mask, *vertex_shader)) {
      XELOGE(
          "MetalCommandProcessor::IssueDraw - RenderTargetCache::Update "
          "failed");
      return false;
    }
    render_target_cache_->SetOrderedBlendCoverageActive(
        ordered_blend_coverage);

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

  if (use_ordered_blend_fallback && ordered_blend_mask && render_target_cache_) {
    if (!guest_scissor_valid) {
      static bool logged_missing_scissor = false;
      if (!logged_missing_scissor) {
        logged_missing_scissor = true;
        XELOGW(
            "Metal: ordered blend fallback pre-dump skipped (guest scissor "
            "unavailable)");
      }
    } else {
      EndRenderEncoder();
      MTL::CommandBuffer* cmd = EnsureCommandBuffer();
      if (cmd) {
        for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
          if (!(ordered_blend_mask & (1u << i))) {
            continue;
          }
          uint32_t rt_write_mask =
              (normalized_color_mask >> (i * 4)) & 0xF;
          if (!rt_write_mask) {
            continue;
          }
          auto* rt = render_target_cache_->GetColorRenderTarget(i);
          if (!rt) {
            continue;
          }
          render_target_cache_->DumpRenderTargetToEdramRect(
              rt, guest_scissor.offset[0], guest_scissor.offset[1],
              guest_scissor.extent[0], guest_scissor.extent[1], cmd);
        }
      }
    }
  }

  // Begin command buffer if needed (will use cache-provided render targets).
  BeginCommandBuffer();
  EnsureDrawRingCapacity();
  if (cvars::metal_readback_memexport && memexport_used) {
    static uint32_t memexport_readback_count = 0;
    if (memexport_readback_count < 4) {
      ++memexport_readback_count;
      MTL::CommandBuffer* cmd = EnsureCommandBuffer();
      MTL::Buffer* shared_mem_buffer =
          shared_memory_ ? shared_memory_->GetBuffer() : nullptr;
      if (!cmd || !shared_mem_buffer) {
        XELOGW("Metal memexport readback skipped (no command buffer/buffer)");
      } else if (shared_mem_buffer->storageMode() !=
                 MTL::StorageModeShared) {
        XELOGW(
            "Metal memexport readback skipped (shared memory not CPU-visible)");
      } else {
        auto ranges = memexport_ranges_;
        uint32_t readback_id = memexport_readback_count;
        cmd->addCompletedHandler(
            [shared_mem_buffer, ranges, readback_id](MTL::CommandBuffer*) {
              const uint8_t* bytes =
                  static_cast<const uint8_t*>(shared_mem_buffer->contents());
              if (!bytes) {
                XELOGW("Metal memexport readback {}: no buffer contents",
                       readback_id);
                return;
              }
              for (const auto& range : ranges) {
                uint32_t base_bytes = range.base_address_dwords << 2;
                uint64_t buffer_length = shared_mem_buffer->length();
                if (base_bytes >= buffer_length) {
                  continue;
                }
                uint32_t available = std::min<uint32_t>(
                    range.size_bytes,
                    uint32_t(buffer_length - base_bytes));
                uint8_t sample_bytes[16] = {};
                uint32_t sample_count = std::min<uint32_t>(available, 16u);
                std::memcpy(sample_bytes, bytes + base_bytes, sample_count);
                XELOGI(
                    "Metal memexport readback {}: base=0x{:08X} size={} "
                    "bytes[0..15]={:02X} {:02X} {:02X} {:02X}  "
                    "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}  "
                    "{:02X} {:02X} {:02X} {:02X}",
                    readback_id, base_bytes, range.size_bytes, sample_bytes[0],
                    sample_bytes[1], sample_bytes[2], sample_bytes[3],
                    sample_bytes[4], sample_bytes[5], sample_bytes[6],
                    sample_bytes[7], sample_bytes[8], sample_bytes[9],
                    sample_bytes[10], sample_bytes[11], sample_bytes[12],
                    sample_bytes[13], sample_bytes[14], sample_bytes[15]);
                if (sample_count < 16) {
                  break;
                }
              }
            });
      }
    }
  }

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
  Shader::HostVertexShaderType host_vertex_shader_type_for_translation =
      primitive_processing_result.host_vertex_shader_type;
  if (host_vertex_shader_type_for_translation ==
          Shader::HostVertexShaderType::kPointListAsTriangleStrip ||
      host_vertex_shader_type_for_translation ==
          Shader::HostVertexShaderType::kRectangleListAsTriangleStrip) {
    if (!mesh_shader_supported_) {
      static bool host_vs_expansion_logged = false;
      if (!host_vs_expansion_logged) {
        host_vs_expansion_logged = true;
        XELOGW(
            "Metal: Host VS expansion requested without mesh shader support; "
            "skipping draw");
      }
      return true;
    }
    // Geometry emulation handles point/rectangle expansion; use the normal
    // vertex shader translation path to avoid unsupported host VS types.
    host_vertex_shader_type_for_translation =
        Shader::HostVertexShaderType::kVertex;
  }
  DxbcShaderTranslator::Modification vertex_shader_modification =
      GetCurrentVertexShaderModification(
          *vertex_shader, host_vertex_shader_type_for_translation,
          interpolator_mask);
  DxbcShaderTranslator::Modification pixel_shader_modification =
      pixel_shader ? GetCurrentPixelShaderModification(
                         *pixel_shader, interpolator_mask, ps_param_gen_pos,
                         normalized_depth_control, ordered_blend_coverage)
                   : DxbcShaderTranslator::Modification(0);

  PipelineGeometryShader geometry_shader_type =
      PipelineGeometryShader::kNone;
  if (!primitive_processing_result.IsTessellated()) {
    switch (primitive_processing_result.host_primitive_type) {
      case xenos::PrimitiveType::kPointList:
        geometry_shader_type = PipelineGeometryShader::kPointList;
        break;
      case xenos::PrimitiveType::kRectangleList:
        geometry_shader_type = PipelineGeometryShader::kRectangleList;
        break;
      case xenos::PrimitiveType::kQuadList:
        geometry_shader_type = PipelineGeometryShader::kQuadList;
        break;
      default:
        break;
    }
  }

  GeometryShaderKey geometry_shader_key;
  bool use_geometry_emulation = false;
  if (geometry_shader_type != PipelineGeometryShader::kNone) {
    bool can_build_geometry_shader =
        pixel_shader ||
        !vertex_shader_modification.vertex.interpolator_mask;
    if (!can_build_geometry_shader) {
      static bool geom_interp_mismatch_logged = false;
      if (!geom_interp_mismatch_logged) {
        geom_interp_mismatch_logged = true;
        XELOGW(
            "Metal: geometry emulation skipped because pixel shader is null "
            "but vertex interpolators are present");
      }
    } else {
      use_geometry_emulation =
          GetGeometryShaderKey(geometry_shader_type, vertex_shader_modification,
                               pixel_shader_modification, geometry_shader_key);
    }
  }
  if (use_geometry_emulation && !mesh_shader_supported_) {
    static bool mesh_support_logged = false;
    if (!mesh_support_logged) {
      mesh_support_logged = true;
      XELOGW(
          "Metal: geometry emulation requested but mesh shaders are not "
          "supported on this device");
    }
    use_geometry_emulation = false;
  }
  if (use_geometry_emulation && !pixel_shader) {
    static bool geom_no_ps_logged = false;
    if (!geom_no_ps_logged) {
      geom_no_ps_logged = true;
      XELOGW(
          "Metal: geometry emulation requested without a pixel shader; using "
          "depth-only PS fallback");
    }
  }

  // Get or create shader translations for the selected modifications.
  auto vertex_translation = static_cast<MetalShader::MetalTranslation*>(
      vertex_shader->GetOrCreateTranslation(vertex_shader_modification.value));
  if (!vertex_translation->is_translated()) {
    if (!shader_translator_->TranslateAnalyzedShader(*vertex_translation)) {
      XELOGE("Failed to translate vertex shader to DXBC");
      return false;
    }
  }
  if (!use_tessellation_emulation && !vertex_translation->is_valid()) {
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

  uint32_t edram_compute_fallback_mask =
      use_ordered_blend_fallback ? ordered_blend_mask : 0;

  TessellationPipelineState* tessellation_pipeline_state = nullptr;
  GeometryPipelineState* geometry_pipeline_state = nullptr;
  if (use_tessellation_emulation) {
    tessellation_pipeline_state = GetOrCreateTessellationPipelineState(
        vertex_translation, pixel_translation, primitive_processing_result,
        regs, edram_compute_fallback_mask);
    pipeline = tessellation_pipeline_state
                   ? tessellation_pipeline_state->pipeline
                   : nullptr;
  } else if (use_geometry_emulation) {
    geometry_pipeline_state = GetOrCreateGeometryPipelineState(
        vertex_translation, pixel_translation, geometry_shader_key, regs,
        edram_compute_fallback_mask);
    pipeline = geometry_pipeline_state ? geometry_pipeline_state->pipeline
                                       : nullptr;
  } else {
    pipeline = GetOrCreatePipelineState(vertex_translation, pixel_translation,
                                        regs, edram_compute_fallback_mask);
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

  struct VertexBindingRange {
    uint32_t binding_index = 0;
    uint32_t offset = 0;
    uint32_t length = 0;
    uint32_t stride = 0;
  };
  std::vector<VertexBindingRange> vertex_ranges;
  const auto& vb_bindings = vertex_shader->vertex_bindings();
  bool uses_vertex_fetch = ShaderUsesVertexFetch(*vertex_shader);

  // Sync shared memory before drawing - ensure GPU has latest data
  // This is particularly important for trace playback where memory is
  // written incrementally
  if (shared_memory_) {
    const Shader::ConstantRegisterMap& constant_map_vertex =
        vertex_shader->constant_register_map();
    for (uint32_t i = 0;
         i < xe::countof(constant_map_vertex.vertex_fetch_bitmap); ++i) {
      uint32_t vfetch_bits_remaining =
          constant_map_vertex.vertex_fetch_bitmap[i];
      uint32_t j;
      while (xe::bit_scan_forward(vfetch_bits_remaining, &j)) {
        vfetch_bits_remaining &= ~(uint32_t(1) << j);
        uint32_t vfetch_index = i * 32 + j;
        xenos::xe_gpu_vertex_fetch_t vfetch =
            regs.GetVertexFetch(vfetch_index);
        switch (vfetch.type) {
          case xenos::FetchConstantType::kVertex:
            break;
          case xenos::FetchConstantType::kInvalidVertex:
            if (::cvars::gpu_allow_invalid_fetch_constants) {
              break;
            }
            XELOGW(
                "Vertex fetch constant {} ({:08X} {:08X}) has \"invalid\" type. "
                "Use --gpu_allow_invalid_fetch_constants to bypass.",
                vfetch_index, vfetch.dword_0, vfetch.dword_1);
            return false;
          default:
            XELOGW(
                "Vertex fetch constant {} ({:08X} {:08X}) is invalid.",
                vfetch_index, vfetch.dword_0, vfetch.dword_1);
            return false;
        }
        uint32_t buffer_offset = vfetch.address << 2;
        uint32_t buffer_length = vfetch.size << 2;
        if (buffer_offset > SharedMemory::kBufferSize ||
            SharedMemory::kBufferSize - buffer_offset < buffer_length) {
          XELOGW(
              "Vertex fetch constant {} out of range (offset=0x{:08X} size={})",
              vfetch_index, buffer_offset, buffer_length);
          return false;
        }
        if (!shared_memory_->RequestRange(buffer_offset, buffer_length)) {
          XELOGE(
              "Failed to request vertex buffer at 0x{:08X} (size {}) in shared "
              "memory",
              buffer_offset, buffer_length);
          return false;
        }
      }
    }

    for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
      uint32_t base_bytes = memexport_range.base_address_dwords << 2;
      if (!shared_memory_->RequestRange(base_bytes,
                                        memexport_range.size_bytes)) {
        XELOGE(
            "Failed to request memexport stream at 0x{:08X} (size {}) in shared "
            "memory",
            base_bytes, memexport_range.size_bytes);
        return false;
      }
    }

    vertex_ranges.reserve(vb_bindings.size());
    for (const auto& binding : vb_bindings) {
      xenos::xe_gpu_vertex_fetch_t vfetch =
          regs.GetVertexFetch(binding.fetch_constant);
      uint32_t buffer_offset = vfetch.address << 2;
      uint32_t buffer_length = vfetch.size << 2;
      VertexBindingRange range;
      range.binding_index = static_cast<uint32_t>(binding.binding_index);
      range.offset = buffer_offset;
      range.length = buffer_length;
      range.stride = binding.stride_words * 4;
      vertex_ranges.push_back(range);
    }

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
  if (use_tessellation_emulation) {
    if (!tessellator_tables_buffer_) {
      XELOGE("Tessellation emulation requires tessellator tables buffer");
      return false;
    }
    current_render_encoder_->setObjectBuffer(
        tessellator_tables_buffer_, 0, kIRRuntimeTessellatorTablesBindPoint);
    current_render_encoder_->setMeshBuffer(
        tessellator_tables_buffer_, 0, kIRRuntimeTessellatorTablesBindPoint);
    current_render_encoder_->useResource(tessellator_tables_buffer_,
                                         MTL::ResourceUsageRead);
  }

  // Determine if shared memory should be UAV (for memexport).
  bool shared_memory_is_uav = memexport_used_vertex || memexport_used_pixel;
  MTL::ResourceUsage shared_memory_usage =
      shared_memory_is_uav
          ? (MTL::ResourceUsageRead | MTL::ResourceUsageWrite)
          : MTL::ResourceUsageRead;
  if (cvars::metal_log_memexport && memexport_used) {
    XELOGI("Metal memexport shared_memory_is_uav={}",
           shared_memory_is_uav ? 1 : 0);
  }

  // Bind IR Converter runtime resources.
  // The Metal Shader Converter expects resources at specific bind points.
  if (res_heap_ab_ && smp_heap_ab_ && uniforms_buffer_ && shared_memory_) {

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
      } else if (MTL::Texture* depth_tex =
                     render_target_cache_->GetDepthTarget()) {
        vp_width = depth_tex->width();
        vp_height = depth_tex->height();
      } else if (MTL::Texture* dummy =
                     render_target_cache_->GetDummyColorTarget()) {
        vp_width = dummy->width();
        vp_height = dummy->height();
      }
    }
    draw_util::ViewportInfo viewport_info;
    auto depth_control = draw_util::GetNormalizedDepthControl(regs);
    constexpr uint32_t kViewportBoundsMax = 32767;
    bool host_render_targets_used = true;
    bool convert_z_to_float24 =
        host_render_targets_used &&
        ::cvars::depth_float24_convert_in_pixel_shader;
    draw_util::GetHostViewportInfo(
        regs, 1, 1,     // draw resolution scale x/y
        true,           // origin_bottom_left (Metal NDC Y is up, like D3D)
        kViewportBoundsMax,  // x_max
        kViewportBoundsMax,  // y_max
        false,          // allow_reverse_z
        depth_control,  // normalized_depth_control
        convert_z_to_float24,          // convert_z_to_float24
        host_render_targets_used,      // full_float24_in_0_to_1
        pixel_shader && pixel_shader->writes_depth(),
        // pixel_shader_writes_depth
        viewport_info);

    // Apply per-draw viewport and scissor so the Metal viewport
    // matches the guest viewport computed by draw_util.
    draw_util::Scissor scissor = guest_scissor;
    if (!guest_scissor_valid) {
      draw_util::GetScissor(regs, scissor);
    }
    uint32_t draw_resolution_scale_x =
        texture_cache_ ? texture_cache_->draw_resolution_scale_x() : 1;
    uint32_t draw_resolution_scale_y =
        texture_cache_ ? texture_cache_->draw_resolution_scale_y() : 1;
    scissor.offset[0] *= draw_resolution_scale_x;
    scissor.offset[1] *= draw_resolution_scale_y;
    scissor.extent[0] *= draw_resolution_scale_x;
    scissor.extent[1] *= draw_resolution_scale_y;

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
    if (::cvars::metal_force_full_scissor_on_swap_resolve) {
      mtl_scissor.x = 0;
      mtl_scissor.y = 0;
      mtl_scissor.width = vp_width;
      mtl_scissor.height = vp_height;
      static bool logged_full_scissor_override = false;
      if (!logged_full_scissor_override) {
        logged_full_scissor_override = true;
        XELOGI("Metal: forcing full scissor override (debug)");
      }
    }
    current_render_encoder_->setScissorRect(mtl_scissor);

    ApplyRasterizerState(primitive_polygonal);

    // Fixed-function depth/stencil state is not part of the pipeline state in
    // Metal, so update it per draw.
    bool edram_rov_used = render_target_cache_ &&
                          render_target_cache_->GetPath() ==
                              RenderTargetCache::Path::kPixelShaderInterlock;
    if (edram_rov_used) {
      // ROV path handles depth/stencil in the shader, so disable fixed-function
      // depth/stencil state.
      current_render_encoder_->setDepthStencilState(nullptr);
    } else {
      ApplyDepthStencilState(primitive_polygonal, depth_control);
    }

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

    // Log scissor changes and deviations from full RT.
    static uint64_t scissor_draw_index = 0;
    static bool last_scissor_valid = false;
    static MTL::ScissorRect last_scissor = {};
    static int scissor_log_count = 0;
    static bool logged_full_scissor = false;
    scissor_draw_index++;
    bool is_full_scissor = mtl_scissor.x == 0 && mtl_scissor.y == 0 &&
                           mtl_scissor.width == vp_width &&
                           mtl_scissor.height == vp_height;
    bool scissor_changed = !last_scissor_valid ||
                           last_scissor.x != mtl_scissor.x ||
                           last_scissor.y != mtl_scissor.y ||
                           last_scissor.width != mtl_scissor.width ||
                           last_scissor.height != mtl_scissor.height;
    if (scissor_changed && scissor_log_count < 64) {
      last_scissor = mtl_scissor;
      last_scissor_valid = true;
      ++scissor_log_count;
      auto scissor_tl = regs.Get<reg::PA_SC_WINDOW_SCISSOR_TL>();
      auto scissor_br = regs.Get<reg::PA_SC_WINDOW_SCISSOR_BR>();
      auto screen_tl = regs.Get<reg::PA_SC_SCREEN_SCISSOR_TL>();
      auto screen_br = regs.Get<reg::PA_SC_SCREEN_SCISSOR_BR>();
      auto window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();
      uint32_t surface_pitch =
          regs.Get<reg::RB_SURFACE_INFO>().surface_pitch;
      XELOGI(
          "SCISSOR_DEBUG: draw={} RT={}x{} scissor=({},{} {}x{}) "
          "window_tl=({},{}), window_br=({},{}), screen_tl=({},{}), "
          "screen_br=({},{}), window_offset=({},{}), surface_pitch={}",
          scissor_draw_index, vp_width, vp_height, mtl_scissor.x,
          mtl_scissor.y, mtl_scissor.width, mtl_scissor.height,
          scissor_tl.tl_x, scissor_tl.tl_y, scissor_br.br_x, scissor_br.br_y,
          screen_tl.tl_x, screen_tl.tl_y, screen_br.br_x, screen_br.br_y,
          window_offset.window_x_offset, window_offset.window_y_offset,
          surface_pitch);
    }
    if (is_full_scissor && !logged_full_scissor) {
      logged_full_scissor = true;
      XELOGI("SCISSOR_DEBUG: draw={} scissor now full RT {}x{}",
             scissor_draw_index, vp_width, vp_height);
    }

    // Update full system constants from GPU registers
    // This populates flags, NDC transform, alpha test, blend constants, etc.
    uint32_t normalized_color_mask =
        pixel_shader ? draw_util::GetNormalizedColorMask(
                           regs, pixel_shader->writes_color_targets())
                     : 0;
    UpdateSystemConstantValues(
        shared_memory_is_uav, primitive_polygonal,
        primitive_processing_result.line_loop_closing_index,
        primitive_processing_result.host_shader_index_endian, viewport_info,
        used_texture_mask, depth_control, normalized_color_mask);

    static int rov_log_count = 0;
    if (rov_log_count < 8) {
      ++rov_log_count;
      XELOGI(
          "Metal ROV: enabled={} shared_memory_is_uav={} ff_depth_stencil={}",
          edram_rov_used, shared_memory_is_uav, !edram_rov_used);
    }

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
    const size_t kDescriptorTableCount = kStageCount * kDrawRingCount;
    const size_t uav_table_base_index =
        kResourceHeapSlotsPerTable * kDescriptorTableCount;
    auto* uav_entries_all = res_entries_all + uav_table_base_index;
    auto* smp_entries_all =
        reinterpret_cast<IRDescriptorTableEntry*>(smp_heap_ab_->contents());
    auto* cbv_entries_all =
        reinterpret_cast<IRDescriptorTableEntry*>(cbv_heap_ab_->contents());

    IRDescriptorTableEntry* res_entries_vertex =
        res_entries_all + table_index_vertex * kResourceHeapSlotsPerTable;
    IRDescriptorTableEntry* res_entries_pixel =
        res_entries_all + table_index_pixel * kResourceHeapSlotsPerTable;
    IRDescriptorTableEntry* uav_entries_vertex =
        uav_entries_all + table_index_vertex * kResourceHeapSlotsPerTable;
    IRDescriptorTableEntry* uav_entries_pixel =
        uav_entries_all + table_index_pixel * kResourceHeapSlotsPerTable;
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
    uint64_t uav_heap_gpu_base_vertex =
        res_heap_ab_->gpuAddress() +
        (uav_table_base_index +
         table_index_vertex * kResourceHeapSlotsPerTable) *
            sizeof(IRDescriptorTableEntry);
    uint64_t uav_heap_gpu_base_pixel =
        res_heap_ab_->gpuAddress() +
        (uav_table_base_index +
         table_index_pixel * kResourceHeapSlotsPerTable) *
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
      IRDescriptorTableSetBuffer(&uav_entries_vertex[0],
                                 shared_mem_buffer->gpuAddress(),
                                 shared_mem_buffer->length());
      IRDescriptorTableSetBuffer(&uav_entries_pixel[0],
                                 shared_mem_buffer->gpuAddress(),
                                 shared_mem_buffer->length());
      current_render_encoder_->useResource(shared_mem_buffer,
                                           shared_memory_usage);
    }
    if (render_target_cache_) {
      if (MTL::Buffer* edram_buffer =
              render_target_cache_->GetEdramBuffer()) {
        IRDescriptorTableSetBuffer(&uav_entries_vertex[1],
                                   edram_buffer->gpuAddress(),
                                   edram_buffer->length());
        IRDescriptorTableSetBuffer(&uav_entries_pixel[1],
                                   edram_buffer->gpuAddress(),
                                   edram_buffer->length());
        current_render_encoder_->useResource(
            edram_buffer,
            MTL::ResourceUsageRead | MTL::ResourceUsageWrite);
      }
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
      MetalTextureCache* metal_texture_cache = texture_cache_.get();
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
          // Use a dimension-compatible null texture to avoid Metal validation
          // errors (for example, cube-array expectations from converted
          // shaders).
          switch (binding.dimension) {
            case xenos::FetchOpDimension::k1D:
            case xenos::FetchOpDimension::k2D:
              texture = metal_texture_cache->GetNullTexture2D();
              break;
            case xenos::FetchOpDimension::k3DOrStacked:
              texture = metal_texture_cache->GetNullTexture3D();
              break;
            case xenos::FetchOpDimension::kCube:
              texture = metal_texture_cache->GetNullTextureCube();
              break;
            default:
              texture = metal_texture_cache->GetNullTexture2D();
              break;
          }
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

    auto write_top_level_and_cbvs =
        [&](size_t table_index, uint64_t res_table_gpu_base,
            uint64_t uav_table_gpu_base, uint64_t smp_table_gpu_base,
            IRDescriptorTableEntry* cbv_entries, uint64_t uniforms_gpu_base) {
      size_t top_level_offset = table_index * kTopLevelABBytesPerTable;
      auto* top_level_ptrs = reinterpret_cast<uint64_t*>(
          static_cast<uint8_t*>(top_level_ab_->contents()) + top_level_offset);
      std::memset(top_level_ptrs, 0, kTopLevelABBytesPerTable);

      for (int i = 0; i < 4; ++i) {
        top_level_ptrs[i] = res_table_gpu_base;
      }
      top_level_ptrs[4] = res_table_gpu_base;
      for (int i = 5; i < 9; ++i) {
        top_level_ptrs[i] = uav_table_gpu_base;
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
                             uav_heap_gpu_base_vertex,
                             smp_heap_gpu_base_vertex, cbv_entries_vertex,
                             uniforms_gpu_base_vertex);
    write_top_level_and_cbvs(table_index_pixel, res_heap_gpu_base_pixel,
                             uav_heap_gpu_base_pixel, smp_heap_gpu_base_pixel,
                             cbv_entries_pixel, uniforms_gpu_base_pixel);

    if (use_geometry_emulation || use_tessellation_emulation) {
      current_render_encoder_->setObjectBuffer(
          top_level_ab_, table_index_vertex * kTopLevelABBytesPerTable,
          kIRArgumentBufferBindPoint);
      current_render_encoder_->setMeshBuffer(
          top_level_ab_, table_index_vertex * kTopLevelABBytesPerTable,
          kIRArgumentBufferBindPoint);
      current_render_encoder_->setFragmentBuffer(
          top_level_ab_, table_index_pixel * kTopLevelABBytesPerTable,
          kIRArgumentBufferBindPoint);

      if (use_tessellation_emulation) {
        current_render_encoder_->setObjectBuffer(
            top_level_ab_, table_index_vertex * kTopLevelABBytesPerTable,
            kIRArgumentBufferHullDomainBindPoint);
        current_render_encoder_->setMeshBuffer(
            top_level_ab_, table_index_vertex * kTopLevelABBytesPerTable,
            kIRArgumentBufferHullDomainBindPoint);
      }

      current_render_encoder_->setObjectBuffer(res_heap_ab_, 0,
                                               kIRDescriptorHeapBindPoint);
      current_render_encoder_->setMeshBuffer(res_heap_ab_, 0,
                                             kIRDescriptorHeapBindPoint);
      current_render_encoder_->setFragmentBuffer(res_heap_ab_, 0,
                                                 kIRDescriptorHeapBindPoint);
      current_render_encoder_->setObjectBuffer(smp_heap_ab_, 0,
                                               kIRSamplerHeapBindPoint);
      current_render_encoder_->setMeshBuffer(smp_heap_ab_, 0,
                                             kIRSamplerHeapBindPoint);
      current_render_encoder_->setFragmentBuffer(smp_heap_ab_, 0,
                                                 kIRSamplerHeapBindPoint);
    } else {
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
    }

    XELOGD("Bound IR Converter resources: res_heap, smp_heap, top_level_ab");
  }

  // Bind vertex buffers / descriptors.
  if (use_geometry_emulation || use_tessellation_emulation) {
    IRRuntimeVertexBuffers vertex_buffers = {};
    MTL::Buffer* shared_mem_buffer =
        shared_memory_ ? shared_memory_->GetBuffer() : nullptr;
    if (shared_mem_buffer) {
      current_render_encoder_->useResource(shared_mem_buffer,
                                           shared_memory_usage);
      for (const auto& range : vertex_ranges) {
        size_t binding_index = range.binding_index;
        if (binding_index <
            (sizeof(vertex_buffers) / sizeof(vertex_buffers[0]))) {
          vertex_buffers[binding_index].addr =
              shared_mem_buffer->gpuAddress() + range.offset;
          vertex_buffers[binding_index].length = range.length;
          vertex_buffers[binding_index].stride = range.stride;
        }
      }
    }
    // MSC manual: bind IRRuntimeVertexBuffers at kIRVertexBufferBindPoint (6)
    // for the object stage when using geometry emulation.
    current_render_encoder_->setObjectBytes(
        vertex_buffers, sizeof(vertex_buffers), kIRVertexBufferBindPoint);
  } else if (uses_vertex_fetch) {
    // Vertex fetch shaders read directly from shared memory via SRV, so avoid
    // stage-in bindings that can trigger invalid buffer loads.
    if (shared_memory_) {
      if (MTL::Buffer* shared_mem_buffer = shared_memory_->GetBuffer()) {
        current_render_encoder_->useResource(shared_mem_buffer,
                                             shared_memory_usage);
      }
    }
  } else {
    // Bind vertex buffers at kIRVertexBufferBindPoint (index 6+) for stage-in.
    // The pipeline's vertex descriptor expects buffers at these indices,
    // populated from the vertex fetch constants. The buffer addresses come from
    // shared memory.
    if (shared_memory_ && !vb_bindings.empty()) {
      MTL::Buffer* shared_mem_buffer = shared_memory_->GetBuffer();
      if (shared_mem_buffer) {
        // Mark shared memory as used for reading
        current_render_encoder_->useResource(shared_mem_buffer,
                                             shared_memory_usage);

        // Bind vertex buffers for each binding
        static int vb_log_count = 0;
        for (const auto& range : vertex_ranges) {
          uint64_t buffer_index =
              kIRVertexBufferBindPoint + uint64_t(range.binding_index);
          current_render_encoder_->setVertexBuffer(shared_mem_buffer,
                                                   range.offset, buffer_index);
          if (vb_log_count < 5) {
            XELOGI(
                "VB_DEBUG: binding={} addr=0x{:08X} size={} stride={} -> "
                "buffer_index={}",
                range.binding_index, range.offset, range.length, range.stride,
                buffer_index);
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
                                             shared_memory_usage);
      }
    }
  }

  auto request_guest_index_range = [&](uint64_t index_base,
                                       uint32_t index_count,
                                       MTL::IndexType index_type) -> bool {
    if (!shared_memory_) {
      return false;
    }
    uint32_t index_stride =
        (index_type == MTL::IndexTypeUInt16) ? sizeof(uint16_t)
                                             : sizeof(uint32_t);
    uint64_t index_length = uint64_t(index_count) * index_stride;
    if (index_base > SharedMemory::kBufferSize ||
        SharedMemory::kBufferSize - index_base < index_length) {
      XELOGW(
          "Index buffer range out of bounds (base=0x{:08X} size={} count={})",
          static_cast<uint32_t>(index_base), index_length, index_count);
      return false;
    }
    return shared_memory_->RequestRange(static_cast<uint32_t>(index_base),
                                        static_cast<uint32_t>(index_length));
  };

  if (use_tessellation_emulation) {
    IRRuntimePrimitiveType tess_primitive =
        IRRuntimePrimitiveTypeTriangle;
    switch (primitive_processing_result.host_primitive_type) {
      case xenos::PrimitiveType::kTriangleList:
        tess_primitive = IRRuntimePrimitiveType3ControlPointPatchlist;
        break;
      case xenos::PrimitiveType::kQuadList:
        tess_primitive = IRRuntimePrimitiveType4ControlPointPatchlist;
        break;
      case xenos::PrimitiveType::kTrianglePatch:
        tess_primitive =
            (primitive_processing_result.tessellation_mode ==
             xenos::TessellationMode::kAdaptive)
                ? IRRuntimePrimitiveType3ControlPointPatchlist
                : IRRuntimePrimitiveType1ControlPointPatchlist;
        break;
      case xenos::PrimitiveType::kQuadPatch:
        tess_primitive =
            (primitive_processing_result.tessellation_mode ==
             xenos::TessellationMode::kAdaptive)
                ? IRRuntimePrimitiveType4ControlPointPatchlist
                : IRRuntimePrimitiveType1ControlPointPatchlist;
        break;
      default:
        XELOGE(
            "Host tessellated primitive type {} returned by the primitive "
            "processor is not supported by the Metal tessellation path",
            uint32_t(primitive_processing_result.host_primitive_type));
        return false;
    }

    const IRRuntimeTessellationPipelineConfig& tess_config =
        tessellation_pipeline_state->config;

    if (primitive_processing_result.index_buffer_type ==
        PrimitiveProcessor::ProcessedIndexBufferType::kNone) {
      IRRuntimeDrawPatchesTessellationEmulation(
          current_render_encoder_, tess_primitive, tess_config, 1,
          primitive_processing_result.host_draw_vertex_count, 0, 0);
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
          if (!request_guest_index_range(index_offset,
                                         primitive_processing_result
                                             .host_draw_vertex_count,
                                         index_type)) {
            XELOGE(
                "IssueDraw: failed to validate guest index buffer range for "
                "tessellation");
            return false;
          }
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
          XELOGE("Unsupported index buffer type {} for tessellation",
                 uint32_t(primitive_processing_result.index_buffer_type));
          return false;
      }
      if (!index_buffer) {
        XELOGE("IssueDraw: index buffer is null for tessellation");
        return false;
      }
      current_render_encoder_->useResource(index_buffer,
                                           MTL::ResourceUsageRead);
      uint32_t index_stride =
          (index_type == MTL::IndexTypeUInt16) ? sizeof(uint16_t)
                                               : sizeof(uint32_t);
      uint32_t start_index =
          index_stride ? uint32_t(index_offset / index_stride) : 0;
      IRRuntimeDrawIndexedPatchesTessellationEmulation(
          current_render_encoder_, tess_primitive, index_type, index_buffer,
          tess_config, 1, primitive_processing_result.host_draw_vertex_count, 0,
          0, start_index);
    }
  } else if (use_geometry_emulation) {
    IRRuntimePrimitiveType geometry_primitive =
        IRRuntimePrimitiveTypeTriangle;
    switch (primitive_processing_result.host_primitive_type) {
      case xenos::PrimitiveType::kPointList:
        geometry_primitive = IRRuntimePrimitiveTypePoint;
        break;
      case xenos::PrimitiveType::kRectangleList:
        geometry_primitive = IRRuntimePrimitiveTypeTriangle;
        break;
      case xenos::PrimitiveType::kQuadList:
        geometry_primitive = IRRuntimePrimitiveTypeLineWithAdj;
        break;
      default:
        XELOGE(
            "Host primitive type {} returned by the primitive processor is not "
            "supported by the Metal geometry path",
            uint32_t(primitive_processing_result.host_primitive_type));
        return false;
    }

    IRRuntimeGeometryPipelineConfig geometry_config = {};
    geometry_config.gsVertexSizeInBytes =
        geometry_pipeline_state->gs_vertex_size_in_bytes;
    geometry_config.gsMaxInputPrimitivesPerMeshThreadgroup =
        geometry_pipeline_state->gs_max_input_primitives_per_mesh_threadgroup;

    if (primitive_processing_result.index_buffer_type ==
        PrimitiveProcessor::ProcessedIndexBufferType::kNone) {
      IRRuntimeDrawPrimitivesGeometryEmulation(
          current_render_encoder_, geometry_primitive, geometry_config, 1,
          primitive_processing_result.host_draw_vertex_count, 0, 0);
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
          if (!request_guest_index_range(index_offset,
                                         primitive_processing_result
                                             .host_draw_vertex_count,
                                         index_type)) {
            XELOGE("IssueDraw: failed to validate guest index buffer range");
            return false;
          }
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
      current_render_encoder_->useResource(index_buffer,
                                           MTL::ResourceUsageRead);
      uint32_t index_stride =
          (index_type == MTL::IndexTypeUInt16) ? sizeof(uint16_t)
                                               : sizeof(uint32_t);
      uint32_t start_index =
          index_stride ? uint32_t(index_offset / index_stride) : 0;
      IRRuntimeDrawIndexedPrimitivesGeometryEmulation(
          current_render_encoder_, geometry_primitive, index_type, index_buffer,
          geometry_config, 1, primitive_processing_result.host_draw_vertex_count,
          start_index, 0, 0);
    }
  } else {
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
          if (!request_guest_index_range(index_offset,
                                         primitive_processing_result
                                             .host_draw_vertex_count,
                                         index_type)) {
            XELOGE("IssueDraw: failed to validate guest index buffer range");
            return false;
          }
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
  }

  XELOGI(
      "IssueDraw: guest_prim={} guest_count={} ib_info={} host_prim={} "
      "host_count={} idx_type={}",
      uint32_t(primitive_type), index_count, index_buffer_info != nullptr,
      uint32_t(primitive_processing_result.host_primitive_type),
      primitive_processing_result.host_draw_vertex_count,
      uint32_t(primitive_processing_result.index_buffer_type));

  if (use_ordered_blend_fallback && ordered_blend_mask && render_target_cache_) {
    if (!guest_scissor_valid) {
      static bool logged_missing_scissor = false;
      if (!logged_missing_scissor) {
        logged_missing_scissor = true;
        XELOGW(
            "Metal: ordered blend fallback skipped (guest scissor unavailable)");
      }
    } else {
      EndRenderEncoder();
      MTL::CommandBuffer* cmd = EnsureCommandBuffer();
      if (cmd) {
        for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
          if (!(ordered_blend_mask & (1u << i))) {
            continue;
          }
          uint32_t rt_write_mask =
              (normalized_color_mask >> (i * 4)) & 0xF;
          if (!rt_write_mask) {
            continue;
          }
          auto* rt = render_target_cache_->GetColorRenderTarget(i);
          if (!rt) {
            continue;
          }
          render_target_cache_->BlendRenderTargetToEdramRect(
              rt, i, rt_write_mask, guest_scissor.offset[0],
              guest_scissor.offset[1], guest_scissor.extent[0],
              guest_scissor.extent[1], cmd);
        }
      }
    }
  }

  if (memexport_used && shared_memory_) {
    for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
      shared_memory_->RangeWrittenByGpu(
          memexport_range.base_address_dwords << 2, memexport_range.size_bytes,
          false);
    }
  }

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

  if (!current_command_buffer_) {
    if (!command_queue_) {
      XELOGE("MetalCommandProcessor::IssueCopy: no command queue");
      return false;
    }
    // Note: commandBuffer() returns an autoreleased object, we must retain it.
    current_command_buffer_ = command_queue_->commandBuffer();
    if (!current_command_buffer_) {
      XELOGE(
          "MetalCommandProcessor::IssueCopy: failed to create command buffer");
      return false;
    }
    current_command_buffer_->retain();
    current_command_buffer_->setLabel(
        NS::String::string("XeniaCopyCommandBuffer", NS::UTF8StringEncoding));
  }

  if (!render_target_cache_) {
    XELOGW("MetalCommandProcessor::IssueCopy - No render target cache");
    return true;
  }

  uint32_t written_address = 0;
  uint32_t written_length = 0;

  if (!render_target_cache_->Resolve(*memory_, written_address,
                                     written_length,
                                     current_command_buffer_)) {
    XELOGE("MetalCommandProcessor::IssueCopy - Resolve failed");
    return false;
  }

  if (!written_length) {
    XELOGI("MetalCommandProcessor::IssueCopy - Resolve wrote no data");
    // Commit any in-flight work so ordering matches D3D12 submission behavior.
    ScheduleDrawRingRelease(current_command_buffer_);
    current_command_buffer_->commit();
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
    SetActiveDrawRing(nullptr);
    current_draw_index_ = 0;
    return true;
  }

  XELOGI("MetalCommandProcessor::IssueCopy - Completed: {} bytes at 0x{:08X}",
         written_length, written_address);

  // Track this resolved region so the trace player can avoid overwriting it
  // with stale MemoryRead commands from the trace file.
  MarkResolvedMemory(written_address, written_length);

  // The resolve writes into guest memory via the shared memory buffer.
  // Any cached views of this memory (especially textures sourced from it)
  // must be invalidated, otherwise subsequent render-to-texture / postprocess
  // passes will sample stale host textures and produce corrupted output.
//   if (shared_memory_) {
//     shared_memory_->MemoryInvalidationCallback(written_address, written_length, true);
//   }
//   if (primitive_processor_) {
//     primitive_processor_->MemoryInvalidationCallback(written_address, written_length, true);
//   }
  

  // Submit the command buffer without waiting - the resolve writes are now
  // ordered in the same submission as the preceding draws.
  ScheduleDrawRingRelease(current_command_buffer_);
  current_command_buffer_->commit();
  current_command_buffer_->release();
  current_command_buffer_ = nullptr;
  SetActiveDrawRing(nullptr);
  current_draw_index_ = 0;

  return true;
}

void MetalCommandProcessor::OnGammaRamp256EntryTableValueWritten() {
  gamma_ramp_256_entry_table_up_to_date_ = false;
}

void MetalCommandProcessor::OnGammaRampPWLValueWritten() {
  gamma_ramp_pwl_up_to_date_ = false;
}

void MetalCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  if (::cvars::metal_log_copy_dest_register_writes) {
    static uint32_t copy_dest_base_log_count = 0;
    static uint32_t copy_dest_info_log_count = 0;
    static uint32_t copy_dest_pitch_log_count = 0;
    static uint32_t copy_control_log_count = 0;
    static uint32_t color_info_log_count = 0;
    constexpr uint32_t kMaxLogCount = 64;
    if (index == XE_GPU_REG_RB_COPY_DEST_BASE &&
        copy_dest_base_log_count < kMaxLogCount) {
      ++copy_dest_base_log_count;
      XELOGI("MetalRegWrite RB_COPY_DEST_BASE=0x{:08X}", value);
    } else if (index == XE_GPU_REG_RB_COPY_DEST_INFO &&
        copy_dest_info_log_count < kMaxLogCount) {
      ++copy_dest_info_log_count;
      reg::RB_COPY_DEST_INFO info;
      info.value = value;
      XELOGI(
          "MetalRegWrite RB_COPY_DEST_INFO=0x{:08X} endian={} array={} "
          "slice={} format={} number={} exp_bias={} swap={}",
          value, uint32_t(info.copy_dest_endian), info.copy_dest_array ? 1 : 0,
          uint32_t(info.copy_dest_slice), uint32_t(info.copy_dest_format),
          uint32_t(info.copy_dest_number), int(info.copy_dest_exp_bias),
          info.copy_dest_swap ? 1 : 0);
    } else if (index == XE_GPU_REG_RB_COPY_DEST_PITCH &&
               copy_dest_pitch_log_count < kMaxLogCount) {
      ++copy_dest_pitch_log_count;
      reg::RB_COPY_DEST_PITCH pitch;
      pitch.value = value;
      XELOGI(
          "MetalRegWrite RB_COPY_DEST_PITCH=0x{:08X} pitch={} height={}",
          value, uint32_t(pitch.copy_dest_pitch),
          uint32_t(pitch.copy_dest_height));
    } else if (index == XE_GPU_REG_RB_COPY_CONTROL &&
               copy_control_log_count < kMaxLogCount) {
      ++copy_control_log_count;
      reg::RB_COPY_CONTROL control;
      control.value = value;
      XELOGI(
          "MetalRegWrite RB_COPY_CONTROL=0x{:08X} src_select={} "
          "sample_select={} color_clear={} depth_clear={} copy_command={}",
          value, uint32_t(control.copy_src_select),
          uint32_t(control.copy_sample_select),
          control.color_clear_enable ? 1 : 0,
          control.depth_clear_enable ? 1 : 0,
          uint32_t(control.copy_command));
    } else if (color_info_log_count < kMaxLogCount) {
      for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
        if (index != reg::RB_COLOR_INFO::rt_register_indices[i]) {
          continue;
        }
        ++color_info_log_count;
        reg::RB_COLOR_INFO color_info;
        color_info.value = value;
        uint32_t color_base =
            color_info.color_base | (color_info.color_base_bit_11 << 11);
        XELOGI(
            "MetalRegWrite RB_COLOR{}_INFO=0x{:08X} base_tiles={} "
            "format={} exp_bias={}",
            i, value, color_base, uint32_t(color_info.color_format),
            int(color_info.color_exp_bias));
        break;
      }
    }
  }

  CommandProcessor::WriteRegister(index, value);

  if (index >= XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 &&
      index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {
    if (texture_cache_) {
      texture_cache_->TextureFetchConstantWritten(
          (index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) / 6);
    }
  }
}

MTL::CommandBuffer* MetalCommandProcessor::EnsureCommandBuffer() {
  if (current_command_buffer_) {
    return current_command_buffer_;
  }
  if (!command_queue_) {
    XELOGE("EnsureCommandBuffer: no command queue");
    return nullptr;
  }

  // Note: commandBuffer() returns an autoreleased object, we must retain it.
  current_command_buffer_ = command_queue_->commandBuffer();
  if (!current_command_buffer_) {
    XELOGE("EnsureCommandBuffer: failed to create command buffer");
    return nullptr;
  }
  current_command_buffer_->retain();
  current_command_buffer_->setLabel(
      NS::String::string("XeniaCommandBuffer", NS::UTF8StringEncoding));

  if (primitive_processor_ && !frame_open_) {
    primitive_processor_->BeginFrame();
    frame_open_ = true;
  }

  return current_command_buffer_;
}

void MetalCommandProcessor::EndRenderEncoder() {
  if (!current_render_encoder_) {
    return;
  }
  current_render_encoder_->endEncoding();
  current_render_encoder_->release();
  current_render_encoder_ = nullptr;
  current_render_pass_descriptor_ = nullptr;
}

void MetalCommandProcessor::BeginCommandBuffer() {
  if (!EnsureCommandBuffer()) {
    return;
  }

  EnsureActiveDrawRing();

  // Obtain the render pass descriptor. Prefer the one provided by
  // MetalRenderTargetCache (host render-target path), falling back to the
  // legacy descriptor if needed.
  MTL::RenderPassDescriptor* pass_descriptor = render_pass_descriptor_;
  if (render_target_cache_) {
    if (MTL::RenderPassDescriptor* cache_desc =
            render_target_cache_->GetRenderPassDescriptor(1)) {
      pass_descriptor = cache_desc;
    }
  }
  if (!pass_descriptor) {
    XELOGE("BeginCommandBuffer: No render pass descriptor available");
    return;
  }

  // Detect Reverse-Z usage and update clear depth.
  if (register_file_) {
    auto depth_control = register_file_->Get<reg::RB_DEPTHCONTROL>();
    bool reverse_z =
        depth_control.z_enable &&
        (depth_control.zfunc == xenos::CompareFunction::kGreater ||
         depth_control.zfunc == xenos::CompareFunction::kGreaterEqual);
    if (auto* da = pass_descriptor->depthAttachment()) {
      if (reverse_z) {
        XELOGI("BeginCommandBuffer: Reverse-Z detected (zfunc={}), setting ClearDepth to 0.0",
               int(depth_control.zfunc));
        da->setClearDepth(0.0);
      } else {
        da->setClearDepth(1.0);
      }
    }
  }

  // If the render pass configuration has changed since the current render
  // encoder was created (e.g. dummy RT0 -> real RTs, depth/stencil binding),
  // restart the render encoder with the updated descriptor.
  if (current_render_encoder_ &&
      current_render_pass_descriptor_ != pass_descriptor) {
    current_render_encoder_->endEncoding();
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
    current_render_pass_descriptor_ = nullptr;
  }

  if (!current_render_encoder_) {
    // Note: renderCommandEncoder() returns an autoreleased object, we must
    // retain it.
    current_render_encoder_ =
        current_command_buffer_->renderCommandEncoder(pass_descriptor);
    if (!current_render_encoder_) {
      XELOGE("Failed to create render command encoder");
      return;
    }
    current_render_encoder_->retain();
    current_render_encoder_->setLabel(
        NS::String::string("XeniaRenderEncoder", NS::UTF8StringEncoding));
    ff_blend_factor_valid_ = false;
    current_render_pass_descriptor_ = pass_descriptor;
  }

  // Derive viewport/scissor from the actual bound render target rather than
  // a hard-coded 1280x720. Prefer color RT 0 from the MetalRenderTargetCache,
  // falling back to depth (depth-only passes) and then legacy
  // render_target_width_/height_ when needed.
  uint32_t rt_width = render_target_width_;
  uint32_t rt_height = render_target_height_;
  if (render_target_cache_) {
    MTL::Texture* pass_size_texture = render_target_cache_->GetColorTarget(0);
    if (!pass_size_texture) {
      pass_size_texture = render_target_cache_->GetDepthTarget();
    }
    if (!pass_size_texture) {
      pass_size_texture = render_target_cache_->GetDummyColorTarget();
    }
    if (pass_size_texture) {
      rt_width = static_cast<uint32_t>(pass_size_texture->width());
      rt_height = static_cast<uint32_t>(pass_size_texture->height());
      XELOGI("BeginCommandBuffer: using pass size {}x{} for viewport/scissor",
             rt_width, rt_height);
    } else {
      XELOGI(
          "BeginCommandBuffer: no pass texture, falling back to {}x{} for "
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

void MetalCommandProcessor::EnsureDrawRingCapacity() {
  if (current_draw_index_ < kDrawRingCount) {
    return;
  }

  auto ring = AcquireDrawRingBuffers();
  if (!ring) {
    XELOGE("Metal draw ring exhausted but failed to allocate a new ring");
    return;
  }

  XELOGW("Metal draw ring exhausted ({} draws); switching ring page",
         current_draw_index_);
  SetActiveDrawRing(ring);
  command_buffer_draw_rings_.push_back(ring);
  current_draw_index_ = 0;
}

void MetalCommandProcessor::EndCommandBuffer() {
  XELOGI("MetalCommandProcessor::EndCommandBuffer: called");
  if (current_render_encoder_) {
    current_render_encoder_->endEncoding();
    current_render_encoder_->release();
    current_render_encoder_ = nullptr;
    current_render_pass_descriptor_ = nullptr;
  }

  if (current_command_buffer_) {
    ScheduleDrawRingRelease(current_command_buffer_);
    current_command_buffer_->commit();
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
    SetActiveDrawRing(nullptr);
    current_draw_index_ = 0;
  }

  FlushEdramFromHostRenderTargetsIfEnabled("EndCommandBuffer");
}

void MetalCommandProcessor::FlushEdramFromHostRenderTargetsIfEnabled(
    const char* reason) {
  if (!render_target_cache_ || !::cvars::metal_edram_rov ||
      !::cvars::metal_edram_compute_fallback ||
      render_target_cache_->GetPath() !=
          RenderTargetCache::Path::kHostRenderTargets) {
    return;
  }
  XELOGI(
      "MetalCommandProcessor: flushing EDRAM from host RTs (reason={})",
      reason ? reason : "unknown");
  render_target_cache_->FlushEdramFromHostRenderTargets();
}

void MetalCommandProcessor::ApplyDepthStencilState(
    bool primitive_polygonal, reg::RB_DEPTHCONTROL normalized_depth_control) {
  if (!current_render_encoder_ || !device_) {
    return;
  }

  const RegisterFile& regs = *register_file_;
  auto stencil_ref_mask_front = regs.Get<reg::RB_STENCILREFMASK>();
  auto stencil_ref_mask_back =
      regs.Get<reg::RB_STENCILREFMASK>(XE_GPU_REG_RB_STENCILREFMASK_BF);

  DepthStencilStateKey key;
  key.depth_control = normalized_depth_control.value;
  key.stencil_ref_mask_front = stencil_ref_mask_front.value;
  key.stencil_ref_mask_back = stencil_ref_mask_back.value;
  key.polygonal_and_backface =
      (primitive_polygonal ? 1u : 0u) |
      (normalized_depth_control.backface_enable ? 2u : 0u);

  MTL::DepthStencilState* state = nullptr;
  auto it = depth_stencil_state_cache_.find(key);
  if (it != depth_stencil_state_cache_.end()) {
    state = it->second;
  } else {
    MTL::DepthStencilDescriptor* ds_desc =
        MTL::DepthStencilDescriptor::alloc()->init();
    if (normalized_depth_control.z_enable) {
      ds_desc->setDepthCompareFunction(
          ToMetalCompareFunction(normalized_depth_control.zfunc));
      ds_desc->setDepthWriteEnabled(
          normalized_depth_control.z_write_enable != 0);
    } else {
      ds_desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
      ds_desc->setDepthWriteEnabled(false);
    }

    if (normalized_depth_control.stencil_enable) {
      auto* front = MTL::StencilDescriptor::alloc()->init();
      front->setStencilCompareFunction(
          ToMetalCompareFunction(normalized_depth_control.stencilfunc));
      front->setStencilFailureOperation(
          ToMetalStencilOperation(normalized_depth_control.stencilfail));
      front->setDepthFailureOperation(
          ToMetalStencilOperation(normalized_depth_control.stencilzfail));
      front->setDepthStencilPassOperation(
          ToMetalStencilOperation(normalized_depth_control.stencilzpass));
      front->setReadMask(stencil_ref_mask_front.stencilmask);
      front->setWriteMask(stencil_ref_mask_front.stencilwritemask);

      ds_desc->setFrontFaceStencil(front);

      if (primitive_polygonal && normalized_depth_control.backface_enable) {
        auto* back = MTL::StencilDescriptor::alloc()->init();
        back->setStencilCompareFunction(
            ToMetalCompareFunction(normalized_depth_control.stencilfunc_bf));
        back->setStencilFailureOperation(
            ToMetalStencilOperation(normalized_depth_control.stencilfail_bf));
        back->setDepthFailureOperation(
            ToMetalStencilOperation(normalized_depth_control.stencilzfail_bf));
        back->setDepthStencilPassOperation(
            ToMetalStencilOperation(normalized_depth_control.stencilzpass_bf));
        back->setReadMask(stencil_ref_mask_back.stencilmask);
        back->setWriteMask(stencil_ref_mask_back.stencilwritemask);
        ds_desc->setBackFaceStencil(back);
        back->release();
      } else {
        ds_desc->setBackFaceStencil(front);
      }

      front->release();
    }

    state = device_->newDepthStencilState(ds_desc);
    ds_desc->release();

    if (!state) {
      XELOGE("Failed to create Metal depth/stencil state");
      return;
    }
    depth_stencil_state_cache_.emplace(key, state);
  }

  current_render_encoder_->setDepthStencilState(state);

  if (normalized_depth_control.stencil_enable) {
    uint32_t ref_front = stencil_ref_mask_front.stencilref;
    uint32_t ref_back = stencil_ref_mask_back.stencilref;
    auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
    uint32_t ref = ref_front;
    if (primitive_polygonal && normalized_depth_control.backface_enable &&
        pa_su_sc_mode_cntl.cull_front && !pa_su_sc_mode_cntl.cull_back) {
      ref = ref_back;
    } else if (primitive_polygonal && normalized_depth_control.backface_enable &&
               ref_front != ref_back) {
      static bool mismatch_logged = false;
      if (!mismatch_logged) {
        mismatch_logged = true;
        XELOGW(
            "Metal: front/back stencil ref differ (front={}, back={}); using "
            "front for both",
            ref_front, ref_back);
      }
    }
    current_render_encoder_->setStencilReferenceValue(ref);
  }
}

void MetalCommandProcessor::ApplyRasterizerState(bool primitive_polygonal) {
  if (!current_render_encoder_ || !render_target_cache_) {
    return;
  }

  const RegisterFile& regs = *register_file_;
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  auto pa_cl_clip_cntl = regs.Get<reg::PA_CL_CLIP_CNTL>();

  MTL::CullMode cull_mode = MTL::CullModeNone;
  if (primitive_polygonal) {
    bool cull_front = pa_su_sc_mode_cntl.cull_front;
    bool cull_back = pa_su_sc_mode_cntl.cull_back;
    if (cull_front && !cull_back) {
      cull_mode = MTL::CullModeFront;
    } else if (cull_back && !cull_front) {
      cull_mode = MTL::CullModeBack;
    }
  }
  current_render_encoder_->setCullMode(cull_mode);

  current_render_encoder_->setFrontFacingWinding(
      pa_su_sc_mode_cntl.face ? MTL::WindingClockwise
                              : MTL::WindingCounterClockwise);

  MTL::TriangleFillMode fill_mode = MTL::TriangleFillModeFill;
  if (primitive_polygonal &&
      pa_su_sc_mode_cntl.poly_mode == xenos::PolygonModeEnable::kDualMode) {
    xenos::PolygonType polygon_type = xenos::PolygonType::kTriangles;
    if (!pa_su_sc_mode_cntl.cull_front) {
      polygon_type =
          std::min(polygon_type, pa_su_sc_mode_cntl.polymode_front_ptype);
    }
    if (!pa_su_sc_mode_cntl.cull_back) {
      polygon_type =
          std::min(polygon_type, pa_su_sc_mode_cntl.polymode_back_ptype);
    }
    if (polygon_type != xenos::PolygonType::kTriangles) {
      fill_mode = MTL::TriangleFillModeLines;
    }
  }
  current_render_encoder_->setTriangleFillMode(fill_mode);

  if (render_target_cache_->GetPath() ==
      RenderTargetCache::Path::kHostRenderTargets) {
    float polygon_offset_scale = 0.0f;
    float polygon_offset = 0.0f;
    draw_util::GetPreferredFacePolygonOffset(regs, primitive_polygonal,
                                             polygon_offset_scale,
                                             polygon_offset);
    float depth_bias_factor =
        regs.Get<reg::RB_DEPTH_INFO>().depth_format ==
                xenos::DepthRenderTargetFormat::kD24S8
            ? draw_util::kD3D10PolygonOffsetFactorUnorm24
            : draw_util::kD3D10PolygonOffsetFactorFloat24;
    float depth_bias_constant = polygon_offset * depth_bias_factor;
    float depth_bias_slope =
        polygon_offset_scale * xenos::kPolygonOffsetScaleSubpixelUnit *
        float(std::max(render_target_cache_->draw_resolution_scale_x(),
                       render_target_cache_->draw_resolution_scale_y()));
    current_render_encoder_->setDepthBias(depth_bias_constant, depth_bias_slope,
                                          0.0f);
  }

  current_render_encoder_->setDepthClipMode(
      pa_cl_clip_cntl.clip_disable ? MTL::DepthClipModeClamp
                                   : MTL::DepthClipModeClip);
}

MTL::RenderPassDescriptor*
MetalCommandProcessor::GetCurrentRenderPassDescriptor() {
  return render_pass_descriptor_;
}

MTL::RenderPipelineState* MetalCommandProcessor::GetOrCreatePipelineState(
    MetalShader::MetalTranslation* vertex_translation,
    MetalShader::MetalTranslation* pixel_translation,
    const RegisterFile& regs, uint32_t edram_compute_fallback_mask) {
  if (!vertex_translation || !vertex_translation->metal_function()) {
    XELOGE("No valid vertex shader function");
    return nullptr;
  }

  // Determine attachment formats and sample count from the render target cache
  // so the pipeline matches the actual render pass. If no real RT is bound,
  // fall back to the dummy RT0 format used by the cache.
  bool edram_rov_used = render_target_cache_ &&
                        render_target_cache_->GetPath() ==
                            RenderTargetCache::Path::kPixelShaderInterlock;
  uint32_t sample_count = 1;
  MTL::PixelFormat color_formats[4] = {
      MTL::PixelFormatInvalid, MTL::PixelFormatInvalid, MTL::PixelFormatInvalid,
      MTL::PixelFormatInvalid};
  MTL::PixelFormat coverage_format = MTL::PixelFormatInvalid;
  MTL::PixelFormat depth_format = MTL::PixelFormatInvalid;
  MTL::PixelFormat stencil_format = MTL::PixelFormatInvalid;
  if (render_target_cache_) {
    for (uint32_t i = 0; i < 4; ++i) {
      if (MTL::Texture* rt = render_target_cache_->GetColorTargetForDraw(i)) {
        color_formats[i] = rt->pixelFormat();
        if (rt->sampleCount() > 0) {
          sample_count = std::max<uint32_t>(
              sample_count, static_cast<uint32_t>(rt->sampleCount()));
        }
      }
    }
    if (color_formats[0] == MTL::PixelFormatInvalid) {
      if (MTL::Texture* dummy =
              render_target_cache_->GetDummyColorTargetForDraw()) {
        color_formats[0] = dummy->pixelFormat();
        if (dummy->sampleCount() > 0) {
          sample_count = std::max<uint32_t>(
              sample_count, static_cast<uint32_t>(dummy->sampleCount()));
        }
      }
    }
    if (MTL::Texture* depth_tex =
            render_target_cache_->GetDepthTargetForDraw()) {
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
    if (render_target_cache_->IsOrderedBlendCoverageActive()) {
      if (MTL::Texture* coverage_tex =
              render_target_cache_->GetOrderedBlendCoverageTexture()) {
        coverage_format = coverage_tex->pixelFormat();
        if (coverage_tex->sampleCount() > 0) {
          sample_count = std::max<uint32_t>(
              sample_count, static_cast<uint32_t>(coverage_tex->sampleCount()));
        }
      } else {
        coverage_format = MTL::PixelFormatR8Unorm;
      }
    }
  }

  struct PipelineKey {
    const void* vs;
    const void* ps;
    uint32_t edram_rov_used;
    uint32_t edram_compute_fallback_mask;
    uint32_t sample_count;
    uint32_t depth_format;
    uint32_t stencil_format;
    uint32_t color_formats[4];
    uint32_t coverage_format;
    uint32_t normalized_color_mask;
    uint32_t alpha_to_mask_enable;
    uint32_t blendcontrol[4];
  } key_data = {};
  key_data.vs = vertex_translation;
  key_data.ps = pixel_translation;
  key_data.edram_rov_used = edram_rov_used ? 1 : 0;
  key_data.edram_compute_fallback_mask = edram_compute_fallback_mask;
  key_data.sample_count = sample_count;
  key_data.depth_format = uint32_t(depth_format);
  key_data.stencil_format = uint32_t(stencil_format);
  for (uint32_t i = 0; i < 4; ++i) {
    key_data.color_formats[i] = uint32_t(color_formats[i]);
  }
  key_data.coverage_format = uint32_t(coverage_format);
  uint32_t pixel_shader_writes_color_targets =
      pixel_translation ? pixel_translation->shader().writes_color_targets()
                        : 0;
  key_data.normalized_color_mask = 0;
  if (!edram_rov_used && pixel_shader_writes_color_targets) {
    key_data.normalized_color_mask =
        draw_util::GetNormalizedColorMask(regs,
                                          pixel_shader_writes_color_targets);
  }
  auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  key_data.alpha_to_mask_enable = rb_colorcontrol.alpha_to_mask_enable ? 1 : 0;
  for (uint32_t i = 0; i < 4; ++i) {
    key_data.blendcontrol[i] = 0;
    if (!edram_rov_used) {
      key_data.blendcontrol[i] =
          regs.Get<reg::RB_BLENDCONTROL>(
                  reg::RB_BLENDCONTROL::rt_register_indices[i])
              .value;
    }
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
  if (coverage_format != MTL::PixelFormatInvalid) {
    desc->colorAttachments()
        ->object(MetalRenderTargetCache::kOrderedBlendCoverageAttachmentIndex)
        ->setPixelFormat(coverage_format);
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

    if (edram_rov_used) {
      color_attachment->setWriteMask(MTL::ColorWriteMaskNone);
      color_attachment->setBlendingEnabled(false);
      continue;
    }

    uint32_t rt_write_mask = (key_data.normalized_color_mask >> (i * 4)) & 0xF;
    if (key_data.edram_compute_fallback_mask & (1u << i)) {
      color_attachment->setWriteMask(
          rt_write_mask ? MTL::ColorWriteMaskAll : MTL::ColorWriteMaskNone);
      color_attachment->setBlendingEnabled(false);
      continue;
    }
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
  if (coverage_format != MTL::PixelFormatInvalid) {
    auto* coverage_attachment = desc->colorAttachments()->object(
        MetalRenderTargetCache::kOrderedBlendCoverageAttachmentIndex);
    coverage_attachment->setWriteMask(MTL::ColorWriteMaskAll);
    coverage_attachment->setBlendingEnabled(false);
  }

  // Configure vertex fetch layout for MSC stage-in.
  // NOTE: The translated shaders use vfetch (buffer load) to read vertices
  // directly from shared memory via SRV descriptors, NOT stage-in attributes.
  // This vertex descriptor may be unnecessary.
  const Shader& vertex_shader_ref = vertex_translation->shader();
  const auto& vertex_bindings = vertex_shader_ref.vertex_bindings();
  if (!ShaderUsesVertexFetch(vertex_shader_ref) && !vertex_bindings.empty()) {
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
      bool used_any_attribute = false;

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
        used_any_attribute = true;
        ++attr_index;
      }

      if (used_any_attribute) {
        auto layout = vertex_desc->layouts()->object(buffer_index);
        layout->setStride(binding.stride_words * 4);
        layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
        layout->setStepRate(1);
      }
    }

    desc->setVertexDescriptor(vertex_desc);
    vertex_desc->release();
  }

  // Create pipeline state
  NS::Error* error = nullptr;
  MTL::RenderPipelineState* pipeline = nullptr;
  MTL::RenderPipelineReflection* reflection = nullptr;
  MTL::PipelineOption options = MTL::PipelineOptionNone;
  if (edram_rov_used) {
    options = MTL::PipelineOptionArgumentInfo;
  }
  pipeline = device_->newRenderPipelineState(desc, options, &reflection, &error);
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
  if (edram_rov_used && reflection) {
    NS::Array* fragment_args = reflection->fragmentArguments();
    if (fragment_args) {
      for (NS::UInteger i = 0; i < fragment_args->count(); ++i) {
        auto* arg =
            static_cast<MTL::Argument*>(fragment_args->object(i));
        if (!arg || !arg->name()) {
          continue;
        }
        const char* name = arg->name()->utf8String();
        if (name && std::strstr(name, "xe_edram")) {
          XELOGI("Metal ROV: fragment arg '{}' access={}", name,
                 static_cast<int>(arg->access()));
        }
      }
    }
  }

  // Log pipeline creation for debugging
  static int pipeline_count = 0;
  pipeline_count++;
  XELOGI("GPU DEBUG: Created pipeline #{} (VS={}, PS={})", pipeline_count,
         vertex_translation->metal_function() ? "valid" : "null",
         (pixel_translation && pixel_translation->metal_function()) ? "valid"
                                                                    : "null");
  if (edram_rov_used) {
    XELOGI("GPU DEBUG: Pipeline #{} uses ROV path (fixed-function blending off)",
           pipeline_count);
  }

  return pipeline;
}

MetalCommandProcessor::GeometryPipelineState*
MetalCommandProcessor::GetOrCreateGeometryPipelineState(
    MetalShader::MetalTranslation* vertex_translation,
    MetalShader::MetalTranslation* pixel_translation,
    GeometryShaderKey geometry_shader_key, const RegisterFile& regs,
    uint32_t edram_compute_fallback_mask) {
  if (!vertex_translation) {
    XELOGE("No valid vertex shader translation for geometry pipeline");
    return nullptr;
  }
  bool use_fallback_pixel_shader = (pixel_translation == nullptr);
  MTL::Library* pixel_library =
      use_fallback_pixel_shader ? nullptr : pixel_translation->metal_library();
  const char* pixel_function =
      use_fallback_pixel_shader ? nullptr
                                : pixel_translation->function_name().c_str();
  if (use_fallback_pixel_shader) {
    if (!EnsureDepthOnlyPixelShader()) {
      XELOGE("Geometry pipeline: failed to create depth-only PS");
      return nullptr;
    }
    pixel_library = depth_only_pixel_library_;
    pixel_function = depth_only_pixel_function_name_.c_str();
  } else if (!pixel_library) {
    XELOGE("No valid pixel shader translation for geometry pipeline");
    return nullptr;
  }

  uint32_t sample_count = 1;
  MTL::PixelFormat color_formats[4] = {
      MTL::PixelFormatInvalid, MTL::PixelFormatInvalid, MTL::PixelFormatInvalid,
      MTL::PixelFormatInvalid};
  MTL::PixelFormat coverage_format = MTL::PixelFormatInvalid;
  MTL::PixelFormat depth_format = MTL::PixelFormatInvalid;
  MTL::PixelFormat stencil_format = MTL::PixelFormatInvalid;
  if (render_target_cache_) {
    for (uint32_t i = 0; i < 4; ++i) {
      if (MTL::Texture* rt = render_target_cache_->GetColorTargetForDraw(i)) {
        color_formats[i] = rt->pixelFormat();
        if (rt->sampleCount() > 0) {
          sample_count = std::max<uint32_t>(
              sample_count, static_cast<uint32_t>(rt->sampleCount()));
        }
      }
    }
    if (color_formats[0] == MTL::PixelFormatInvalid) {
      if (MTL::Texture* dummy =
              render_target_cache_->GetDummyColorTargetForDraw()) {
        color_formats[0] = dummy->pixelFormat();
        if (dummy->sampleCount() > 0) {
          sample_count = std::max<uint32_t>(
              sample_count, static_cast<uint32_t>(dummy->sampleCount()));
        }
      }
    }
    if (MTL::Texture* depth_tex =
            render_target_cache_->GetDepthTargetForDraw()) {
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
    if (render_target_cache_->IsOrderedBlendCoverageActive()) {
      if (MTL::Texture* coverage_tex =
              render_target_cache_->GetOrderedBlendCoverageTexture()) {
        coverage_format = coverage_tex->pixelFormat();
        if (coverage_tex->sampleCount() > 0) {
          sample_count = std::max<uint32_t>(
              sample_count, static_cast<uint32_t>(coverage_tex->sampleCount()));
        }
      } else {
        coverage_format = MTL::PixelFormatR8Unorm;
      }
    }
  }

  struct GeometryPipelineKey {
    const void* vs;
    const void* ps;
    uint32_t geometry_key;
    uint32_t edram_compute_fallback_mask;
    uint32_t sample_count;
    uint32_t depth_format;
    uint32_t stencil_format;
    uint32_t color_formats[4];
    uint32_t coverage_format;
    uint32_t normalized_color_mask;
    uint32_t alpha_to_mask_enable;
    uint32_t blendcontrol[4];
  } key_data = {};

  key_data.vs = vertex_translation;
  key_data.ps = use_fallback_pixel_shader
                    ? static_cast<const void*>(pixel_library)
                    : static_cast<const void*>(pixel_translation);
  key_data.geometry_key = geometry_shader_key.key;
  key_data.edram_compute_fallback_mask = edram_compute_fallback_mask;
  key_data.sample_count = sample_count;
  key_data.depth_format = uint32_t(depth_format);
  key_data.stencil_format = uint32_t(stencil_format);
  for (uint32_t i = 0; i < 4; ++i) {
    key_data.color_formats[i] = uint32_t(color_formats[i]);
  }
  key_data.coverage_format = uint32_t(coverage_format);
  uint32_t pixel_shader_writes_color_targets =
      use_fallback_pixel_shader
          ? 0
          : (pixel_translation
                 ? pixel_translation->shader().writes_color_targets()
                 : 0);
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

  auto it = geometry_pipeline_cache_.find(key);
  if (it != geometry_pipeline_cache_.end()) {
    return &it->second;
  }

  const bool dump_geometry = ShouldDumpGeometryShaders();
  const char* geometry_dump_dir = dump_geometry ? GetShaderDumpDir() : nullptr;
  static int geometry_dump_counter = 0;
  const int geometry_dump_id = dump_geometry ? geometry_dump_counter++ : -1;
  const PipelineGeometryShader geometry_dump_type = geometry_shader_key.type;
  const uint32_t geometry_dump_key = geometry_shader_key.key;

  auto get_vertex_stage = [&]() -> GeometryVertexStageState* {
    auto vertex_it = geometry_vertex_stage_cache_.find(vertex_translation);
    if (vertex_it != geometry_vertex_stage_cache_.end()) {
      return &vertex_it->second;
    }

    std::vector<uint8_t> dxil_data = vertex_translation->dxil_data();
    if (dxil_data.empty()) {
      std::string dxil_error;
      if (!dxbc_to_dxil_converter_->Convert(
              vertex_translation->translated_binary(), dxil_data,
              &dxil_error)) {
        XELOGE("Geometry VS: DXBC to DXIL conversion failed: {}", dxil_error);
        return nullptr;
      }
    }

    struct InputAttribute {
      uint32_t input_slot = 0;
      uint32_t offset = 0;
      IRFormat format = IRFormatUnknown;
    };
    std::vector<InputAttribute> attribute_map;
    attribute_map.reserve(32);

    auto map_ir_format = [](const ParsedVertexFetchInstruction::Attributes& attrs)
        -> IRFormat {
      using xenos::VertexFormat;
      switch (attrs.data_format) {
        case VertexFormat::k_8_8_8_8:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR8G8B8A8Sint
                                   : IRFormatR8G8B8A8Uint;
          }
          return attrs.is_signed ? IRFormatR8G8B8A8Snorm
                                 : IRFormatR8G8B8A8Unorm;
        case VertexFormat::k_2_10_10_10:
          if (attrs.is_integer) {
            return IRFormatR10G10B10A2Uint;
          }
          return IRFormatR10G10B10A2Unorm;
        case VertexFormat::k_10_11_11:
        case VertexFormat::k_11_11_10:
          return IRFormatR11G11B10Float;
        case VertexFormat::k_16_16:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR16G16Sint
                                   : IRFormatR16G16Uint;
          }
          return attrs.is_signed ? IRFormatR16G16Snorm
                                 : IRFormatR16G16Unorm;
        case VertexFormat::k_16_16_16_16:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR16G16B16A16Sint
                                   : IRFormatR16G16B16A16Uint;
          }
          return attrs.is_signed ? IRFormatR16G16B16A16Snorm
                                 : IRFormatR16G16B16A16Unorm;
        case VertexFormat::k_16_16_FLOAT:
          return IRFormatR16G16Float;
        case VertexFormat::k_16_16_16_16_FLOAT:
          return IRFormatR16G16B16A16Float;
        case VertexFormat::k_32:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR32Sint : IRFormatR32Uint;
          }
          return IRFormatR32Float;
        case VertexFormat::k_32_32:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR32G32Sint
                                   : IRFormatR32G32Uint;
          }
          return IRFormatR32G32Float;
        case VertexFormat::k_32_32_32_FLOAT:
          return IRFormatR32G32B32Float;
        case VertexFormat::k_32_32_32_32:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR32G32B32A32Sint
                                   : IRFormatR32G32B32A32Uint;
          }
          return IRFormatR32G32B32A32Float;
        case VertexFormat::k_32_32_32_32_FLOAT:
          return IRFormatR32G32B32A32Float;
        default:
          return IRFormatUnknown;
      }
    };

    const Shader& vertex_shader_ref = vertex_translation->shader();
    const auto& vertex_bindings = vertex_shader_ref.vertex_bindings();
    uint32_t attr_index = 0;
    for (const auto& binding : vertex_bindings) {
      for (const auto& attr : binding.attributes) {
        if (attr_index >= 31) {
          break;
        }
        InputAttribute mapped = {};
        mapped.input_slot = static_cast<uint32_t>(binding.binding_index);
        mapped.offset = static_cast<uint32_t>(
            attr.fetch_instr.attributes.offset * 4);
        mapped.format = map_ir_format(attr.fetch_instr.attributes);
        attribute_map.push_back(mapped);
        ++attr_index;
      }
      if (attr_index >= 31) {
        break;
      }
    }

    IRInputTopology input_topology = IRInputTopologyUndefined;
    switch (geometry_shader_key.type) {
      case PipelineGeometryShader::kPointList:
        input_topology = IRInputTopologyPoint;
        break;
      case PipelineGeometryShader::kRectangleList:
        input_topology = IRInputTopologyTriangle;
        break;
      case PipelineGeometryShader::kQuadList:
        // Quad lists use LineWithAdjacency in DXBC; MSC input topology doesn't
        // model adjacency, so leave undefined to avoid mismatches.
        input_topology = IRInputTopologyUndefined;
        break;
      default:
        input_topology = IRInputTopologyUndefined;
        break;
    }
    if (input_topology == IRInputTopologyUndefined &&
        geometry_shader_key.type == PipelineGeometryShader::kQuadList) {
      XELOGI("Geometry VS: quad list uses adjacency; input topology left undefined");
    }

    MetalShaderConversionResult vertex_result;
    MetalShaderReflectionInfo vertex_reflection;

    // First pass: get reflection for vertex inputs.
    if (!metal_shader_converter_->ConvertWithStageEx(
            MetalShaderStage::kVertex, dxil_data, vertex_result,
            &vertex_reflection, nullptr, nullptr, true,
            static_cast<int>(input_topology))) {
      XELOGE("Geometry VS: DXIL to Metal conversion failed: {}",
             vertex_result.error_message);
      return nullptr;
    }

    IRVersionedInputLayoutDescriptor input_layout = {};
    input_layout.version = IRInputLayoutDescriptorVersion_1;
    input_layout.desc_1_0.numElements = 0;
    std::vector<std::string> semantic_names_storage;
    if (!vertex_reflection.vertex_inputs.empty()) {
      semantic_names_storage.reserve(vertex_reflection.vertex_inputs.size());
      uint32_t element_count = 0;
      for (const auto& input : vertex_reflection.vertex_inputs) {
        if (element_count >= 31) {
          break;
        }
        if (input.attribute_index >= attribute_map.size()) {
          XELOGW("Geometry VS: vertex input {} out of range (max {})",
                 input.attribute_index, attribute_map.size());
          continue;
        }
        const InputAttribute& mapped = attribute_map[input.attribute_index];
        if (mapped.format == IRFormatUnknown) {
          XELOGW("Geometry VS: unknown IRFormat for vertex input {}",
                 input.attribute_index);
          continue;
        }
        std::string semantic_base = input.name;
        uint32_t semantic_index = 0;
        if (!semantic_base.empty()) {
          size_t digit_pos = semantic_base.size();
          while (digit_pos > 0 &&
                 std::isdigit(static_cast<unsigned char>(
                     semantic_base[digit_pos - 1]))) {
            --digit_pos;
          }
          if (digit_pos < semantic_base.size()) {
            semantic_index =
                static_cast<uint32_t>(std::strtoul(
                    semantic_base.c_str() + digit_pos, nullptr, 10));
            semantic_base.resize(digit_pos);
          }
        }
        if (semantic_base.empty()) {
          semantic_base = "TEXCOORD";
        }
        semantic_names_storage.push_back(std::move(semantic_base));
        input_layout.desc_1_0.semanticNames[element_count] =
            semantic_names_storage.back().c_str();
        IRInputElementDescriptor1& element =
            input_layout.desc_1_0.inputElementDescs[element_count];
        element.semanticIndex = semantic_index;
        element.format = mapped.format;
        element.inputSlot = mapped.input_slot;
        element.alignedByteOffset = mapped.offset;
        element.instanceDataStepRate = 0;
        element.inputSlotClass = IRInputClassificationPerVertexData;
        ++element_count;
      }
      input_layout.desc_1_0.numElements = element_count;
    }

    std::string input_summary;
    {
      std::ostringstream summary;
      summary << "inputs=" << vertex_reflection.vertex_inputs.size()
              << " attrs=" << attribute_map.size();
      if (!vertex_reflection.vertex_inputs.empty()) {
        summary << " [";
        for (size_t i = 0; i < vertex_reflection.vertex_inputs.size(); ++i) {
          const auto& input = vertex_reflection.vertex_inputs[i];
          if (i) {
            summary << "; ";
          }
          summary << input.name << "#" << int(input.attribute_index);
        }
        summary << "]";
      }
      input_summary = summary.str();
    }

    std::string layout_json_storage;
    if (input_layout.desc_1_0.numElements) {
      const char* layout_json =
          IRInputLayoutDescriptor1CopyJSONString(&input_layout.desc_1_0);
      if (layout_json) {
        layout_json_storage = layout_json;
      }
      IRInputLayoutDescriptor1ReleaseString(layout_json);
    }

    // Second pass: synthesize stage-in using the input layout.
    std::vector<uint8_t> stage_in_metallib;
    if (!metal_shader_converter_->ConvertWithStageEx(
            MetalShaderStage::kVertex, dxil_data, vertex_result,
            &vertex_reflection, &input_layout, &stage_in_metallib, true,
            static_cast<int>(input_topology))) {
      XELOGE("Geometry VS: DXIL to Metal conversion failed: {}",
             vertex_result.error_message);
      return nullptr;
    }
    if (stage_in_metallib.empty()) {
      XELOGE(
          "Geometry VS: Failed to synthesize stage-in function "
          "(vertex_inputs={}, output_size={})",
          vertex_reflection.vertex_input_count,
          vertex_reflection.vertex_output_size_in_bytes);
      return nullptr;
    }

    if (dump_geometry && geometry_dump_id >= 0) {
      DumpGeometryArtifact(geometry_dump_dir, geometry_dump_id,
                           geometry_dump_type, geometry_dump_key, "vs.dxil",
                           dxil_data.data(), dxil_data.size());
      DumpGeometryArtifact(geometry_dump_dir, geometry_dump_id,
                           geometry_dump_type, geometry_dump_key, "vs.metallib",
                           vertex_result.metallib_data.data(),
                           vertex_result.metallib_data.size());
      DumpGeometryArtifact(geometry_dump_dir, geometry_dump_id,
                           geometry_dump_type, geometry_dump_key,
                           "vs_stagein.metallib", stage_in_metallib.data(),
                           stage_in_metallib.size());
      std::string layout_json =
          layout_json_storage.empty()
              ? std::string("{\"InputElements\":[],\"SemanticNames\":[]}\n")
              : layout_json_storage;
      DumpGeometryText(geometry_dump_dir, geometry_dump_id,
                       geometry_dump_type, geometry_dump_key,
                       "vs_layout.json", layout_json);
      if (!input_summary.empty()) {
        DumpGeometryText(geometry_dump_dir, geometry_dump_id,
                         geometry_dump_type, geometry_dump_key,
                         "vs_input_summary.txt", input_summary);
      }
      XELOGI(
          "Geometry dump {} {} key={:#010x}: vs_dxil={}B hash=0x{:016x}, "
          "vs_metallib={}B hash=0x{:016x}, stagein_metallib={}B hash=0x{:016x}",
          geometry_dump_id, GetGeometryShaderTypeName(geometry_dump_type),
          geometry_dump_key, dxil_data.size(),
          HashBytes(dxil_data.data(), dxil_data.size()),
          vertex_result.metallib_data.size(),
          HashBytes(vertex_result.metallib_data.data(),
                    vertex_result.metallib_data.size()),
          stage_in_metallib.size(),
          HashBytes(stage_in_metallib.data(), stage_in_metallib.size()));
    }

    NS::Error* error = nullptr;
    dispatch_data_t vertex_data = dispatch_data_create(
        vertex_result.metallib_data.data(),
        vertex_result.metallib_data.size(), nullptr,
        DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    MTL::Library* vertex_library = device_->newLibrary(vertex_data, &error);
    dispatch_release(vertex_data);
    if (!vertex_library) {
      XELOGE("Geometry VS: Failed to create Metal library: {}",
             error ? error->localizedDescription()->utf8String()
                   : "unknown error");
      return nullptr;
    }

    NS::Error* stage_in_error = nullptr;
    dispatch_data_t stage_in_data = dispatch_data_create(
        stage_in_metallib.data(), stage_in_metallib.size(), nullptr,
        DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    MTL::Library* stage_in_library =
        device_->newLibrary(stage_in_data, &stage_in_error);
    dispatch_release(stage_in_data);
    if (!stage_in_library) {
      XELOGE("Geometry VS: Failed to create stage-in library: {}",
             stage_in_error ? stage_in_error->localizedDescription()->utf8String()
                            : "unknown error");
      vertex_library->release();
      return nullptr;
    }

    GeometryVertexStageState state;
    state.library = vertex_library;
    state.stage_in_library = stage_in_library;
    state.function_name = vertex_result.function_name;
    state.vertex_output_size_in_bytes =
        vertex_reflection.vertex_output_size_in_bytes;
    state.input_layout_json = std::move(layout_json_storage);
    state.input_summary = std::move(input_summary);
    if (state.vertex_output_size_in_bytes == 0) {
      XELOGE(
          "Geometry VS: reflection returned zero output size "
          "(vertex_inputs={})",
          vertex_reflection.vertex_input_count);
    }

    auto [inserted_it, inserted] = geometry_vertex_stage_cache_.emplace(
        vertex_translation, std::move(state));
    return &inserted_it->second;
  };

  auto get_geometry_stage = [&]() -> GeometryShaderStageState* {
    auto geom_it = geometry_shader_stage_cache_.find(geometry_shader_key);
    if (geom_it != geometry_shader_stage_cache_.end()) {
      return &geom_it->second;
    }

    const std::vector<uint32_t>& dxbc_dwords =
        GetGeometryShader(geometry_shader_key);
    std::vector<uint8_t> dxbc_bytes(dxbc_dwords.size() * sizeof(uint32_t));
    std::memcpy(dxbc_bytes.data(), dxbc_dwords.data(), dxbc_bytes.size());

    std::vector<uint8_t> dxil_data;
    std::string dxil_error;
    if (!dxbc_to_dxil_converter_->Convert(dxbc_bytes, dxil_data, &dxil_error)) {
      XELOGE("Geometry GS: DXBC to DXIL conversion failed: {}", dxil_error);
      return nullptr;
    }

    IRInputTopology input_topology = IRInputTopologyUndefined;
    switch (geometry_shader_key.type) {
      case PipelineGeometryShader::kPointList:
        input_topology = IRInputTopologyPoint;
        break;
      case PipelineGeometryShader::kRectangleList:
        input_topology = IRInputTopologyTriangle;
        break;
      case PipelineGeometryShader::kQuadList:
        // Quad lists use LineWithAdjacency in DXBC; MSC input topology doesn't
        // model adjacency, so leave undefined to avoid mismatches.
        input_topology = IRInputTopologyUndefined;
        break;
      default:
        input_topology = IRInputTopologyUndefined;
        break;
    }
    if (input_topology == IRInputTopologyUndefined &&
        geometry_shader_key.type == PipelineGeometryShader::kQuadList) {
      XELOGI("Geometry GS: quad list uses adjacency; input topology left undefined");
    }

    MetalShaderConversionResult geometry_result;
    MetalShaderReflectionInfo geometry_reflection;
    if (!metal_shader_converter_->ConvertWithStageEx(
            MetalShaderStage::kGeometry, dxil_data, geometry_result,
            &geometry_reflection, nullptr, nullptr, true,
            static_cast<int>(input_topology))) {
      XELOGE("Geometry GS: DXIL to Metal conversion failed: {}",
             geometry_result.error_message);
      return nullptr;
    }
    if (dump_geometry && geometry_dump_id >= 0) {
      DumpGeometryArtifact(geometry_dump_dir, geometry_dump_id,
                           geometry_dump_type, geometry_dump_key, "gs.dxbc",
                           dxbc_bytes.data(), dxbc_bytes.size());
      DumpGeometryArtifact(geometry_dump_dir, geometry_dump_id,
                           geometry_dump_type, geometry_dump_key, "gs.dxil",
                           dxil_data.data(), dxil_data.size());
      DumpGeometryArtifact(geometry_dump_dir, geometry_dump_id,
                           geometry_dump_type, geometry_dump_key,
                           "gs.metallib", geometry_result.metallib_data.data(),
                           geometry_result.metallib_data.size());
      std::ostringstream info;
      info << "function_name=" << geometry_result.function_name << "\n";
      info << "has_mesh_stage=" << geometry_result.has_mesh_stage << "\n";
      info << "has_geometry_stage=" << geometry_result.has_geometry_stage
           << "\n";
      info << "gs_max_input_primitives_per_mesh_threadgroup="
           << geometry_reflection.gs_max_input_primitives_per_mesh_threadgroup
           << "\n";
      DumpGeometryText(geometry_dump_dir, geometry_dump_id,
                       geometry_dump_type, geometry_dump_key, "gs_info.txt",
                       info.str());
      XELOGI(
          "Geometry dump {} {} key={:#010x}: gs_dxbc={}B hash=0x{:016x}, "
          "gs_dxil={}B hash=0x{:016x}, gs_metallib={}B hash=0x{:016x}",
          geometry_dump_id, GetGeometryShaderTypeName(geometry_dump_type),
          geometry_dump_key, dxbc_bytes.size(),
          HashBytes(dxbc_bytes.data(), dxbc_bytes.size()), dxil_data.size(),
          HashBytes(dxil_data.data(), dxil_data.size()),
          geometry_result.metallib_data.size(),
          HashBytes(geometry_result.metallib_data.data(),
                    geometry_result.metallib_data.size()));
    }
    if (!geometry_result.has_mesh_stage && !geometry_result.has_geometry_stage) {
      XELOGE(
          "Geometry GS: MSC did not emit mesh or geometry stage (mesh={}, "
          "geometry={})",
          geometry_result.has_mesh_stage, geometry_result.has_geometry_stage);
      return nullptr;
    }
    if (!geometry_result.has_mesh_stage) {
      static bool mesh_missing_logged = false;
      if (!mesh_missing_logged) {
        mesh_missing_logged = true;
        XELOGW(
            "Geometry GS: MSC did not emit mesh stage; using geometry stage "
            "library");
      }
    }

    NS::Error* error = nullptr;
    dispatch_data_t geometry_data = dispatch_data_create(
        geometry_result.metallib_data.data(),
        geometry_result.metallib_data.size(), nullptr,
        DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    MTL::Library* geometry_library =
        device_->newLibrary(geometry_data, &error);
    dispatch_release(geometry_data);
    if (!geometry_library) {
      XELOGE("Geometry GS: Failed to create Metal library: {}",
             error ? error->localizedDescription()->utf8String()
                   : "unknown error");
      return nullptr;
    }

    GeometryShaderStageState state;
    state.library = geometry_library;
    state.function_name = geometry_result.function_name;
    state.max_input_primitives_per_mesh_threadgroup =
        geometry_reflection.gs_max_input_primitives_per_mesh_threadgroup;
    state.function_constants = geometry_reflection.function_constants;
    if (state.max_input_primitives_per_mesh_threadgroup == 0) {
      XELOGE("Geometry GS: reflection returned zero max input primitives");
    }

    auto [inserted_it, inserted] =
        geometry_shader_stage_cache_.emplace(geometry_shader_key, std::move(state));
    return &inserted_it->second;
  };

  GeometryVertexStageState* vertex_stage = get_vertex_stage();
  if (!vertex_stage || !vertex_stage->library ||
      !vertex_stage->stage_in_library) {
    return nullptr;
  }
  GeometryShaderStageState* geometry_stage = get_geometry_stage();
  if (!geometry_stage || !geometry_stage->library) {
    return nullptr;
  }

  MTL::MeshRenderPipelineDescriptor* desc =
      MTL::MeshRenderPipelineDescriptor::alloc()->init();

  for (uint32_t i = 0; i < 4; ++i) {
    desc->colorAttachments()->object(i)->setPixelFormat(color_formats[i]);
  }
  if (coverage_format != MTL::PixelFormatInvalid) {
    desc->colorAttachments()
        ->object(MetalRenderTargetCache::kOrderedBlendCoverageAttachmentIndex)
        ->setPixelFormat(coverage_format);
  }
  desc->setDepthAttachmentPixelFormat(depth_format);
  desc->setStencilAttachmentPixelFormat(stencil_format);
  desc->setRasterSampleCount(sample_count);
  desc->setAlphaToCoverageEnabled(key_data.alpha_to_mask_enable != 0);

  for (uint32_t i = 0; i < 4; ++i) {
    auto* color_attachment = desc->colorAttachments()->object(i);
    if (color_formats[i] == MTL::PixelFormatInvalid) {
      color_attachment->setWriteMask(MTL::ColorWriteMaskNone);
      color_attachment->setBlendingEnabled(false);
      continue;
    }

    uint32_t rt_write_mask = (key_data.normalized_color_mask >> (i * 4)) & 0xF;
    if (key_data.edram_compute_fallback_mask & (1u << i)) {
      color_attachment->setWriteMask(
          rt_write_mask ? MTL::ColorWriteMaskAll : MTL::ColorWriteMaskNone);
      color_attachment->setBlendingEnabled(false);
      continue;
    }
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
  if (coverage_format != MTL::PixelFormatInvalid) {
    auto* coverage_attachment = desc->colorAttachments()->object(
        MetalRenderTargetCache::kOrderedBlendCoverageAttachmentIndex);
    coverage_attachment->setWriteMask(MTL::ColorWriteMaskAll);
    coverage_attachment->setBlendingEnabled(false);
  }

  if (!vertex_stage->vertex_output_size_in_bytes ||
      !geometry_stage->max_input_primitives_per_mesh_threadgroup) {
    XELOGE(
        "Geometry pipeline: invalid reflection (vs_output={}, gs_max_input={})",
        vertex_stage->vertex_output_size_in_bytes,
        geometry_stage->max_input_primitives_per_mesh_threadgroup);
    return nullptr;
  }

  IRGeometryEmulationPipelineDescriptor ir_desc = {};
  ir_desc.stageInLibrary = vertex_stage->stage_in_library;
  ir_desc.vertexLibrary = vertex_stage->library;
  ir_desc.vertexFunctionName = vertex_stage->function_name.c_str();
  ir_desc.geometryLibrary = geometry_stage->library;
  ir_desc.geometryFunctionName = geometry_stage->function_name.c_str();
  ir_desc.fragmentLibrary = pixel_library;
  ir_desc.fragmentFunctionName = pixel_function;
  ir_desc.basePipelineDescriptor = desc;
  ir_desc.pipelineConfig.gsVertexSizeInBytes =
      vertex_stage->vertex_output_size_in_bytes;
  ir_desc.pipelineConfig.gsMaxInputPrimitivesPerMeshThreadgroup =
      geometry_stage->max_input_primitives_per_mesh_threadgroup;

  NS::Error* error = nullptr;
  MTL::RenderPipelineState* pipeline =
      IRRuntimeNewGeometryEmulationPipeline(device_, &ir_desc, &error);
  desc->release();

  if (!pipeline) {
    XELOGE("Failed to create geometry pipeline state: {}",
           error ? error->localizedDescription()->utf8String()
                 : "unknown error");
    {
      static bool logged_materialize_probe = false;
      if (!logged_materialize_probe && geometry_stage && geometry_stage->library) {
        logged_materialize_probe = true;
        NS::Error* probe_error = nullptr;
        MTL::Function* probe_no_constants =
            geometry_stage->library->newFunction(
                NS::String::string(geometry_stage->function_name.c_str(),
                                   NS::UTF8StringEncoding));
        if (!probe_no_constants && probe_error) {
          LogMetalErrorDetails("Geometry pipeline probe: geometry function (no constants)",
                               probe_error);
        } else if (probe_no_constants) {
          XELOGI("Geometry pipeline probe: geometry function materialized without constants");
          probe_no_constants->release();
        }

        // UInt4-by-index probe (matches MSC function-constant register-space rule).
        MTL::FunctionConstantValues* probe_fc =
            MTL::FunctionConstantValues::alloc()->init();
        const uint32_t tessellation_enabled_u4[4] = {0, 0, 0, 0};
        const uint32_t output_size_u4[4] = {
            static_cast<uint32_t>(vertex_stage->vertex_output_size_in_bytes), 0,
            0, 0};
        probe_fc->setConstantValue(tessellation_enabled_u4, MTL::DataTypeUInt4,
                                   NS::UInteger(0));
        probe_fc->setConstantValue(output_size_u4, MTL::DataTypeUInt4,
                                   NS::UInteger(1));
        probe_error = nullptr;
        MTL::Function* probe_uint4 =
            geometry_stage->library->newFunction(
                NS::String::string(geometry_stage->function_name.c_str(),
                                   NS::UTF8StringEncoding),
                probe_fc, &probe_error);
        if (!probe_uint4 && probe_error) {
          LogMetalErrorDetails(
              "Geometry pipeline probe: geometry function (UInt4 by index)",
              probe_error);
        } else if (probe_uint4) {
          XELOGI(
              "Geometry pipeline probe: geometry function materialized with "
              "UInt4 constants by index");
          probe_uint4->release();
        }
        probe_fc->release();
      }
    }
    if (dump_geometry && geometry_dump_id >= 0) {
      XELOGE(
          "Geometry pipeline failure: geom_dump_id={} type={} key={:#010x}",
          geometry_dump_id, GetGeometryShaderTypeName(geometry_dump_type),
          geometry_dump_key);
    }
    LogMetalErrorDetails("Geometry pipeline error", error);
    XELOGE("Geometry pipeline debug: VS={} GS={} PS={}",
           vertex_stage->function_name, geometry_stage->function_name,
           pixel_function ? pixel_function : "<null>");
    XELOGE("Geometry pipeline debug: vs_output={} gs_max_input={}",
           vertex_stage->vertex_output_size_in_bytes,
           geometry_stage->max_input_primitives_per_mesh_threadgroup);
    auto log_library_functions = [](const char* label, MTL::Library* lib) {
      if (!lib) {
        XELOGE("Geometry pipeline debug: {} library is null", label);
        return;
      }
      NS::Array* names = lib->functionNames();
      if (!names || !names->count()) {
        XELOGE("Geometry pipeline debug: {} library has no functions", label);
        return;
      }
      XELOGE("Geometry pipeline debug: {} library functions:", label);
      for (NS::UInteger i = 0; i < names->count(); ++i) {
        auto* name = static_cast<NS::String*>(names->object(i));
        XELOGE("  - {}", name->utf8String());
      }
    };
    log_library_functions("stage-in", vertex_stage->stage_in_library);
    log_library_functions("vertex", vertex_stage->library);
    log_library_functions("geometry", geometry_stage->library);
    log_library_functions("fragment", pixel_library);
    auto log_materialization = [&]() {
      static bool logged = false;
      if (logged) {
        return;
      }
      logged = true;

      // Stage-in function.
      if (vertex_stage->stage_in_library) {
        NS::Array* names = vertex_stage->stage_in_library->functionNames();
        if (names && names->count()) {
          auto* name = static_cast<NS::String*>(names->object(0));
          MTL::Function* fn =
              vertex_stage->stage_in_library->newFunction(name);
          if (!fn) {
            XELOGE(
                "Geometry pipeline debug: stage-in function creation failed");
          } else {
            fn->release();
          }
        } else {
          XELOGE("Geometry pipeline debug: stage-in library has no functions");
        }
      }

      // Vertex object function.
      if (vertex_stage->library) {
        MTL::FunctionConstantValues* fc =
            MTL::FunctionConstantValues::alloc()->init();
        bool tessellation_enabled = false;
        fc->setConstantValue(&tessellation_enabled, MTL::DataTypeBool,
                             NS::String::string("tessellationEnabled",
                                                NS::UTF8StringEncoding));
        std::string vertex_name =
            vertex_stage->function_name + ".dxil_irconverter_object_shader";
        NS::Error* err = nullptr;
        MTL::Function* fn = vertex_stage->library->newFunction(
            NS::String::string(vertex_name.c_str(), NS::UTF8StringEncoding), fc,
            &err);
        if (!fn) {
          LogMetalErrorDetails("Geometry pipeline debug: vertex function", err);
        } else {
          fn->release();
        }
        fc->release();
      }

      // Geometry function.
      if (geometry_stage->library) {
        MTL::FunctionConstantValues* fc =
            MTL::FunctionConstantValues::alloc()->init();
        bool tessellation_enabled = false;
        int32_t output_size =
            static_cast<int32_t>(vertex_stage->vertex_output_size_in_bytes);
        fc->setConstantValue(&tessellation_enabled, MTL::DataTypeBool,
                             NS::String::string("tessellationEnabled",
                                                NS::UTF8StringEncoding));
        fc->setConstantValue(&output_size, MTL::DataTypeInt,
                             NS::String::string("vertex_shader_output_size_fc",
                                                NS::UTF8StringEncoding));
        NS::Error* err = nullptr;
        MTL::Function* fn = geometry_stage->library->newFunction(
            NS::String::string(geometry_stage->function_name.c_str(),
                               NS::UTF8StringEncoding),
            fc, &err);
        if (!fn) {
          LogMetalErrorDetails("Geometry pipeline debug: geometry function",
                               err);
        } else {
          fn->release();
        }
        fc->release();
      }

      // Fragment function.
      if (pixel_library && pixel_function) {
        MTL::Function* fn = pixel_library->newFunction(
            NS::String::string(pixel_function, NS::UTF8StringEncoding));
        if (!fn) {
          XELOGE("Geometry pipeline debug: fragment function creation failed");
        } else {
          fn->release();
        }
      }
    };
    log_materialization();
    if (!vertex_stage->input_summary.empty()) {
      XELOGE("Geometry pipeline debug: VS input summary: {}",
             vertex_stage->input_summary);
    }
    if (!vertex_stage->input_layout_json.empty()) {
      XELOGE("Geometry pipeline debug: VS input layout: {}",
             vertex_stage->input_layout_json);
    }
    return nullptr;
  }

  GeometryPipelineState state;
  state.pipeline = pipeline;
  state.gs_vertex_size_in_bytes = ir_desc.pipelineConfig.gsVertexSizeInBytes;
  state.gs_max_input_primitives_per_mesh_threadgroup =
      ir_desc.pipelineConfig.gsMaxInputPrimitivesPerMeshThreadgroup;

  auto [inserted_it, inserted] =
      geometry_pipeline_cache_.emplace(key, std::move(state));
  return &inserted_it->second;
}

MetalCommandProcessor::TessellationPipelineState*
MetalCommandProcessor::GetOrCreateTessellationPipelineState(
    MetalShader::MetalTranslation* domain_translation,
    MetalShader::MetalTranslation* pixel_translation,
    const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
    const RegisterFile& regs, uint32_t edram_compute_fallback_mask) {
  if (!domain_translation) {
    XELOGE("No valid domain shader translation for tessellation pipeline");
    return nullptr;
  }
  bool use_fallback_pixel_shader = (pixel_translation == nullptr);
  MTL::Library* pixel_library =
      use_fallback_pixel_shader ? nullptr : pixel_translation->metal_library();
  const char* pixel_function =
      use_fallback_pixel_shader ? nullptr
                                : pixel_translation->function_name().c_str();
  if (use_fallback_pixel_shader) {
    if (!EnsureDepthOnlyPixelShader()) {
      XELOGE("Tessellation pipeline: failed to create depth-only PS");
      return nullptr;
    }
    pixel_library = depth_only_pixel_library_;
    pixel_function = depth_only_pixel_function_name_.c_str();
  } else if (!pixel_library) {
    XELOGE("No valid pixel shader translation for tessellation pipeline");
    return nullptr;
  }

  uint32_t sample_count = 1;
  MTL::PixelFormat color_formats[4] = {
      MTL::PixelFormatInvalid, MTL::PixelFormatInvalid, MTL::PixelFormatInvalid,
      MTL::PixelFormatInvalid};
  MTL::PixelFormat coverage_format = MTL::PixelFormatInvalid;
  MTL::PixelFormat depth_format = MTL::PixelFormatInvalid;
  MTL::PixelFormat stencil_format = MTL::PixelFormatInvalid;
  if (render_target_cache_) {
    for (uint32_t i = 0; i < 4; ++i) {
      if (MTL::Texture* rt = render_target_cache_->GetColorTargetForDraw(i)) {
        color_formats[i] = rt->pixelFormat();
        if (rt->sampleCount() > 0) {
          sample_count = std::max<uint32_t>(
              sample_count, static_cast<uint32_t>(rt->sampleCount()));
        }
      }
    }
    if (color_formats[0] == MTL::PixelFormatInvalid) {
      if (MTL::Texture* dummy =
              render_target_cache_->GetDummyColorTargetForDraw()) {
        color_formats[0] = dummy->pixelFormat();
        if (dummy->sampleCount() > 0) {
          sample_count = std::max<uint32_t>(
              sample_count, static_cast<uint32_t>(dummy->sampleCount()));
        }
      }
    }
    if (MTL::Texture* depth_tex =
            render_target_cache_->GetDepthTargetForDraw()) {
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
    if (render_target_cache_->IsOrderedBlendCoverageActive()) {
      if (MTL::Texture* coverage_tex =
              render_target_cache_->GetOrderedBlendCoverageTexture()) {
        coverage_format = coverage_tex->pixelFormat();
        if (coverage_tex->sampleCount() > 0) {
          sample_count = std::max<uint32_t>(
              sample_count, static_cast<uint32_t>(coverage_tex->sampleCount()));
        }
      } else {
        coverage_format = MTL::PixelFormatR8Unorm;
      }
    }
  }

  struct TessellationPipelineKey {
    const void* ds;
    const void* ps;
    uint32_t host_vs_type;
    uint32_t tessellation_mode;
    uint32_t host_prim;
    uint32_t edram_compute_fallback_mask;
    uint32_t sample_count;
    uint32_t depth_format;
    uint32_t stencil_format;
    uint32_t color_formats[4];
    uint32_t coverage_format;
    uint32_t normalized_color_mask;
    uint32_t alpha_to_mask_enable;
    uint32_t blendcontrol[4];
  } key_data = {};

  key_data.ds = domain_translation;
  key_data.ps = use_fallback_pixel_shader
                    ? static_cast<const void*>(pixel_library)
                    : static_cast<const void*>(pixel_translation);
  key_data.host_vs_type =
      uint32_t(primitive_processing_result.host_vertex_shader_type);
  key_data.tessellation_mode =
      uint32_t(primitive_processing_result.tessellation_mode);
  key_data.host_prim =
      uint32_t(primitive_processing_result.host_primitive_type);
  key_data.edram_compute_fallback_mask = edram_compute_fallback_mask;
  key_data.sample_count = sample_count;
  key_data.depth_format = uint32_t(depth_format);
  key_data.stencil_format = uint32_t(stencil_format);
  for (uint32_t i = 0; i < 4; ++i) {
    key_data.color_formats[i] = uint32_t(color_formats[i]);
  }
  key_data.coverage_format = uint32_t(coverage_format);
  uint32_t pixel_shader_writes_color_targets =
      use_fallback_pixel_shader
          ? 0
          : (pixel_translation
                 ? pixel_translation->shader().writes_color_targets()
                 : 0);
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

  auto it = tessellation_pipeline_cache_.find(key);
  if (it != tessellation_pipeline_cache_.end()) {
    return &it->second;
  }

  xenos::TessellationMode tessellation_mode =
      primitive_processing_result.tessellation_mode;

  auto get_vertex_stage = [&]() -> TessellationVertexStageState* {
    struct VertexStageKey {
      const void* shader;
      uint32_t tessellation_mode;
    } vertex_key = {domain_translation, uint32_t(tessellation_mode)};
    uint32_t vertex_key_hash =
        uint32_t(XXH3_64bits(&vertex_key, sizeof(vertex_key)));
    auto vertex_it = tessellation_vertex_stage_cache_.find(vertex_key_hash);
    if (vertex_it != tessellation_vertex_stage_cache_.end()) {
      return &vertex_it->second;
    }

    const uint8_t* vs_bytes = nullptr;
    size_t vs_size = 0;
    if (tessellation_mode == xenos::TessellationMode::kAdaptive) {
      vs_bytes = ::tessellation_adaptive_vs;
      vs_size = sizeof(::tessellation_adaptive_vs);
    } else {
      vs_bytes = ::tessellation_indexed_vs;
      vs_size = sizeof(::tessellation_indexed_vs);
    }
    std::vector<uint8_t> dxbc_bytes(vs_bytes, vs_bytes + vs_size);
    std::vector<uint8_t> dxil_data;
    std::string dxil_error;
    if (!dxbc_to_dxil_converter_->Convert(dxbc_bytes, dxil_data, &dxil_error)) {
      XELOGE("Tessellation VS: DXBC to DXIL conversion failed: {}",
             dxil_error);
      return nullptr;
    }

    struct InputAttribute {
      uint32_t input_slot = 0;
      uint32_t offset = 0;
      IRFormat format = IRFormatUnknown;
    };
    std::vector<InputAttribute> attribute_map;
    attribute_map.reserve(32);

    auto map_ir_format =
        [](const ParsedVertexFetchInstruction::Attributes& attrs) -> IRFormat {
      using xenos::VertexFormat;
      switch (attrs.data_format) {
        case VertexFormat::k_8_8_8_8:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR8G8B8A8Sint
                                   : IRFormatR8G8B8A8Uint;
          }
          return attrs.is_signed ? IRFormatR8G8B8A8Snorm
                                 : IRFormatR8G8B8A8Unorm;
        case VertexFormat::k_2_10_10_10:
          if (attrs.is_integer) {
            return IRFormatR10G10B10A2Uint;
          }
          return IRFormatR10G10B10A2Unorm;
        case VertexFormat::k_10_11_11:
        case VertexFormat::k_11_11_10:
          return IRFormatR11G11B10Float;
        case VertexFormat::k_16_16:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR16G16Sint
                                   : IRFormatR16G16Uint;
          }
          return attrs.is_signed ? IRFormatR16G16Snorm
                                 : IRFormatR16G16Unorm;
        case VertexFormat::k_16_16_16_16:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR16G16B16A16Sint
                                   : IRFormatR16G16B16A16Uint;
          }
          return attrs.is_signed ? IRFormatR16G16B16A16Snorm
                                 : IRFormatR16G16B16A16Unorm;
        case VertexFormat::k_16_16_FLOAT:
          return IRFormatR16G16Float;
        case VertexFormat::k_16_16_16_16_FLOAT:
          return IRFormatR16G16B16A16Float;
        case VertexFormat::k_32:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR32Sint : IRFormatR32Uint;
          }
          return IRFormatR32Float;
        case VertexFormat::k_32_32:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR32G32Sint
                                   : IRFormatR32G32Uint;
          }
          return IRFormatR32G32Float;
        case VertexFormat::k_32_32_32_FLOAT:
          return IRFormatR32G32B32Float;
        case VertexFormat::k_32_32_32_32:
          if (attrs.is_integer) {
            return attrs.is_signed ? IRFormatR32G32B32A32Sint
                                   : IRFormatR32G32B32A32Uint;
          }
          return IRFormatR32G32B32A32Float;
        case VertexFormat::k_32_32_32_32_FLOAT:
          return IRFormatR32G32B32A32Float;
        default:
          return IRFormatUnknown;
      }
    };

    const Shader& vertex_shader_ref = domain_translation->shader();
    const auto& vertex_bindings = vertex_shader_ref.vertex_bindings();
    uint32_t attr_index = 0;
    for (const auto& binding : vertex_bindings) {
      for (const auto& attr : binding.attributes) {
        if (attr_index >= 31) {
          break;
        }
        InputAttribute mapped = {};
        mapped.input_slot = static_cast<uint32_t>(binding.binding_index);
        mapped.offset =
            static_cast<uint32_t>(attr.fetch_instr.attributes.offset * 4);
        mapped.format = map_ir_format(attr.fetch_instr.attributes);
        attribute_map.push_back(mapped);
        ++attr_index;
      }
      if (attr_index >= 31) {
        break;
      }
    }

    IRInputTopology input_topology = IRInputTopologyUndefined;

    MetalShaderConversionResult vertex_result;
    MetalShaderReflectionInfo vertex_reflection;
    if (!metal_shader_converter_->ConvertWithStageEx(
            MetalShaderStage::kVertex, dxil_data, vertex_result,
            &vertex_reflection, nullptr, nullptr, true,
            static_cast<int>(input_topology))) {
      XELOGE("Tessellation VS: DXIL to Metal conversion failed: {}",
             vertex_result.error_message);
      return nullptr;
    }

    IRVersionedInputLayoutDescriptor input_layout = {};
    input_layout.version = IRInputLayoutDescriptorVersion_1;
    input_layout.desc_1_0.numElements = 0;
    std::vector<std::string> semantic_names_storage;
    if (!vertex_reflection.vertex_inputs.empty()) {
      semantic_names_storage.reserve(vertex_reflection.vertex_inputs.size());
      uint32_t element_count = 0;
      for (const auto& input : vertex_reflection.vertex_inputs) {
        if (element_count >= 31) {
          break;
        }
        if (input.attribute_index >= attribute_map.size()) {
          XELOGW("Tessellation VS: vertex input {} out of range (max {})",
                 input.attribute_index, attribute_map.size());
          continue;
        }
        const InputAttribute& mapped = attribute_map[input.attribute_index];
        if (mapped.format == IRFormatUnknown) {
          XELOGW("Tessellation VS: unknown IRFormat for vertex input {}",
                 input.attribute_index);
          continue;
        }
        std::string semantic_base = input.name;
        uint32_t semantic_index = 0;
        if (!semantic_base.empty()) {
          size_t digit_pos = semantic_base.size();
          while (digit_pos > 0 &&
                 std::isdigit(static_cast<unsigned char>(
                     semantic_base[digit_pos - 1]))) {
            --digit_pos;
          }
          if (digit_pos < semantic_base.size()) {
            semantic_index =
                static_cast<uint32_t>(std::strtoul(
                    semantic_base.c_str() + digit_pos, nullptr, 10));
            semantic_base.resize(digit_pos);
          }
        }
        if (semantic_base.empty()) {
          semantic_base = "TEXCOORD";
        }
        semantic_names_storage.push_back(std::move(semantic_base));
        input_layout.desc_1_0.semanticNames[element_count] =
            semantic_names_storage.back().c_str();
        IRInputElementDescriptor1& element =
            input_layout.desc_1_0.inputElementDescs[element_count];
        element.semanticIndex = semantic_index;
        element.format = mapped.format;
        element.inputSlot = mapped.input_slot;
        element.alignedByteOffset = mapped.offset;
        element.instanceDataStepRate = 0;
        element.inputSlotClass = IRInputClassificationPerVertexData;
        ++element_count;
      }
      input_layout.desc_1_0.numElements = element_count;
    }

    std::vector<uint8_t> stage_in_metallib;
    if (!metal_shader_converter_->ConvertWithStageEx(
            MetalShaderStage::kVertex, dxil_data, vertex_result,
            &vertex_reflection, &input_layout, &stage_in_metallib, true,
            static_cast<int>(input_topology))) {
      XELOGE("Tessellation VS: DXIL to Metal conversion failed: {}",
             vertex_result.error_message);
      return nullptr;
    }
    if (stage_in_metallib.empty()) {
      XELOGE("Tessellation VS: Failed to synthesize stage-in function");
      return nullptr;
    }

    NS::Error* error = nullptr;
    dispatch_data_t vertex_data = dispatch_data_create(
        vertex_result.metallib_data.data(),
        vertex_result.metallib_data.size(), nullptr,
        DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    MTL::Library* vertex_library = device_->newLibrary(vertex_data, &error);
    dispatch_release(vertex_data);
    if (!vertex_library) {
      XELOGE("Tessellation VS: Failed to create Metal library: {}",
             error ? error->localizedDescription()->utf8String()
                   : "unknown error");
      return nullptr;
    }

    NS::Error* stage_in_error = nullptr;
    dispatch_data_t stage_in_data = dispatch_data_create(
        stage_in_metallib.data(), stage_in_metallib.size(), nullptr,
        DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    MTL::Library* stage_in_library =
        device_->newLibrary(stage_in_data, &stage_in_error);
    dispatch_release(stage_in_data);
    if (!stage_in_library) {
      XELOGE("Tessellation VS: Failed to create stage-in library: {}",
             stage_in_error ? stage_in_error->localizedDescription()->utf8String()
                            : "unknown error");
      vertex_library->release();
      return nullptr;
    }

    TessellationVertexStageState state;
    state.library = vertex_library;
    state.stage_in_library = stage_in_library;
    state.function_name = vertex_result.function_name;
    state.vertex_output_size_in_bytes =
        vertex_reflection.vertex_output_size_in_bytes;
    if (state.vertex_output_size_in_bytes == 0) {
      XELOGE("Tessellation VS: reflection returned zero output size");
    }

    auto [inserted_it, inserted] = tessellation_vertex_stage_cache_.emplace(
        vertex_key_hash, std::move(state));
    return &inserted_it->second;
  };

  auto get_hull_stage = [&]() -> TessellationHullStageState* {
    struct HullStageKey {
      uint32_t host_vs_type;
      uint32_t tessellation_mode;
    } hull_key = {uint32_t(primitive_processing_result.host_vertex_shader_type),
                  uint32_t(tessellation_mode)};
    uint64_t hull_key_hash = XXH3_64bits(&hull_key, sizeof(hull_key));
    auto hull_it = tessellation_hull_stage_cache_.find(hull_key_hash);
    if (hull_it != tessellation_hull_stage_cache_.end()) {
      return &hull_it->second;
    }

    const uint8_t* hs_bytes = nullptr;
    size_t hs_size = 0;
    switch (tessellation_mode) {
      case xenos::TessellationMode::kDiscrete:
        switch (primitive_processing_result.host_vertex_shader_type) {
          case Shader::HostVertexShaderType::kTriangleDomainCPIndexed:
            hs_bytes = ::discrete_triangle_3cp_hs;
            hs_size = sizeof(::discrete_triangle_3cp_hs);
            break;
          case Shader::HostVertexShaderType::kTriangleDomainPatchIndexed:
            hs_bytes = ::discrete_triangle_1cp_hs;
            hs_size = sizeof(::discrete_triangle_1cp_hs);
            break;
          case Shader::HostVertexShaderType::kQuadDomainCPIndexed:
            hs_bytes = ::discrete_quad_4cp_hs;
            hs_size = sizeof(::discrete_quad_4cp_hs);
            break;
          case Shader::HostVertexShaderType::kQuadDomainPatchIndexed:
            hs_bytes = ::discrete_quad_1cp_hs;
            hs_size = sizeof(::discrete_quad_1cp_hs);
            break;
          default:
            XELOGE("Tessellation HS: unsupported host vertex shader type {}",
                   uint32_t(primitive_processing_result.host_vertex_shader_type));
            return nullptr;
        }
        break;
      case xenos::TessellationMode::kContinuous:
        switch (primitive_processing_result.host_vertex_shader_type) {
          case Shader::HostVertexShaderType::kTriangleDomainCPIndexed:
            hs_bytes = ::continuous_triangle_3cp_hs;
            hs_size = sizeof(::continuous_triangle_3cp_hs);
            break;
          case Shader::HostVertexShaderType::kTriangleDomainPatchIndexed:
            hs_bytes = ::continuous_triangle_1cp_hs;
            hs_size = sizeof(::continuous_triangle_1cp_hs);
            break;
          case Shader::HostVertexShaderType::kQuadDomainCPIndexed:
            hs_bytes = ::continuous_quad_4cp_hs;
            hs_size = sizeof(::continuous_quad_4cp_hs);
            break;
          case Shader::HostVertexShaderType::kQuadDomainPatchIndexed:
            hs_bytes = ::continuous_quad_1cp_hs;
            hs_size = sizeof(::continuous_quad_1cp_hs);
            break;
          default:
            XELOGE("Tessellation HS: unsupported host vertex shader type {}",
                   uint32_t(primitive_processing_result.host_vertex_shader_type));
            return nullptr;
        }
        break;
      case xenos::TessellationMode::kAdaptive:
        switch (primitive_processing_result.host_vertex_shader_type) {
          case Shader::HostVertexShaderType::kTriangleDomainPatchIndexed:
            hs_bytes = ::adaptive_triangle_hs;
            hs_size = sizeof(::adaptive_triangle_hs);
            break;
          case Shader::HostVertexShaderType::kQuadDomainPatchIndexed:
            hs_bytes = ::adaptive_quad_hs;
            hs_size = sizeof(::adaptive_quad_hs);
            break;
          default:
            XELOGE("Tessellation HS: unsupported host vertex shader type {}",
                   uint32_t(primitive_processing_result.host_vertex_shader_type));
            return nullptr;
        }
        break;
      default:
        XELOGE("Tessellation HS: unsupported tessellation mode {}",
               uint32_t(tessellation_mode));
        return nullptr;
    }

    std::vector<uint8_t> dxbc_bytes(hs_bytes, hs_bytes + hs_size);
    std::vector<uint8_t> dxil_data;
    std::string dxil_error;
    if (!dxbc_to_dxil_converter_->Convert(dxbc_bytes, dxil_data, &dxil_error)) {
      XELOGE("Tessellation HS: DXBC to DXIL conversion failed: {}", dxil_error);
      return nullptr;
    }

    MetalShaderConversionResult hull_result;
    MetalShaderReflectionInfo hull_reflection;
    if (!metal_shader_converter_->ConvertWithStageEx(
            MetalShaderStage::kHull, dxil_data, hull_result, &hull_reflection,
            nullptr, nullptr, true, static_cast<int>(IRInputTopologyUndefined))) {
      XELOGE("Tessellation HS: DXIL to Metal conversion failed: {}",
             hull_result.error_message);
      return nullptr;
    }

    NS::Error* error = nullptr;
    dispatch_data_t hull_data = dispatch_data_create(
        hull_result.metallib_data.data(), hull_result.metallib_data.size(),
        nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    MTL::Library* hull_library = device_->newLibrary(hull_data, &error);
    dispatch_release(hull_data);
    if (!hull_library) {
      XELOGE("Tessellation HS: Failed to create Metal library: {}",
             error ? error->localizedDescription()->utf8String()
                   : "unknown error");
      return nullptr;
    }

    TessellationHullStageState state;
    state.library = hull_library;
    state.function_name = hull_result.function_name;
    state.reflection = hull_reflection;
    if (!state.reflection.has_hull_info) {
      XELOGE("Tessellation HS: reflection missing hull info");
    }

    auto [inserted_it, inserted] =
        tessellation_hull_stage_cache_.emplace(hull_key_hash, std::move(state));
    return &inserted_it->second;
  };

  auto get_domain_stage = [&]() -> TessellationDomainStageState* {
    uint64_t domain_key =
        XXH3_64bits(&domain_translation, sizeof(domain_translation));
    auto domain_it = tessellation_domain_stage_cache_.find(domain_key);
    if (domain_it != tessellation_domain_stage_cache_.end()) {
      return &domain_it->second;
    }

    std::vector<uint8_t> dxil_data = domain_translation->dxil_data();
    if (dxil_data.empty()) {
      std::string dxil_error;
      if (!dxbc_to_dxil_converter_->Convert(
              domain_translation->translated_binary(), dxil_data,
              &dxil_error)) {
        XELOGE("Tessellation DS: DXBC to DXIL conversion failed: {}",
               dxil_error);
        return nullptr;
      }
    }

    MetalShaderConversionResult domain_result;
    MetalShaderReflectionInfo domain_reflection;
    if (!metal_shader_converter_->ConvertWithStageEx(
            MetalShaderStage::kDomain, dxil_data, domain_result,
            &domain_reflection, nullptr, nullptr, true,
            static_cast<int>(IRInputTopologyUndefined))) {
      XELOGE("Tessellation DS: DXIL to Metal conversion failed: {}",
             domain_result.error_message);
      return nullptr;
    }

    NS::Error* error = nullptr;
    dispatch_data_t domain_data = dispatch_data_create(
        domain_result.metallib_data.data(), domain_result.metallib_data.size(),
        nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    MTL::Library* domain_library = device_->newLibrary(domain_data, &error);
    dispatch_release(domain_data);
    if (!domain_library) {
      XELOGE("Tessellation DS: Failed to create Metal library: {}",
             error ? error->localizedDescription()->utf8String()
                   : "unknown error");
      return nullptr;
    }

    TessellationDomainStageState state;
    state.library = domain_library;
    state.function_name = domain_result.function_name;
    state.reflection = domain_reflection;
    if (!state.reflection.has_domain_info) {
      XELOGE("Tessellation DS: reflection missing domain info");
    }

    auto [inserted_it, inserted] =
        tessellation_domain_stage_cache_.emplace(domain_key, std::move(state));
    return &inserted_it->second;
  };

  TessellationVertexStageState* vertex_stage = get_vertex_stage();
  if (!vertex_stage || !vertex_stage->library ||
      !vertex_stage->stage_in_library) {
    return nullptr;
  }
  TessellationHullStageState* hull_stage = get_hull_stage();
  if (!hull_stage || !hull_stage->library) {
    return nullptr;
  }
  TessellationDomainStageState* domain_stage = get_domain_stage();
  if (!domain_stage || !domain_stage->library) {
    return nullptr;
  }

  IRRuntimeTessellatorOutputPrimitive output_primitive =
      IRRuntimeTessellatorOutputUndefined;
  switch (hull_stage->reflection.hs_tessellator_output_primitive) {
    case IRRuntimeTessellatorOutputPoint:
      output_primitive = IRRuntimeTessellatorOutputPoint;
      break;
    case IRRuntimeTessellatorOutputLine:
      output_primitive = IRRuntimeTessellatorOutputLine;
      break;
    case IRRuntimeTessellatorOutputTriangleCW:
      output_primitive = IRRuntimeTessellatorOutputTriangleCW;
      break;
    case IRRuntimeTessellatorOutputTriangleCCW:
      output_primitive = IRRuntimeTessellatorOutputTriangleCCW;
      break;
    default:
      XELOGE("Tessellation pipeline: unsupported tessellator output {}",
             hull_stage->reflection.hs_tessellator_output_primitive);
      return nullptr;
  }

  IRRuntimePrimitiveType geometry_primitive = IRRuntimePrimitiveTypeTriangle;
  const char* geometry_function = kIRTrianglePassthroughGeometryShader;
  switch (output_primitive) {
    case IRRuntimeTessellatorOutputPoint:
      geometry_primitive = IRRuntimePrimitiveTypePoint;
      geometry_function = kIRPointPassthroughGeometryShader;
      break;
    case IRRuntimeTessellatorOutputLine:
      geometry_primitive = IRRuntimePrimitiveTypeLine;
      geometry_function = kIRLinePassthroughGeometryShader;
      break;
    case IRRuntimeTessellatorOutputTriangleCW:
    case IRRuntimeTessellatorOutputTriangleCCW:
      geometry_primitive = IRRuntimePrimitiveTypeTriangle;
      geometry_function = kIRTrianglePassthroughGeometryShader;
      break;
    default:
      break;
  }

  if (!IRRuntimeValidateTessellationPipeline(
          output_primitive, geometry_primitive,
          hull_stage->reflection.hs_output_control_point_size,
          domain_stage->reflection.ds_input_control_point_size,
          hull_stage->reflection.hs_patch_constants_size,
          domain_stage->reflection.ds_patch_constants_size,
          hull_stage->reflection.hs_output_control_point_count,
          domain_stage->reflection.ds_input_control_point_count)) {
    XELOGE("Tessellation pipeline: validation failed for HS/DS pairing");
    return nullptr;
  }

  MTL::MeshRenderPipelineDescriptor* desc =
      MTL::MeshRenderPipelineDescriptor::alloc()->init();
  for (uint32_t i = 0; i < 4; ++i) {
    desc->colorAttachments()->object(i)->setPixelFormat(color_formats[i]);
  }
  if (coverage_format != MTL::PixelFormatInvalid) {
    desc->colorAttachments()
        ->object(MetalRenderTargetCache::kOrderedBlendCoverageAttachmentIndex)
        ->setPixelFormat(coverage_format);
  }
  desc->setDepthAttachmentPixelFormat(depth_format);
  desc->setStencilAttachmentPixelFormat(stencil_format);
  desc->setRasterSampleCount(sample_count);
  desc->setAlphaToCoverageEnabled(key_data.alpha_to_mask_enable != 0);

  for (uint32_t i = 0; i < 4; ++i) {
    auto* color_attachment = desc->colorAttachments()->object(i);
    if (color_formats[i] == MTL::PixelFormatInvalid) {
      color_attachment->setWriteMask(MTL::ColorWriteMaskNone);
      color_attachment->setBlendingEnabled(false);
      continue;
    }
    uint32_t rt_write_mask = (key_data.normalized_color_mask >> (i * 4)) & 0xF;
    if (key_data.edram_compute_fallback_mask & (1u << i)) {
      color_attachment->setWriteMask(
          rt_write_mask ? MTL::ColorWriteMaskAll : MTL::ColorWriteMaskNone);
      color_attachment->setBlendingEnabled(false);
      continue;
    }
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
  if (coverage_format != MTL::PixelFormatInvalid) {
    auto* coverage_attachment = desc->colorAttachments()->object(
        MetalRenderTargetCache::kOrderedBlendCoverageAttachmentIndex);
    coverage_attachment->setWriteMask(MTL::ColorWriteMaskAll);
    coverage_attachment->setBlendingEnabled(false);
  }

  IRGeometryTessellationEmulationPipelineDescriptor ir_desc = {};
  ir_desc.stageInLibrary = vertex_stage->stage_in_library;
  ir_desc.vertexLibrary = vertex_stage->library;
  ir_desc.vertexFunctionName = vertex_stage->function_name.c_str();
  ir_desc.hullLibrary = hull_stage->library;
  ir_desc.hullFunctionName = hull_stage->function_name.c_str();
  ir_desc.domainLibrary = domain_stage->library;
  ir_desc.domainFunctionName = domain_stage->function_name.c_str();
  ir_desc.geometryLibrary = nullptr;
  ir_desc.geometryFunctionName = geometry_function;
  ir_desc.fragmentLibrary = pixel_library;
  ir_desc.fragmentFunctionName = pixel_function;
  ir_desc.basePipelineDescriptor = desc;
  ir_desc.pipelineConfig.outputPrimitiveType = output_primitive;
  ir_desc.pipelineConfig.vsOutputSizeInBytes =
      vertex_stage->vertex_output_size_in_bytes;
  ir_desc.pipelineConfig.gsMaxInputPrimitivesPerMeshThreadgroup =
      domain_stage->reflection.ds_max_input_prims_per_mesh_threadgroup;
  ir_desc.pipelineConfig.hsMaxPatchesPerObjectThreadgroup =
      hull_stage->reflection.hs_max_patches_per_object_threadgroup;
  ir_desc.pipelineConfig.hsInputControlPointCount =
      hull_stage->reflection.hs_input_control_point_count;
  ir_desc.pipelineConfig.hsMaxObjectThreadsPerThreadgroup =
      hull_stage->reflection.hs_max_object_threads_per_patch;
  ir_desc.pipelineConfig.hsMaxTessellationFactor =
      hull_stage->reflection.hs_max_tessellation_factor;
  ir_desc.pipelineConfig.gsInstanceCount = 1;

  if (!ir_desc.pipelineConfig.vsOutputSizeInBytes ||
      !ir_desc.pipelineConfig.gsMaxInputPrimitivesPerMeshThreadgroup ||
      !ir_desc.pipelineConfig.hsMaxPatchesPerObjectThreadgroup ||
      !ir_desc.pipelineConfig.hsInputControlPointCount ||
      !ir_desc.pipelineConfig.hsMaxObjectThreadsPerThreadgroup) {
    XELOGE(
        "Tessellation pipeline: invalid reflection values (vs_output={}, "
        "gs_max_input={}, hs_patches={}, hs_cp_count={}, hs_threads={})",
        ir_desc.pipelineConfig.vsOutputSizeInBytes,
        ir_desc.pipelineConfig.gsMaxInputPrimitivesPerMeshThreadgroup,
        ir_desc.pipelineConfig.hsMaxPatchesPerObjectThreadgroup,
        ir_desc.pipelineConfig.hsInputControlPointCount,
        ir_desc.pipelineConfig.hsMaxObjectThreadsPerThreadgroup);
    desc->release();
    return nullptr;
  }

  NS::Error* error = nullptr;
  MTL::RenderPipelineState* pipeline =
      IRRuntimeNewGeometryTessellationEmulationPipeline(device_, &ir_desc,
                                                        &error);
  desc->release();
  if (!pipeline) {
    XELOGE("Failed to create tessellation pipeline state: {}",
           error ? error->localizedDescription()->utf8String()
                 : "unknown error");
    return nullptr;
  }

  TessellationPipelineState state;
  state.pipeline = pipeline;
  state.config = ir_desc.pipelineConfig;
  state.primitive = geometry_primitive;

  auto [inserted_it, inserted] =
      tessellation_pipeline_cache_.emplace(key, std::move(state));
  return &inserted_it->second;
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

bool MetalCommandProcessor::EnsureDepthOnlyPixelShader() {
  if (depth_only_pixel_library_) {
    return true;
  }
  if (!shader_translator_ || !dxbc_to_dxil_converter_ ||
      !metal_shader_converter_) {
    XELOGE("Depth-only PS: shader translation not initialized");
    return false;
  }

  std::vector<uint8_t> dxbc_data =
      shader_translator_->CreateDepthOnlyPixelShader();
  if (dxbc_data.empty()) {
    XELOGE("Depth-only PS: failed to create DXBC");
    return false;
  }

  std::vector<uint8_t> dxil_data;
  std::string dxil_error;
  if (!dxbc_to_dxil_converter_->Convert(dxbc_data, dxil_data, &dxil_error)) {
    XELOGE("Depth-only PS: DXBC to DXIL conversion failed: {}", dxil_error);
    return false;
  }

  MetalShaderConversionResult result;
  if (!metal_shader_converter_->ConvertWithStage(MetalShaderStage::kFragment,
                                                 dxil_data, result)) {
    XELOGE("Depth-only PS: DXIL to Metal conversion failed: {}",
           result.error_message);
    return false;
  }

  NS::Error* error = nullptr;
  dispatch_data_t lib_data = dispatch_data_create(
      result.metallib_data.data(), result.metallib_data.size(), nullptr,
      DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  depth_only_pixel_library_ = device_->newLibrary(lib_data, &error);
  dispatch_release(lib_data);
  if (!depth_only_pixel_library_) {
    XELOGE("Depth-only PS: Failed to create Metal library: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    return false;
  }
  depth_only_pixel_function_name_ = result.function_name;
  if (depth_only_pixel_function_name_.empty()) {
    XELOGE("Depth-only PS: missing function name");
    return false;
  }
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

std::shared_ptr<MetalCommandProcessor::DrawRingBuffers>
MetalCommandProcessor::CreateDrawRingBuffers() {
  if (!device_) {
    XELOGE("CreateDrawRingBuffers: Metal device is null");
    return nullptr;
  }
  if (!null_buffer_ || !null_texture_ || !null_sampler_) {
    XELOGE("CreateDrawRingBuffers: Null resources not initialized");
    return nullptr;
  }

  static uint32_t ring_id = 0;
  auto ring = std::make_shared<DrawRingBuffers>();

  const size_t kDescriptorTableCount = kStageCount * kDrawRingCount;
  const size_t kResourceHeapSlots =
      kResourceHeapSlotsPerTable * kDescriptorTableCount;
  const size_t kUavTableBaseIndex = kResourceHeapSlots;
  const size_t kResourceHeapSlotsTotal = kResourceHeapSlots * 2;
  const size_t kResourceHeapBytes =
      kResourceHeapSlotsTotal * sizeof(IRDescriptorTableEntry);
  const size_t kSamplerHeapSlots =
      kSamplerHeapSlotsPerTable * kDescriptorTableCount;
  const size_t kSamplerHeapBytes =
      kSamplerHeapSlots * sizeof(IRDescriptorTableEntry);
  const size_t kUniformsBufferSize =
      kUniformsBytesPerTable * kDescriptorTableCount;
  const size_t kTopLevelABTotalBytes =
      kTopLevelABBytesPerTable * kDescriptorTableCount;
  const size_t kDrawArgsSize = 64;  // Enough for draw arguments struct
  const size_t kCBVHeapSlots = kCbvHeapSlotsPerTable * kDescriptorTableCount;
  const size_t kCBVHeapBytes = kCBVHeapSlots * sizeof(IRDescriptorTableEntry);

  ring->res_heap_ab =
      device_->newBuffer(kResourceHeapBytes, MTL::ResourceStorageModeShared);
  if (!ring->res_heap_ab) {
    XELOGE("Failed to create resource descriptor heap buffer");
    return nullptr;
  }
  std::string ring_label_suffix = std::to_string(ring_id);
  ring->res_heap_ab->setLabel(NS::String::string(
      ("ResourceDescriptorHeap_" + ring_label_suffix).c_str(),
      NS::UTF8StringEncoding));

  // Initialize all tables:
  // - Slot 0: null buffer (will be replaced with shared memory per draw).
  // - Slots 1+: null texture (safe default for any accidental access).
  auto* res_entries = reinterpret_cast<IRDescriptorTableEntry*>(
      ring->res_heap_ab->contents());
  auto* uav_entries = res_entries + kUavTableBaseIndex;
  for (size_t table = 0; table < kDescriptorTableCount; ++table) {
    IRDescriptorTableEntry* table_entries =
        res_entries + table * kResourceHeapSlotsPerTable;
    IRDescriptorTableSetBuffer(&table_entries[0], null_buffer_->gpuAddress(),
                               kNullBufferSize);
    for (size_t i = 1; i < kResourceHeapSlotsPerTable; ++i) {
      IRDescriptorTableSetTexture(&table_entries[i], null_texture_, 0.0f, 0);
    }
    IRDescriptorTableEntry* uav_table_entries =
        uav_entries + table * kResourceHeapSlotsPerTable;
    for (size_t i = 0; i < kResourceHeapSlotsPerTable; ++i) {
      IRDescriptorTableSetBuffer(&uav_table_entries[i],
                                 null_buffer_->gpuAddress(), kNullBufferSize);
    }
  }

  ring->smp_heap_ab =
      device_->newBuffer(kSamplerHeapBytes, MTL::ResourceStorageModeShared);
  if (!ring->smp_heap_ab) {
    XELOGE("Failed to create sampler descriptor heap buffer");
    return nullptr;
  }
  ring->smp_heap_ab->setLabel(NS::String::string(
      ("SamplerDescriptorHeap_" + ring_label_suffix).c_str(),
      NS::UTF8StringEncoding));
  auto* smp_entries = reinterpret_cast<IRDescriptorTableEntry*>(
      ring->smp_heap_ab->contents());
  for (size_t i = 0; i < kSamplerHeapSlots; ++i) {
    IRDescriptorTableSetSampler(&smp_entries[i], null_sampler_, 0.0f);
  }

  ring->uniforms_buffer =
      device_->newBuffer(kUniformsBufferSize, MTL::ResourceStorageModeShared);
  if (!ring->uniforms_buffer) {
    XELOGE("Failed to create uniforms buffer");
    return nullptr;
  }
  ring->uniforms_buffer->setLabel(NS::String::string(
      ("UniformsBuffer_" + ring_label_suffix).c_str(),
      NS::UTF8StringEncoding));
  std::memset(ring->uniforms_buffer->contents(), 0, kUniformsBufferSize);

  ring->top_level_ab =
      device_->newBuffer(kTopLevelABTotalBytes, MTL::ResourceStorageModeShared);
  if (!ring->top_level_ab) {
    XELOGE("Failed to create top-level argument buffer");
    return nullptr;
  }
  ring->top_level_ab->setLabel(NS::String::string(
      ("TopLevelArgumentBuffer_" + ring_label_suffix).c_str(),
      NS::UTF8StringEncoding));
  std::memset(ring->top_level_ab->contents(), 0, kTopLevelABTotalBytes);

  ring->draw_args_buffer =
      device_->newBuffer(kDrawArgsSize, MTL::ResourceStorageModeShared);
  if (!ring->draw_args_buffer) {
    XELOGE("Failed to create draw arguments buffer");
    return nullptr;
  }
  ring->draw_args_buffer->setLabel(NS::String::string(
      ("DrawArgumentsBuffer_" + ring_label_suffix).c_str(),
      NS::UTF8StringEncoding));
  std::memset(ring->draw_args_buffer->contents(), 0, kDrawArgsSize);

  ring->cbv_heap_ab =
      device_->newBuffer(kCBVHeapBytes, MTL::ResourceStorageModeShared);
  if (!ring->cbv_heap_ab) {
    XELOGE("Failed to create CBV descriptor heap buffer");
    return nullptr;
  }
  ring->cbv_heap_ab->setLabel(NS::String::string(
      ("CBVDescriptorHeap_" + ring_label_suffix).c_str(),
      NS::UTF8StringEncoding));
  std::memset(ring->cbv_heap_ab->contents(), 0, kCBVHeapBytes);

  XELOGI(
      "MetalCommandProcessor: Created draw ring {} (res_heap={} entries, "
      "smp_heap={} entries, cbv_heap={} entries, uniforms={}B, top_level={}B, "
      "draw_args={}B)",
      ring_id, kResourceHeapSlotsTotal, kSamplerHeapSlots, kCBVHeapSlots,
      kUniformsBufferSize, kTopLevelABTotalBytes, kDrawArgsSize);
  ++ring_id;

  return ring;
}

std::shared_ptr<MetalCommandProcessor::DrawRingBuffers>
MetalCommandProcessor::AcquireDrawRingBuffers() {
  std::lock_guard<std::mutex> lock(draw_ring_mutex_);
  if (!draw_ring_pool_.empty()) {
    auto ring = draw_ring_pool_.back();
    draw_ring_pool_.pop_back();
    return ring;
  }
  return CreateDrawRingBuffers();
}

void MetalCommandProcessor::SetActiveDrawRing(
    const std::shared_ptr<DrawRingBuffers>& ring) {
  active_draw_ring_ = ring;
  res_heap_ab_ = ring ? ring->res_heap_ab : nullptr;
  smp_heap_ab_ = ring ? ring->smp_heap_ab : nullptr;
  cbv_heap_ab_ = ring ? ring->cbv_heap_ab : nullptr;
  uniforms_buffer_ = ring ? ring->uniforms_buffer : nullptr;
  top_level_ab_ = ring ? ring->top_level_ab : nullptr;
  draw_args_buffer_ = ring ? ring->draw_args_buffer : nullptr;
}

void MetalCommandProcessor::EnsureActiveDrawRing() {
  if (!active_draw_ring_) {
    auto ring = AcquireDrawRingBuffers();
    if (!ring) {
      return;
    }
    SetActiveDrawRing(ring);
  }
  if (command_buffer_draw_rings_.empty()) {
    command_buffer_draw_rings_.push_back(active_draw_ring_);
    current_draw_index_ = 0;
  }
}

void MetalCommandProcessor::ScheduleDrawRingRelease(
    MTL::CommandBuffer* command_buffer) {
  if (!command_buffer || command_buffer_draw_rings_.empty()) {
    return;
  }
  auto rings = std::move(command_buffer_draw_rings_);
  command_buffer->addCompletedHandler(
      [this, rings](MTL::CommandBuffer*) mutable {
        std::lock_guard<std::mutex> lock(draw_ring_mutex_);
        for (auto& ring : rings) {
          draw_ring_pool_.push_back(ring);
        }
      });
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
    reg::RB_DEPTHCONTROL normalized_depth_control,
    bool ordered_blend_coverage) const {
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

  modification.pixel.coverage_output =
      ordered_blend_coverage && shader.writes_color_targets();

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
  bool edram_rov_used = render_target_cache_ &&
                        render_target_cache_->GetPath() ==
                            RenderTargetCache::Path::kPixelShaderInterlock;
  if (!edram_rov_used) {
    if (shader.implicit_early_z_write_allowed() &&
        (!shader.writes_color_target(0) ||
         !draw_util::DoesCoverageDependOnAlpha(
             regs.Get<reg::RB_COLORCONTROL>()))) {
      modification.pixel.depth_stencil_mode = DepthStencilMode::kEarlyHint;
    } else {
      modification.pixel.depth_stencil_mode = DepthStencilMode::kNoModifiers;
    }
  } else {
    modification.pixel.depth_stencil_mode = DepthStencilMode::kNoModifiers;
  }

  return modification;
}

void MetalCommandProcessor::UpdateSystemConstantValues(
    bool shared_memory_is_uav, bool primitive_polygonal,
    uint32_t line_loop_closing_index, xenos::Endian index_endian,
    const draw_util::ViewportInfo& viewport_info, uint32_t used_texture_mask,
    reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask) {
  const RegisterFile& regs = *register_file_;
  auto pa_cl_clip_cntl = regs.Get<reg::PA_CL_CLIP_CNTL>();
  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  auto rb_alpha_ref = regs.Get<float>(XE_GPU_REG_RB_ALPHA_REF);
  auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
  auto rb_stencilrefmask = regs.Get<reg::RB_STENCILREFMASK>();
  auto rb_stencilrefmask_bf =
      regs.Get<reg::RB_STENCILREFMASK>(XE_GPU_REG_RB_STENCILREFMASK_BF);
  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  uint32_t vgt_indx_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  uint32_t vgt_max_vtx_indx = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  uint32_t vgt_min_vtx_indx = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;

  bool edram_rov_used = render_target_cache_ &&
                        render_target_cache_->GetPath() ==
                            RenderTargetCache::Path::kPixelShaderInterlock;
  uint32_t draw_resolution_scale_x =
      texture_cache_ ? texture_cache_->draw_resolution_scale_x() : 1;
  uint32_t draw_resolution_scale_y =
      texture_cache_ ? texture_cache_->draw_resolution_scale_y() : 1;

  // Get color info for each render target
  reg::RB_COLOR_INFO color_infos[4];
  float rt_clamp[4][4];
  uint32_t rt_keep_masks[4][2];
  for (uint32_t i = 0; i < 4; ++i) {
    color_infos[i] = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[i]);
    if (edram_rov_used) {
      RenderTargetCache::GetPSIColorFormatInfo(
          color_infos[i].color_format,
          (normalized_color_mask >> (i * 4)) & 0b1111, rt_clamp[i][0],
          rt_clamp[i][1], rt_clamp[i][2], rt_clamp[i][3], rt_keep_masks[i][0],
          rt_keep_masks[i][1]);
    }
  }

  bool depth_stencil_enabled = normalized_depth_control.stencil_enable ||
                               normalized_depth_control.z_enable;
  if (edram_rov_used && depth_stencil_enabled) {
    for (uint32_t i = 0; i < 4; ++i) {
      if (rb_depth_info.depth_base == color_infos[i].color_base &&
          (rt_keep_masks[i][0] != UINT32_MAX ||
           rt_keep_masks[i][1] != UINT32_MAX)) {
        depth_stencil_enabled = false;
        break;
      }
    }
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

  if (edram_rov_used && depth_stencil_enabled) {
    flags |= DxbcShaderTranslator::kSysFlag_ROVDepthStencil;
    if (normalized_depth_control.z_enable) {
      flags |= uint32_t(normalized_depth_control.zfunc)
               << DxbcShaderTranslator::kSysFlag_ROVDepthPassIfLess_Shift;
      if (normalized_depth_control.z_write_enable) {
        flags |= DxbcShaderTranslator::kSysFlag_ROVDepthWrite;
      }
    } else {
      flags |= DxbcShaderTranslator::kSysFlag_ROVDepthPassIfLess |
               DxbcShaderTranslator::kSysFlag_ROVDepthPassIfEqual |
               DxbcShaderTranslator::kSysFlag_ROVDepthPassIfGreater;
    }
    if (normalized_depth_control.stencil_enable) {
      flags |= DxbcShaderTranslator::kSysFlag_ROVStencilTest;
    }
    if (alpha_test_function == xenos::CompareFunction::kAlways &&
        !rb_colorcontrol.alpha_to_mask_enable) {
      flags |= DxbcShaderTranslator::kSysFlag_ROVDepthStencilEarlyWrite;
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
    // Fixed-point render targets (k_16_16 / k_16_16_16_16) are backed by *_SNORM
    // in the host render targets path. If full-range emulation is requested,
    // remap from -32...32 to -1...1 by dividing the output values by 32.
    if (render_target_cache_->GetPath() ==
        RenderTargetCache::Path::kHostRenderTargets) {
      if (color_infos[i].color_format ==
          xenos::ColorRenderTargetFormat::k_16_16) {
        if (!render_target_cache_->IsFixedRG16TruncatedToMinus1To1()) {
          color_exp_bias -= 5;
        }
      } else if (color_infos[i].color_format ==
                 xenos::ColorRenderTargetFormat::k_16_16_16_16) {
        if (!render_target_cache_->IsFixedRGBA16TruncatedToMinus1To1()) {
          color_exp_bias -= 5;
        }
      }
    }
    auto color_exp_bias_scale = xe::memory::Reinterpret<float>(
        int32_t(0x3F800000 + (color_exp_bias << 23)));
    system_constants_.color_exp_bias[i] = color_exp_bias_scale;
    if (edram_rov_used) {
      system_constants_.edram_rt_keep_mask[i][0] = rt_keep_masks[i][0];
      system_constants_.edram_rt_keep_mask[i][1] = rt_keep_masks[i][1];
      if (rt_keep_masks[i][0] != UINT32_MAX ||
          rt_keep_masks[i][1] != UINT32_MAX) {
        uint32_t edram_tile_dwords_scaled =
            xenos::kEdramTileWidthSamples * xenos::kEdramTileHeightSamples *
            (draw_resolution_scale_x * draw_resolution_scale_y);
        uint32_t rt_base_dwords_scaled =
            color_infos[i].color_base * edram_tile_dwords_scaled;
        system_constants_.edram_rt_base_dwords_scaled[i] =
            rt_base_dwords_scaled;
        uint32_t format_flags =
            RenderTargetCache::AddPSIColorFormatFlags(
                color_infos[i].color_format);
        system_constants_.edram_rt_format_flags[i] = format_flags;
        uint32_t blend_factors_ops =
            regs[reg::RB_BLENDCONTROL::rt_register_indices[i]] & 0x1FFF1FFF;
        system_constants_.edram_rt_blend_factors_ops[i] = blend_factors_ops;
        std::memcpy(system_constants_.edram_rt_clamp[i], rt_clamp[i],
                    4 * sizeof(float));
      }
    }
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

  if (edram_rov_used) {
    uint32_t edram_tile_dwords_scaled =
        xenos::kEdramTileWidthSamples * xenos::kEdramTileHeightSamples *
        (draw_resolution_scale_x * draw_resolution_scale_y);
    uint32_t edram_32bpp_tile_pitch_dwords_scaled =
        ((rb_surface_info.surface_pitch *
          (rb_surface_info.msaa_samples >= xenos::MsaaSamples::k4X ? 2 : 1)) +
         (xenos::kEdramTileWidthSamples - 1)) /
        xenos::kEdramTileWidthSamples * edram_tile_dwords_scaled;
    system_constants_.edram_32bpp_tile_pitch_dwords_scaled =
        edram_32bpp_tile_pitch_dwords_scaled;
    system_constants_.edram_depth_base_dwords_scaled =
        rb_depth_info.depth_base * edram_tile_dwords_scaled;

    float poly_offset_front_scale = 0.0f, poly_offset_front_offset = 0.0f;
    float poly_offset_back_scale = 0.0f, poly_offset_back_offset = 0.0f;
    if (primitive_polygonal) {
      if (pa_su_sc_mode_cntl.poly_offset_front_enable) {
        poly_offset_front_scale =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
        poly_offset_front_offset =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
      }
      if (pa_su_sc_mode_cntl.poly_offset_back_enable) {
        poly_offset_back_scale =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE);
        poly_offset_back_offset =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET);
      }
    } else if (pa_su_sc_mode_cntl.poly_offset_para_enable) {
      poly_offset_front_scale =
          regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
      poly_offset_front_offset =
          regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
      poly_offset_back_scale = poly_offset_front_scale;
      poly_offset_back_offset = poly_offset_front_offset;
    }

    float poly_offset_scale_factor =
        xenos::kPolygonOffsetScaleSubpixelUnit *
        std::max(draw_resolution_scale_x, draw_resolution_scale_y);
    system_constants_.edram_poly_offset_front_scale =
        poly_offset_front_scale * poly_offset_scale_factor;
    system_constants_.edram_poly_offset_front_offset = poly_offset_front_offset;
    system_constants_.edram_poly_offset_back_scale =
        poly_offset_back_scale * poly_offset_scale_factor;
    system_constants_.edram_poly_offset_back_offset = poly_offset_back_offset;

    if (depth_stencil_enabled && normalized_depth_control.stencil_enable) {
      system_constants_.edram_stencil_front_reference =
          rb_stencilrefmask.stencilref;
      system_constants_.edram_stencil_front_read_mask =
          rb_stencilrefmask.stencilmask;
      system_constants_.edram_stencil_front_write_mask =
          rb_stencilrefmask.stencilwritemask;
      uint32_t stencil_func_ops =
          (normalized_depth_control.value >> 8) & ((1 << 12) - 1);
      system_constants_.edram_stencil_front_func_ops = stencil_func_ops;

      if (primitive_polygonal && normalized_depth_control.backface_enable) {
        system_constants_.edram_stencil_back_reference =
            rb_stencilrefmask_bf.stencilref;
        system_constants_.edram_stencil_back_read_mask =
            rb_stencilrefmask_bf.stencilmask;
        system_constants_.edram_stencil_back_write_mask =
            rb_stencilrefmask_bf.stencilwritemask;
        uint32_t stencil_func_ops_bf =
            (normalized_depth_control.value >> 20) & ((1 << 12) - 1);
        system_constants_.edram_stencil_back_func_ops = stencil_func_ops_bf;
      } else {
        std::memcpy(system_constants_.edram_stencil_back,
                    system_constants_.edram_stencil_front,
                    4 * sizeof(uint32_t));
      }
    }
  }

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
