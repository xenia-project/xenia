/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_D3D12_COMMAND_PROCESSOR_H_
#define XENIA_GPU_D3D12_D3D12_COMMAND_PROCESSOR_H_

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#include "xenia/gpu/d3d12/pipeline_cache.h"
#include "xenia/gpu/d3d12/primitive_converter.h"
#include "xenia/gpu/d3d12/render_target_cache.h"
#include "xenia/gpu/d3d12/shared_memory.h"
#include "xenia/gpu/d3d12/texture_cache.h"
#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/ui/d3d12/command_list.h"
#include "xenia/ui/d3d12/d3d12_context.h"
#include "xenia/ui/d3d12/pools.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor : public CommandProcessor {
 public:
  explicit D3D12CommandProcessor(D3D12GraphicsSystem* graphics_system,
                                 kernel::KernelState* kernel_state);
  ~D3D12CommandProcessor();

  void ClearCaches() override;

  void RequestFrameTrace(const std::wstring& root_path) override;

  // Needed by everything that owns transient objects.
  xe::ui::d3d12::D3D12Context* GetD3D12Context() const {
    return static_cast<xe::ui::d3d12::D3D12Context*>(context_.get());
  }

  // Returns the drawing command list for the currently open frame.
  ID3D12GraphicsCommandList* GetCurrentCommandList() const;
  ID3D12GraphicsCommandList1* GetCurrentCommandList1() const;

  // Should a rasterizer-ordered UAV of the EDRAM buffer with format conversion
  // and blending performed in pixel shaders be used instead of host render
  // targets.
  bool IsROVUsedForEDRAM() const;

  // Gets the current color write mask, taking the pixel shader's write mask
  // into account. If a shader doesn't write to a render target, it shouldn't be
  // written to and it shouldn't be even bound - otherwise, in Halo 3, one
  // render target is being destroyed by a shader not writing anything, and in
  // Banjo-Tooie, the result of clearing the top tile is being ignored because
  // there are 4 render targets bound with the same EDRAM base (clearly not
  // correct usage), but the shader only clears 1, and then EDRAM buffer stores
  // conflict with each other.
  uint32_t GetCurrentColorMask(const D3D12Shader* pixel_shader) const;

  void PushTransitionBarrier(
      ID3D12Resource* resource, D3D12_RESOURCE_STATES old_state,
      D3D12_RESOURCE_STATES new_state,
      UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  void PushAliasingBarrier(ID3D12Resource* old_resource,
                           ID3D12Resource* new_resource);
  void PushUAVBarrier(ID3D12Resource* resource);
  void SubmitBarriers();

  // Finds or creates root signature for a pipeline.
  ID3D12RootSignature* GetRootSignature(const D3D12Shader* vertex_shader,
                                        const D3D12Shader* pixel_shader,
                                        PrimitiveType primitive_type);

  ui::d3d12::UploadBufferPool* GetConstantBufferPool() const {
    return constant_buffer_pool_.get();
  }
  // Request and automatically rebind descriptors on the draw command list.
  // Refer to DescriptorHeapPool::Request for partial/full update explanation.
  uint64_t RequestViewDescriptors(uint64_t previous_full_update,
                                  uint32_t count_for_partial_update,
                                  uint32_t count_for_full_update,
                                  D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle_out,
                                  D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle_out);
  uint64_t RequestSamplerDescriptors(
      uint64_t previous_full_update, uint32_t count_for_partial_update,
      uint32_t count_for_full_update,
      D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle_out,
      D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle_out);

  // Returns a single temporary GPU-side buffer within a frame for tasks like
  // texture untiling and resolving.
  ID3D12Resource* RequestScratchGPUBuffer(uint32_t size,
                                          D3D12_RESOURCE_STATES state);
  // This must be called when done with the scratch buffer, to notify the
  // command processor about the new state in case the buffer was transitioned
  // by its user.
  void ReleaseScratchGPUBuffer(ID3D12Resource* buffer,
                               D3D12_RESOURCE_STATES new_state);

  // Sets the current SSAA sample positions, needs to be done before setting
  // render targets or copying to depth render targets.
  void SetSamplePositions(MsaaSamples sample_positions);

  // Sets the current pipeline state to a compute pipeline. This is for cache
  // invalidation primarily. A frame must be open.
  void SetComputePipeline(ID3D12PipelineState* pipeline);

  // Stores and unbinds render targets before binding changing render targets
  // externally. This is separate from SetExternalGraphicsPipeline because it
  // causes computations to be dispatched, and the scratch buffer may also be
  // used.
  void UnbindRenderTargets();

  // Sets the current pipeline state to a special drawing pipeline, invalidating
  // various cached state variables. UnbindRenderTargets may be needed before
  // calling this. A frame must be open.
  void SetExternalGraphicsPipeline(ID3D12PipelineState* pipeline,
                                   bool reset_viewport = true,
                                   bool reset_blend_factor = false,
                                   bool reset_stencil_ref = false);

  // Returns the text to display in the GPU backend name in the window title.
  std::wstring GetWindowTitleText() const;

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;

  void WriteRegister(uint32_t index, uint32_t value) override;

  void PerformSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                   uint32_t frontbuffer_height) override;

  Shader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(PrimitiveType primitive_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info) override;
  bool IssueCopy() override;

 private:
  enum RootParameter : UINT {
    // These are always present.

    // Very frequently changed, especially for UI draws, and for models drawn in
    // multiple parts - contains vertex and texture fetch constants.
    kRootParameter_FetchConstants,
    // Quite frequently changed (for one object drawn multiple times, for
    // instance - may contain projection matrices).
    kRootParameter_FloatConstantsVertex,
    // Less frequently changed (per-material).
    kRootParameter_FloatConstantsPixel,
    // Rarely changed - system constants like viewport and alpha testing.
    kRootParameter_SystemConstants,
    // Pretty rarely used and rarely changed - flow control constants.
    kRootParameter_BoolLoopConstants,
    // Never changed except for when starting a new descriptor heap - shared
    // memory byte address buffer (t0) and, if ROV is used for EDRAM, EDRAM UAV
    // (u0).
    kRootParameter_SharedMemoryAndEDRAM,

    kRootParameter_Count_Base,

    // Extra parameter that may or may not exist:
    // - Pixel textures (t1+).
    // - Pixel samplers (s0+).
    // - Vertex textures (t1+).
    // - Vertex samplers (s0+).

    kRootParameter_Count_Max = kRootParameter_Count_Base + 4,
  };

  struct RootExtraParameterIndices {
    uint32_t textures_pixel;
    uint32_t samplers_pixel;
    uint32_t textures_vertex;
    uint32_t samplers_vertex;
    static constexpr uint32_t kUnavailable = UINT32_MAX;
  };
  // Gets the indices of optional root parameters. Returns the total parameter
  // count.
  static uint32_t GetRootExtraParameterIndices(
      const D3D12Shader* vertex_shader, const D3D12Shader* pixel_shader,
      RootExtraParameterIndices& indices_out);

  // Returns true if a new frame was started.
  bool BeginFrame();
  // Returns true if an open frame was ended.
  bool EndFrame();

  void UpdateFixedFunctionState(ID3D12GraphicsCommandList* command_list);
  void UpdateSystemConstantValues(
      bool shared_memory_is_uav, PrimitiveType primitive_type,
      Endian index_endian, uint32_t edge_factor_base, uint32_t color_mask,
      const RenderTargetCache::PipelineRenderTarget render_targets[4]);
  bool UpdateBindings(ID3D12GraphicsCommandList* command_list,
                      const D3D12Shader* vertex_shader,
                      const D3D12Shader* pixel_shader,
                      ID3D12RootSignature* root_signature);

  // Returns dword count for one element for a memexport format, or 0 if it's
  // not supported by the D3D12 command processor (if it's smaller that 1 dword,
  // for instance).
  // TODO(Triang3l): Check if any game uses memexport with formats smaller than
  // 32 bits per element.
  static uint32_t GetSupportedMemExportFormatSize(ColorFormat format);

  bool cache_clear_requested_ = false;

  std::unique_ptr<ui::d3d12::CommandList>
      command_lists_[ui::d3d12::D3D12Context::kQueuedFrames] = {};

  std::unique_ptr<SharedMemory> shared_memory_ = nullptr;

  // Root signatures for different descriptor counts.
  std::unordered_map<uint32_t, ID3D12RootSignature*> root_signatures_;

  std::unique_ptr<PipelineCache> pipeline_cache_ = nullptr;

  std::unique_ptr<TextureCache> texture_cache_ = nullptr;

  std::unique_ptr<RenderTargetCache> render_target_cache_ = nullptr;

  std::unique_ptr<PrimitiveConverter> primitive_converter_ = nullptr;

  std::unique_ptr<ui::d3d12::UploadBufferPool> constant_buffer_pool_ = nullptr;
  std::unique_ptr<ui::d3d12::DescriptorHeapPool> view_heap_pool_ = nullptr;
  std::unique_ptr<ui::d3d12::DescriptorHeapPool> sampler_heap_pool_ = nullptr;

  // Mip 0 contains the normal gamma ramp (256 entries), mip 1 contains the PWL
  // ramp (128 entries). DXGI_FORMAT_R10G10B10A2_UNORM 1D.
  ID3D12Resource* gamma_ramp_texture_ = nullptr;
  D3D12_RESOURCE_STATES gamma_ramp_texture_state_;
  // Upload buffer for an image that is the same as gamma_ramp_, but with
  // ui::d3d12::D3D12Context::kQueuedFrames array layers.
  ID3D12Resource* gamma_ramp_upload_ = nullptr;
  uint8_t* gamma_ramp_upload_mapping_ = nullptr;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT
  gamma_ramp_footprints_[ui::d3d12::D3D12Context::kQueuedFrames * 2];

  static constexpr uint32_t kSwapTextureWidth = 1280;
  static constexpr uint32_t kSwapTextureHeight = 720;
  ID3D12Resource* swap_texture_ = nullptr;
  ID3D12DescriptorHeap* swap_texture_rtv_descriptor_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE swap_texture_rtv_;
  ID3D12DescriptorHeap* swap_texture_srv_descriptor_heap_ = nullptr;

  // Unsubmitted barrier batch.
  std::vector<D3D12_RESOURCE_BARRIER> barriers_;

  struct BufferForDeletion {
    ID3D12Resource* buffer;
    uint64_t last_usage_frame;
  };
  std::deque<BufferForDeletion> buffers_for_deletion_;

  static constexpr uint32_t kScratchBufferSizeIncrement = 16 * 1024 * 1024;
  ID3D12Resource* scratch_buffer_ = nullptr;
  uint32_t scratch_buffer_size_ = 0;
  D3D12_RESOURCE_STATES scratch_buffer_state_;
  bool scratch_buffer_used_ = false;

  uint32_t current_queue_frame_ = UINT32_MAX;

  std::atomic<bool> pix_capture_requested_ = false;
  bool pix_capturing_;

  // The current fixed-function drawing state.
  D3D12_VIEWPORT ff_viewport_;
  D3D12_RECT ff_scissor_;
  float ff_blend_factor_[4];
  uint32_t ff_stencil_ref_;
  bool ff_viewport_update_needed_;
  bool ff_scissor_update_needed_;
  bool ff_blend_factor_update_needed_;
  bool ff_stencil_ref_update_needed_;

  // Current SSAA sample positions (to be updated by the render target cache).
  MsaaSamples current_sample_positions_;

  // Currently bound graphics or compute pipeline.
  ID3D12PipelineState* current_pipeline_;
  // Currently bound graphics root signature.
  ID3D12RootSignature* current_graphics_root_signature_;
  // Extra parameters which may or may not be present.
  RootExtraParameterIndices current_graphics_root_extras_;
  // Whether root parameters are up to date - reset if a new signature is bound.
  uint32_t current_graphics_root_up_to_date_;

  // Currently bound descriptor heaps - update by RequestViewDescriptors and
  // RequestSamplerDescriptors.
  ID3D12DescriptorHeap* current_view_heap_;
  ID3D12DescriptorHeap* current_sampler_heap_;

  // System shader constants.
  DxbcShaderTranslator::SystemConstants system_constants_;
  ColorRenderTargetFormat system_constants_color_formats_[4];

  // Float constant usage masks of the last draw call.
  uint64_t current_float_constant_map_vertex_[4];
  uint64_t current_float_constant_map_pixel_[4];

  // Constant buffer bindings.
  struct ConstantBufferBinding {
    D3D12_GPU_VIRTUAL_ADDRESS buffer_address;
    bool up_to_date;
  };
  ConstantBufferBinding cbuffer_bindings_system_;
  ConstantBufferBinding cbuffer_bindings_float_vertex_;
  ConstantBufferBinding cbuffer_bindings_float_pixel_;
  ConstantBufferBinding cbuffer_bindings_bool_loop_;
  ConstantBufferBinding cbuffer_bindings_fetch_;

  // Pages with the descriptors currently used for handling Xenos draw calls.
  uint64_t draw_view_full_update_;
  uint64_t draw_sampler_full_update_;

  // Whether the last used texture bindings have been written to the current
  // view descriptor heap.
  bool texture_bindings_written_vertex_;
  bool texture_bindings_written_pixel_;
  // Hashes of the last texture bindings written to the current view descriptor
  // heap with the last used descriptor layout. Valid only when the
  // corresponding "written" variables are true.
  uint64_t current_texture_bindings_hash_vertex_;
  uint64_t current_texture_bindings_hash_pixel_;

  // Whether the last used samplers have been written to the current sampler
  // descriptor heap.
  bool samplers_written_vertex_;
  bool samplers_written_pixel_;
  // Hashes of the last sampler parameters written to the current sampler
  // descriptor heap with the last used descriptor layout. Valid only when the
  // corresponding "written" variables are true.
  uint64_t current_samplers_hash_vertex_;
  uint64_t current_samplers_hash_pixel_;

  // Latest descriptor handles used for handling Xenos draw calls.
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_system_constants_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_float_constants_vertex_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_float_constants_pixel_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_bool_loop_constants_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_fetch_constants_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_shared_memory_and_edram_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_textures_vertex_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_textures_pixel_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_samplers_vertex_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_samplers_pixel_;

  // Current primitive topology.
  D3D_PRIMITIVE_TOPOLOGY primitive_topology_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_COMMAND_PROCESSOR_H_
