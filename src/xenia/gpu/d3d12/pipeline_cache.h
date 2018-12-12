/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_PIPELINE_CACHE_H_
#define XENIA_GPU_D3D12_PIPELINE_CACHE_H_

#include <unordered_map>
#include <vector>

#include "third_party/xxhash/xxhash.h"

#include "xenia/gpu/d3d12/d3d12_shader.h"
#include "xenia/gpu/d3d12/render_target_cache.h"
#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor;

class PipelineCache {
 public:
  enum class UpdateStatus {
    kCompatible,
    kMismatch,
    kError,
  };

  PipelineCache(D3D12CommandProcessor* command_processor,
                RegisterFile* register_file, bool edram_rov_used);
  ~PipelineCache();

  void Shutdown();

  D3D12Shader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                          const uint32_t* host_address, uint32_t dword_count);

  // Translates shaders if needed, also making shader info up to date.
  bool EnsureShadersTranslated(D3D12Shader* vertex_shader,
                               D3D12Shader* pixel_shader,
                               PrimitiveType primitive_type);

  UpdateStatus ConfigurePipeline(
      D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
      PrimitiveType primitive_type, IndexFormat index_format,
      const RenderTargetCache::PipelineRenderTarget render_targets[5],
      ID3D12PipelineState** pipeline_out,
      ID3D12RootSignature** root_signature_out);

  void ClearCache();

 private:
  bool SetShadowRegister(uint32_t* dest, uint32_t register_name);
  bool SetShadowRegister(float* dest, uint32_t register_name);

  bool TranslateShader(D3D12Shader* shader, xenos::xe_gpu_program_cntl_t cntl,
                       PrimitiveType primitive_type);

  UpdateStatus UpdateState(
      D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
      PrimitiveType primitive_type, IndexFormat index_format,
      const RenderTargetCache::PipelineRenderTarget render_targets[5]);

  // pRootSignature, VS, PS, DS, HS, GS, PrimitiveTopologyType.
  UpdateStatus UpdateShaderStages(D3D12Shader* vertex_shader,
                                  D3D12Shader* pixel_shader,
                                  PrimitiveType primitive_type);
  // BlendState, NumRenderTargets, RTVFormats.
  UpdateStatus UpdateBlendStateAndRenderTargets(
      D3D12Shader* pixel_shader,
      const RenderTargetCache::PipelineRenderTarget render_targets[4]);
  // RasterizerState.
  UpdateStatus UpdateRasterizerState(PrimitiveType primitive_type);
  // DepthStencilState, DSVFormat.
  UpdateStatus UpdateDepthStencilState(DXGI_FORMAT format);
  // IBStripCutValue.
  UpdateStatus UpdateIBStripCutValue(IndexFormat index_format);

  D3D12CommandProcessor* command_processor_;
  RegisterFile* register_file_;

  // Whether the output merger is emulated in pixel shaders.
  bool edram_rov_used_;

  // Reusable shader translator.
  std::unique_ptr<DxbcShaderTranslator> shader_translator_ = nullptr;
  // All loaded shaders mapped by their guest hash key.
  std::unordered_map<uint64_t, D3D12Shader*> shader_map_;

  // Empty depth-only pixel shader for writing to depth buffer via ROV when no
  // Xenos pixel shader provided.
  std::vector<uint8_t> depth_only_pixel_shader_;

  // Hash state used to incrementally produce pipeline hashes during update.
  // By the time the full update pass has run the hash will represent the
  // current state in a way that can uniquely identify the produced
  // ID3D12PipelineState.
  XXH64_state_t hash_state_;
  struct Pipeline {
    ID3D12PipelineState* state;
    // Root signature taken from the command processor.
    ID3D12RootSignature* root_signature;
  };
  // All previously generated pipelines mapped by hash.
  std::unordered_map<uint64_t, Pipeline*> pipelines_;
  Pipeline* GetPipeline(uint64_t hash_key);

  // Previously used pipeline. This matches our current state settings
  // and allows us to quickly(ish) reuse the pipeline if no registers have
  // changed.
  Pipeline* current_pipeline_ = nullptr;

  // Description of the pipeline being created.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC update_desc_;

  struct UpdateShaderStagesRegisters {
    D3D12Shader* vertex_shader;
    D3D12Shader* pixel_shader;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type;
    // Primitive type if it needs hull/domain shaders or a geometry shader, or
    // kNone if should be transformed with only a vertex shader.
    PrimitiveType hs_gs_ds_primitive_type;
    // Tessellation mode - ignored when not using tessellation (when
    // hs_gs_ds_primitive_type is not a patch).
    TessellationMode tessellation_mode;

    UpdateShaderStagesRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_shader_stages_regs_;

  struct UpdateBlendStateAndRenderTargetsRegisters {
    RenderTargetCache::PipelineRenderTarget render_targets[5];
    // RB_COLOR_MASK with unused render targets removed.
    uint32_t color_mask;
    // Blend control updated only for used render targets.
    uint32_t blendcontrol[4];
    bool colorcontrol_blend_enable;

    UpdateBlendStateAndRenderTargetsRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_blend_state_and_render_targets_regs_;

  struct UpdateRasterizerStateRegisters {
    // The constant factor of the polygon offset is multiplied by a value that
    // depends on the depth buffer format here.
    float poly_offset;
    float poly_offset_scale;
    uint8_t cull_mode;
    bool fill_mode_wireframe;
    bool front_counter_clockwise;
    bool depth_clamp_enable;
    bool msaa_rov;

    UpdateRasterizerStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_rasterizer_state_regs_;

  struct UpdateDepthStencilStateRegisters {
    DXGI_FORMAT format;
    uint32_t rb_depthcontrol;
    uint32_t rb_stencilrefmask;

    UpdateDepthStencilStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_depth_stencil_state_regs_;

  struct UpdateIBStripCutValueRegisters {
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ib_strip_cut_value;

    UpdateIBStripCutValueRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_ib_strip_cut_value_regs_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_PIPELINE_CACHE_H_
