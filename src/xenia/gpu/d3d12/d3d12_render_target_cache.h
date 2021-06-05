/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_D3D12_RENDER_TARGET_CACHE_H_
#define XENIA_GPU_D3D12_D3D12_RENDER_TARGET_CACHE_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/gpu/d3d12/d3d12_shared_memory.h"
#include "xenia/gpu/d3d12/texture_cache.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/render_target_cache.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"
#include "xenia/ui/d3d12/d3d12_cpu_descriptor_pool.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/d3d12/d3d12_upload_buffer_pool.h"
#include "xenia/ui/d3d12/d3d12_util.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor;

class D3D12RenderTargetCache final : public RenderTargetCache {
 public:
  D3D12RenderTargetCache(const RegisterFile& register_file,
                         D3D12CommandProcessor& command_processor,
                         TraceWriter& trace_writer,
                         bool bindless_resources_used)
      : RenderTargetCache(register_file),
        command_processor_(command_processor),
        trace_writer_(trace_writer),
        bindless_resources_used_(bindless_resources_used) {}
  ~D3D12RenderTargetCache() override;

  bool Initialize();
  void Shutdown(bool from_destructor = false);

  void CompletedSubmissionUpdated();
  void BeginSubmission();

  Path GetPath() const override { return path_; }

  uint32_t GetResolutionScale() const override { return resolution_scale_; }

  bool Update(bool is_rasterization_done,
              uint32_t shader_writes_color_targets) override;

  void InvalidateCommandListRenderTargets() {
    are_current_command_list_render_targets_valid_ = false;
  }

  bool msaa_2x_supported() const { return msaa_2x_supported_; }

  void WriteEdramRawSRVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  void WriteEdramRawUAVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  void WriteEdramUintPow2SRVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                       uint32_t element_size_bytes_pow2);
  void WriteEdramUintPow2UAVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                       uint32_t element_size_bytes_pow2);

  // Performs the resolve to a shared memory area according to the current
  // register values, and also clears the render targets if needed. Must be in a
  // frame for calling.
  bool Resolve(const Memory& memory, D3D12SharedMemory& shared_memory,
               TextureCache& texture_cache, uint32_t& written_address_out,
               uint32_t& written_length_out);

  // Returns true if any downloads were submitted to the command processor.
  bool InitializeTraceSubmitDownloads();
  void InitializeTraceCompleteDownloads();
  void RestoreEdramSnapshot(const void* snapshot);

  // For host render targets.

  bool gamma_render_target_as_srgb() const {
    return gamma_render_target_as_srgb_;
  }

  // Using R16G16[B16A16]_SNORM, which are -1...1, not the needed -32...32.
  // Persistent data doesn't depend on this, so can be overriden by per-game
  // configuration.
  bool IsFixed16TruncatedToMinus1To1() const {
    return GetPath() == Path::kHostRenderTargets &&
           !cvars::snorm16_render_target_full_range;
  }

  DepthFloat24Conversion depth_float24_conversion() const {
    return depth_float24_conversion_;
  }

  DXGI_FORMAT GetColorResourceDXGIFormat(
      xenos::ColorRenderTargetFormat format) const;
  DXGI_FORMAT GetColorDrawDXGIFormat(
      xenos::ColorRenderTargetFormat format) const;
  DXGI_FORMAT GetColorOwnershipTransferDXGIFormat(
      xenos::ColorRenderTargetFormat format,
      bool* is_integer_out = nullptr) const;
  static DXGI_FORMAT GetDepthResourceDXGIFormat(
      xenos::DepthRenderTargetFormat format);
  static DXGI_FORMAT GetDepthDSVDXGIFormat(
      xenos::DepthRenderTargetFormat format);
  static DXGI_FORMAT GetDepthSRVDepthDXGIFormat(
      xenos::DepthRenderTargetFormat format);
  static DXGI_FORMAT GetDepthSRVStencilDXGIFormat(
      xenos::DepthRenderTargetFormat format);

 protected:
  class D3D12RenderTarget final : public RenderTarget {
   public:
    // descriptor_draw_srgb is only used for k_8_8_8_8 render targets when host
    // sRGB (gamma_render_target_as_srgb) is used. descriptor_load is present
    // when the DXGI formats are different for drawing and bit-exact loading
    // (for NaN pattern preservation across EDRAM tile ownership transfers in
    // floating-point formats, and to distinguish between two -1 representations
    // in snorm formats).
    D3D12RenderTarget(
        RenderTargetKey key, D3D12RenderTargetCache& render_target_cache,
        ID3D12Resource* resource,
        ui::d3d12::D3D12CpuDescriptorPool::Descriptor&& descriptor_draw,
        ui::d3d12::D3D12CpuDescriptorPool::Descriptor&& descriptor_draw_srgb,
        ui::d3d12::D3D12CpuDescriptorPool::Descriptor&&
            descriptor_load_separate,
        ui::d3d12::D3D12CpuDescriptorPool::Descriptor&& descriptor_srv,
        ui::d3d12::D3D12CpuDescriptorPool::Descriptor&& descriptor_srv_stencil,
        D3D12_RESOURCE_STATES resource_state)
        : RenderTarget(key),
          render_target_cache_(render_target_cache),
          resource_(resource),
          descriptor_draw_(std::move(descriptor_draw)),
          descriptor_draw_srgb_(std::move(descriptor_draw_srgb)),
          descriptor_load_separate_(std::move(descriptor_load_separate)),
          descriptor_srv_(std::move(descriptor_srv)),
          descriptor_srv_stencil_(std::move(descriptor_srv_stencil)),
          resource_state_(resource_state) {}

    ID3D12Resource* resource() const { return resource_.Get(); }
    const ui::d3d12::D3D12CpuDescriptorPool::Descriptor& descriptor_draw()
        const {
      return descriptor_draw_;
    }
    const ui::d3d12::D3D12CpuDescriptorPool::Descriptor& descriptor_draw_srgb()
        const {
      return descriptor_draw_srgb_;
    }
    const ui::d3d12::D3D12CpuDescriptorPool::Descriptor& descriptor_srv()
        const {
      return descriptor_srv_;
    }
    const ui::d3d12::D3D12CpuDescriptorPool::Descriptor&
    descriptor_srv_stencil() const {
      return descriptor_srv_stencil_;
    }
    const ui::d3d12::D3D12CpuDescriptorPool::Descriptor&
    descriptor_load_separate() const {
      return descriptor_load_separate_;
    }

    D3D12_RESOURCE_STATES SetResourceState(D3D12_RESOURCE_STATES new_state) {
      D3D12_RESOURCE_STATES old_state = resource_state_;
      resource_state_ = new_state;
      return old_state;
    }

    uint32_t temporary_srv_descriptor_index() const {
      return temporary_srv_descriptor_index_;
    }
    void SetTemporarySRVDescriptorIndex(uint32_t index) {
      temporary_srv_descriptor_index_ = index;
    }
    uint32_t temporary_srv_descriptor_index_stencil() const {
      return temporary_srv_descriptor_index_stencil_;
    }
    void SetTemporarySRVDescriptorIndexStencil(uint32_t index) {
      temporary_srv_descriptor_index_stencil_ = index;
    }
    uint32_t temporary_sort_index() const { return temporary_sort_index_; }
    void SetTemporarySortIndex(uint32_t index) {
      temporary_sort_index_ = index;
    }

   private:
    D3D12RenderTargetCache& render_target_cache_;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    ui::d3d12::D3D12CpuDescriptorPool::Descriptor descriptor_draw_;
    ui::d3d12::D3D12CpuDescriptorPool::Descriptor descriptor_draw_srgb_;
    ui::d3d12::D3D12CpuDescriptorPool::Descriptor descriptor_load_separate_;
    // Texture SRV non-shader-visible descriptors, to prepare shader-visible
    // descriptors faster, by copying rather than by creating every time.
    // TODO(Triang3l): With bindless resources, persistently store them in the
    // heap.
    ui::d3d12::D3D12CpuDescriptorPool::Descriptor descriptor_srv_;
    ui::d3d12::D3D12CpuDescriptorPool::Descriptor descriptor_srv_stencil_;
    D3D12_RESOURCE_STATES resource_state_;
    // Temporary storage for indices in operations like transfers and dumps.
    uint32_t temporary_srv_descriptor_index_ = UINT32_MAX;
    uint32_t temporary_srv_descriptor_index_stencil_ = UINT32_MAX;
    uint32_t temporary_sort_index_ = 0;
  };

  uint32_t GetMaxRenderTargetWidth() const override {
    return D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
  }
  uint32_t GetMaxRenderTargetHeight() const override {
    return D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
  }

  xenos::ColorRenderTargetFormat GetHostRelevantColorFormat(
      xenos::ColorRenderTargetFormat format) const override;

  RenderTarget* CreateRenderTarget(RenderTargetKey key) override;

  bool IsHostDepthEncodingDifferent(
      xenos::DepthRenderTargetFormat format) const override;

  void RequestPixelShaderInterlockBarrier() override;

 private:
  enum class EdramBufferModificationStatus {
    // No uncommitted ROV/UAV writes.
    kUnmodified,
    // Need to commit before the next ROV usage with overlap.
    kAsROV,
    // Need to commit before any next ROV usage.
    kAsUAV,
  };
  void TransitionEdramBuffer(D3D12_RESOURCE_STATES new_state);
  void MarkEdramBufferModified(
      EdramBufferModificationStatus modification_status =
          EdramBufferModificationStatus::kAsUAV);
  void CommitEdramBufferUAVWrites(EdramBufferModificationStatus commit_status =
                                      EdramBufferModificationStatus::kAsROV);

  D3D12CommandProcessor& command_processor_;
  TraceWriter& trace_writer_;
  bool bindless_resources_used_;

  Path path_ = Path::kHostRenderTargets;
  uint32_t resolution_scale_ = 1;

  // For host render targets, an EDRAM-sized scratch buffer for:
  // - Guest render target data copied from host render targets during copying
  //   in resolves and in frame trace creation.
  // - Host float32 depth in ownership transfers when the host depth texture and
  //   the destination are the same.
  // For rasterizer-ordered view, the buffer containing the EDRAM data.
  // (Note that if a hybrid RTV / DSV + ROV approach to color render targets is
  //  added, which is, however, unlikely as it would have very complicated
  //  interaction with depth / stencil testing, host depth will need to be
  //  copied to a different buffer - the same range may have ROV-owned color and
  //  host float32 depth at the same time).
  ID3D12Resource* edram_buffer_ = nullptr;
  D3D12_RESOURCE_STATES edram_buffer_state_;
  EdramBufferModificationStatus edram_buffer_modification_status_ =
      EdramBufferModificationStatus::kUnmodified;

  // Non-shader-visible descriptor heap containing pre-created SRV and UAV
  // descriptors of the EDRAM buffer, for faster binding (by copying rather
  // than creation).
  enum class EdramBufferDescriptorIndex : uint32_t {
    kRawSRV,
    kR32UintSRV,
    kR32G32UintSRV,
    kR32G32B32A32UintSRV,
    kRawUAV,
    kR32UintUAV,
    kR32G32UintUAV,
    kR32G32B32A32UintUAV,

    kCount,
  };
  ID3D12DescriptorHeap* edram_buffer_descriptor_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE edram_buffer_descriptor_heap_start_;

  // Resolve copying root signature and pipelines.
  // Parameter 0 - draw_util::ResolveCopyShaderConstants or its ::DestRelative.
  // Parameter 1 - destination (shared memory or a part of it).
  // Parameter 2 - source (EDRAM).
  ID3D12RootSignature* resolve_copy_root_signature_ = nullptr;
  static const std::pair<const void*, size_t>
      kResolveCopyShaders[size_t(draw_util::ResolveCopyShaderIndex::kCount)];
  ID3D12PipelineState* resolve_copy_pipelines_[size_t(
      draw_util::ResolveCopyShaderIndex::kCount)] = {};

  // For traces.
  ID3D12Resource* edram_snapshot_download_buffer_ = nullptr;
  std::unique_ptr<ui::d3d12::D3D12UploadBufferPool>
      edram_snapshot_restore_pool_;

  // For host render targets.

  enum TransferCBVRegister : uint32_t {
    kTransferCBVRegisterStencilMask,
    kTransferCBVRegisterAddress,
    kTransferCBVRegisterHostDepthAddress,
  };
  enum TransferSRVRegister : uint32_t {
    kTransferSRVRegisterColor,
    kTransferSRVRegisterDepth,
    kTransferSRVRegisterStencil,
    kTransferSRVRegisterHostDepth,
    kTransferSRVRegisterCount,
  };
  enum TransferUsedRootParameter : uint32_t {
    // Changed 8 times per transfer.
    kTransferUsedRootParameterStencilMaskConstant,
    kTransferUsedRootParameterColorSRV,
    // Mutually exclusive with ColorSRV.
    kTransferUsedRootParameterDepthSRV,
    // Mutually exclusive with ColorSRV.
    kTransferUsedRootParameterStencilSRV,
    // May happen to be the same for different sources.
    kTransferUsedRootParameterAddressConstant,
    kTransferUsedRootParameterHostDepthSRV,
    kTransferUsedRootParameterHostDepthAddressConstant,
    kTransferUsedRootParameterCount,

    kTransferUsedRootParameterStencilMaskConstantBit =
        uint32_t(1) << kTransferUsedRootParameterStencilMaskConstant,
    kTransferUsedRootParameterColorSRVBit =
        uint32_t(1) << kTransferUsedRootParameterColorSRV,
    kTransferUsedRootParameterDepthSRVBit =
        uint32_t(1) << kTransferUsedRootParameterDepthSRV,
    kTransferUsedRootParameterStencilSRVBit =
        uint32_t(1) << kTransferUsedRootParameterStencilSRV,
    kTransferUsedRootParameterAddressConstantBit =
        uint32_t(1) << kTransferUsedRootParameterAddressConstant,
    kTransferUsedRootParameterHostDepthSRVBit =
        uint32_t(1) << kTransferUsedRootParameterHostDepthSRV,
    kTransferUsedRootParameterHostDepthAddressConstantBit =
        uint32_t(1) << kTransferUsedRootParameterHostDepthAddressConstant,

    kTransferUsedRootParametersDescriptorMask =
        kTransferUsedRootParameterColorSRVBit |
        kTransferUsedRootParameterDepthSRVBit |
        kTransferUsedRootParameterStencilSRVBit |
        kTransferUsedRootParameterHostDepthSRVBit,
  };
  enum class TransferRootSignatureIndex {
    kColor,
    kDepth,
    kDepthStencil,
    kColorToStencilBit,
    kStencilToStencilBit,
    kColorAndHostDepth,
    kDepthAndHostDepth,
    kDepthStencilAndHostDepth,
    kCount,
  };
  static const uint32_t
      kTransferUsedRootParameters[size_t(TransferRootSignatureIndex::kCount)];
  enum class TransferMode : uint32_t {
    // 1 SRV (color texture), source constant.
    kColorToDepth,
    // 1 SRV (color texture), source constant.
    kColorToColor,

    // 1 or 2 SRVs (depth texture, stencil texture if SV_StencilRef is
    // supported), source constant.
    kDepthToDepth,
    // 2 SRVs (depth texture, stencil texture), source constant.
    kDepthToColor,

    // 1 SRV (color texture), mask constant (most frequently changed, 8 times
    // per transfer), source constant.
    kColorToStencilBit,
    // 1 SRV (stencil texture), mask constant, source constant.
    kDepthToStencilBit,

    // Two-source modes, using the host depth if it, when converted to the guest
    // format, matches what's in the owner source (not modified, keep host
    // precision), or the guest data otherwise (significantly modified, possibly
    // cleared). Stencil for SV_StencilRef is always taken from the guest
    // source.

    // 2 SRVs (color texture, host depth texture or buffer), source constant,
    // host depth source constant.
    kColorAndHostDepthToDepth,
    // When using different source and destination depth formats. 2 or 3 SRVs
    // (depth texture, stencil texture if SV_StencilRef is supported, host depth
    // texture or buffer), source constant, host depth source constant.
    kDepthAndHostDepthToDepth,

    kCount,
  };
  enum class TransferOutput {
    kColor,
    kDepth,
    // With this output, kTransferCBVRegisterStencilMask is used.
    kStencilBit,
  };
  struct TransferModeInfo {
    TransferOutput output;
    TransferRootSignatureIndex root_signature_no_stencil_ref;
    TransferRootSignatureIndex root_signature_with_stencil_ref;
  };
  static const TransferModeInfo kTransferModes[size_t(TransferMode::kCount)];

  union TransferShaderKey {
    struct {
      xenos::MsaaSamples dest_msaa_samples : xenos::kMsaaSamplesBits;
      uint32_t dest_host_relevant_format : xenos::kRenderTargetFormatBits;
      xenos::MsaaSamples source_msaa_samples : xenos::kMsaaSamplesBits;
      // Always 1x when host_depth_source_is_copy is true not to create the same
      // pipeline for different MSAA sample counts as it doesn't matter in this
      // case.
      xenos::MsaaSamples host_depth_source_msaa_samples
          : xenos::kMsaaSamplesBits;
      uint32_t source_host_relevant_format : xenos::kRenderTargetFormatBits;
      // If host depth is also fetched, whether it's pre-copied to the EDRAM
      // buffer (but since it's just a scratch buffer, with tiles laid out
      // linearly with the same pitch as in the original render target; also no
      // swapping of 40-sample columns as opposed to the host render target -
      // this is done only for the color source).
      uint32_t host_depth_source_is_copy : 1;

      // Last bits because this affects the root signature - after sorting, only
      // change it as fewer times as possible. Depth buffers have an additional
      // depth SRV.
      static_assert(size_t(TransferMode::kCount) <= (size_t(1) << 3));
      TransferMode mode : 3;
    };
    uint32_t key = 0;
    struct Hasher {
      size_t operator()(const TransferShaderKey& key) const {
        return std::hash<uint32_t>{}(key.key);
      }
    };
    bool operator==(const TransferShaderKey& other_key) const {
      return key == other_key.key;
    }
    bool operator!=(const TransferShaderKey& other_key) const {
      return !(*this == other_key);
    }
    bool operator<(const TransferShaderKey& other_key) const {
      return key < other_key.key;
    }
  };

  union TransferAddressConstant {
    struct {
      // All in tiles.
      uint32_t dest_pitch : xenos::kEdramPitchTilesBits;
      uint32_t source_pitch : xenos::kEdramPitchTilesBits;
      // Safe to use 12 bits for signed difference - no ownership transfer can
      // ever occur between render targets with EDRAM base >= 2048 as this would
      // result in 0-length spans. 10 + 10 + 12 is exactly 32, any more bits,
      // and more root 32-bit constants will be used.
      // Destination base in tiles minus source base in tiles (not vice versa
      // because this is a transform of the coordinate system, not addresses
      // themselves).
      // 0 for host_depth_source_is_copy (ignored in this case anyway as
      // destination == source anyway).
      int32_t source_to_dest : xenos::kEdramBaseTilesBits;
    };
    uint32_t constant = 0;
    bool operator==(const TransferAddressConstant& other_constant) const {
      return constant == other_constant.constant;
    }
    bool operator!=(const TransferAddressConstant& other_constant) const {
      return !(*this == other_constant);
    }
  };
  static_assert(sizeof(TransferAddressConstant) == sizeof(uint32_t));

  struct TransferInvocation {
    Transfer transfer;
    TransferShaderKey shader_key;
    TransferInvocation(const Transfer& transfer,
                       const TransferShaderKey& shader_key)
        : transfer(transfer), shader_key(shader_key) {}
    bool operator<(const TransferInvocation& other_invocation) {
      // TODO(Triang3l): See if it may be better to sort by the source in the
      // first place, especially when reading the same data multiple times (like
      // to write the stencil bits after depth) for better read locality.
      // Sort by the shader key primarily to reduce pipeline state (context)
      // switches.
      if (shader_key != other_invocation.shader_key) {
        return shader_key < other_invocation.shader_key;
      }
      // Host depth render targets are changed rarely if they exist, won't save
      // many binding changes, ignore them for simplicity (their existence is
      // caught by the shader key change).
      assert_not_null(transfer.source);
      assert_not_null(other_invocation.transfer.source);
      uint32_t source_index =
          static_cast<const D3D12RenderTarget*>(transfer.source)
              ->temporary_sort_index();
      uint32_t other_source_index = static_cast<const D3D12RenderTarget*>(
                                        other_invocation.transfer.source)
                                        ->temporary_sort_index();
      if (source_index != other_source_index) {
        return source_index < other_source_index;
      }
      return transfer.start_tiles < other_invocation.transfer.start_tiles;
    }
    bool CanBeMergedIntoOneDraw(
        const TransferInvocation& other_invocation) const {
      return shader_key == other_invocation.shader_key &&
             transfer.AreSourcesSame(other_invocation.transfer);
    }
  };

  union HostDepthStoreRectangleConstant {
    struct {
      // - 1 because the maximum is 0x1FFF / 8, not 0x2000 / 8.
      uint32_t x_pixels_div_8 : xenos::kResolveSizeBits - 1 -
                                xenos::kResolveAlignmentPixelsLog2;
      uint32_t y_pixels_div_8 : xenos::kResolveSizeBits - 1 -
                                xenos::kResolveAlignmentPixelsLog2;
      uint32_t width_pixels_div_8_minus_1 : xenos::kResolveSizeBits - 1 -
                                            xenos::kResolveAlignmentPixelsLog2;
    };
    uint32_t constant = 0;
  };
  static_assert(sizeof(HostDepthStoreRectangleConstant) == sizeof(uint32_t));

  union HostDepthStoreRenderTargetConstant {
    struct {
      uint32_t pitch_tiles : xenos::kEdramPitchTilesBits;
      // 1 to 3.
      uint32_t resolution_scale : 2;
      // For native 2x MSAA vs. 2x over 4x.
      uint32_t second_sample_index : 2;
    };
    uint32_t constant = 0;
  };
  static_assert(sizeof(HostDepthStoreRenderTargetConstant) == sizeof(uint32_t));

  enum {
    kHostDepthStoreRootParameterRectangleConstant,
    kHostDepthStoreRootParameterRenderTargetConstant,
    kHostDepthStoreRootParameterSource,
    kHostDepthStoreRootParameterDest,
    kHostDepthStoreRootParameterCount,
  };

  union DumpPipelineKey {
    struct {
      xenos::MsaaSamples msaa_samples : 2;
      uint32_t host_relevant_format : 4;
      // Last bit because this affects the root signature - after sorting, only
      // change it at most once. Depth buffers have an additional stencil SRV.
      uint32_t is_depth : 1;
    };
    uint32_t key = 0;
    struct Hasher {
      size_t operator()(const DumpPipelineKey& key) const {
        return std::hash<uint32_t>{}(key.key);
      }
    };
    bool operator==(const DumpPipelineKey& other_key) const {
      return key == other_key.key;
    }
    bool operator!=(const DumpPipelineKey& other_key) const {
      return !(*this == other_key);
    }
    bool operator<(const DumpPipelineKey& other_key) const {
      return key < other_key.key;
    }

    xenos::ColorRenderTargetFormat GetColorFormat() const {
      assert_false(is_depth);
      return xenos::ColorRenderTargetFormat(host_relevant_format);
    }
    xenos::DepthRenderTargetFormat GetDepthFormat() const {
      assert_true(is_depth);
      return xenos::DepthRenderTargetFormat(host_relevant_format);
    }
  };

  union DumpOffsets {
    struct {
      // Absolute index of the first thread group's tile within the source
      // texture.
      uint32_t first_group_tile_source_relative : xenos::kEdramBaseTilesBits;
      uint32_t source_base_tiles : xenos::kEdramBaseTilesBits;
    };
    uint32_t offsets = 0;
    bool operator==(const DumpOffsets& other_offsets) const {
      return offsets == other_offsets.offsets;
    }
    bool operator!=(const DumpOffsets& other_offsets) const {
      return !(*this == other_offsets);
    }
  };
  static_assert(sizeof(DumpOffsets) == sizeof(uint32_t));

  union DumpPitches {
    struct {
      // Both in tiles.
      uint32_t source_pitch : xenos::kEdramPitchTilesBits;
      uint32_t dest_pitch : xenos::kEdramPitchTilesBits;
    };
    uint32_t pitches = 0;
    bool operator==(const DumpPitches& other_pitches) const {
      return pitches == other_pitches.pitches;
    }
    bool operator!=(const DumpPitches& other_pitches) const {
      return !(*this == other_pitches);
    }
  };
  static_assert(sizeof(DumpPitches) == sizeof(uint32_t));

  enum DumpCbuffer : uint32_t {
    kDumpCbufferOffsets,
    kDumpCbufferPitches,
    kDumpCbufferCount,
  };

  enum DumpRootParameter : uint32_t {
    // May be changed multiple times for the same source.
    kDumpRootParameterOffsets,
    // One resolve may need multiple sources.
    kDumpRootParameterSource,

    // May be different for different sources.
    kDumpRootParameterColorPitches = kDumpRootParameterSource + 1,
    // Only changed between 32bpp and 64bpp.
    kDumpRootParameterColorEdram,

    kDumpRootParameterColorCount,

    // Same change frequency than the source (though currently the command
    // processor can't contiguously allocate multiple descriptors with bindless,
    // when such functionality is added, switch to one root signature).
    kDumpRootParameterDepthStencil = kDumpRootParameterSource + 1,
    kDumpRootParameterDepthPitches,
    kDumpRootParameterDepthEdram,

    kDumpRootParameterDepthCount,
  };

  struct DumpInvocation {
    ResolveCopyDumpRectangle rectangle;
    DumpPipelineKey pipeline_key;
    DumpInvocation(const ResolveCopyDumpRectangle& rectangle,
                   const DumpPipelineKey& pipeline_key)
        : rectangle(rectangle), pipeline_key(pipeline_key) {}
    bool operator<(const DumpInvocation& other_invocation) {
      // Sort by the pipeline key primarily to reduce pipeline state (context)
      // switches.
      if (pipeline_key != other_invocation.pipeline_key) {
        return pipeline_key < other_invocation.pipeline_key;
      }
      assert_not_null(rectangle.render_target);
      uint32_t render_target_index =
          static_cast<const D3D12RenderTarget*>(rectangle.render_target)
              ->temporary_sort_index();
      const ResolveCopyDumpRectangle& other_rectangle =
          other_invocation.rectangle;
      uint32_t other_render_target_index =
          static_cast<const D3D12RenderTarget*>(other_rectangle.render_target)
              ->temporary_sort_index();
      if (render_target_index != other_render_target_index) {
        return render_target_index < other_render_target_index;
      }
      if (rectangle.row_first != other_rectangle.row_first) {
        return rectangle.row_first < other_rectangle.row_first;
      }
      return rectangle.row_first_start < other_rectangle.row_first_start;
    }
  };

  // Returns:
  // - A pointer to 1 pipeline for writing color or depth (or stencil via
  //   SV_StencilRef).
  // - A pointer to 8 pipelines for writing stencil by discarding samples
  //   depending on whether they have one bit set, from 1 << 0 to 1 << 7.
  // - Null if failed to create.
  ID3D12PipelineState* const* GetOrCreateTransferPipelines(
      TransferShaderKey key);

  static TransferMode GetTransferMode(bool dest_is_stencil_bit,
                                      bool dest_is_depth, bool source_is_depth,
                                      bool source_has_host_depth) {
    assert_true(dest_is_depth ||
                (!dest_is_stencil_bit && !source_has_host_depth));
    if (dest_is_stencil_bit) {
      return source_is_depth ? TransferMode::kDepthToStencilBit
                             : TransferMode::kColorToStencilBit;
    }
    if (dest_is_depth) {
      if (source_is_depth) {
        return source_has_host_depth ? TransferMode::kDepthAndHostDepthToDepth
                                     : TransferMode::kDepthToDepth;
      }
      return source_has_host_depth ? TransferMode::kColorAndHostDepthToDepth
                                   : TransferMode::kColorToDepth;
    }
    return source_is_depth ? TransferMode::kDepthToColor
                           : TransferMode::kColorToColor;
  }

  // Do ownership transfers for render targets - each render target / vector may
  // be null / empty in case there's nothing to do for them.
  // resolve_clear_rectangle is expected to be provided by
  // PrepareHostRenderTargetsResolveClear which should do all the needed size
  // bound checks.
  void PerformTransfersAndResolveClears(
      uint32_t render_target_count, RenderTarget* const* render_targets,
      const std::vector<Transfer>* render_target_transfers,
      const uint64_t* render_target_resolve_clear_values = nullptr,
      const Transfer::Rectangle* resolve_clear_rectangle = nullptr);

  // Accepts an array of (1 + xenos::kMaxColorRenderTargets) render targets,
  // first depth, then color.
  void SetCommandListRenderTargets(
      RenderTarget* const* depth_and_color_render_targets);

  ID3D12PipelineState* GetOrCreateDumpPipeline(DumpPipelineKey key);

  // Writes contents of host render targets within rectangles from
  // ResolveInfo::GetCopyEdramTileSpan to edram_buffer_.
  void DumpRenderTargets(uint32_t dump_base, uint32_t dump_row_length_used,
                         uint32_t dump_rows, uint32_t dump_pitch);

  bool use_stencil_reference_output_ = false;

  bool gamma_render_target_as_srgb_ = false;

  DepthFloat24Conversion depth_float24_conversion_ =
      DepthFloat24Conversion::kOnCopy;

  bool msaa_2x_supported_ = false;

  std::shared_ptr<ui::d3d12::D3D12CpuDescriptorPool> descriptor_pool_color_;
  std::shared_ptr<ui::d3d12::D3D12CpuDescriptorPool> descriptor_pool_depth_;
  std::shared_ptr<ui::d3d12::D3D12CpuDescriptorPool> descriptor_pool_srv_;
  ui::d3d12::D3D12CpuDescriptorPool::Descriptor null_rtv_descriptor_ss_;
  ui::d3d12::D3D12CpuDescriptorPool::Descriptor null_rtv_descriptor_ms_;

  // Possible tile ownership transfer paths:
  // - To color:
  //   - From color: 1 SRV (color).
  //   - From depth: 2 SRVs (depth, stencil).
  // - To depth / stencil (with SV_StencilRef):
  //   - From color: 1 SRV (color).
  //   - From depth: 2 SRVs (depth, stencil).
  //   - From color and float32 depth: 2 SRVs (color with stencil, depth).
  //     - Different depth buffer: depth SRV is a texture.
  //     - Same depth buffer: depth SRV is a buffer (pre-copied).
  // - To depth (no SV_StencilRef):
  //   - From color: 1 SRV (color).
  //   - From depth: 1 SRV (depth).
  //   - From color and float32 depth: 2 SRVs (color, depth).
  //     - Different depth buffer: depth SRV is a texture.
  //     - Same depth buffer: depth SRV is a buffer (pre-copied).
  // - To stencil (no SV_StencilRef):
  //   - From color: 1 SRV (color).
  //   - From depth: 1 SRV (stencil).

  const RenderTarget* const*
      current_command_list_render_targets_[1 + xenos::kMaxColorRenderTargets];
  uint32_t are_current_command_list_render_targets_srgb_ = 0;
  bool are_current_command_list_render_targets_valid_ = false;

  // Temporary storage for descriptors used in PerformTransfersAndResolveClears
  // and DumpRenderTargets.
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> current_temporary_descriptors_cpu_;
  std::vector<ui::d3d12::util::DescriptorCpuGpuHandlePair>
      current_temporary_descriptors_gpu_;

  ID3D12RootSignature* host_depth_store_root_signature_ = nullptr;
  ID3D12PipelineState*
      host_depth_store_pipelines_[size_t(xenos::MsaaSamples::k4X) + 1] = {};

  std::unique_ptr<ui::d3d12::D3D12UploadBufferPool>
      transfer_vertex_buffer_pool_;

  ID3D12RootSignature* transfer_root_signatures_[size_t(
      TransferRootSignatureIndex::kCount)] = {};
  std::unordered_map<TransferShaderKey, ID3D12PipelineState*,
                     TransferShaderKey::Hasher>
      transfer_pipelines_;
  std::unordered_map<TransferShaderKey, std::array<ID3D12PipelineState*, 8>,
                     TransferShaderKey::Hasher>
      transfer_stencil_bit_pipelines_;

  // Temporary storage for PerformTransfersAndResolveClears.
  std::vector<TransferInvocation> current_transfer_invocations_;

  // Temporary storage for DumpRenderTargets.
  std::vector<ResolveCopyDumpRectangle> dump_rectangles_;
  std::vector<DumpInvocation> dump_invocations_;

  ID3D12RootSignature* dump_root_signature_color_ = nullptr;
  ID3D12RootSignature* dump_root_signature_depth_ = nullptr;
  // Compute pipelines for copying host render target contents to the EDRAM
  // buffer. May be null if failed to create.
  std::unordered_map<DumpPipelineKey, ID3D12PipelineState*,
                     DumpPipelineKey::Hasher>
      dump_pipelines_;

  // Parameter 0 - 2 root constants (red, green).
  ID3D12RootSignature* uint32_rtv_clear_root_signature_ = nullptr;
  // [32 or 32_32][MSAA samples].
  ID3D12PipelineState*
      uint32_rtv_clear_pipelines_[2][size_t(xenos::MsaaSamples::k4X) + 1] = {};

  std::vector<Transfer> clear_transfers_[2];

  // Temporary storage for DXBC building.
  std::vector<uint32_t> built_shader_;

  // For rasterizer-ordered view (pixel shader interlock).

  static const std::pair<const void*, size_t> kResolveROVClear32bppShaders[3];
  static const std::pair<const void*, size_t> kResolveROVClear64bppShaders[3];

  ID3D12RootSignature* resolve_rov_clear_root_signature_ = nullptr;
  // Clearing 32bpp color or depth.
  ID3D12PipelineState* resolve_rov_clear_32bpp_pipeline_ = nullptr;
  // Clearing 64bpp color.
  ID3D12PipelineState* resolve_rov_clear_64bpp_pipeline_ = nullptr;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_RENDER_TARGET_CACHE_H_
