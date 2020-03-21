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
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "xenia/base/platform.h"
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
                RegisterFile* register_file, bool edram_rov_used,
                uint32_t resolution_scale);
  ~PipelineCache();

  bool Initialize();
  void Shutdown();
  void ClearCache(bool shutting_down = false);

  void InitializeShaderStorage(const std::wstring& storage_root,
                               uint32_t title_id, bool blocking);
  void ShutdownShaderStorage();

  void EndSubmission();
  bool IsCreatingPipelineStates();

  D3D12Shader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                          const uint32_t* host_address, uint32_t dword_count);

  // Translates shaders if needed, also making shader info up to date.
  bool EnsureShadersTranslated(D3D12Shader* vertex_shader,
                               D3D12Shader* pixel_shader, bool tessellated,
                               PrimitiveType primitive_type);

  bool ConfigurePipeline(
      D3D12Shader* vertex_shader, D3D12Shader* pixel_shader, bool tessellated,
      PrimitiveType primitive_type, IndexFormat index_format, bool early_z,
      const RenderTargetCache::PipelineRenderTarget render_targets[5],
      void** pipeline_state_handle_out,
      ID3D12RootSignature** root_signature_out);

  // Returns a pipeline state object with deferred creation by its handle. May
  // return nullptr if failed to create the pipeline state object.
  inline ID3D12PipelineState* GetD3D12PipelineStateByHandle(
      void* handle) const {
    return reinterpret_cast<const PipelineState*>(handle)->state;
  }

 private:
  XEPACKEDSTRUCT(ShaderStoredHeader, {
    uint64_t ucode_data_hash;

    uint32_t ucode_dword_count : 16;
    ShaderType type : 1;
    PrimitiveType patch_primitive_type : 6;

    reg::SQ_PROGRAM_CNTL sq_program_cntl;

    static constexpr uint32_t kVersion = 0x20200301;
  });

  // Update PipelineDescription::kVersion if any of the Pipeline* enums are
  // changed!

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

  // Update PipelineDescription::kVersion if anything is changed!
  XEPACKEDSTRUCT(PipelineRenderTarget, {
    uint32_t used : 1;                         // 1
    ColorRenderTargetFormat format : 4;        // 5
    PipelineBlendFactor src_blend : 4;         // 9
    PipelineBlendFactor dest_blend : 4;        // 13
    BlendOp blend_op : 3;                      // 16
    PipelineBlendFactor src_blend_alpha : 4;   // 20
    PipelineBlendFactor dest_blend_alpha : 4;  // 24
    BlendOp blend_op_alpha : 3;                // 27
    uint32_t write_mask : 4;                   // 31
  });

  XEPACKEDSTRUCT(PipelineDescription, {
    uint64_t vertex_shader_hash;
    // 0 if drawing without a pixel shader.
    uint64_t pixel_shader_hash;

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
    CompareFunction depth_func : 3;                             // 20
    uint32_t depth_write : 1;                                   // 21
    uint32_t stencil_enable : 1;                                // 22
    uint32_t stencil_read_mask : 8;                             // 30
    uint32_t force_early_z : 1;                                 // 31

    uint32_t stencil_write_mask : 8;            // 8
    StencilOp stencil_front_fail_op : 3;        // 11
    StencilOp stencil_front_depth_fail_op : 3;  // 14
    StencilOp stencil_front_pass_op : 3;        // 17
    CompareFunction stencil_front_func : 3;     // 20
    StencilOp stencil_back_fail_op : 3;         // 23
    StencilOp stencil_back_depth_fail_op : 3;   // 26
    StencilOp stencil_back_pass_op : 3;         // 29
    CompareFunction stencil_back_func : 3;      // 32

    PipelineRenderTarget render_targets[4];

    static constexpr uint32_t kVersion = 0x20200309;
  });

  XEPACKEDSTRUCT(PipelineStoredDescription, {
    uint64_t description_hash;
    PipelineDescription description;
  });

  struct PipelineRuntimeDescription {
    ID3D12RootSignature* root_signature;
    D3D12Shader* vertex_shader;
    D3D12Shader* pixel_shader;
    PipelineDescription description;
  };

  bool TranslateShader(DxbcShaderTranslator& translator, D3D12Shader* shader,
                       reg::SQ_PROGRAM_CNTL cntl,
                       PrimitiveType patch_primitive_type);

  bool GetCurrentStateDescription(
      D3D12Shader* vertex_shader, D3D12Shader* pixel_shader, bool tessellated,
      PrimitiveType primitive_type, IndexFormat index_format, bool early_z,
      const RenderTargetCache::PipelineRenderTarget render_targets[5],
      PipelineRuntimeDescription& runtime_description_out);

  ID3D12PipelineState* CreateD3D12PipelineState(
      const PipelineRuntimeDescription& runtime_description);

  D3D12CommandProcessor* command_processor_;
  RegisterFile* register_file_;

  // Whether the output merger is emulated in pixel shaders.
  bool edram_rov_used_;
  uint32_t resolution_scale_;

  // Reusable shader translator.
  std::unique_ptr<DxbcShaderTranslator> shader_translator_ = nullptr;
  // All loaded shaders mapped by their guest hash key.
  std::unordered_map<uint64_t, D3D12Shader*> shader_map_;

  // Empty depth-only pixel shader for writing to depth buffer via ROV when no
  // Xenos pixel shader provided.
  std::vector<uint8_t> depth_only_pixel_shader_;

  struct PipelineState {
    // nullptr if creation has failed.
    ID3D12PipelineState* state;
    PipelineRuntimeDescription description;
  };
  // All previously generated pipeline state objects identified by hash and the
  // description.
  std::unordered_multimap<uint64_t, PipelineState*> pipeline_states_;

  // Previously used pipeline state object. This matches our current state
  // settings and allows us to quickly(ish) reuse the pipeline state if no
  // registers have changed.
  PipelineState* current_pipeline_state_ = nullptr;

  // Currently open shader storage path.
  std::wstring shader_storage_root_;
  uint32_t shader_storage_title_id_ = 0;

  // Shader storage output stream, for preload in the next emulator runs.
  FILE* shader_storage_file_ = nullptr;
  bool shader_storage_file_flush_needed_ = false;

  // Pipeline state storage output stream, for preload in the next emulator
  // runs.
  FILE* pipeline_state_storage_file_ = nullptr;
  bool pipeline_state_storage_file_flush_needed_ = false;

  // Thread for asynchronous writing to the storage streams.
  void StorageWriteThread();
  std::mutex storage_write_request_lock_;
  std::condition_variable storage_write_request_cond_;
  // Storage thread input is protected with storage_write_request_lock_, and the
  // thread is notified about its change via storage_write_request_cond_.
  std::deque<std::pair<const Shader*, reg::SQ_PROGRAM_CNTL>>
      storage_write_shader_queue_;
  std::deque<PipelineStoredDescription> storage_write_pipeline_state_queue_;
  bool storage_write_flush_shaders_ = false;
  bool storage_write_flush_pipeline_states_ = false;
  bool storage_write_thread_shutdown_ = false;
  std::unique_ptr<xe::threading::Thread> storage_write_thread_;

  // Pipeline state object creation threads.
  void CreationThread(size_t thread_index);
  void CreateQueuedPipelineStatesOnProcessorThread();
  std::mutex creation_request_lock_;
  std::condition_variable creation_request_cond_;
  // Protected with creation_request_lock_, notify_one creation_request_cond_
  // when set.
  std::deque<PipelineState*> creation_queue_;
  // Number of threads that are currently creating a pipeline state object -
  // incremented when a pipeline state object is dequeued (the completion event
  // can't be triggered before this is zero). Protected with
  // creation_request_lock_.
  size_t creation_threads_busy_ = 0;
  // Manual-reset event set when the last queued pipeline state object is
  // created and there are no more pipeline state objects to create. This is
  // triggered by the thread creating the last pipeline state object.
  std::unique_ptr<xe::threading::Event> creation_completion_event_ = nullptr;
  // Whether setting the event on completion is queued. Protected with
  // creation_request_lock_, notify_one creation_request_cond_ when set.
  bool creation_completion_set_event_ = false;
  // Creation threads with this index or above need to be shut down as soon as
  // possible. Protected with creation_request_lock_, notify_all
  // creation_request_cond_ when set.
  size_t creation_threads_shutdown_from_ = SIZE_MAX;
  std::vector<std::unique_ptr<xe::threading::Thread>> creation_threads_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_PIPELINE_CACHE_H_
