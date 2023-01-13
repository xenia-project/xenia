/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_D3D12_COMMAND_PROCESSOR_H_
#define XENIA_GPU_D3D12_D3D12_COMMAND_PROCESSOR_H_

#include <algorithm>
#include <atomic>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#include "xenia/gpu/d3d12/d3d12_primitive_processor.h"
#include "xenia/gpu/d3d12/d3d12_render_target_cache.h"
#include "xenia/gpu/d3d12/d3d12_shared_memory.h"
#include "xenia/gpu/d3d12/d3d12_texture_cache.h"
#include "xenia/gpu/d3d12/deferred_command_list.h"
#include "xenia/gpu/d3d12/pipeline_cache.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/dxbc_shader.h"
#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/ui/d3d12/d3d12_descriptor_heap_pool.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/d3d12/d3d12_upload_buffer_pool.h"
#include "xenia/ui/d3d12/d3d12_util.h"

namespace xe {
namespace gpu {

enum class D3D12GPUSetting {
  ReadbackResolve,
  ClearMemoryPageState,
};

void D3D12SaveGPUSetting(D3D12GPUSetting setting, uint64_t value);

namespace d3d12 {
struct MemExportRange {
  uint32_t base_address_dwords;
  uint32_t size_dwords;
};
class D3D12CommandProcessor final : public CommandProcessor {
 protected:
#define OVERRIDING_BASE_CMDPROCESSOR
#include "../pm4_command_processor_declare.h"
#undef OVERRIDING_BASE_CMDPROCESSOR
 public:
  explicit D3D12CommandProcessor(D3D12GraphicsSystem* graphics_system,
                                 kernel::KernelState* kernel_state);
  ~D3D12CommandProcessor();

  void ClearCaches() override;

  void InitializeShaderStorage(const std::filesystem::path& cache_root,
                               uint32_t title_id, bool blocking) override;

  void RequestFrameTrace(const std::filesystem::path& root_path) override;

  void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length) override;

  void RestoreEdramSnapshot(const void* snapshot) override;

  ui::d3d12::D3D12Provider& GetD3D12Provider() const {
    return *static_cast<ui::d3d12::D3D12Provider*>(
        graphics_system_->provider());
  }

  // Returns the deferred drawing command list for the currently open
  // submission.
  DeferredCommandList& GetDeferredCommandList() {
    assert_true(submission_open_);
    return deferred_command_list_;
  }

  uint64_t GetCurrentSubmission() const { return submission_current_; }
  uint64_t GetCompletedSubmission() const { return submission_completed_; }

  // Must be called when a subsystem does something like UpdateTileMappings so
  // it can be awaited in CheckSubmissionFence(submission_current_) if it was
  // done after the latest ExecuteCommandLists + Signal.
  void NotifyQueueOperationsDoneDirectly() {
    queue_operations_done_since_submission_signal_ = true;
  }

  uint64_t GetCurrentFrame() const { return frame_current_; }
  uint64_t GetCompletedFrame() const { return frame_completed_; }

  // Returns true if the barrier has been inserted (the new state is different).
  bool PushTransitionBarrier(
      ID3D12Resource* resource, D3D12_RESOURCE_STATES old_state,
      D3D12_RESOURCE_STATES new_state,
      UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  void PushAliasingBarrier(ID3D12Resource* old_resource,
                           ID3D12Resource* new_resource);
  void PushUAVBarrier(ID3D12Resource* resource);
  void SubmitBarriers();

  // Finds or creates root signature for a pipeline.
  ID3D12RootSignature* GetRootSignature(const DxbcShader* vertex_shader,
                                        const DxbcShader* pixel_shader,
                                        bool tessellated);

  ui::d3d12::D3D12UploadBufferPool& GetConstantBufferPool() const {
    return *constant_buffer_pool_;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE GetViewBindlessHeapCPUStart() const {
    assert_true(bindless_resources_used_);
    return view_bindless_heap_cpu_start_;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE GetViewBindlessHeapGPUStart() const {
    assert_true(bindless_resources_used_);
    return view_bindless_heap_gpu_start_;
  }
  // Returns UINT32_MAX if no free descriptors. If the unbounded SRV range for
  // bindless resources is also used in the root signature of the draw /
  // dispatch referencing this descriptor, this must only be used to allocate
  // SRVs, otherwise it won't work on Nvidia Fermi (root signature creation will
  // fail)!
  uint32_t RequestPersistentViewBindlessDescriptor();
  void ReleaseViewBindlessDescriptorImmediately(uint32_t descriptor_index);
  // Request non-contiguous CBV/SRV/UAV descriptors for use only within the next
  // draw or dispatch command done for internal purposes. May change the current
  // descriptor heap. If the unbounded SRV range for bindless resources is also
  // used in the root signature of the draw / dispatch referencing these
  // descriptors, this must only be used to allocate SRVs, otherwise it won't
  // work on Nvidia Fermi (root signature creation will fail)!
  bool RequestOneUseSingleViewDescriptors(
      uint32_t count, ui::d3d12::util::DescriptorCpuGpuHandlePair* handles_out);
  // These are needed often, so they are always allocated.
  enum class SystemBindlessView : uint32_t {
    // Both may be bound as one root parameter.
    kSharedMemoryRawSRVAndNullRawUAVStart,
    kSharedMemoryRawSRV = kSharedMemoryRawSRVAndNullRawUAVStart,
    kNullRawUAV,

    // Both may be bound as one root parameter.
    kNullRawSRVAndSharedMemoryRawUAVStart,
    kNullRawSRV = kNullRawSRVAndSharedMemoryRawUAVStart,
    kSharedMemoryRawUAV,

    kSharedMemoryR32UintSRV,
    kSharedMemoryR32G32UintSRV,
    kSharedMemoryR32G32B32A32UintSRV,
    kSharedMemoryR32UintUAV,
    kSharedMemoryR32G32UintUAV,
    kSharedMemoryR32G32B32A32UintUAV,

    kEdramRawSRV,
    kEdramR32UintSRV,
    kEdramR32G32UintSRV,
    kEdramR32G32B32A32UintSRV,
    kEdramRawUAV,
    kEdramR32UintUAV,
    kEdramR32G32UintUAV,
    kEdramR32G32B32A32UintUAV,

    kGammaRampTableSRV,
    kGammaRampPWLSRV,

    // Beyond this point, SRVs are accessible to shaders through an unbounded
    // range - no descriptors of other types bound to shaders alongside
    // unbounded ranges - must be located beyond this point.
    kUnboundedSRVsStart,
    kNullTexture2DArray = kUnboundedSRVsStart,
    kNullTexture3D,
    kNullTextureCube,

    kCount,
  };
  ui::d3d12::util::DescriptorCpuGpuHandlePair GetSystemBindlessViewHandlePair(
      SystemBindlessView view) const;
  ui::d3d12::util::DescriptorCpuGpuHandlePair
  GetSharedMemoryUintPow2BindlessSRVHandlePair(
      uint32_t element_size_bytes_pow2) const;
  ui::d3d12::util::DescriptorCpuGpuHandlePair
  GetSharedMemoryUintPow2BindlessUAVHandlePair(
      uint32_t element_size_bytes_pow2) const;
  ui::d3d12::util::DescriptorCpuGpuHandlePair
  GetEdramUintPow2BindlessSRVHandlePair(uint32_t element_size_bytes_pow2) const;
  ui::d3d12::util::DescriptorCpuGpuHandlePair
  GetEdramUintPow2BindlessUAVHandlePair(uint32_t element_size_bytes_pow2) const;

  // Returns a single temporary GPU-side buffer within a submission for tasks
  // like texture untiling and resolving.
  ID3D12Resource* RequestScratchGPUBuffer(uint32_t size,
                                          D3D12_RESOURCE_STATES state);
  // This must be called when done with the scratch buffer, to notify the
  // command processor about the new state in case the buffer was transitioned
  // by its user.
  void ReleaseScratchGPUBuffer(ID3D12Resource* buffer,
                               D3D12_RESOURCE_STATES new_state);

  // Returns a pipeline with deferred creation by its handle. May return nullptr
  // if failed to create the pipeline.
  ID3D12PipelineState* GetD3D12PipelineByHandle(void* handle) const {
    return pipeline_cache_->GetD3D12PipelineByHandle(handle);
  }

  // Sets the current cached values to external ones. This is for cache
  // invalidation primarily. A submission must be open.
  void SetExternalPipeline(ID3D12PipelineState* pipeline);
  void SetExternalGraphicsRootSignature(ID3D12RootSignature* root_signature);
  void SetViewport(const D3D12_VIEWPORT& viewport);
  void SetScissorRect(const D3D12_RECT& scissor_rect);
  void SetStencilReference(uint32_t stencil_ref);
  void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY primitive_topology);

  // Returns the text to display in the GPU backend name in the window title.
  std::string GetWindowTitleText() const;

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;
  XE_FORCEINLINE
  void WriteRegisterForceinline(uint32_t index, uint32_t value);
  void WriteRegister(uint32_t index, uint32_t value) override;
  
  virtual void WriteRegistersFromMem(uint32_t start_index, uint32_t* base,
                                     uint32_t num_registers) override;
  /*helper functions for WriteRegistersFromMem*/
  XE_FORCEINLINE
  void WriteShaderConstantsFromMem(uint32_t start_index, uint32_t* base,
                                     uint32_t num_registers);
  XE_FORCEINLINE
  void WriteBoolLoopFromMem(uint32_t start_index, uint32_t* base,
                            uint32_t num_registers);
  XE_FORCEINLINE
  void WriteFetchFromMem(uint32_t start_index, uint32_t* base,
                         uint32_t num_registers);

  void WritePossiblySpecialRegistersFromMem(uint32_t start_index, uint32_t* base,
                                           uint32_t num_registers);
  template <uint32_t register_lower_bound, uint32_t register_upper_bound>
  XE_FORCEINLINE void WriteRegisterRangeFromMem_WithKnownBound(
      uint32_t start_index, uint32_t* base, uint32_t num_registers);
  XE_FORCEINLINE
  virtual void WriteRegisterRangeFromRing(xe::RingBuffer* ring, uint32_t base,
                                          uint32_t num_registers) override;
  template <uint32_t register_lower_bound, uint32_t register_upper_bound>
  XE_FORCEINLINE void WriteRegisterRangeFromRing_WithKnownBound(
      xe::RingBuffer* ring, uint32_t base, uint32_t num_registers);

  XE_NOINLINE
  void WriteRegisterRangeFromRing_WraparoundCase(xe::RingBuffer* ring,
                                                 uint32_t base,
                                                 uint32_t num_registers);
  XE_NOINLINE
  void WriteOneRegisterFromRing(uint32_t base,
                                        uint32_t num_times);

  XE_FORCEINLINE
  void WriteALURangeFromRing(xe::RingBuffer* ring, uint32_t base,
                             uint32_t num_times);

  XE_FORCEINLINE
  void WriteFetchRangeFromRing(xe::RingBuffer* ring, uint32_t base,
                               uint32_t num_times);

  XE_FORCEINLINE
  void WriteBoolRangeFromRing(xe::RingBuffer* ring, uint32_t base,
                              uint32_t num_times);

  XE_FORCEINLINE
  void WriteLoopRangeFromRing(xe::RingBuffer* ring, uint32_t base,
                              uint32_t num_times);

  XE_FORCEINLINE
  void WriteREGISTERSRangeFromRing(xe::RingBuffer* ring, uint32_t base,
                                   uint32_t num_times);

  XE_FORCEINLINE
  void WriteALURangeFromMem(uint32_t start_index, uint32_t* base,
                            uint32_t num_registers);

  XE_FORCEINLINE
  void WriteFetchRangeFromMem(uint32_t start_index, uint32_t* base,
                              uint32_t num_registers);

  XE_FORCEINLINE
  void WriteBoolRangeFromMem(uint32_t start_index, uint32_t* base,
                             uint32_t num_registers);

  XE_FORCEINLINE
  void WriteLoopRangeFromMem(uint32_t start_index, uint32_t* base,
                             uint32_t num_registers);

  XE_FORCEINLINE
  void WriteREGISTERSRangeFromMem(uint32_t start_index, uint32_t* base,
                                  uint32_t num_registers);

  void OnGammaRamp256EntryTableValueWritten() override;
  void OnGammaRampPWLValueWritten() override;

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

  void OnPrimaryBufferEnd() override;

  Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(xenos::PrimitiveType primitive_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override;
  XE_COLD
  XE_NOINLINE
  bool HandleMemexportGuestDMA(ID3D12Resource*& scratch_index_buffer,
                               D3D12_INDEX_BUFFER_VIEW& index_buffer_view,
                               uint32_t guest_index_base,
                               bool& retflag);
  XE_NOINLINE
  XE_COLD
  bool GatherMemexportRangesAndMakeResident(bool& retflag);
  XE_NOINLINE
  XE_COLD
  void HandleMemexportDrawOrdering_AndReadback();
  bool IssueCopy() override;
  XE_NOINLINE
  bool IssueCopy_ReadbackResolvePath();
  void InitializeTrace() override;

 private:
  static constexpr uint32_t kQueueFrames = 3;

  enum RootParameter : UINT {
    // Keep the size of the root signature at each stage 13 dwords or less
    // (better 12 or less) so it fits in user data on AMD. Descriptor tables are
    // 1 dword, root descriptors are 2 dwords (however, root descriptors require
    // less setup on the CPU - balance needs to be maintained).

    // CBVs are set in both bindful and bindless cases via root descriptors.

    // - Bindful resources - multiple root signatures depending on extra
    //   parameters.

    // These are always present.

    // Very frequently changed, especially for UI draws, and for models drawn in
    // multiple parts - contains vertex and texture fetch constants.
    kRootParameter_Bindful_FetchConstants = 0,  // +2 dwords = 2 in all.
    // Quite frequently changed (for one object drawn multiple times, for
    // instance - may contain projection matrices).
    kRootParameter_Bindful_FloatConstantsVertex,  // +2 = 4 in VS.
    // Less frequently changed (per-material).
    kRootParameter_Bindful_FloatConstantsPixel,  // +2 = 4 in PS.
    // May stay the same across many draws.
    kRootParameter_Bindful_SystemConstants,  // +2 = 6 in all.
    // Pretty rarely used and rarely changed - flow control constants.
    kRootParameter_Bindful_BoolLoopConstants,  // +2 = 8 in all.
    // Changed only when starting a new descriptor heap or when switching
    // between shared memory as SRV and UAV - shared memory byte address buffer
    // (as SRV and as UAV, either may be null if not used), and, if ROV is used
    // for EDRAM, EDRAM R32_UINT UAV.
    kRootParameter_Bindful_SharedMemoryAndEdram,  // +1 = 9 in all.

    kRootParameter_Bindful_Count_Base,

    // Extra parameter that may or may not exist:
    // - Pixel textures (+1 = 10 in PS).
    // - Pixel samplers (+1 = 11 in PS).
    // - Vertex textures (+1 = 10 in VS).
    // - Vertex samplers (+1 = 11 in VS).

    kRootParameter_Bindful_Count_Max = kRootParameter_Bindful_Count_Base + 4,

    // - Bindless resources - two global root signatures (for non-tessellated
    //   and tessellated drawing), so these are always present.

    kRootParameter_Bindless_FetchConstants = 0,    // +2 = 2 in all.
    kRootParameter_Bindless_FloatConstantsVertex,  // +2 = 4 in VS.
    kRootParameter_Bindless_FloatConstantsPixel,   // +2 = 4 in PS.
    // Changed per-material, texture and sampler descriptor indices.
    kRootParameter_Bindless_DescriptorIndicesPixel,   // +2 = 6 in PS.
    kRootParameter_Bindless_DescriptorIndicesVertex,  // +2 = 6 in VS.
    kRootParameter_Bindless_SystemConstants,          // +2 = 8 in all.
    kRootParameter_Bindless_BoolLoopConstants,        // +2 = 10 in all.
    // Changed only when switching between shared memory as SRV and UAV - shared
    // memory byte address buffer (as SRV and as UAV, either may be null if not
    // used).
    kRootParameter_Bindless_SharedMemory,  // +1 = 11 in all.
    // Unbounded sampler descriptor table - changed in case of overflow.
    kRootParameter_Bindless_SamplerHeap,  // +1 = 12 in all.
    // Unbounded SRV/UAV descriptor table - never changed.
    kRootParameter_Bindless_ViewHeap,  // +1 = 13 in all.

    kRootParameter_Bindless_Count,
  };

  struct RootBindfulExtraParameterIndices {
    uint32_t textures_pixel;
    uint32_t samplers_pixel;
    uint32_t textures_vertex;
    uint32_t samplers_vertex;
    static constexpr uint32_t kUnavailable = UINT32_MAX;
  };
  // Gets the indices of optional root parameters. Returns the total parameter
  // count.
  XE_NOINLINE
  XE_COLD
  static uint32_t GetRootBindfulExtraParameterIndices(
      const DxbcShader* vertex_shader, const DxbcShader* pixel_shader,
      RootBindfulExtraParameterIndices& indices_out);

  // BeginSubmission and EndSubmission may be called at any time. If there's an
  // open non-frame submission, BeginSubmission(true) will promote it to a
  // frame. EndSubmission(true) will close the frame no matter whether the
  // submission has already been closed.
  // Submission (ExecuteCommandLists) boundaries are implicit full UAV and
  // aliasing barriers, and also result in common resource state promotion and
  // decay.

  // Rechecks submission number and reclaims per-submission resources. Pass 0 as
  // the submission to await to simply check status, or pass submission_current_
  // to wait for all queue operations to be completed.
  void CheckSubmissionFence(uint64_t await_submission);
  // If is_guest_command is true, a new full frame - with full cleanup of
  // resources and, if needed, starting capturing - is opened if pending (as
  // opposed to simply resuming after mid-frame synchronization). Returns
  // whether a submission is open currently and the device is not removed.
  bool BeginSubmission(bool is_guest_command);
  // If is_swap is true, a full frame is closed - with, if needed, cache
  // clearing and stopping capturing. Returns whether the submission was done
  // successfully, if it has failed, leaves it open.
  bool EndSubmission(bool is_swap);
  // Checks if ending a submission right now would not cause potentially more
  // delay than it would reduce by making the GPU start working earlier - such
  // as when there are unfinished graphics pipeline creation requests that would
  // need to be fulfilled before actually submitting the command list.
  bool CanEndSubmissionImmediately() const;
  bool AwaitAllQueueOperationsCompletion() {
    CheckSubmissionFence(submission_current_);
    return submission_completed_ + 1 >= submission_current_;
  }
  // Need to await submission completion before calling.
  void ClearCommandAllocatorCache();

  // Request descriptors and automatically rebind the descriptor heap on the
  // draw command list. Refer to D3D12DescriptorHeapPool::Request for partial /
  // full update explanation. Doesn't work when bindless descriptors are used.
  uint64_t RequestViewBindfulDescriptors(
      uint64_t previous_heap_index, uint32_t count_for_partial_update,
      uint32_t count_for_full_update,
      D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle_out,
      D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle_out);
  uint64_t RequestSamplerBindfulDescriptors(
      uint64_t previous_heap_index, uint32_t count_for_partial_update,
      uint32_t count_for_full_update,
      D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle_out,
      D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle_out);

  void UpdateFixedFunctionState(const draw_util::ViewportInfo& viewport_info,
                                const draw_util::Scissor& scissor,
                                bool primitive_polygonal,
                                reg::RB_DEPTHCONTROL normalized_depth_control);

  template <bool primitive_polygonal, bool edram_rov_used>
  XE_NOINLINE void UpdateSystemConstantValues_Impl(
      bool shared_memory_is_uav, uint32_t line_loop_closing_index,
      xenos::Endian index_endian, const draw_util::ViewportInfo& viewport_info,
      uint32_t used_texture_mask, reg::RB_DEPTHCONTROL normalized_depth_control,
      uint32_t normalized_color_mask);

  void UpdateSystemConstantValues(bool shared_memory_is_uav,
                                  bool primitive_polygonal,
                                  uint32_t line_loop_closing_index,
                                  xenos::Endian index_endian,
                                  const draw_util::ViewportInfo& viewport_info,
                                  uint32_t used_texture_mask,
                                  reg::RB_DEPTHCONTROL normalized_depth_control,
                                  uint32_t normalized_color_mask);
  bool UpdateBindings(const D3D12Shader* vertex_shader,
                      const D3D12Shader* pixel_shader,
                      ID3D12RootSignature* root_signature,
                      bool shared_memory_is_uav);
  XE_COLD
  XE_NOINLINE
  void UpdateBindings_UpdateRootBindful();
  XE_NOINLINE
  XE_COLD
  bool UpdateBindings_BindfulPath(
      const size_t texture_layout_uid_vertex,
      const std::vector<xe::gpu::DxbcShader::TextureBinding>& textures_vertex,
      const size_t texture_layout_uid_pixel,
      const std::vector<xe::gpu::DxbcShader::TextureBinding>* textures_pixel,
      const size_t sampler_count_vertex, const size_t sampler_count_pixel,
      bool& retflag);

  // Returns dword count for one element for a memexport format, or 0 if it's
  // not supported by the D3D12 command processor (if it's smaller that 1 dword,
  // for instance).
  // TODO(Triang3l): Check if any game uses memexport with formats smaller than
  // 32 bits per element.
  static uint32_t GetSupportedMemExportFormatSize(xenos::ColorFormat format);

  // Returns a buffer for reading GPU data back to the CPU. Assuming
  // synchronizing immediately after use. Always in COPY_DEST state.
  ID3D12Resource* RequestReadbackBuffer(uint32_t size);

  void WriteGammaRampSRV(bool is_pwl, D3D12_CPU_DESCRIPTOR_HANDLE handle) const;

  bool device_removed_ = false;

  bool cache_clear_requested_ = false;

  HANDLE fence_completion_event_ = nullptr;

  bool submission_open_ = false;
  // Values of submission_fence_.
  uint64_t submission_current_ = 1;
  uint64_t submission_completed_ = 0;
  ID3D12Fence* submission_fence_ = nullptr;

  // For awaiting non-submission queue operations such as UpdateTileMappings in
  // AwaitAllQueueOperationsCompletion when they're queued after the latest
  // ExecuteCommandLists + Signal, thus won't be awaited by just awaiting the
  // submission.
  ID3D12Fence* queue_operations_since_submission_fence_ = nullptr;
  uint64_t queue_operations_since_submission_fence_last_ = 0;
  bool queue_operations_done_since_submission_signal_ = false;

  bool frame_open_ = false;
  // Guest frame index, since some transient resources can be reused across
  // submissions. Values updated in the beginning of a frame.
  uint64_t frame_current_ = 1;
  uint64_t frame_completed_ = 0;
  // Submission indices of frames that have already been submitted.
  uint64_t closed_frame_submissions_[kQueueFrames] = {};

  struct CommandAllocator {
    ID3D12CommandAllocator* command_allocator;
    uint64_t last_usage_submission;
    CommandAllocator* next;
  };
  CommandAllocator* command_allocator_writable_first_ = nullptr;
  CommandAllocator* command_allocator_writable_last_ = nullptr;
  CommandAllocator* command_allocator_submitted_first_ = nullptr;
  CommandAllocator* command_allocator_submitted_last_ = nullptr;
  ID3D12GraphicsCommandList* command_list_ = nullptr;
  ID3D12GraphicsCommandList1* command_list_1_ = nullptr;
  DeferredCommandList deferred_command_list_;

  // Should bindless textures and samplers be used - many times faster
  // UpdateBindings than bindful (that becomes a significant bottleneck with
  // bindful - mainly because of CopyDescriptorsSimple, which takes the majority
  // of UpdateBindings time, and that's outside the emulator's control even).
  bool bindless_resources_used_ = false;

  std::unique_ptr<D3D12SharedMemory> shared_memory_;

  std::unique_ptr<D3D12RenderTargetCache> render_target_cache_;

  std::unique_ptr<ui::d3d12::D3D12UploadBufferPool> constant_buffer_pool_;

  static constexpr uint32_t kViewBindfulHeapSize = 32768;
  static_assert(kViewBindfulHeapSize <=
                D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1);
  std::unique_ptr<ui::d3d12::D3D12DescriptorHeapPool> view_bindful_heap_pool_;
  // Currently bound descriptor heap - updated by RequestViewBindfulDescriptors.
  ID3D12DescriptorHeap* view_bindful_heap_current_;
  // Rationale: textures have 4 KB alignment in guest memory, and there can be
  // 512 MB / 4 KB in total of them at most, and multiply by 3 for different
  // swizzles, signedness, and multiple host textures for one guest texture, and
  // transient descriptors. Though in reality there will be a lot fewer of
  // course, this is just a "safe" value. The limit is 1000000 for resource
  // binding tier 2.
  static constexpr uint32_t kViewBindlessHeapSize = 262144;
  static_assert(kViewBindlessHeapSize <=
                D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2);
  ID3D12DescriptorHeap* view_bindless_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE view_bindless_heap_cpu_start_;
  D3D12_GPU_DESCRIPTOR_HANDLE view_bindless_heap_gpu_start_;
  uint32_t view_bindless_heap_allocated_ = 0;
  std::vector<uint32_t> view_bindless_heap_free_;
  // <Descriptor index, submission where requested>, sorted by the submission
  // number.
  std::deque<std::pair<uint32_t, uint64_t>> view_bindless_one_use_descriptors_;

  // Direct3D 12 only allows shader-visible heaps with no more than 2048
  // samplers (due to Nvidia addressing). However, there's also possibly a weird
  // bug in the Nvidia driver (tested on 440.97 and earlier on Windows 10 1803)
  // that caused the sampler with index 2047 not to work if a heap with 8 or
  // less samplers also exists - in case of Xenia, it's the immediate drawer's
  // sampler heap.
  // FIXME(Triang3l): Investigate the issue with the sampler 2047 on Nvidia.
  static constexpr uint32_t kSamplerHeapSize = 2000;
  static_assert(kSamplerHeapSize <= D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE);
  std::unique_ptr<ui::d3d12::D3D12DescriptorHeapPool>
      sampler_bindful_heap_pool_;
  ID3D12DescriptorHeap* sampler_bindful_heap_current_;
  ID3D12DescriptorHeap* sampler_bindless_heap_current_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_bindless_heap_cpu_start_;
  D3D12_GPU_DESCRIPTOR_HANDLE sampler_bindless_heap_gpu_start_;
  // Currently the sampler heap is used only for texture cache samplers, so
  // individual samplers are never freed, and using a simple linear allocator
  // inside the current heap without a free list.
  uint32_t sampler_bindless_heap_allocated_ = 0;
  // <Heap, overflow submission number>, if total sampler count used so far
  // exceeds kSamplerHeapSize, and the heap has been switched (this is not a
  // totally impossible situation considering Direct3D 9 has sampler parameter
  // state instead of sampler objects, and having one "unimportant" parameter
  // changed may result in doubling of sampler count). Sorted by the submission
  // number (so checking if the first can be reused is enough).
  std::deque<std::pair<ID3D12DescriptorHeap*, uint64_t>>
      sampler_bindless_heaps_overflowed_;
  // D3D12TextureCache::SamplerParameters::value -> indices within the current
  // bindless sampler heap.
  std::unordered_map<uint32_t, uint32_t> texture_cache_bindless_sampler_map_;

  // Root signatures for different descriptor counts.
  std::unordered_map<uint32_t, ID3D12RootSignature*> root_signatures_bindful_;
  ID3D12RootSignature* root_signature_bindless_vs_ = nullptr;
  ID3D12RootSignature* root_signature_bindless_ds_ = nullptr;

  std::unique_ptr<D3D12PrimitiveProcessor> primitive_processor_;

  std::unique_ptr<PipelineCache> pipeline_cache_;

  std::unique_ptr<D3D12TextureCache> texture_cache_;

  // Bytes 0x0...0x3FF - 256-entry gamma ramp table with B10G10R10X2 data (read
  // as R10G10B10X2 with swizzle).
  // Bytes 0x400...0x9FF - 128-entry PWL R16G16 gamma ramp (R - base, G - delta,
  // low 6 bits of each are zero, 3 elements per entry).
  Microsoft::WRL::ComPtr<ID3D12Resource> gamma_ramp_buffer_;
  D3D12_RESOURCE_STATES gamma_ramp_buffer_state_;
  // Upload buffer for an image that is the same as gamma_ramp_, but with
  // kQueueFrames array layers.
  Microsoft::WRL::ComPtr<ID3D12Resource> gamma_ramp_upload_buffer_;
  uint8_t* gamma_ramp_upload_buffer_mapping_ = nullptr;
  bool gamma_ramp_256_entry_table_up_to_date_ = false;
  bool gamma_ramp_pwl_up_to_date_ = false;

  struct ApplyGammaConstants {
    uint32_t size[2];
  };
  enum class ApplyGammaRootParameter : UINT {
    kConstants,
    kDestination,
    kSource,
    kRamp,

    kCount,
  };
  Microsoft::WRL::ComPtr<ID3D12RootSignature> apply_gamma_root_signature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> apply_gamma_table_pipeline_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState>
      apply_gamma_table_fxaa_luma_pipeline_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> apply_gamma_pwl_pipeline_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState>
      apply_gamma_pwl_fxaa_luma_pipeline_;

  struct FxaaConstants {
    uint32_t size[2];
    float size_inv[2];
  };
  enum class FxaaRootParameter : UINT {
    kConstants,
    kDestination,
    kSource,

    kCount,
  };
  Microsoft::WRL::ComPtr<ID3D12RootSignature> fxaa_root_signature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> fxaa_pipeline_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> fxaa_extreme_pipeline_;

  // PWL gamma ramp can result in values with more precision than 10bpc. Though
  // those sub-10bpc bits don't have any noticeable visual effect, so normally
  // R10G10B10A2_UNORM is enough. But what's the most important is that for the
  // original FXAA shader, the luma needs to be written to the alpha channel.
  // For simplicity (to avoid modifying the FXAA shader and adding more texture
  // fetches into it), and for the highest quality (preserving all 13 bits that
  // may be generated by applying the PWL gamma ramp with an increment of 2^3,
  // and also leaving some space for the result of applying fractional weights
  // to calculate the luma), using R16G16B16A16_UNORM instead of
  // R10G10B10X2_UNORM with a separate alpha texture.
  static constexpr DXGI_FORMAT kFxaaSourceTextureFormat =
      DXGI_FORMAT_R16G16B16A16_UNORM;
  // Kept in NON_PIXEL_SHADER_RESOURCE state.
  Microsoft::WRL::ComPtr<ID3D12Resource> fxaa_source_texture_;
  uint64_t fxaa_source_texture_submission_ = 0;

  // Unsubmitted barrier batch.
  std::vector<D3D12_RESOURCE_BARRIER> barriers_;

  // <Submission where requested, resource>, sorted by the submission number.
  std::deque<std::pair<uint64_t, ID3D12Resource*>> resources_for_deletion_;

  static constexpr uint32_t kScratchBufferSizeIncrement = 16 * 1024 * 1024;
  ID3D12Resource* scratch_buffer_ = nullptr;
  uint32_t scratch_buffer_size_ = 0;
  D3D12_RESOURCE_STATES scratch_buffer_state_;
  bool scratch_buffer_used_ = false;

  static constexpr uint32_t kReadbackBufferSizeIncrement = 16 * 1024 * 1024;
  ID3D12Resource* readback_buffer_ = nullptr;
  uint32_t readback_buffer_size_ = 0;

  // The current fixed-function drawing state.
  D3D12_VIEWPORT ff_viewport_;
  D3D12_RECT ff_scissor_;
  float ff_blend_factor_[4];
  uint32_t ff_stencil_ref_;
  bool ff_viewport_update_needed_;
  bool ff_scissor_update_needed_;
  bool ff_blend_factor_update_needed_;
  bool ff_stencil_ref_update_needed_;

  // Currently bound pipeline, either a graphics pipeline from the pipeline
  // cache (with potentially deferred creation - current_external_pipeline_ is
  // nullptr in this case) or a non-Xenos graphics or compute pipeline
  // (current_guest_pipeline_ is nullptr in this case).
  void* current_guest_pipeline_;
  ID3D12PipelineState* current_external_pipeline_;

  // Currently bound graphics root signature.
  ID3D12RootSignature* current_graphics_root_signature_;
  // Extra parameters which may or may not be present.
  RootBindfulExtraParameterIndices current_graphics_root_bindful_extras_;
  // Whether root parameters are up to date - reset if a new signature is bound.
  uint32_t current_graphics_root_up_to_date_;

  // System shader constants.
  alignas(XE_HOST_CACHE_LINE_SIZE)
      DxbcShaderTranslator::SystemConstants system_constants_;

  // Float constant usage masks of the last draw call.
  // chrispy: make sure accesses to these cant cross cacheline boundaries
  struct alignas(XE_HOST_CACHE_LINE_SIZE) {
    uint64_t current_float_constant_map_vertex_[4];
    uint64_t current_float_constant_map_pixel_[4];
  };
  // Constant buffer bindings.
  struct ConstantBufferBinding {
    D3D12_GPU_VIRTUAL_ADDRESS address;
    bool up_to_date;
  };
  ConstantBufferBinding cbuffer_binding_system_;
  ConstantBufferBinding cbuffer_binding_float_vertex_;
  ConstantBufferBinding cbuffer_binding_float_pixel_;
  ConstantBufferBinding cbuffer_binding_bool_loop_;
  ConstantBufferBinding cbuffer_binding_fetch_;
  ConstantBufferBinding cbuffer_binding_descriptor_indices_vertex_;
  ConstantBufferBinding cbuffer_binding_descriptor_indices_pixel_;

  // Whether the latest shared memory and EDRAM buffer binding contains the
  // shared memory UAV rather than the SRV.
  // Separate descriptor tables for the SRV and the UAV, even though only one is
  // accessed dynamically in the shaders, are used to prevent a validation
  // message about missing resource states in PIX.
  std::optional<bool> current_shared_memory_binding_is_uav_;

  // Pages with the descriptors currently used for handling Xenos draw calls.
  uint64_t draw_view_bindful_heap_index_;
  uint64_t draw_sampler_bindful_heap_index_;

  // Whether the last used texture sampler bindings have been written to the
  // current view descriptor heap.
  bool bindful_textures_written_vertex_;
  bool bindful_textures_written_pixel_;
  bool bindful_samplers_written_vertex_;
  bool bindful_samplers_written_pixel_;
  // Layout UIDs and last texture and sampler bindings written to the current
  // descriptor heaps (for bindful) or descriptor index constant buffer (for
  // bindless) with the last used descriptor layout. Valid only when:
  // - For bindful, when bindful_#_written_#_ is true.
  // - For bindless, when cbuffer_binding_descriptor_indices_#_.up_to_date is
  //   true.
  size_t current_texture_layout_uid_vertex_;
  size_t current_texture_layout_uid_pixel_;
  size_t current_sampler_layout_uid_vertex_;
  size_t current_sampler_layout_uid_pixel_;
  // Size of these should be ignored when checking whether these are up to date,
  // layout UID should be checked first (they will be different for different
  // binding counts).
  std::vector<D3D12TextureCache::TextureSRVKey>
      current_texture_srv_keys_vertex_;
  std::vector<D3D12TextureCache::TextureSRVKey> current_texture_srv_keys_pixel_;
  std::vector<D3D12TextureCache::SamplerParameters> current_samplers_vertex_;
  std::vector<D3D12TextureCache::SamplerParameters> current_samplers_pixel_;
  std::vector<uint32_t> current_sampler_bindless_indices_vertex_;
  std::vector<uint32_t> current_sampler_bindless_indices_pixel_;

  // Latest bindful descriptor handles used for handling Xenos draw calls.
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_shared_memory_srv_and_edram_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_shared_memory_uav_and_edram_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_textures_vertex_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_textures_pixel_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_samplers_vertex_;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_samplers_pixel_;

  // Current primitive topology.
  D3D_PRIMITIVE_TOPOLOGY primitive_topology_;

  draw_util::GetViewportInfoArgs previous_viewport_info_args_;
  draw_util::ViewportInfo previous_viewport_info_;
  // scratch memexport data
  MemExportRange memexport_ranges_[512];
  uint32_t memexport_range_count_ = 0;

  std::atomic<bool> pix_capture_requested_ = false;
  bool pix_capturing_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_COMMAND_PROCESSOR_H_
