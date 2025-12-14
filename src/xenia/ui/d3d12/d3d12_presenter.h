/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_PRESENTER_H_
#define XENIA_UI_D3D12_D3D12_PRESENTER_H_

#include <array>
#include <cstdint>
#include <memory>
#include <utility>

#include "xenia/base/math.h"
#include "xenia/ui/d3d12/d3d12_gpu_completion_timeline.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/surface.h"

namespace xe {
namespace ui {
namespace d3d12 {

class D3D12UIDrawContext final : public UIDrawContext {
 public:
  D3D12UIDrawContext(Presenter& presenter, uint32_t render_target_width,
                     uint32_t render_target_height,
                     ID3D12GraphicsCommandList* command_list,
                     uint64_t submission_index_current,
                     uint64_t submission_index_completed)
      : UIDrawContext(presenter, render_target_width, render_target_height),
        command_list_(command_list),
        submission_index_current_(submission_index_current),
        submission_index_completed_(submission_index_completed) {}

  ID3D12GraphicsCommandList* command_list() const {
    return command_list_.Get();
  }
  uint64_t submission_index_current() const {
    return submission_index_current_;
  }
  uint64_t submission_index_completed() const {
    return submission_index_completed_;
  }

 private:
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
  uint64_t submission_index_current_;
  uint64_t submission_index_completed_;
};

class D3D12Presenter final : public Presenter {
 public:
  static constexpr DXGI_FORMAT kGuestOutputFormat =
      DXGI_FORMAT_R10G10B10A2_UNORM;
  static constexpr D3D12_RESOURCE_STATES kGuestOutputInternalState =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

  static constexpr DXGI_FORMAT kGuestOutputIntermediateFormat =
      DXGI_FORMAT_R10G10B10A2_UNORM;

  // The format used internally by Windows composition.
  static constexpr DXGI_FORMAT kSwapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

  // The callback must use the main direct queue of the provider.
  class D3D12GuestOutputRefreshContext final
      : public GuestOutputRefreshContext {
   public:
    D3D12GuestOutputRefreshContext(bool& is_8bpc_out_ref,
                                   ID3D12Resource* resource)
        : GuestOutputRefreshContext(is_8bpc_out_ref), resource_(resource) {}

    // kGuestOutputFormat, supports UAV. The initial state in the callback is
    // kGuestOutputInternalState, and the callback must also transition it back
    // to kGuestOutputInternalState before finishing.
    ID3D12Resource* resource_uav_capable() const { return resource_.Get(); }

   private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
  };

  static std::unique_ptr<D3D12Presenter> Create(
      HostGpuLossCallback host_gpu_loss_callback,
      const D3D12Provider& provider) {
    auto presenter = std::unique_ptr<D3D12Presenter>(
        new D3D12Presenter(host_gpu_loss_callback, provider));
    if (!presenter->InitializeSurfaceIndependent()) {
      return nullptr;
    }
    return presenter;
  }

  ~D3D12Presenter();

  const D3D12Provider& provider() const { return provider_; }

  Surface::TypeFlags GetSupportedSurfaceTypes() const override;

  bool CaptureGuestOutput(RawImage& image_out) override;

  void AwaitUISubmissionCompletionFromUIThread(uint64_t submission_index) {
    ui_completion_timeline_->AwaitSubmissionAndUpdateCompleted(
        submission_index);
  }

 protected:
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(
      Surface& new_surface, uint32_t new_surface_width,
      uint32_t new_surface_height, bool was_paintable,
      bool& is_vsync_implicit_out) override;
  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override;

  bool RefreshGuestOutputImpl(
      uint32_t mailbox_index, uint32_t frontbuffer_width,
      uint32_t frontbuffer_height,
      std::function<bool(GuestOutputRefreshContext& context)> refresher,
      bool& is_8bpc_out) override;

  PaintResult PaintAndPresentImpl(bool execute_ui_drawers) override;

 private:
  struct GuestOutputPaintRectangleConstants {
    union {
      struct {
        float x;
        float y;
      };
      float offset[2];
    };
    union {
      struct {
        float width;
        float height;
      };
      float size[2];
    };
  };

  enum class GuestOutputPaintRootParameter : UINT {
    kSource,
    kRectangle,
    kEffectConstants,

    kCount,
  };

  enum GuestOutputPaintRootSignatureIndex : size_t {
    kGuestOutputPaintRootSignatureIndexBilinear,
    kGuestOutputPaintRootSignatureIndexCasSharpen,
    kGuestOutputPaintRootSignatureIndexCasResample,
    kGuestOutputPaintRootSignatureIndexFsrEasu,
    kGuestOutputPaintRootSignatureIndexFsrRcas,

    kGuestOutputPaintRootSignatureCount,
  };

  static constexpr GuestOutputPaintRootSignatureIndex
  GetGuestOutputPaintRootSignatureIndex(GuestOutputPaintEffect effect) {
    switch (effect) {
      case GuestOutputPaintEffect::kBilinear:
      case GuestOutputPaintEffect::kBilinearDither:
        return kGuestOutputPaintRootSignatureIndexBilinear;
      case GuestOutputPaintEffect::kCasSharpen:
      case GuestOutputPaintEffect::kCasSharpenDither:
        return kGuestOutputPaintRootSignatureIndexCasSharpen;
      case GuestOutputPaintEffect::kCasResample:
      case GuestOutputPaintEffect::kCasResampleDither:
        return kGuestOutputPaintRootSignatureIndexCasResample;
      case GuestOutputPaintEffect::kFsrEasu:
        return kGuestOutputPaintRootSignatureIndexFsrEasu;
      case GuestOutputPaintEffect::kFsrRcas:
      case GuestOutputPaintEffect::kFsrRcasDither:
        return kGuestOutputPaintRootSignatureIndexFsrRcas;
      default:
        assert_unhandled_case(effect);
        return kGuestOutputPaintRootSignatureCount;
    }
  }

  struct PaintContext {
    explicit PaintContext() = default;
    PaintContext(const PaintContext& paint_context) = delete;
    PaintContext& operator=(const PaintContext& paint_context) = delete;

    static constexpr uint32_t kSwapChainBufferCount = 3;

    enum RTVIndex : UINT {
      // Swap chain buffers - updated when creating the swap chain
      // (connection-specific).
      kRTVIndexSwapChainBuffer0,

      // Intermediate textures - the last usage is
      // guest_output_intermediate_texture_paint_last_usage_.
      kRTVIndexGuestOutputIntermediate0 =
          kRTVIndexSwapChainBuffer0 + kSwapChainBufferCount,

      kRTVCount =
          kRTVIndexGuestOutputIntermediate0 + kGuestOutputMailboxSize - 1,
    };

    enum ViewIndex : UINT {
      // Guest output textures - indices are the same as in
      // guest_output_resource_paint_refs, and the last usage is tied to them.
      kViewIndexGuestOutput0Srv,

      // Intermediate textures - the last usage is
      // guest_output_intermediate_texture_paint_last_usage_.
      kViewIndexGuestOutputIntermediate0Srv =
          kViewIndexGuestOutput0Srv + kGuestOutputMailboxSize,

      kViewCount = kViewIndexGuestOutputIntermediate0Srv +
                   kMaxGuestOutputPaintEffects - 1,
    };

    void AwaitSwapChainUsageCompletion() {
      // May be called during destruction.

      // Presentation engine usage.
      if (present_completion_timeline) {
        present_completion_timeline->AwaitAllSubmissions();
      }

      // Paint (render target) usage. While the presentation fence is signaled
      // on the same queue, and presentation happens after painting, awaiting
      // anyway for safety just to make less assumptions in the architecture.
      if (paint_completion_timeline) {
        paint_completion_timeline->AwaitAllSubmissions();
      }
    }

    void DestroySwapChain();

    // Connection-independent.

    // Signaled before presenting.
    std::unique_ptr<D3D12GPUCompletionTimeline> paint_completion_timeline;
    // Signaled after presenting.
    std::unique_ptr<D3D12GPUCompletionTimeline> present_completion_timeline;

    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>,
               kSwapChainBufferCount>
        command_allocators;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;

    // Descriptor heaps for views of the current resources related to the guest
    // output and to painting, updated either during painting or during
    // connection lifetime management if outdated after awaiting usage
    // completion.
    // RTV heap.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap;
    // Shader-visible CBV/SRV/UAV heap.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> view_heap;

    // Refreshed and cleaned up during guest output painting. The first is the
    // paint submission index in which the guest output texture (and its
    // descriptors) was last used, the second is the reference to the texture,
    // which may be null. The indices are not mailbox indices here, rather, if
    // the reference is not in this array yet, the most outdated reference, if
    // needed, is replaced with the new one, awaiting the completion of the last
    // paint usage.
    std::array<std::pair<uint64_t, Microsoft::WRL::ComPtr<ID3D12Resource>>,
               kGuestOutputMailboxSize>
        guest_output_resource_paint_refs;

    // Current intermediate textures for guest output painting, refreshed when
    // painting guest output. While not in use, they are in
    // D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE.
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
               kMaxGuestOutputPaintEffects - 1>
        guest_output_intermediate_textures;
    uint64_t guest_output_intermediate_texture_last_usage = 0;

    // Connection-specific.

    uint32_t swap_chain_width = 0;
    uint32_t swap_chain_height = 0;
    bool swap_chain_allows_tearing = false;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kSwapChainBufferCount>
        swap_chain_buffers;
  };

  explicit D3D12Presenter(HostGpuLossCallback host_gpu_loss_callback,
                          const D3D12Provider& provider)
      : Presenter(host_gpu_loss_callback), provider_(provider) {}

  bool dxgi_supports_tearing() const { return dxgi_supports_tearing_; }

  bool InitializeSurfaceIndependent();

  const D3D12Provider& provider_;

  // Whether DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING is supported by DXGI (depends in
  // particular on the Windows 10 version and hardware support), primarily for
  // variable refresh rate support.
  bool dxgi_supports_tearing_ = false;

  // Static objects for guest output presentation, used only when painting the
  // main target (can be destroyed only after awaiting main target usage
  // completion).
  std::array<Microsoft::WRL::ComPtr<ID3D12RootSignature>,
             kGuestOutputPaintRootSignatureCount>
      guest_output_paint_root_signatures_;
  std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
             size_t(GuestOutputPaintEffect::kCount)>
      guest_output_paint_intermediate_pipelines_;
  std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
             size_t(GuestOutputPaintEffect::kCount)>
      guest_output_paint_final_pipelines_;

  // The first is the refresher completion timeline submission index at which
  // the guest output texture was last refreshed, the second is the reference to
  // the texture, which may be null. The indices are the mailbox indices.
  std::array<std::pair<uint64_t, Microsoft::WRL::ComPtr<ID3D12Resource>>,
             kGuestOutputMailboxSize>
      guest_output_resources_;
  // The guest output resources are protected by two completion timelines - the
  // refresher one (for writing to them via the guest_output_resources_
  // references) and the paint one (for presenting it via the
  // paint_context_.guest_output_resource_paint_refs references taken from
  // guest_output_resources_).
  std::unique_ptr<D3D12GPUCompletionTimeline>
      guest_output_resource_refresher_completion_timeline_;

  // UI completion timeline with the submission index that can be given to UI
  // drawers (accessible from the UI thread only, at any time).
  std::unique_ptr<D3D12GPUCompletionTimeline> ui_completion_timeline_;

  // Accessible only by painting and by surface connection lifetime management
  // (ConnectOrReconnectPaintingToSurfaceFromUIThread,
  // DisconnectPaintingFromSurfaceFromUIThreadImpl) by the thread doing it, as
  // well as by presenter initialization and shutdown.
  PaintContext paint_context_;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_PRESENTER_H_
