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

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "xenia/base/threading.h"
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
  PipelineCache(D3D12CommandProcessor* command_processor,
                RegisterFile* register_file, bool edram_rov_used);
  ~PipelineCache();

  bool Initialize();
  void Shutdown();
  void ClearCache();

  void EndFrame();

  D3D12Shader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                          const uint32_t* host_address, uint32_t dword_count);

  // Translates shaders if needed, also making shader info up to date.
  bool EnsureShadersTranslated(D3D12Shader* vertex_shader,
                               D3D12Shader* pixel_shader,
                               PrimitiveType primitive_type);

  bool ConfigurePipeline(
      D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
      PrimitiveType primitive_type, IndexFormat index_format,
      const RenderTargetCache::PipelineRenderTarget render_targets[5],
      void** pipeline_handle_out, ID3D12RootSignature** root_signature_out);

  // Returns a pipeline with deferred creation by its handle. May return nullptr
  // if failed to create the pipeline.
  inline ID3D12PipelineState* GetPipelineStateByHandle(void* handle) const {
    return reinterpret_cast<const Pipeline*>(handle)->state;
  }

 private:
  enum class PipelineStripCutIndex : uint32_t {
    kNone,
    kFFFF,
    kFFFFFFFF,
  };

  enum class PipelineTessellationMode : uint32_t {
    kNone,
    kDiscrete,
    kContinuous,
    kAdaptive,
  };

  enum class PipelinePatchType : uint32_t {
    kNone,
    kLine,
    kTriangle,
    kQuad,
  };

  enum class PipelinePrimitiveTopologyType : uint32_t {
    kPoint,
    kLine,
    kTriangle,
    kPatch,
  };

  enum class PipelineGeometryShader : uint32_t {
    kNone,
    kPointList,
    kRectangleList,
    kQuadList,
  };

  enum class PipelineCullMode : uint32_t {
    kNone,
    kFront,
    kBack,
  };

  enum class PipelineBlendFactor : uint32_t {
    kZero,
    kOne,
    kSrcColor,
    kInvSrcColor,
    kSrcAlpha,
    kInvSrcAlpha,
    kDestColor,
    kInvDestColor,
    kDestAlpha,
    kInvDestAlpha,
    kBlendFactor,
    kInvBlendFactor,
    kSrcAlphaSat,
  };

  struct PipelineRenderTarget {
    uint32_t used : 1;                         // 1
    ColorRenderTargetFormat format : 4;        // 5
    PipelineBlendFactor src_blend : 4;         // 9
    PipelineBlendFactor dest_blend : 4;        // 13
    BlendOp blend_op : 3;                      // 16
    PipelineBlendFactor src_blend_alpha : 4;   // 20
    PipelineBlendFactor dest_blend_alpha : 4;  // 24
    BlendOp blend_op_alpha : 3;                // 27
    uint32_t write_mask : 4;                   // 31
  };

  struct PipelineDescription {
    ID3D12RootSignature* root_signature;
    D3D12Shader* vertex_shader;
    D3D12Shader* pixel_shader;

    int32_t depth_bias;
    float depth_bias_slope_scaled;

    PipelineStripCutIndex strip_cut_index : 2;                  // 2
    PipelineTessellationMode tessellation_mode : 2;             // 4
    PipelinePrimitiveTopologyType primitive_topology_type : 2;  // 6
    PipelinePatchType patch_type : 2;                           // 8
    PipelineGeometryShader geometry_shader : 2;                 // 10
    uint32_t fill_mode_wireframe : 1;                           // 11
    PipelineCullMode cull_mode : 2;                             // 13
    uint32_t front_counter_clockwise : 1;                       // 14
    uint32_t depth_clip : 1;                                    // 15
    uint32_t rov_msaa : 1;                                      // 16
    DepthRenderTargetFormat depth_format : 1;                   // 17
    uint32_t depth_func : 3;                                    // 20
    uint32_t depth_write : 1;                                   // 21
    uint32_t stencil_enable : 1;                                // 22
    uint32_t stencil_read_mask : 8;                             // 30

    uint32_t stencil_write_mask : 8;           // 8
    uint32_t stencil_front_fail_op : 3;        // 11
    uint32_t stencil_front_depth_fail_op : 3;  // 14
    uint32_t stencil_front_pass_op : 3;        // 17
    uint32_t stencil_front_func : 3;           // 20
    uint32_t stencil_back_fail_op : 3;         // 23
    uint32_t stencil_back_depth_fail_op : 3;   // 26
    uint32_t stencil_back_pass_op : 3;         // 29
    uint32_t stencil_back_func : 3;            // 32

    PipelineRenderTarget render_targets[4];
  };

  bool TranslateShader(D3D12Shader* shader, xenos::xe_gpu_program_cntl_t cntl,
                       PrimitiveType primitive_type);

  bool GetCurrentStateDescription(
      D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
      PrimitiveType primitive_type, IndexFormat index_format,
      const RenderTargetCache::PipelineRenderTarget render_targets[5],
      PipelineDescription& description_out);

  ID3D12PipelineState* CreatePipelineState(
      const PipelineDescription& description);

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

  struct Pipeline {
    // nullptr if creation has failed.
    ID3D12PipelineState* state;
    PipelineDescription description;
  };
  // All previously generated pipelines identified by hash and the description.
  std::unordered_multimap<uint64_t, Pipeline*> pipelines_;

  // Previously used pipeline. This matches our current state settings
  // and allows us to quickly(ish) reuse the pipeline if no registers have
  // changed.
  Pipeline* current_pipeline_ = nullptr;

  // Pipeline creation threads.
  void CreationThread();
  std::mutex creation_request_lock_;
  std::condition_variable creation_request_cond_;
  // Protected with creation_request_lock_, notify_one creation_request_cond_
  // when set.
  std::deque<Pipeline*> creation_queue_;
  // Number of threads that are currently creating a pipeline - incremented when
  // a pipeline is dequeued (the completion event can't be triggered before this
  // is zero). Protected with creation_request_lock_.
  uint32_t creation_threads_busy_ = 0;
  // Manual-reset event set when the last queued pipeline is created and there
  // are no more pipelines to create. This is triggered by the thread creating
  // the last pipeline.
  std::unique_ptr<xe::threading::Event> creation_completion_event_ = nullptr;
  // Whether setting the event on completion is queued. Protected with
  // creation_request_lock_, notify_one creation_request_cond_ when set.
  bool creation_completion_set_event_ = false;
  // Whether to shut down the creation threads as soon as possible. Protected
  // with creation_request_lock_, notify_all creation_request_cond_ when set.
  bool creation_threads_shutdown_ = false;
  std::vector<std::unique_ptr<xe::threading::Thread>> creation_threads_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_PIPELINE_CACHE_H_
