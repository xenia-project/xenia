/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/pipeline_cache.h"

#include <gflags/gflags.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <utility>

#include "third_party/xxhash/xxhash.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/string.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/gpu_flags.h"

DEFINE_bool(d3d12_dxbc_disasm, false,
            "Disassemble DXBC shaders after generation.");
DEFINE_int32(
    d3d12_pipeline_creation_threads, -1,
    "Number of threads used for graphics pipeline state creation. -1 to "
    "calculate automatically (75% of logical CPU cores), 1-16 to specify the "
    "number of threads explicitly, 0 to disable multithreaded pipeline state "
    "creation.");
DEFINE_bool(
    d3d12_tessellation_adaptive, false,
    "Allow games to use adaptive tessellation - may be disabled if the game "
    "has issues with memexport, the maximum factor will be used in this case. "
    "Temporarily disabled by default since there are visible cracks currently "
    "in Halo 3.");
DEFINE_bool(d3d12_tessellation_wireframe, false,
            "Display tessellated surfaces as wireframe for debugging.");

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildhlsl`.
#include "xenia/gpu/d3d12/shaders/dxbc/adaptive_triangle_hs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/continuous_quad_hs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/continuous_triangle_hs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/discrete_quad_hs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/discrete_triangle_hs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/primitive_point_list_gs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/primitive_quad_list_gs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/primitive_rectangle_list_gs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/tessellation_quad_vs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/tessellation_triangle_vs.h"

PipelineCache::PipelineCache(D3D12CommandProcessor* command_processor,
                             RegisterFile* register_file, bool edram_rov_used)
    : command_processor_(command_processor),
      register_file_(register_file),
      edram_rov_used_(edram_rov_used) {
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();

  shader_translator_ = std::make_unique<DxbcShaderTranslator>(
      provider->GetAdapterVendorID(), edram_rov_used_);

  if (edram_rov_used_) {
    depth_only_pixel_shader_ =
        std::move(shader_translator_->CreateDepthOnlyPixelShader());
  }
}

PipelineCache::~PipelineCache() { Shutdown(); }

bool PipelineCache::Initialize() {
  if (FLAGS_d3d12_pipeline_creation_threads != 0) {
    creation_threads_busy_ = 0;
    creation_completion_event_ =
        xe::threading::Event::CreateManualResetEvent(true);
    creation_completion_set_event_ = false;
    creation_threads_shutdown_ = false;
    uint32_t creation_thread_count;
    if (FLAGS_d3d12_pipeline_creation_threads < 0) {
      creation_thread_count = std::max(
          xe::threading::logical_processor_count() * 3 / 4, uint32_t(1));
    } else {
      creation_thread_count = uint32_t(FLAGS_d3d12_pipeline_creation_threads);
    }
    creation_thread_count = std::min(creation_thread_count, uint32_t(16));
    // TODO(Triang3l): Change the thread count to something non-fixed (3 is just
    // for testing).
    for (uint32_t i = 0; i < 3; ++i) {
      std::unique_ptr<xe::threading::Thread> creation_thread =
          xe::threading::Thread::Create({}, [this]() { CreationThread(); });
      creation_thread->set_name("D3D12 Pipelines");
      creation_threads_.push_back(std::move(creation_thread));
    }
  }
  return true;
}

void PipelineCache::Shutdown() {
  ClearCache();

  // Shut down all threads.
  if (!creation_threads_.empty()) {
    {
      std::lock_guard<std::mutex> lock(creation_request_lock_);
      creation_threads_shutdown_ = true;
    }
    creation_request_cond_.notify_all();
    for (size_t i = 0; i < creation_threads_.size(); ++i) {
      xe::threading::Wait(creation_threads_[i].get(), false);
    }
    creation_threads_.clear();
    creation_completion_event_.reset();
  }
}

void PipelineCache::ClearCache() {
  // Remove references to the current pipeline.
  current_pipeline_ = nullptr;

  if (!creation_threads_.empty()) {
    // Empty the pipeline creation queue.
    {
      std::lock_guard<std::mutex> lock(creation_request_lock_);
      creation_queue_.clear();
      creation_completion_set_event_ = true;
    }
    creation_request_cond_.notify_one();
  }

  // Destroy all pipelines.
  for (auto it : pipelines_) {
    it.second->state->Release();
    delete it.second;
  }
  pipelines_.clear();
  COUNT_profile_set("gpu/pipeline_cache/pipelines", 0);

  // Destroy all shaders.
  for (auto it : shader_map_) {
    delete it.second;
  }
  shader_map_.clear();
}

void PipelineCache::EndFrame() {
  if (!creation_threads_.empty()) {
    // Await creation of all queued pipelines.
    bool await_event = false;
    {
      std::lock_guard<std::mutex> lock(creation_request_lock_);
      if (!creation_queue_.empty() || creation_threads_busy_ != 0) {
        creation_completion_event_->Reset();
        creation_completion_set_event_ = true;
        await_event = true;
      }
    }
    if (await_event) {
      xe::threading::Wait(creation_completion_event_.get(), false);
    }
  }
}

D3D12Shader* PipelineCache::LoadShader(ShaderType shader_type,
                                       uint32_t guest_address,
                                       const uint32_t* host_address,
                                       uint32_t dword_count) {
  // Hash the input memory and lookup the shader.
  uint64_t data_hash = XXH64(host_address, dword_count * sizeof(uint32_t), 0);
  auto it = shader_map_.find(data_hash);
  if (it != shader_map_.end()) {
    // Shader has been previously loaded.
    return it->second;
  }

  // Always create the shader and stash it away.
  // We need to track it even if it fails translation so we know not to try
  // again.
  D3D12Shader* shader =
      new D3D12Shader(shader_type, data_hash, host_address, dword_count);
  shader_map_.insert({data_hash, shader});

  return shader;
}

bool PipelineCache::EnsureShadersTranslated(D3D12Shader* vertex_shader,
                                            D3D12Shader* pixel_shader,
                                            PrimitiveType primitive_type) {
  auto& regs = *register_file_;

  // These are the constant base addresses/ranges for shaders.
  // We have these hardcoded right now cause nothing seems to differ.
  assert_true(regs[XE_GPU_REG_SQ_VS_CONST].u32 == 0x000FF000 ||
              regs[XE_GPU_REG_SQ_VS_CONST].u32 == 0x00000000);
  assert_true(regs[XE_GPU_REG_SQ_PS_CONST].u32 == 0x000FF100 ||
              regs[XE_GPU_REG_SQ_PS_CONST].u32 == 0x00000000);

  xenos::xe_gpu_program_cntl_t sq_program_cntl;
  sq_program_cntl.dword_0 = regs[XE_GPU_REG_SQ_PROGRAM_CNTL].u32;
  if (!vertex_shader->is_translated() &&
      !TranslateShader(vertex_shader, sq_program_cntl, primitive_type)) {
    XELOGE("Failed to translate the vertex shader!");
    return false;
  }
  if (pixel_shader != nullptr && !pixel_shader->is_translated() &&
      !TranslateShader(pixel_shader, sq_program_cntl, primitive_type)) {
    XELOGE("Failed to translate the pixel shader!");
    return false;
  }
  return true;
}

bool PipelineCache::ConfigurePipeline(
    D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
    PrimitiveType primitive_type, IndexFormat index_format,
    const RenderTargetCache::PipelineRenderTarget render_targets[5],
    void** pipeline_handle_out, ID3D12RootSignature** root_signature_out) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  assert_not_null(pipeline_handle_out);
  assert_not_null(root_signature_out);

  PipelineDescription description;
  if (!GetCurrentStateDescription(vertex_shader, pixel_shader, primitive_type,
                                  index_format, render_targets, description)) {
    return false;
  }

  if (current_pipeline_ != nullptr &&
      !std::memcmp(&current_pipeline_->description, &description,
                   sizeof(description))) {
    *pipeline_handle_out = current_pipeline_;
    *root_signature_out = description.root_signature;
    return true;
  }

  // Find an existing pipeline in the cache.
  uint64_t hash = XXH64(&description, sizeof(description), 0);
  auto found_range = pipelines_.equal_range(hash);
  for (auto iter = found_range.first; iter != found_range.second; ++iter) {
    Pipeline* found_pipeline = iter->second;
    if (!std::memcmp(&found_pipeline->description, &description,
                     sizeof(description))) {
      current_pipeline_ = found_pipeline;
      *pipeline_handle_out = found_pipeline;
      *root_signature_out = found_pipeline->description.root_signature;
      return true;
    }
  }

  if (!EnsureShadersTranslated(vertex_shader, pixel_shader, primitive_type)) {
    return false;
  }

  Pipeline* new_pipeline = new Pipeline;
  new_pipeline->state = nullptr;
  std::memcpy(&new_pipeline->description, &description, sizeof(description));
  pipelines_.insert(std::make_pair(hash, new_pipeline));
  COUNT_profile_set("gpu/pipeline_cache/pipelines", pipelines_.size());

  if (!creation_threads_.empty()) {
    // Submit the pipeline for creation to any available thread.
    {
      std::lock_guard<std::mutex> lock(creation_request_lock_);
      creation_queue_.push_back(new_pipeline);
    }
    creation_request_cond_.notify_one();
  } else {
    new_pipeline->state = CreatePipelineState(description);
  }

  current_pipeline_ = new_pipeline;
  *pipeline_handle_out = new_pipeline;
  *root_signature_out = description.root_signature;
  return true;
}

bool PipelineCache::TranslateShader(D3D12Shader* shader,
                                    xenos::xe_gpu_program_cntl_t cntl,
                                    PrimitiveType primitive_type) {
  // Set the target for vertex shader translation.
  DxbcShaderTranslator::VertexShaderType vertex_shader_type;
  if (primitive_type == PrimitiveType::kTrianglePatch) {
    vertex_shader_type =
        DxbcShaderTranslator::VertexShaderType::kTriangleDomain;
  } else if (primitive_type == PrimitiveType::kQuadPatch) {
    vertex_shader_type = DxbcShaderTranslator::VertexShaderType::kQuadDomain;
  } else {
    vertex_shader_type = DxbcShaderTranslator::VertexShaderType::kVertex;
  }
  shader_translator_->SetVertexShaderType(vertex_shader_type);

  // Perform translation.
  // If this fails the shader will be marked as invalid and ignored later.
  if (!shader_translator_->Translate(shader, cntl)) {
    XELOGE("Shader %.16" PRIX64 " translation failed; marking as ignored",
           shader->ucode_data_hash());
    return false;
  }

  if (vertex_shader_type != DxbcShaderTranslator::VertexShaderType::kVertex) {
    // For checking later for safety (so a vertex shader won't be accidentally
    // used as a domain shader or vice versa).
    shader->SetDomainShaderPrimitiveType(primitive_type);
  }

  uint32_t texture_srv_count;
  const DxbcShaderTranslator::TextureSRV* texture_srvs =
      shader_translator_->GetTextureSRVs(texture_srv_count);
  uint32_t sampler_binding_count;
  const DxbcShaderTranslator::SamplerBinding* sampler_bindings =
      shader_translator_->GetSamplerBindings(sampler_binding_count);
  shader->SetTexturesAndSamplers(texture_srvs, texture_srv_count,
                                 sampler_bindings, sampler_binding_count);

  if (shader->is_valid()) {
    XELOGGPU("Generated %s shader (%db) - hash %.16" PRIX64 ":\n%s\n",
             shader->type() == ShaderType::kVertex ? "vertex" : "pixel",
             shader->ucode_dword_count() * 4, shader->ucode_data_hash(),
             shader->ucode_disassembly().c_str());
  }

  // If may be useful, create a version of the shader with early depth/stencil
  // forced.
  if (shader->type() == ShaderType::kPixel && !edram_rov_used_ &&
      shader->early_z_allowed()) {
    shader->SetForcedEarlyZShaderObject(
        std::move(DxbcShaderTranslator::ForceEarlyDepthStencil(
            shader->translated_binary().data())));
  }

  // Disassemble the shader for dumping.
  if (FLAGS_d3d12_dxbc_disasm) {
    auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
    if (!shader->DisassembleDxbc(provider)) {
      XELOGE("Failed to disassemble DXBC shader %.16" PRIX64,
             shader->ucode_data_hash());
    }
  }

  // Dump shader files if desired.
  if (!FLAGS_dump_shaders.empty()) {
    shader->Dump(FLAGS_dump_shaders, "d3d12");
  }

  return shader->is_valid();
}

bool PipelineCache::GetCurrentStateDescription(
    D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
    PrimitiveType primitive_type, IndexFormat index_format,
    const RenderTargetCache::PipelineRenderTarget render_targets[5],
    PipelineDescription& description_out) {
  auto& regs = *register_file_;
  uint32_t pa_su_sc_mode_cntl = regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32;

  // Initialize all unused fields to zero for comparison/hashing.
  std::memset(&description_out, 0, sizeof(description_out));

  // Root signature.
  description_out.root_signature = command_processor_->GetRootSignature(
      vertex_shader, pixel_shader, primitive_type);
  if (description_out.root_signature == nullptr) {
    return false;
  }

  // Shaders.
  description_out.vertex_shader = vertex_shader;
  description_out.pixel_shader = pixel_shader;

  // Index buffer strip cut value.
  if (pa_su_sc_mode_cntl & (1 << 21)) {
    // Not using 0xFFFF with 32-bit indices because in index buffers it will be
    // 0xFFFF0000 anyway due to endianness.
    description_out.strip_cut_index = index_format == IndexFormat::kInt32
                                          ? PipelineStripCutIndex::kFFFFFFFF
                                          : PipelineStripCutIndex::kFFFF;
  } else {
    description_out.strip_cut_index = PipelineStripCutIndex::kNone;
  }

  // Primitive topology type, tessellation mode and geometry shader.
  description_out.tessellation_mode = PipelineTessellationMode::kNone;
  switch (primitive_type) {
    case PrimitiveType::kPointList:
      description_out.primitive_topology_type =
          PipelinePrimitiveTopologyType::kPoint;
      break;
    case PrimitiveType::kLineList:
    case PrimitiveType::kLineStrip:
    case PrimitiveType::kLineLoop:
    // Quads are emulated as line lists with adjacency.
    case PrimitiveType::kQuadList:
    case PrimitiveType::k2DLineStrip:
      description_out.primitive_topology_type =
          PipelinePrimitiveTopologyType::kLine;
      break;
    case PrimitiveType::kTrianglePatch:
    case PrimitiveType::kQuadPatch:
      description_out.primitive_topology_type =
          PipelinePrimitiveTopologyType::kPatch;
      switch (TessellationMode(regs[XE_GPU_REG_VGT_HOS_CNTL].u32 & 0x3)) {
        case TessellationMode::kContinuous:
          description_out.tessellation_mode =
              PipelineTessellationMode::kContinuous;
          break;
        case TessellationMode::kAdaptive:
          description_out.tessellation_mode =
              FLAGS_d3d12_tessellation_adaptive
                  ? PipelineTessellationMode::kAdaptive
                  : PipelineTessellationMode::kContinuous;
          break;
        default:
          description_out.tessellation_mode =
              PipelineTessellationMode::kDiscrete;
          break;
      }
      break;
    default:
      description_out.primitive_topology_type =
          PipelinePrimitiveTopologyType::kTriangle;
      break;
  }
  switch (primitive_type) {
    case PrimitiveType::kLinePatch:
      description_out.patch_type = PipelinePatchType::kLine;
      break;
    case PrimitiveType::kTrianglePatch:
      description_out.patch_type = PipelinePatchType::kTriangle;
      break;
    case PrimitiveType::kQuadPatch:
      description_out.patch_type = PipelinePatchType::kQuad;
      break;
    default:
      description_out.patch_type = PipelinePatchType::kNone;
      break;
  }
  switch (primitive_type) {
    case PrimitiveType::kPointList:
      description_out.geometry_shader = PipelineGeometryShader::kPointList;
      break;
    case PrimitiveType::kRectangleList:
      description_out.geometry_shader = PipelineGeometryShader::kRectangleList;
      break;
    case PrimitiveType::kQuadList:
      description_out.geometry_shader = PipelineGeometryShader::kQuadList;
      break;
    default:
      description_out.geometry_shader = PipelineGeometryShader::kNone;
      break;
  }

  // Rasterizer state.
  uint32_t cull_mode;
  if (primitive_type == PrimitiveType::kPointList ||
      primitive_type == PrimitiveType::kRectangleList) {
    cull_mode = 0;
  } else {
    cull_mode = pa_su_sc_mode_cntl & 0x3;
  }
  if (cull_mode & 1) {
    // More special, so checked first - generally back faces are culled.
    description_out.cull_mode = PipelineCullMode::kFront;
  } else if (cull_mode & 2) {
    description_out.cull_mode = PipelineCullMode::kBack;
  } else {
    description_out.cull_mode = PipelineCullMode::kNone;
  }
  description_out.front_counter_clockwise = (pa_su_sc_mode_cntl & 0x4) == 0;
  // Because Direct3D 12 doesn't support per-side fill mode and depth bias, the
  // values to use depends on the current culling state.
  // If front faces are culled, use the ones for back faces.
  // If back faces are culled, it's the other way around.
  // If culling is not enabled, assume the developer wanted to draw things in a
  // more special way - so if one side is wireframe or has a depth bias, then
  // that's intentional (if both sides have a depth bias, the one for the front
  // faces is used, though it's unlikely that they will ever be different -
  // SetRenderState sets the same offset for both sides).
  // Points fill mode (0) also isn't supported in Direct3D 12, but assume the
  // developer didn't want to fill the whole primitive and use wireframe (like
  // Xenos fill mode 1).
  // Here we also assume that only one side is culled - if two sides are culled,
  // the D3D12 command processor will drop such draw early.
  float poly_offset = 0.0f, poly_offset_scale = 0.0f;
  // With ROV, the depth bias is applied in the pixel shader because per-sample
  // depth is needed for MSAA.
  if (!(cull_mode & 1)) {
    // Front faces aren't culled.
    uint32_t fill_mode = (pa_su_sc_mode_cntl >> 5) & 0x7;
    if (fill_mode == 0 || fill_mode == 1) {
      description_out.fill_mode_wireframe = 1;
    }
    if (!edram_rov_used_ && ((pa_su_sc_mode_cntl >> 11) & 0x1)) {
      poly_offset = regs[XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET].f32;
      poly_offset_scale = regs[XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE].f32;
    }
  }
  if (!(cull_mode & 2)) {
    // Back faces aren't culled.
    uint32_t fill_mode = (pa_su_sc_mode_cntl >> 8) & 0x7;
    if (fill_mode == 0 || fill_mode == 1) {
      description_out.fill_mode_wireframe = 1;
    }
    // Prefer front depth bias because in general, front faces are the ones
    // that are rendered (except for shadow volumes).
    if (!edram_rov_used_ && ((pa_su_sc_mode_cntl >> 12) & 0x1) &&
        poly_offset == 0.0f && poly_offset_scale == 0.0f) {
      poly_offset = regs[XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET].f32;
      poly_offset_scale = regs[XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE].f32;
    }
  }
  if (!edram_rov_used_) {
    // Conversion based on the calculations in Call of Duty 4 and the values it
    // writes to the registers, and also on:
    // https://github.com/mesa3d/mesa/blob/54ad9b444c8e73da498211870e785239ad3ff1aa/src/gallium/drivers/radeonsi/si_state.c#L943
    // Dividing the scale by 2 - Call of Duty 4 sets the constant bias of
    // 1/32768 for decals, however, it's done in two steps in separate places:
    // first it's divided by 65536, and then it's multiplied by 2 (which is
    // consistent with what si_create_rs_state does, which multiplies the offset
    // by 2 if it comes from a non-D3D9 API for 24-bit depth buffers) - and
    // multiplying by 2 to the number of significand bits. Tested mostly in Call
    // of Duty 4 (vehicledamage map explosion decals) and Red Dead Redemption
    // (shadows - 2^17 is not enough, 2^18 hasn't been tested, but 2^19
    // eliminates the acne).
    if (((register_file_->values[XE_GPU_REG_RB_DEPTH_INFO].u32 >> 16) & 0x1) ==
        uint32_t(DepthRenderTargetFormat::kD24FS8)) {
      poly_offset *= float(1 << 19);
    } else {
      poly_offset *= float(1 << 23);
    }
    // Reversed depth is emulated in vertex shaders because MinDepth > MaxDepth
    // in viewports doesn't seem to work on Nvidia.
    if ((regs[XE_GPU_REG_PA_CL_VTE_CNTL].u32 & (1 << 4)) &&
        regs[XE_GPU_REG_PA_CL_VPORT_ZSCALE].f32 < 0.0f) {
      poly_offset = -poly_offset;
      poly_offset_scale = -poly_offset_scale;
    }
    // Using ceil here just in case a game wants the offset but passes a value
    // that is too small - it's better to apply more offset than to make depth
    // fighting worse or to disable the offset completely (Direct3D 12 takes an
    // integer value).
    description_out.depth_bias = int32_t(std::ceil(std::abs(poly_offset))) *
                                 (poly_offset < 0.0f ? -1 : 1);
    description_out.depth_bias_slope_scaled =
        poly_offset_scale * (1.0f / 16.0f);
  }
  if ((pa_su_sc_mode_cntl & (0x3 << 3)) == 0) {
    // Fill mode is disabled.
    description_out.fill_mode_wireframe = 0;
  }
  if (FLAGS_d3d12_tessellation_wireframe &&
      description_out.tessellation_mode != PipelineTessellationMode::kNone) {
    description_out.fill_mode_wireframe = 1;
  }
  // CLIP_DISABLE
  description_out.depth_clip =
      (regs[XE_GPU_REG_PA_CL_CLIP_CNTL].u32 & (1 << 16)) == 0;
  // TODO(DrChat): This seem to differ. Need to examine this.
  // https://github.com/decaf-emu/decaf-emu/blob/c017a9ff8128852fb9a5da19466778a171cea6e1/src/libdecaf/src/gpu/latte_registers_pa.h#L11
  // ZCLIP_NEAR_DISABLE
  // description_out.depth_clip = (PA_CL_CLIP_CNTL & (1 << 26)) == 0;
  // RASTERIZER_DISABLE
  // Disable rendering in command processor if PA_CL_CLIP_CNTL & (1 << 22)?
  if (edram_rov_used_) {
    description_out.rov_msaa =
        ((regs[XE_GPU_REG_RB_SURFACE_INFO].u32 >> 16) & 0x3) != 0;
  }

  if (!edram_rov_used_) {
    uint32_t rb_colorcontrol = regs[XE_GPU_REG_RB_COLORCONTROL].u32;

    // Depth/stencil. No stencil, always passing depth test and no depth writing
    // means depth disabled.
    if (render_targets[4].format != DXGI_FORMAT_UNKNOWN) {
      uint32_t rb_depthcontrol = regs[XE_GPU_REG_RB_DEPTHCONTROL].u32;
      if (rb_depthcontrol & 0x2) {
        description_out.depth_func = (rb_depthcontrol >> 4) & 0x7;
        description_out.depth_write = (rb_depthcontrol & 0x4) != 0;
      } else {
        description_out.depth_func = 0b111;
      }
      if (rb_depthcontrol & 0x1) {
        description_out.stencil_enable = 1;
        uint32_t rb_stencilrefmask = regs[XE_GPU_REG_RB_STENCILREFMASK].u32;
        description_out.stencil_read_mask = (rb_stencilrefmask >> 8) & 0xFF;
        description_out.stencil_write_mask = (rb_stencilrefmask >> 16) & 0xFF;
        description_out.stencil_front_fail_op = (rb_depthcontrol >> 11) & 0x7;
        description_out.stencil_front_depth_fail_op =
            (rb_depthcontrol >> 17) & 0x7;
        description_out.stencil_front_pass_op = (rb_depthcontrol >> 14) & 0x7;
        description_out.stencil_front_func = (rb_depthcontrol >> 8) & 0x7;
        if (rb_depthcontrol & 0x80) {
          description_out.stencil_back_fail_op = (rb_depthcontrol >> 23) & 0x7;
          description_out.stencil_back_depth_fail_op =
              (rb_depthcontrol >> 29) & 0x7;
          description_out.stencil_back_pass_op = (rb_depthcontrol >> 26) & 0x7;
          description_out.stencil_back_func = (rb_depthcontrol >> 20) & 0x7;
        } else {
          description_out.stencil_back_fail_op =
              description_out.stencil_front_fail_op;
          description_out.stencil_back_depth_fail_op =
              description_out.stencil_front_depth_fail_op;
          description_out.stencil_back_pass_op =
              description_out.stencil_front_pass_op;
          description_out.stencil_back_func =
              description_out.stencil_front_func;
        }
      }
      // If not binding the DSV, ignore the format in the hash.
      if (description_out.depth_func != 0b111 || description_out.depth_write ||
          description_out.stencil_enable) {
        description_out.depth_format = DepthRenderTargetFormat(
            (regs[XE_GPU_REG_RB_DEPTH_INFO].u32 >> 16) & 1);
      }
    } else {
      description_out.depth_func = 0b111;
    }

    // Forced early Z if the shader allows that and alpha testing is disabled.
    // TODO(Triang3l): For memexporting shaders, possibly choose this according
    // to the early Z toggle in RB_DEPTHCONTROL (the correct behavior is still
    // unknown).
    if (pixel_shader != nullptr &&
        pixel_shader->GetForcedEarlyZShaderObject().size() != 0 &&
        (!(rb_colorcontrol & 0x8) || (rb_colorcontrol & 0x7) == 0x7)) {
      description_out.force_early_z = 1;
    }

    // Render targets and blending state. 32 because of 0x1F mask, for safety
    // (all unknown to zero).
    uint32_t color_mask = command_processor_->GetCurrentColorMask(pixel_shader);
    static const PipelineBlendFactor kBlendFactorMap[32] = {
        /*  0 */ PipelineBlendFactor::kZero,
        /*  1 */ PipelineBlendFactor::kOne,
        /*  2 */ PipelineBlendFactor::kZero,  // ?
        /*  3 */ PipelineBlendFactor::kZero,  // ?
        /*  4 */ PipelineBlendFactor::kSrcColor,
        /*  5 */ PipelineBlendFactor::kInvSrcColor,
        /*  6 */ PipelineBlendFactor::kSrcAlpha,
        /*  7 */ PipelineBlendFactor::kInvSrcAlpha,
        /*  8 */ PipelineBlendFactor::kDestColor,
        /*  9 */ PipelineBlendFactor::kInvDestColor,
        /* 10 */ PipelineBlendFactor::kDestAlpha,
        /* 11 */ PipelineBlendFactor::kInvDestAlpha,
        // CONSTANT_COLOR
        /* 12 */ PipelineBlendFactor::kBlendFactor,
        // ONE_MINUS_CONSTANT_COLOR
        /* 13 */ PipelineBlendFactor::kInvBlendFactor,
        // CONSTANT_ALPHA
        /* 14 */ PipelineBlendFactor::kBlendFactor,
        // ONE_MINUS_CONSTANT_ALPHA
        /* 15 */ PipelineBlendFactor::kInvBlendFactor,
        /* 16 */ PipelineBlendFactor::kSrcAlphaSat,
    };
    // Like kBlendFactorMap, but with color modes changed to alpha. Some
    // pipelines aren't created in Prey because a color mode is used for alpha.
    static const PipelineBlendFactor kBlendFactorAlphaMap[32] = {
        /*  0 */ PipelineBlendFactor::kZero,
        /*  1 */ PipelineBlendFactor::kOne,
        /*  2 */ PipelineBlendFactor::kZero,  // ?
        /*  3 */ PipelineBlendFactor::kZero,  // ?
        /*  4 */ PipelineBlendFactor::kSrcAlpha,
        /*  5 */ PipelineBlendFactor::kInvSrcAlpha,
        /*  6 */ PipelineBlendFactor::kSrcAlpha,
        /*  7 */ PipelineBlendFactor::kInvSrcAlpha,
        /*  8 */ PipelineBlendFactor::kDestAlpha,
        /*  9 */ PipelineBlendFactor::kInvDestAlpha,
        /* 10 */ PipelineBlendFactor::kDestAlpha,
        /* 11 */ PipelineBlendFactor::kInvDestAlpha,
        /* 12 */ PipelineBlendFactor::kBlendFactor,
        // ONE_MINUS_CONSTANT_COLOR
        /* 13 */ PipelineBlendFactor::kInvBlendFactor,
        // CONSTANT_ALPHA
        /* 14 */ PipelineBlendFactor::kBlendFactor,
        // ONE_MINUS_CONSTANT_ALPHA
        /* 15 */ PipelineBlendFactor::kInvBlendFactor,
        /* 16 */ PipelineBlendFactor::kSrcAlphaSat,
    };
    for (uint32_t i = 0; i < 4; ++i) {
      if (render_targets[i].format == DXGI_FORMAT_UNKNOWN) {
        break;
      }
      uint32_t guest_rt_index = render_targets[i].guest_render_target;
      uint32_t color_info, blendcontrol;
      switch (guest_rt_index) {
        case 1:
          color_info = regs[XE_GPU_REG_RB_COLOR1_INFO].u32;
          blendcontrol = regs[XE_GPU_REG_RB_BLENDCONTROL_1].u32;
          break;
        case 2:
          color_info = regs[XE_GPU_REG_RB_COLOR2_INFO].u32;
          blendcontrol = regs[XE_GPU_REG_RB_BLENDCONTROL_2].u32;
          break;
        case 3:
          color_info = regs[XE_GPU_REG_RB_COLOR3_INFO].u32;
          blendcontrol = regs[XE_GPU_REG_RB_BLENDCONTROL_3].u32;
          break;
        default:
          color_info = regs[XE_GPU_REG_RB_COLOR_INFO].u32;
          blendcontrol = regs[XE_GPU_REG_RB_BLENDCONTROL_0].u32;
          break;
      }
      PipelineRenderTarget& rt = description_out.render_targets[i];
      rt.used = 1;
      rt.format = RenderTargetCache::GetBaseColorFormat(
          ColorRenderTargetFormat((color_info >> 16) & 0xF));
      rt.write_mask = (color_mask >> (guest_rt_index * 4)) & 0xF;
      if (!(rb_colorcontrol & 0x20) && rt.write_mask) {
        rt.src_blend = kBlendFactorMap[blendcontrol & 0x1F];
        rt.dest_blend = kBlendFactorMap[(blendcontrol >> 8) & 0x1F];
        rt.blend_op = BlendOp((blendcontrol >> 5) & 0x7);
        rt.src_blend_alpha = kBlendFactorAlphaMap[(blendcontrol >> 16) & 0x1F];
        rt.dest_blend_alpha = kBlendFactorAlphaMap[(blendcontrol >> 24) & 0x1F];
        rt.blend_op_alpha = BlendOp((blendcontrol >> 21) & 0x7);
      } else {
        rt.src_blend = PipelineBlendFactor::kOne;
        rt.dest_blend = PipelineBlendFactor::kZero;
        rt.blend_op = BlendOp::kAdd;
        rt.src_blend_alpha = PipelineBlendFactor::kOne;
        rt.dest_blend_alpha = PipelineBlendFactor::kZero;
        rt.blend_op_alpha = BlendOp::kAdd;
      }
    }
  }

  return true;
}

ID3D12PipelineState* PipelineCache::CreatePipelineState(
    const PipelineDescription& description) {
  if (description.pixel_shader != nullptr) {
    XELOGGPU("Creating graphics pipeline state with VS %.16" PRIX64
             ", PS %.16" PRIX64,
             description.vertex_shader->ucode_data_hash(),
             description.pixel_shader->ucode_data_hash());
  } else {
    XELOGGPU("Creating graphics pipeline state with VS %.16" PRIX64,
             description.vertex_shader->ucode_data_hash());
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC state_desc;
  std::memset(&state_desc, 0, sizeof(state_desc));

  // Root signature.
  state_desc.pRootSignature = description.root_signature;

  // Index buffer strip cut value.
  switch (description.strip_cut_index) {
    case PipelineStripCutIndex::kFFFF:
      state_desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
      break;
    case PipelineStripCutIndex::kFFFFFFFF:
      state_desc.IBStripCutValue =
          D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
      break;
    default:
      state_desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
      break;
  }

  // Vertex or hull/domain shaders.
  if (!description.vertex_shader->is_translated()) {
    XELOGE("Vertex shader %.16" PRIX64 " not translated",
           description.vertex_shader->ucode_data_hash());
    assert_always();
    return nullptr;
  }
  if (description.tessellation_mode != PipelineTessellationMode::kNone) {
    switch (description.patch_type) {
      case PipelinePatchType::kTriangle:
        if (description.vertex_shader->GetDomainShaderPrimitiveType() !=
            PrimitiveType::kTrianglePatch) {
          XELOGE(
              "Tried to use vertex shader %.16" PRIX64
              " for triangle patch tessellation, but it's not a tessellation "
              "domain shader or has the wrong domain",
              description.vertex_shader->ucode_data_hash());
          assert_always();
          return nullptr;
        }
        if (description.tessellation_mode ==
            PipelineTessellationMode::kDiscrete) {
          state_desc.HS.pShaderBytecode = discrete_triangle_hs;
          state_desc.HS.BytecodeLength = sizeof(discrete_triangle_hs);
        } else if (description.tessellation_mode ==
                   PipelineTessellationMode::kAdaptive) {
          state_desc.HS.pShaderBytecode = adaptive_triangle_hs;
          state_desc.HS.BytecodeLength = sizeof(adaptive_triangle_hs);
        } else {
          state_desc.HS.pShaderBytecode = continuous_triangle_hs;
          state_desc.HS.BytecodeLength = sizeof(continuous_triangle_hs);
        }
        state_desc.VS.pShaderBytecode = tessellation_triangle_vs;
        state_desc.VS.BytecodeLength = sizeof(tessellation_triangle_vs);
        break;
      case PipelinePatchType::kQuad:
        if (description.vertex_shader->GetDomainShaderPrimitiveType() !=
            PrimitiveType::kQuadPatch) {
          XELOGE("Tried to use vertex shader %.16" PRIX64
                 " for quad patch tessellation, but it's not a tessellation "
                 "domain shader or has the wrong domain",
                 description.vertex_shader->ucode_data_hash());
          assert_always();
          return nullptr;
        }
        if (description.tessellation_mode ==
            PipelineTessellationMode::kDiscrete) {
          state_desc.HS.pShaderBytecode = discrete_quad_hs;
          state_desc.HS.BytecodeLength = sizeof(discrete_quad_hs);
        } else {
          state_desc.HS.pShaderBytecode = continuous_quad_hs;
          state_desc.HS.BytecodeLength = sizeof(continuous_quad_hs);
          // TODO(Triang3l): True adaptive tessellation when properly tested.
        }
        state_desc.VS.pShaderBytecode = tessellation_quad_vs;
        state_desc.VS.BytecodeLength = sizeof(tessellation_quad_vs);
        break;
      default:
        assert_unhandled_case(description.patch_type);
        return nullptr;
    }
    // The Xenos vertex shader works like a domain shader with tessellation.
    state_desc.DS.pShaderBytecode =
        description.vertex_shader->translated_binary().data();
    state_desc.DS.BytecodeLength =
        description.vertex_shader->translated_binary().size();
  } else {
    if (description.vertex_shader->GetDomainShaderPrimitiveType() !=
        PrimitiveType::kNone) {
      XELOGE("Tried to use vertex shader %.16" PRIX64
             " without tessellation, but it's a tessellation domain shader",
             description.vertex_shader->ucode_data_hash());
      assert_always();
      return nullptr;
    }
    state_desc.VS.pShaderBytecode =
        description.vertex_shader->translated_binary().data();
    state_desc.VS.BytecodeLength =
        description.vertex_shader->translated_binary().size();
  }

  // Pre-GS primitive topology type.
  switch (description.primitive_topology_type) {
    case PipelinePrimitiveTopologyType::kPoint:
      state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
      break;
    case PipelinePrimitiveTopologyType::kLine:
      state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
      break;
    case PipelinePrimitiveTopologyType::kTriangle:
      state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      break;
    case PipelinePrimitiveTopologyType::kPatch:
      state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
      break;
    default:
      assert_unhandled_case(description.primitive_topology_type);
      return nullptr;
  }

  // Geometry shader.
  switch (description.geometry_shader) {
    case PipelineGeometryShader::kPointList:
      state_desc.GS.pShaderBytecode = primitive_point_list_gs;
      state_desc.GS.BytecodeLength = sizeof(primitive_point_list_gs);
      break;
    case PipelineGeometryShader::kRectangleList:
      state_desc.GS.pShaderBytecode = primitive_rectangle_list_gs;
      state_desc.GS.BytecodeLength = sizeof(primitive_rectangle_list_gs);
      break;
    case PipelineGeometryShader::kQuadList:
      state_desc.GS.pShaderBytecode = primitive_quad_list_gs;
      state_desc.GS.BytecodeLength = sizeof(primitive_quad_list_gs);
      break;
    default:
      break;
  }

  // Pixel shader.
  if (description.pixel_shader != nullptr) {
    if (!description.pixel_shader->is_translated()) {
      XELOGE("Pixel shader %.16" PRIX64 " not translated",
             description.pixel_shader->ucode_data_hash());
      assert_always();
      return nullptr;
    }
    const auto& forced_early_z_shader =
        description.pixel_shader->GetForcedEarlyZShaderObject();
    if (description.force_early_z && forced_early_z_shader.size() != 0) {
      state_desc.PS.pShaderBytecode = forced_early_z_shader.data();
      state_desc.PS.BytecodeLength = forced_early_z_shader.size();
    } else {
      state_desc.PS.pShaderBytecode =
          description.pixel_shader->translated_binary().data();
      state_desc.PS.BytecodeLength =
          description.pixel_shader->translated_binary().size();
    }
  } else if (edram_rov_used_) {
    state_desc.PS.pShaderBytecode = depth_only_pixel_shader_.data();
    state_desc.PS.BytecodeLength = depth_only_pixel_shader_.size();
  }

  // Rasterizer state.
  state_desc.SampleMask = UINT_MAX;
  state_desc.RasterizerState.FillMode = description.fill_mode_wireframe
                                            ? D3D12_FILL_MODE_WIREFRAME
                                            : D3D12_FILL_MODE_SOLID;
  switch (description.cull_mode) {
    case PipelineCullMode::kFront:
      state_desc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
      break;
    case PipelineCullMode::kBack:
      state_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
      break;
    default:
      state_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      break;
  }
  state_desc.RasterizerState.FrontCounterClockwise =
      description.front_counter_clockwise ? TRUE : FALSE;
  state_desc.RasterizerState.DepthBias = description.depth_bias;
  state_desc.RasterizerState.DepthBiasClamp = 0.0f;
  state_desc.RasterizerState.SlopeScaledDepthBias =
      description.depth_bias_slope_scaled;
  state_desc.RasterizerState.DepthClipEnable =
      description.depth_clip ? TRUE : FALSE;
  if (edram_rov_used_) {
    // Only 1, 4, 8 and (not on all GPUs) 16 are allowed, using sample 0 as 0
    // and 3 as 1 for 2x instead (not exactly the same sample positions, but
    // still top-left and bottom-right - however, this can be adjusted with
    // programmable sample positions).
    state_desc.RasterizerState.ForcedSampleCount = description.rov_msaa ? 4 : 1;
  }

  // Sample description.
  state_desc.SampleDesc.Count = 1;

  if (!edram_rov_used_) {
    // Depth/stencil.
    if (description.depth_func != 0b111 || description.depth_write) {
      state_desc.DepthStencilState.DepthEnable = TRUE;
      state_desc.DepthStencilState.DepthWriteMask =
          description.depth_write ? D3D12_DEPTH_WRITE_MASK_ALL
                                  : D3D12_DEPTH_WRITE_MASK_ZERO;
      // Comparison functions are the same in Direct3D 12 but plus one (minus
      // one, bit 0 for less, bit 1 for equal, bit 2 for greater).
      state_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC(
          uint32_t(D3D12_COMPARISON_FUNC_NEVER) + description.depth_func);
    }
    if (description.stencil_enable) {
      state_desc.DepthStencilState.StencilEnable = TRUE;
      state_desc.DepthStencilState.StencilReadMask =
          description.stencil_read_mask;
      state_desc.DepthStencilState.StencilWriteMask =
          description.stencil_write_mask;
      // Stencil operations are the same in Direct3D 12 too but plus one.
      state_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP(
          uint32_t(D3D12_STENCIL_OP_KEEP) + description.stencil_front_fail_op);
      state_desc.DepthStencilState.FrontFace.StencilDepthFailOp =
          D3D12_STENCIL_OP(uint32_t(D3D12_STENCIL_OP_KEEP) +
                           description.stencil_front_depth_fail_op);
      state_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP(
          uint32_t(D3D12_STENCIL_OP_KEEP) + description.stencil_front_pass_op);
      state_desc.DepthStencilState.FrontFace.StencilFunc =
          D3D12_COMPARISON_FUNC(uint32_t(D3D12_COMPARISON_FUNC_NEVER) +
                                description.stencil_front_func);
      state_desc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP(
          uint32_t(D3D12_STENCIL_OP_KEEP) + description.stencil_back_fail_op);
      state_desc.DepthStencilState.BackFace.StencilDepthFailOp =
          D3D12_STENCIL_OP(uint32_t(D3D12_STENCIL_OP_KEEP) +
                           description.stencil_back_depth_fail_op);
      state_desc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP(
          uint32_t(D3D12_STENCIL_OP_KEEP) + description.stencil_back_pass_op);
      state_desc.DepthStencilState.BackFace.StencilFunc =
          D3D12_COMPARISON_FUNC(uint32_t(D3D12_COMPARISON_FUNC_NEVER) +
                                description.stencil_back_func);
    }
    if (state_desc.DepthStencilState.DepthEnable ||
        state_desc.DepthStencilState.StencilEnable) {
      state_desc.DSVFormat =
          RenderTargetCache::GetDepthDXGIFormat(description.depth_format);
    }
    // TODO(Triang3l): EARLY_Z_ENABLE (needs to be enabled in shaders, but alpha
    // test is dynamic - should be enabled anyway if there's no alpha test,
    // discarding and depth output).

    // Render targets and blending.
    state_desc.BlendState.IndependentBlendEnable = TRUE;
    static const D3D12_BLEND kBlendFactorMap[] = {
        D3D12_BLEND_ZERO,          D3D12_BLEND_ONE,
        D3D12_BLEND_SRC_COLOR,     D3D12_BLEND_INV_SRC_COLOR,
        D3D12_BLEND_SRC_ALPHA,     D3D12_BLEND_INV_SRC_ALPHA,
        D3D12_BLEND_DEST_COLOR,    D3D12_BLEND_INV_DEST_COLOR,
        D3D12_BLEND_DEST_ALPHA,    D3D12_BLEND_INV_DEST_ALPHA,
        D3D12_BLEND_BLEND_FACTOR,  D3D12_BLEND_INV_BLEND_FACTOR,
        D3D12_BLEND_SRC_ALPHA_SAT,
    };
    static const D3D12_BLEND_OP kBlendOpMap[] = {
        D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_SUBTRACT,     D3D12_BLEND_OP_MIN,
        D3D12_BLEND_OP_MAX, D3D12_BLEND_OP_REV_SUBTRACT,
    };
    for (uint32_t i = 0; i < 4; ++i) {
      const PipelineRenderTarget& rt = description.render_targets[i];
      if (!rt.used) {
        break;
      }
      ++state_desc.NumRenderTargets;
      state_desc.RTVFormats[i] =
          RenderTargetCache::GetColorDXGIFormat(rt.format);
      if (state_desc.RTVFormats[i] == DXGI_FORMAT_UNKNOWN) {
        assert_always();
        return nullptr;
      }
      D3D12_RENDER_TARGET_BLEND_DESC& blend_desc =
          state_desc.BlendState.RenderTarget[i];
      // Treat 1 * src + 0 * dest as disabled blending (there are opaque
      // surfaces drawn with blending enabled, but it's 1 * src + 0 * dest, in
      // Call of Duty 4 - GPU performance is better when not blending.
      if (rt.src_blend != PipelineBlendFactor::kOne ||
          rt.dest_blend != PipelineBlendFactor::kZero ||
          rt.blend_op != BlendOp::kAdd ||
          rt.src_blend_alpha != PipelineBlendFactor::kOne ||
          rt.dest_blend_alpha != PipelineBlendFactor::kZero ||
          rt.blend_op_alpha != BlendOp::kAdd) {
        blend_desc.BlendEnable = TRUE;
        blend_desc.SrcBlend = kBlendFactorMap[uint32_t(rt.src_blend)];
        blend_desc.DestBlend = kBlendFactorMap[uint32_t(rt.dest_blend)];
        blend_desc.BlendOp = kBlendOpMap[uint32_t(rt.blend_op)];
        blend_desc.SrcBlendAlpha =
            kBlendFactorMap[uint32_t(rt.src_blend_alpha)];
        blend_desc.DestBlendAlpha =
            kBlendFactorMap[uint32_t(rt.dest_blend_alpha)];
        blend_desc.BlendOpAlpha = kBlendOpMap[uint32_t(rt.blend_op_alpha)];
      }
      blend_desc.RenderTargetWriteMask = rt.write_mask;
    }
  }

  // Create the pipeline.
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  ID3D12PipelineState* state;
  if (FAILED(device->CreateGraphicsPipelineState(&state_desc,
                                                 IID_PPV_ARGS(&state)))) {
    if (description.pixel_shader != nullptr) {
      XELOGE("Failed to create graphics pipeline state with VS %.16" PRIX64
             ", PS %.16" PRIX64,
             description.vertex_shader->ucode_data_hash(),
             description.pixel_shader->ucode_data_hash());
    } else {
      XELOGE("Failed to create graphics pipeline state with VS %.16" PRIX64,
             description.vertex_shader->ucode_data_hash());
    }
    return nullptr;
  }
  std::wstring name;
  if (description.pixel_shader != nullptr) {
    name = xe::format_string(L"VS %.16I64X, PS %.16I64X",
                             description.vertex_shader->ucode_data_hash(),
                             description.pixel_shader->ucode_data_hash());
  } else {
    name = xe::format_string(L"VS %.16I64X",
                             description.vertex_shader->ucode_data_hash());
  }
  state->SetName(name.c_str());
  return state;
}

void PipelineCache::CreationThread() {
  while (true) {
    Pipeline* pipeline_to_create = nullptr;

    // Check if need to shut down or set the completion event and dequeue the
    // pipeline if there is any.
    {
      std::unique_lock<std::mutex> lock(creation_request_lock_);
      if (creation_threads_shutdown_ || creation_queue_.empty()) {
        if (creation_completion_set_event_ && creation_threads_busy_ == 0) {
          // Last pipeline in the queue created - signal the event if requested.
          creation_completion_set_event_ = false;
          creation_completion_event_->Set();
        }
        if (creation_threads_shutdown_) {
          return;
        }
        creation_request_cond_.wait(lock);
        continue;
      }
      // Take the pipeline from the queue and increment the busy thread count
      // until the pipeline in created - other threads must be able to dequeue
      // requests, but can't set the completion event until the pipelines are
      // fully created (rather than just started creating).
      pipeline_to_create = creation_queue_.front();
      creation_queue_.pop_front();
      ++creation_threads_busy_;
    }

    // Create the pipeline.
    pipeline_to_create->state =
        CreatePipelineState(pipeline_to_create->description);

    // Pipeline created - the thread is not busy anymore, safe to set the
    // completion event if needed (at the next iteration, or in some other
    // thread).
    {
      std::unique_lock<std::mutex> lock(creation_request_lock_);
      --creation_threads_busy_;
    }
  }
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
