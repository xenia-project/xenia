/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_presenter.h"

#include <algorithm>
#include <climits>
#include <memory>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/d3d12/d3d12_util.h"
#include "xenia/ui/surface_win.h"

DEFINE_bool(
    d3d12_allow_variable_refresh_rate_and_tearing, true,
    "In fullscreen, allow using variable refresh rate on displays supporting "
    "it. On displays not supporting VRR, screen tearing may occur in certain "
    "cases.",
    "D3D12");

namespace xe {
namespace ui {
namespace d3d12 {

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/ui/shaders/bytecode/d3d12_5_1/guest_output_bilinear_dither_ps.h"
#include "xenia/ui/shaders/bytecode/d3d12_5_1/guest_output_bilinear_ps.h"
#include "xenia/ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_cas_resample_dither_ps.h"
#include "xenia/ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_cas_resample_ps.h"
#include "xenia/ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_cas_sharpen_dither_ps.h"
#include "xenia/ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_cas_sharpen_ps.h"
#include "xenia/ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_fsr_easu_ps.h"
#include "xenia/ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_fsr_rcas_dither_ps.h"
#include "xenia/ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_fsr_rcas_ps.h"
#include "xenia/ui/shaders/bytecode/d3d12_5_1/guest_output_triangle_strip_rect_vs.h"
}  // namespace shaders

D3D12Presenter::~D3D12Presenter() {
  // Await completion of the usage of everything before destroying anything.
  // From most likely the latest to most likely the earliest to be signaled, so
  // just one sleep will likely be needed.
  paint_context_.AwaitSwapChainUsageCompletion();
  guest_output_resource_refresher_submission_tracker_.Shutdown();
  ui_submission_tracker_.Shutdown();
}

Surface::TypeFlags D3D12Presenter::GetSupportedSurfaceTypes() const {
  Surface::TypeFlags types = 0;
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_GAMES)
  types |= Surface::kTypeFlag_Win32Hwnd;
#endif
  return types;
}

bool D3D12Presenter::CaptureGuestOutput(RawImage& image_out) {
  Microsoft::WRL::ComPtr<ID3D12Resource> guest_output_resource;
  {
    uint32_t guest_output_mailbox_index;
    std::unique_lock<std::mutex> guest_output_consumer_lock(
        ConsumeGuestOutput(guest_output_mailbox_index, nullptr, nullptr));
    if (guest_output_mailbox_index != UINT32_MAX) {
      guest_output_resource =
          guest_output_resources_[guest_output_mailbox_index].second;
    }
    // Incremented the reference count of the guest output resource - safe to
    // leave the consumer critical section now.
  }
  if (!guest_output_resource) {
    return false;
  }

  ID3D12Device* device = provider_.GetDevice();

  D3D12_RESOURCE_DESC texture_desc = guest_output_resource->GetDesc();
  D3D12_TEXTURE_COPY_LOCATION copy_dest;
  UINT64 copy_dest_size;
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0,
                                &copy_dest.PlacedFootprint, nullptr, nullptr,
                                &copy_dest_size);

  D3D12_RESOURCE_DESC buffer_desc;
  util::FillBufferResourceDesc(buffer_desc, copy_dest_size,
                               D3D12_RESOURCE_FLAG_NONE);
  Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
  // Create zeroed not to leak data in the row padding.
  if (FAILED(device->CreateCommittedResource(
          &util::kHeapPropertiesReadback, D3D12_HEAP_FLAG_NONE, &buffer_desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)))) {
    XELOGE("D3D12Presenter: Failed to create the guest output capture buffer");
    return false;
  }

  {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
    if (FAILED(
            device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&command_allocator)))) {
      XELOGE(
          "D3D12Presenter: Failed to create the guest output capturing command "
          "allocator");
      return false;
    }
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         command_allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&command_list)))) {
      XELOGE(
          "D3D12Presenter: Failed to create the guest output capturing command "
          "list");
      return false;
    }

    D3D12_RESOURCE_BARRIER barrier;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = guest_output_resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = kGuestOutputInternalState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    if constexpr (kGuestOutputInternalState !=
                  D3D12_RESOURCE_STATE_COPY_SOURCE) {
      command_list->ResourceBarrier(1, &barrier);
    }
    copy_dest.pResource = buffer.Get();
    copy_dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    D3D12_TEXTURE_COPY_LOCATION copy_source;
    copy_source.pResource = guest_output_resource.Get();
    copy_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    copy_source.SubresourceIndex = 0;
    command_list->CopyTextureRegion(&copy_dest, 0, 0, 0, &copy_source, nullptr);
    if constexpr (kGuestOutputInternalState !=
                  D3D12_RESOURCE_STATE_COPY_SOURCE) {
      std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      command_list->ResourceBarrier(1, &barrier);
    }
    if (FAILED(command_list->Close())) {
      XELOGE(
          "D3D12Presenter: Failed to close the guest output capturing command "
          "list");
      return false;
    }

    ID3D12CommandQueue* direct_queue = provider_.GetDirectQueue();

    // Make sure that if any work is submitted, any `return` will cause an await
    // before releasing the command allocator / list and the resource the RAII
    // way in the destruction of the submission tracker - so create after the
    // objects referenced in the submission - but don't submit anything if
    // failed to initialize the fence.
    D3D12SubmissionTracker submission_tracker;
    if (!submission_tracker.Initialize(device, direct_queue)) {
      return false;
    }
    ID3D12CommandList* execute_command_list = command_list.Get();
    direct_queue->ExecuteCommandLists(1, &execute_command_list);
    if (!submission_tracker.NextSubmission()) {
      XELOGE(
          "D3D12Presenter: Failed to signal the guest output capturing fence");
      return false;
    }
    if (!submission_tracker.AwaitAllSubmissionsCompletion()) {
      XELOGE(
          "D3D12Presenter: Failed to await the guest output capturing fence");
      return false;
    }
  }

  D3D12_RANGE read_range;
  read_range.Begin = copy_dest.PlacedFootprint.Offset;
  read_range.End = copy_dest_size;
  void* mapping;
  if (FAILED(buffer->Map(0, &read_range, &mapping))) {
    XELOGE("D3D12Presenter: Failed to map the guest output capture buffer");
    return false;
  }
  image_out.width = uint32_t(texture_desc.Width);
  image_out.height = uint32_t(texture_desc.Height);
  image_out.stride = sizeof(uint32_t) * image_out.width;
  image_out.data.resize(image_out.stride * image_out.height);
  uint32_t* image_out_pixels =
      reinterpret_cast<uint32_t*>(image_out.data.data());
  for (uint32_t y = 0; y < image_out.height; ++y) {
    uint32_t* dest_row = &image_out_pixels[size_t(image_out.width) * y];
    const uint32_t* source_row = reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const uint8_t*>(mapping) +
        copy_dest.PlacedFootprint.Offset +
        size_t(copy_dest.PlacedFootprint.Footprint.RowPitch) * y);
    for (uint32_t x = 0; x < image_out.width; ++x) {
      dest_row[x] = Packed10bpcRGBTo8bpcBytes(source_row[x]);
    }
  }
  // Unmapping will be done implicitly when the resource goes out of scope and
  // gets destroyed.
  return true;
}

Presenter::SurfacePaintConnectResult
D3D12Presenter::ConnectOrReconnectPaintingToSurfaceFromUIThread(
    Surface& new_surface, uint32_t new_surface_width,
    uint32_t new_surface_height, bool was_paintable,
    bool& is_vsync_implicit_out) {
  uint32_t new_swap_chain_width = std::min(
      new_surface_width, uint32_t(D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION));
  uint32_t new_swap_chain_height = std::min(
      new_surface_height, uint32_t(D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION));

  // ConnectOrReconnectPaintingToSurfaceFromUIThread may be called only for the
  // surface of the current swap chain or when the old swap chain has already
  // been destroyed, if the surface is the same, try resizing.
  if (paint_context_.swap_chain) {
    if (was_paintable &&
        paint_context_.swap_chain_width == new_swap_chain_width &&
        paint_context_.swap_chain_height == new_swap_chain_height) {
      is_vsync_implicit_out = false;
      return SurfacePaintConnectResult::kSuccessUnchanged;
    }
    paint_context_.AwaitSwapChainUsageCompletion();
    // Using the current swap_chain_allows_tearing_ value that's consistent with
    // the creation of the swap chain because ResizeBuffers can't toggle the
    // tearing flag.
    for (Microsoft::WRL::ComPtr<ID3D12Resource>& swap_chain_buffer_ref :
         paint_context_.swap_chain_buffers) {
      swap_chain_buffer_ref.Reset();
    }
    bool swap_chain_resized =
        SUCCEEDED(paint_context_.swap_chain->ResizeBuffers(
            0, UINT(new_swap_chain_width), UINT(new_swap_chain_height),
            DXGI_FORMAT_UNKNOWN,
            paint_context_.swap_chain_allows_tearing
                ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
                : 0));
    if (swap_chain_resized) {
      for (uint32_t i = 0; i < PaintContext::kSwapChainBufferCount; ++i) {
        if (FAILED(paint_context_.swap_chain->GetBuffer(
                i, IID_PPV_ARGS(&paint_context_.swap_chain_buffers[i])))) {
          swap_chain_resized = false;
          break;
        }
      }
      if (swap_chain_resized) {
        paint_context_.swap_chain_width = new_swap_chain_width;
        paint_context_.swap_chain_height = new_swap_chain_height;
      }
    }
    if (!swap_chain_resized) {
      XELOGE("D3D12Presenter: Failed to resize a swap chain");
      // Failed to resize, retry creating from scratch.
      paint_context_.DestroySwapChain();
    }
  }

  if (!paint_context_.swap_chain) {
    // Create a new swap chain.
    Surface::TypeIndex surface_type = new_surface.GetType();
    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
    swap_chain_desc.Width = UINT(new_swap_chain_width);
    swap_chain_desc.Height = UINT(new_swap_chain_height);
    swap_chain_desc.Format = kSwapChainFormat;
    swap_chain_desc.Stereo = false;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.SampleDesc.Quality = 0;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = UINT(PaintContext::kSwapChainBufferCount);
    // DXGI_SCALING_STRETCH may cause the content to "shake" while resizing,
    // with relayout done for the guest output twice visually rather than once,
    // and the UI becoming stretched and then jumping to normal. If it's
    // possible to cover the entire surface without stretching, don't stretch.
    // After resizing, the presenter repaints as soon as possible anyway, so
    swap_chain_desc.Scaling = (new_swap_chain_width == new_surface_width &&
                               new_swap_chain_height == new_surface_height)
                                  ? DXGI_SCALING_NONE
                                  : DXGI_SCALING_STRETCH;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swap_chain_desc.Flags = 0;
    if (cvars::d3d12_allow_variable_refresh_rate_and_tearing &&
        dxgi_supports_tearing_) {
      // Allow tearing in borderless fullscreen to support variable refresh
      // rate.
      swap_chain_desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    IDXGIFactory2* dxgi_factory = provider_.GetDXGIFactory();
    ID3D12CommandQueue* direct_queue = provider_.GetDirectQueue();
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_1;
    switch (surface_type) {
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_GAMES)
      case Surface::kTypeIndex_Win32Hwnd: {
        HWND surface_hwnd =
            static_cast<const Win32HwndSurface&>(new_surface).hwnd();
        if (FAILED(dxgi_factory->CreateSwapChainForHwnd(
                direct_queue, surface_hwnd, &swap_chain_desc, nullptr, nullptr,
                &swap_chain_1))) {
          XELOGE("D3D12Presenter: Failed to create a swap chain for the HWND");
          return SurfacePaintConnectResult::kFailure;
        }
        // Disable automatic Alt+Enter handling - DXGI fullscreen doesn't
        // support ALLOW_TEARING, and using custom fullscreen in ui::Win32Window
        // anyway as with Alt+Enter the menu is kept, state changes are tracked
        // better, and nothing is presented for some reason.
        dxgi_factory->MakeWindowAssociation(surface_hwnd,
                                            DXGI_MWA_NO_ALT_ENTER);
      } break;
#endif
      default:
        assert_unhandled_case(surface_type);
        XELOGE(
            "D3D12Presenter: Tried to create a swap chain for an unsupported "
            "Xenia surface type");
        return SurfacePaintConnectResult::kFailureSurfaceUnusable;
    }
    if (FAILED(swap_chain_1->QueryInterface(
            IID_PPV_ARGS(&paint_context_.swap_chain)))) {
      XELOGE(
          "D3D12Presenter: Failed to get version 3 of the swap chain "
          "interface");
      return SurfacePaintConnectResult::kFailure;
    }
    // From now on, in case of any failure, DestroySwapChain must be called
    // before returning.
    paint_context_.swap_chain_width = new_swap_chain_width;
    paint_context_.swap_chain_height = new_swap_chain_height;
    paint_context_.swap_chain_allows_tearing =
        (swap_chain_desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;
    for (uint32_t i = 0; i < PaintContext::kSwapChainBufferCount; ++i) {
      if (FAILED(paint_context_.swap_chain->GetBuffer(
              i, IID_PPV_ARGS(&paint_context_.swap_chain_buffers[i])))) {
        XELOGE(
            "D3D12Presenter: Failed to get buffer {} of a {}-buffer swap chain",
            i, PaintContext::kSwapChainBufferCount);
        paint_context_.DestroySwapChain();
        return SurfacePaintConnectResult::kFailure;
      }
    }
  }

  ID3D12Device* device = provider_.GetDevice();

  // Create the RTV descriptors.
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_heap_start =
      paint_context_.rtv_heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  rtv_desc.Texture2D.MipSlice = 0;
  rtv_desc.Texture2D.PlaneSlice = 0;
  for (uint32_t i = 0; i < PaintContext::kSwapChainBufferCount; ++i) {
    ID3D12Resource* swap_chain_buffer =
        paint_context_.swap_chain_buffers[i].Get();
    rtv_desc.Format = kSwapChainFormat;
    device->CreateRenderTargetView(
        swap_chain_buffer, &rtv_desc,
        provider_.OffsetRTVDescriptor(
            rtv_heap_start, PaintContext::kRTVIndexSwapChainBuffer0 + i));
  }

  is_vsync_implicit_out = false;
  return SurfacePaintConnectResult::kSuccess;
}

void D3D12Presenter::DisconnectPaintingFromSurfaceFromUIThreadImpl() {
  paint_context_.DestroySwapChain();
}

bool D3D12Presenter::RefreshGuestOutputImpl(
    uint32_t mailbox_index, uint32_t frontbuffer_width,
    uint32_t frontbuffer_height,
    std::function<bool(GuestOutputRefreshContext& context)> refresher,
    bool& is_8bpc_out_ref) {
  assert_not_zero(frontbuffer_width);
  assert_not_zero(frontbuffer_height);
  std::pair<UINT64, Microsoft::WRL::ComPtr<ID3D12Resource>>&
      guest_output_resource_ref = guest_output_resources_[mailbox_index];
  if (guest_output_resource_ref.second) {
    D3D12_RESOURCE_DESC guest_output_resource_current_desc =
        guest_output_resource_ref.second->GetDesc();
    if (guest_output_resource_current_desc.Width != frontbuffer_width ||
        guest_output_resource_current_desc.Height != frontbuffer_height) {
      // Main target painting has its own reference to the textures for reading
      // in its own submission tracker timeline, safe to release here.
      guest_output_resource_refresher_submission_tracker_
          .AwaitSubmissionCompletion(guest_output_resource_ref.first);
      guest_output_resource_ref.second.Reset();
    }
  }
  if (!guest_output_resource_ref.second) {
    ID3D12Device* device = provider_.GetDevice();
    D3D12_RESOURCE_DESC guest_output_resource_new_desc;
    guest_output_resource_new_desc.Dimension =
        D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    guest_output_resource_new_desc.Alignment = 0;
    guest_output_resource_new_desc.Width = frontbuffer_width;
    guest_output_resource_new_desc.Height = frontbuffer_height;
    guest_output_resource_new_desc.DepthOrArraySize = 1;
    guest_output_resource_new_desc.MipLevels = 1;
    guest_output_resource_new_desc.Format = kGuestOutputFormat;
    guest_output_resource_new_desc.SampleDesc.Count = 1;
    guest_output_resource_new_desc.SampleDesc.Quality = 0;
    guest_output_resource_new_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    guest_output_resource_new_desc.Flags =
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device->CreateCommittedResource(
            &util::kHeapPropertiesDefault,
            provider_.GetHeapFlagCreateNotZeroed(),
            &guest_output_resource_new_desc, kGuestOutputInternalState, nullptr,
            IID_PPV_ARGS(&guest_output_resource_ref.second)))) {
      XELOGE("D3D12Presenter: Failed to create the guest output {}x{} texture",
             frontbuffer_width, frontbuffer_height);
      return false;
    }
  }
  D3D12GuestOutputRefreshContext context(
      is_8bpc_out_ref, guest_output_resource_ref.second.Get());
  bool refresher_succeeded = refresher(context);
  // Even if the refresher has returned false, it still might have submitted
  // some commands referencing the resource. It's better to put an excessive
  // signal and wait slightly longer, for nothing important, while shutting down
  // than to destroy the resource while it's still in use.
  guest_output_resource_ref.first =
      guest_output_resource_refresher_submission_tracker_
          .GetCurrentSubmission();
  guest_output_resource_refresher_submission_tracker_.NextSubmission();
  return refresher_succeeded;
}

void D3D12Presenter::PaintContext::DestroySwapChain() {
  if (!swap_chain) {
    return;
  }
  AwaitSwapChainUsageCompletion();
  for (Microsoft::WRL::ComPtr<ID3D12Resource>& swap_chain_buffer_ref :
       swap_chain_buffers) {
    swap_chain_buffer_ref.Reset();
  }
  swap_chain.Reset();
  swap_chain_allows_tearing = false;
  swap_chain_height = 0;
  swap_chain_width = 0;
}

Presenter::PaintResult D3D12Presenter::PaintAndPresentImpl(
    bool execute_ui_drawers) {
  // Begin the command list with the command allocator not currently potentially
  // used on the GPU.
  UINT64 current_paint_submission =
      paint_context_.paint_submission_tracker.GetCurrentSubmission();
  UINT64 command_allocator_count =
      UINT64(paint_context_.command_allocators.size());
  if (current_paint_submission >= command_allocator_count) {
    paint_context_.paint_submission_tracker.AwaitSubmissionCompletion(
        current_paint_submission - command_allocator_count);
  }
  ID3D12CommandAllocator* command_allocator =
      paint_context_
          .command_allocators[current_paint_submission %
                              command_allocator_count]
          .Get();
  command_allocator->Reset();
  ID3D12GraphicsCommandList* command_list = paint_context_.command_list.Get();
  command_list->Reset(command_allocator, nullptr);

  ID3D12Device* device = provider_.GetDevice();

  // Obtain the RTV heap and the back buffer.
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_heap_start =
      paint_context_.rtv_heap->GetCPUDescriptorHandleForHeapStart();
  UINT back_buffer_index =
      paint_context_.swap_chain->GetCurrentBackBufferIndex();
  D3D12_CPU_DESCRIPTOR_HANDLE back_buffer_rtv = provider_.OffsetRTVDescriptor(
      rtv_heap_start,
      PaintContext::kRTVIndexSwapChainBuffer0 + back_buffer_index);
  ID3D12Resource* back_buffer =
      paint_context_.swap_chain_buffers[back_buffer_index].Get();
  bool back_buffer_acquired = false;
  bool back_buffer_bound = false;
  bool back_buffer_clear_needed = true;
  constexpr float kBackBufferClearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};

  // Draw the guest output.

  GuestOutputProperties guest_output_properties;
  GuestOutputPaintConfig guest_output_paint_config;
  Microsoft::WRL::ComPtr<ID3D12Resource> guest_output_resource;
  {
    uint32_t guest_output_mailbox_index;
    std::unique_lock<std::mutex> guest_output_consumer_lock(
        ConsumeGuestOutput(guest_output_mailbox_index, &guest_output_properties,
                           &guest_output_paint_config));
    if (guest_output_mailbox_index != UINT32_MAX) {
      guest_output_resource =
          guest_output_resources_[guest_output_mailbox_index].second;
    }
    // Incremented the reference count of the guest output resource - safe to
    // leave the consumer critical section now as everything here either will be
    // using the new reference or is exclusively owned by main target painting
    // (and multiple threads can't paint the main target at the same time).
  }

  if (guest_output_resource) {
    GuestOutputPaintFlow guest_output_flow = GetGuestOutputPaintFlow(
        guest_output_properties, paint_context_.swap_chain_width,
        paint_context_.swap_chain_height, D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION,
        D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION, guest_output_paint_config);

    // Check if all guest output paint effects are supported by the
    // implementation.
    if (guest_output_flow.effect_count) {
      if (!guest_output_paint_final_pipelines_[size_t(
              guest_output_flow.effects[guest_output_flow.effect_count - 1])]) {
        guest_output_flow.effect_count = 0;
      }
      for (size_t i = 0; i + 1 < guest_output_flow.effect_count; ++i) {
        if (!guest_output_paint_intermediate_pipelines_[size_t(
                guest_output_flow.effects[i])]) {
          guest_output_flow.effect_count = 0;
          break;
        }
      }
    }

    if (guest_output_flow.effect_count) {
      ID3D12DescriptorHeap* view_heap = paint_context_.view_heap.Get();
      D3D12_CPU_DESCRIPTOR_HANDLE view_heap_cpu_start =
          view_heap->GetCPUDescriptorHandleForHeapStart();

      // Store the main target reference to the guest output texture so it's not
      // destroyed while it's still potentially in use by main target painting
      // queued on the GPU.
      size_t guest_output_resource_paint_ref_index = SIZE_MAX;
      size_t guest_output_resource_paint_ref_new_index = SIZE_MAX;
      // Try to find the existing reference to the same texture, or an already
      // released (or a taken, but never actually used) slot.
      for (size_t i = 0;
           i < paint_context_.guest_output_resource_paint_refs.size(); ++i) {
        const std::pair<UINT64, Microsoft::WRL::ComPtr<ID3D12Resource>>&
            guest_output_resource_paint_ref =
                paint_context_.guest_output_resource_paint_refs[i];
        if (guest_output_resource_paint_ref.second == guest_output_resource) {
          guest_output_resource_paint_ref_index = i;
          break;
        }
        if (guest_output_resource_paint_ref_new_index == SIZE_MAX &&
            (!guest_output_resource_paint_ref.second ||
             !guest_output_resource_paint_ref.first)) {
          guest_output_resource_paint_ref_new_index = i;
        }
      }
      if (guest_output_resource_paint_ref_index == SIZE_MAX) {
        // New texture - store the reference and create the descriptors.
        if (guest_output_resource_paint_ref_new_index == SIZE_MAX) {
          // Replace the earliest used reference.
          guest_output_resource_paint_ref_new_index = 0;
          for (size_t i = 1;
               i < paint_context_.guest_output_resource_paint_refs.size();
               ++i) {
            if (paint_context_.guest_output_resource_paint_refs[i].first <
                paint_context_
                    .guest_output_resource_paint_refs
                        [guest_output_resource_paint_ref_new_index]
                    .first) {
              guest_output_resource_paint_ref_new_index = i;
            }
          }
          // Await the completion of the usage of the old guest output
          // resource and its SRV descriptors.
          paint_context_.paint_submission_tracker.AwaitSubmissionCompletion(
              paint_context_
                  .guest_output_resource_paint_refs
                      [guest_output_resource_paint_ref_new_index]
                  .first);
        }
        guest_output_resource_paint_ref_index =
            guest_output_resource_paint_ref_new_index;
        // The actual submission index will be set if the texture is actually
        // used, not dropped due to some error.
        paint_context_.guest_output_resource_paint_refs
            [guest_output_resource_paint_ref_index] =
            std::make_pair(UINT64(0), guest_output_resource);
        // Create the SRV descriptor of the new texture.
        D3D12_SHADER_RESOURCE_VIEW_DESC guest_output_resource_srv_desc;
        guest_output_resource_srv_desc.Format = kGuestOutputFormat;
        guest_output_resource_srv_desc.ViewDimension =
            D3D12_SRV_DIMENSION_TEXTURE2D;
        guest_output_resource_srv_desc.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        guest_output_resource_srv_desc.Texture2D.MostDetailedMip = 0;
        guest_output_resource_srv_desc.Texture2D.MipLevels = 1;
        guest_output_resource_srv_desc.Texture2D.PlaneSlice = 0;
        guest_output_resource_srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
        device->CreateShaderResourceView(
            guest_output_resource.Get(), &guest_output_resource_srv_desc,
            provider_.OffsetViewDescriptor(
                view_heap_cpu_start,
                PaintContext::kViewIndexGuestOutput0Srv +
                    uint32_t(guest_output_resource_paint_ref_index)));
      }

      // Make sure intermediate textures of the needed size are available, and
      // unneeded intermediate textures are destroyed.
      for (size_t i = 0; i < kMaxGuestOutputPaintEffects - 1; ++i) {
        std::pair<uint32_t, uint32_t> intermediate_needed_size(0, 0);
        if (i + 1 < guest_output_flow.effect_count) {
          intermediate_needed_size = guest_output_flow.effect_output_sizes[i];
        }
        Microsoft::WRL::ComPtr<ID3D12Resource>& intermediate_texture_ptr_ref =
            paint_context_.guest_output_intermediate_textures[i];
        std::pair<uint32_t, uint32_t> intermediate_current_size(0, 0);
        if (intermediate_texture_ptr_ref) {
          D3D12_RESOURCE_DESC intermediate_current_desc =
              intermediate_texture_ptr_ref->GetDesc();
          intermediate_current_size.first =
              uint32_t(intermediate_current_desc.Width);
          intermediate_current_size.second = intermediate_current_desc.Height;
        }
        if (intermediate_current_size != intermediate_needed_size) {
          if (intermediate_needed_size.first &&
              intermediate_needed_size.second) {
            // Need to replace immediately as a new texture with the requested
            // size is needed.
            if (intermediate_texture_ptr_ref) {
              paint_context_.paint_submission_tracker.AwaitSubmissionCompletion(
                  paint_context_.guest_output_intermediate_texture_last_usage);
              intermediate_texture_ptr_ref.Reset();
            }
            // Resource.
            D3D12_RESOURCE_DESC intermediate_desc;
            intermediate_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            intermediate_desc.Alignment = 0;
            intermediate_desc.Width = intermediate_needed_size.first;
            intermediate_desc.Height = intermediate_needed_size.second;
            intermediate_desc.DepthOrArraySize = 1;
            intermediate_desc.MipLevels = 1;
            intermediate_desc.Format = kGuestOutputIntermediateFormat;
            intermediate_desc.SampleDesc.Count = 1;
            intermediate_desc.SampleDesc.Quality = 0;
            intermediate_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            intermediate_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            if (FAILED(device->CreateCommittedResource(
                    &util::kHeapPropertiesDefault,
                    provider_.GetHeapFlagCreateNotZeroed(), &intermediate_desc,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                    IID_PPV_ARGS(&intermediate_texture_ptr_ref)))) {
              XELOGE(
                  "D3D12Presenter: Failed to create a guest output "
                  "presentation intermediate texture");
              // Don't display the guest output, and don't try to create more
              // intermediate textures (only destroy them).
              guest_output_flow.effect_count = 0;
              continue;
            }
            ID3D12Resource* intermediate_texture =
                intermediate_texture_ptr_ref.Get();
            // SRV.
            D3D12_SHADER_RESOURCE_VIEW_DESC intermediate_srv_desc;
            intermediate_srv_desc.Format = kGuestOutputIntermediateFormat;
            intermediate_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            intermediate_srv_desc.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            intermediate_srv_desc.Texture2D.MostDetailedMip = 0;
            intermediate_srv_desc.Texture2D.MipLevels = 1;
            intermediate_srv_desc.Texture2D.PlaneSlice = 0;
            intermediate_srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
            device->CreateShaderResourceView(
                intermediate_texture, &intermediate_srv_desc,
                provider_.OffsetViewDescriptor(
                    view_heap_cpu_start,
                    uint32_t(
                        PaintContext::kViewIndexGuestOutputIntermediate0Srv +
                        i)));
            // RTV.
            D3D12_RENDER_TARGET_VIEW_DESC intermediate_rtv_desc;
            intermediate_rtv_desc.Format = kGuestOutputIntermediateFormat;
            intermediate_rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            intermediate_rtv_desc.Texture2D.MipSlice = 0;
            intermediate_rtv_desc.Texture2D.PlaneSlice = 0;
            device->CreateRenderTargetView(
                intermediate_texture, &intermediate_rtv_desc,
                provider_.OffsetRTVDescriptor(
                    rtv_heap_start,
                    uint32_t(PaintContext::kRTVIndexGuestOutputIntermediate0 +
                             i)));
          } else {
            // Was previously needed, but not anymore - destroy when possible.
            if (intermediate_texture_ptr_ref &&
                paint_context_.paint_submission_tracker
                        .GetCompletedSubmission() >=
                    paint_context_
                        .guest_output_intermediate_texture_last_usage) {
              intermediate_texture_ptr_ref.Reset();
            }
          }
        }
      }

      if (guest_output_flow.effect_count) {
        paint_context_
            .guest_output_resource_paint_refs
                [guest_output_resource_paint_ref_index]
            .first = current_paint_submission;
        if (guest_output_flow.effect_count > 1) {
          paint_context_.guest_output_intermediate_texture_last_usage =
              current_paint_submission;
        }

        command_list->SetDescriptorHeaps(1, &view_heap);
      }

      // This effect loop must not be aborted so the states of the resources
      // involved are consistent.
      D3D12_GPU_DESCRIPTOR_HANDLE view_heap_gpu_start =
          view_heap->GetGPUDescriptorHandleForHeapStart();
      for (size_t i = 0; i < guest_output_flow.effect_count; ++i) {
        bool is_final_effect = i + 1 >= guest_output_flow.effect_count;

        GuestOutputPaintEffect effect = guest_output_flow.effects[i];

        ID3D12Resource* effect_dest_resource;
        int32_t effect_rect_x, effect_rect_y;
        if (is_final_effect) {
          effect_dest_resource = back_buffer;
          if (!back_buffer_acquired) {
            D3D12_RESOURCE_BARRIER barrier_present_to_rtv;
            barrier_present_to_rtv.Type =
                D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier_present_to_rtv.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier_present_to_rtv.Transition.pResource = back_buffer;
            barrier_present_to_rtv.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier_present_to_rtv.Transition.StateBefore =
                D3D12_RESOURCE_STATE_PRESENT;
            barrier_present_to_rtv.Transition.StateAfter =
                D3D12_RESOURCE_STATE_RENDER_TARGET;
            command_list->ResourceBarrier(1, &barrier_present_to_rtv);
            back_buffer_acquired = true;
          }
          effect_rect_x = guest_output_flow.output_x;
          effect_rect_y = guest_output_flow.output_y;
        } else {
          effect_dest_resource =
              paint_context_.guest_output_intermediate_textures[i].Get();
          if (!i) {
            // If this is not the first effect, the transition has been done at
            // the end of the previous effect in a single command.
            D3D12_RESOURCE_BARRIER barrier_srv_to_rtv;
            barrier_srv_to_rtv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier_srv_to_rtv.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier_srv_to_rtv.Transition.pResource = effect_dest_resource;
            barrier_srv_to_rtv.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier_srv_to_rtv.Transition.StateBefore =
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier_srv_to_rtv.Transition.StateAfter =
                D3D12_RESOURCE_STATE_RENDER_TARGET;
            command_list->ResourceBarrier(1, &barrier_srv_to_rtv);
          }
          command_list->DiscardResource(effect_dest_resource, nullptr);
          effect_rect_x = 0;
          effect_rect_y = 0;
        }

        if (is_final_effect) {
          if (!back_buffer_bound) {
            command_list->OMSetRenderTargets(1, &back_buffer_rtv, true,
                                             nullptr);
            back_buffer_bound = true;
          }
        } else {
          D3D12_CPU_DESCRIPTOR_HANDLE intermediate_rtv =
              provider_.OffsetRTVDescriptor(
                  rtv_heap_start,
                  uint32_t(PaintContext::kRTVIndexGuestOutputIntermediate0 +
                           i));
          command_list->OMSetRenderTargets(1, &intermediate_rtv, true, nullptr);
          back_buffer_bound = false;
        }
        if (is_final_effect) {
          back_buffer_bound = true;
        }
        D3D12_RESOURCE_DESC effect_dest_resource_desc =
            effect_dest_resource->GetDesc();
        D3D12_VIEWPORT viewport;
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = float(effect_dest_resource_desc.Width);
        viewport.Height = float(effect_dest_resource_desc.Height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        command_list->RSSetViewports(1, &viewport);
        D3D12_RECT scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = LONG(effect_dest_resource_desc.Width);
        scissor.bottom = LONG(effect_dest_resource_desc.Height);
        command_list->RSSetScissorRects(1, &scissor);

        command_list->SetPipelineState(
            is_final_effect
                ? guest_output_paint_final_pipelines_[size_t(effect)].Get()
                : guest_output_paint_intermediate_pipelines_[size_t(effect)]
                      .Get());
        GuestOutputPaintRootSignatureIndex
            guest_output_paint_root_signature_index =
                GetGuestOutputPaintRootSignatureIndex(effect);
        command_list->SetGraphicsRootSignature(
            guest_output_paint_root_signatures_
                [size_t(guest_output_paint_root_signature_index)]
                    .Get());

        UINT effect_src_view_index = UINT(
            i ? (PaintContext::kViewIndexGuestOutputIntermediate0Srv + (i - 1))
              : (PaintContext::kViewIndexGuestOutput0Srv +
                 guest_output_resource_paint_ref_index));
        command_list->SetGraphicsRootDescriptorTable(
            UINT(GuestOutputPaintRootParameter::kSource),
            provider_.OffsetViewDescriptor(view_heap_gpu_start,
                                           effect_src_view_index));

        GuestOutputPaintRectangleConstants effect_rect_constants;
        float effect_x_to_ndc = 2.0f / viewport.Width;
        float effect_y_to_ndc = 2.0f / viewport.Height;
        effect_rect_constants.x =
            -1.0f + float(effect_rect_x) * effect_x_to_ndc;
        // +Y is -V.
        effect_rect_constants.y = 1.0f - float(effect_rect_y) * effect_y_to_ndc;
        effect_rect_constants.width =
            float(guest_output_flow.effect_output_sizes[i].first) *
            effect_x_to_ndc;
        effect_rect_constants.height =
            -float(guest_output_flow.effect_output_sizes[i].second) *
            effect_y_to_ndc;
        command_list->SetGraphicsRoot32BitConstants(
            UINT(GuestOutputPaintRootParameter::kRectangle),
            sizeof(effect_rect_constants) / sizeof(uint32_t),
            &effect_rect_constants, 0);

        UINT effect_constants_size = 0;
        union {
          BilinearConstants bilinear;
          CasSharpenConstants cas_sharpen;
          CasResampleConstants cas_resample;
          FsrEasuConstants fsr_easu;
          FsrRcasConstants fsr_rcas;
        } effect_constants;
        switch (guest_output_paint_root_signature_index) {
          case kGuestOutputPaintRootSignatureIndexBilinear: {
            effect_constants_size = sizeof(effect_constants.bilinear);
            effect_constants.bilinear.Initialize(guest_output_flow, i);
          } break;
          case kGuestOutputPaintRootSignatureIndexCasSharpen: {
            effect_constants_size = sizeof(effect_constants.cas_sharpen);
            effect_constants.cas_sharpen.Initialize(guest_output_flow, i,
                                                    guest_output_paint_config);
          } break;
          case kGuestOutputPaintRootSignatureIndexCasResample: {
            effect_constants_size = sizeof(effect_constants.cas_resample);
            effect_constants.cas_resample.Initialize(guest_output_flow, i,
                                                     guest_output_paint_config);
          } break;
          case kGuestOutputPaintRootSignatureIndexFsrEasu: {
            effect_constants_size = sizeof(effect_constants.fsr_easu);
            effect_constants.fsr_easu.Initialize(guest_output_flow, i);
          } break;
          case kGuestOutputPaintRootSignatureIndexFsrRcas: {
            effect_constants_size = sizeof(effect_constants.fsr_rcas);
            effect_constants.fsr_rcas.Initialize(guest_output_flow, i,
                                                 guest_output_paint_config);
          } break;
          default:
            break;
        }
        if (effect_constants_size) {
          command_list->SetGraphicsRoot32BitConstants(
              UINT(GuestOutputPaintRootParameter::kEffectConstants),
              effect_constants_size / sizeof(uint32_t), &effect_constants, 0);
        }

        command_list->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        command_list->DrawInstanced(4, 1, 0, 0);

        if (is_final_effect) {
          // Clear the letterbox around the guest output if the guest output
          // doesn't cover the entire back buffer.
          if (guest_output_flow.letterbox_clear_rectangle_count) {
            D3D12_RECT letterbox_clear_d3d12_rectangles
                [GuestOutputPaintFlow::kMaxClearRectangles];
            for (size_t i = 0;
                 i < guest_output_flow.letterbox_clear_rectangle_count; ++i) {
              D3D12_RECT& letterbox_clear_d3d12_rectangle =
                  letterbox_clear_d3d12_rectangles[i];
              const GuestOutputPaintFlow::ClearRectangle&
                  letterbox_clear_rectangle =
                      guest_output_flow.letterbox_clear_rectangles[i];
              letterbox_clear_d3d12_rectangle.left =
                  LONG(letterbox_clear_rectangle.x);
              letterbox_clear_d3d12_rectangle.top =
                  LONG(letterbox_clear_rectangle.y);
              letterbox_clear_d3d12_rectangle.right =
                  LONG(letterbox_clear_rectangle.x +
                       letterbox_clear_rectangle.width);
              letterbox_clear_d3d12_rectangle.bottom =
                  LONG(letterbox_clear_rectangle.y +
                       letterbox_clear_rectangle.height);
            }
            command_list->ClearRenderTargetView(
                back_buffer_rtv, kBackBufferClearColor,
                UINT(guest_output_flow.letterbox_clear_rectangle_count),
                letterbox_clear_d3d12_rectangles);
          }
          back_buffer_clear_needed = false;
        } else {
          D3D12_RESOURCE_BARRIER barriers[2];
          UINT barrier_count = 0;
          // Transition the newly written intermediate image to SRV for use as
          // the source in the next effect.
          {
            assert_true(barrier_count < xe::countof(barriers));
            D3D12_RESOURCE_BARRIER& barrier_rtv_to_srv =
                barriers[barrier_count++];
            barrier_rtv_to_srv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier_rtv_to_srv.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier_rtv_to_srv.Transition.pResource = effect_dest_resource;
            barrier_rtv_to_srv.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier_rtv_to_srv.Transition.StateBefore =
                D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier_rtv_to_srv.Transition.StateAfter =
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          }
          // Merge the current destination > next source transition with the
          // acquisition of the destination for the next effect.
          if (i + 2 < guest_output_flow.effect_count) {
            // The next effect won't be the last - transition the next
            // intermediate destination to RTV.
            assert_true(barrier_count < xe::countof(barriers));
            D3D12_RESOURCE_BARRIER& barrier_srv_to_rtv =
                barriers[barrier_count++];
            barrier_srv_to_rtv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier_srv_to_rtv.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier_srv_to_rtv.Transition.pResource =
                paint_context_.guest_output_intermediate_textures[i + 1].Get();
            barrier_srv_to_rtv.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier_srv_to_rtv.Transition.StateBefore =
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier_srv_to_rtv.Transition.StateAfter =
                D3D12_RESOURCE_STATE_RENDER_TARGET;
          } else {
            // The next effect draws to the back buffer - merge into one
            // ResourceBarrier command.
            if (!back_buffer_acquired) {
              assert_true(barrier_count < xe::countof(barriers));
              D3D12_RESOURCE_BARRIER& barrier_present_to_rtv =
                  barriers[barrier_count++];
              barrier_present_to_rtv.Type =
                  D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
              barrier_present_to_rtv.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
              barrier_present_to_rtv.Transition.pResource = back_buffer;
              barrier_present_to_rtv.Transition.Subresource =
                  D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
              barrier_present_to_rtv.Transition.StateBefore =
                  D3D12_RESOURCE_STATE_PRESENT;
              barrier_present_to_rtv.Transition.StateAfter =
                  D3D12_RESOURCE_STATE_RENDER_TARGET;
              back_buffer_acquired = true;
            }
          }
          if (barrier_count) {
            command_list->ResourceBarrier(barrier_count, barriers);
          }
        }
      }
    }
  }

  // Release main target guest output texture references that aren't needed
  // anymore (this is done after various potential guest-output-related main
  // target submission tracker waits so the completed submission value is the
  // most actual).
  UINT64 completed_paint_submission =
      paint_context_.paint_submission_tracker.GetCompletedSubmission();
  for (std::pair<UINT64, Microsoft::WRL::ComPtr<ID3D12Resource>>&
           guest_output_resource_paint_ref :
       paint_context_.guest_output_resource_paint_refs) {
    if (!guest_output_resource_paint_ref.second ||
        guest_output_resource_paint_ref.second == guest_output_resource) {
      continue;
    }
    if (completed_paint_submission >= guest_output_resource_paint_ref.first) {
      guest_output_resource_paint_ref.second.Reset();
    }
  }

  // If no guest output has been drawn, the transitioned of the back buffer to
  // RTV hasn't been done yet, and it's needed to clear it, and optionally to
  // draw the UI.
  if (!back_buffer_acquired) {
    D3D12_RESOURCE_BARRIER barrier_present_to_rtv;
    barrier_present_to_rtv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier_present_to_rtv.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier_present_to_rtv.Transition.pResource = back_buffer;
    barrier_present_to_rtv.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier_present_to_rtv.Transition.StateBefore =
        D3D12_RESOURCE_STATE_PRESENT;
    barrier_present_to_rtv.Transition.StateAfter =
        D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_list->ResourceBarrier(1, &barrier_present_to_rtv);
    back_buffer_acquired = true;
  }

  if (back_buffer_clear_needed) {
    command_list->ClearRenderTargetView(back_buffer_rtv, kBackBufferClearColor,
                                        0, nullptr);
    back_buffer_clear_needed = false;
  }

  if (execute_ui_drawers) {
    // Draw the UI.
    if (!back_buffer_bound) {
      command_list->OMSetRenderTargets(1, &back_buffer_rtv, true, nullptr);
      back_buffer_bound = true;
    }
    D3D12UIDrawContext ui_draw_context(
        *this, paint_context_.swap_chain_width,
        paint_context_.swap_chain_height, command_list,
        ui_submission_tracker_.GetCurrentSubmission(),
        ui_submission_tracker_.GetCompletedSubmission());
    ExecuteUIDrawersFromUIThread(ui_draw_context);
  }

  // End drawing to the back buffer.
  D3D12_RESOURCE_BARRIER barrier_rtv_to_present;
  barrier_rtv_to_present.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier_rtv_to_present.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier_rtv_to_present.Transition.pResource = back_buffer;
  barrier_rtv_to_present.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier_rtv_to_present.Transition.StateBefore =
      D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier_rtv_to_present.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  command_list->ResourceBarrier(1, &barrier_rtv_to_present);

  // Execute and present.
  command_list->Close();
  ID3D12CommandList* execute_command_list = command_list;
  provider_.GetDirectQueue()->ExecuteCommandLists(1, &execute_command_list);
  if (execute_ui_drawers) {
    ui_submission_tracker_.NextSubmission();
  }
  paint_context_.paint_submission_tracker.NextSubmission();
  // Present as soon as possible, without waiting for vsync (the host refresh
  // rate may be something like 144 Hz, which is not a multiple of the common
  // 30 Hz or 60 Hz guest refresh rate), and allowing dropping outdated queued
  // frames for lower latency. Also, if possible, allowing tearing to use
  // variable refresh rate in borderless fullscreen (note that if DXGI
  // fullscreen is ever used in, the allow tearing flag must not be passed in
  // fullscreen, but DXGI fullscreen is largely unneeded with the flip
  // presentation model used in Direct3D 12).
  HRESULT present_result = paint_context_.swap_chain->Present(
      0, DXGI_PRESENT_RESTART | (paint_context_.swap_chain_allows_tearing
                                     ? DXGI_PRESENT_ALLOW_TEARING
                                     : 0));
  // Even if presentation has failed, work might have been enqueued anyway
  // internally before the failure according to Jesse Natalie from the DirectX
  // Discord server.
  paint_context_.present_submission_tracker.NextSubmission();
  switch (present_result) {
    case DXGI_ERROR_DEVICE_REMOVED:
      return PaintResult::kGpuLostExternally;
    case DXGI_ERROR_DEVICE_RESET:
      return PaintResult::kGpuLostResponsible;
    default:
      return SUCCEEDED(present_result) ? PaintResult::kPresented
                                       : PaintResult::kNotPresented;
  }
}

bool D3D12Presenter::InitializeSurfaceIndependent() {
  // Check if DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING is supported.
  {
    Microsoft::WRL::ComPtr<IDXGIFactory5> dxgi_factory_5;
    if (SUCCEEDED(provider_.GetDXGIFactory()->QueryInterface(
            IID_PPV_ARGS(&dxgi_factory_5)))) {
      BOOL tearing_feature_data;
      dxgi_supports_tearing_ =
          SUCCEEDED(dxgi_factory_5->CheckFeatureSupport(
              DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing_feature_data,
              sizeof(tearing_feature_data))) &&
          tearing_feature_data;
    }
  }

  ID3D12Device* device = provider_.GetDevice();

  // Initialize static guest output painting objects.

  // Guest output painting root signatures.
  // One (texture) for bilinear, two (texture and constants) for AMD FidelityFX
  // CAS and FSR.
  D3D12_ROOT_PARAMETER guest_output_paint_root_parameters[UINT(
      GuestOutputPaintRootParameter::kCount)];
  // Source texture.
  D3D12_DESCRIPTOR_RANGE guest_output_paint_root_descriptor_range_source;
  guest_output_paint_root_descriptor_range_source.RangeType =
      D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  guest_output_paint_root_descriptor_range_source.NumDescriptors = 1;
  guest_output_paint_root_descriptor_range_source.BaseShaderRegister = 0;
  guest_output_paint_root_descriptor_range_source.RegisterSpace = 0;
  guest_output_paint_root_descriptor_range_source
      .OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& guest_output_paint_root_parameter_source =
        guest_output_paint_root_parameters[UINT(
            GuestOutputPaintRootParameter::kSource)];
    guest_output_paint_root_parameter_source.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    guest_output_paint_root_parameter_source.DescriptorTable
        .NumDescriptorRanges = 1;
    guest_output_paint_root_parameter_source.DescriptorTable.pDescriptorRanges =
        &guest_output_paint_root_descriptor_range_source;
    guest_output_paint_root_parameter_source.ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;
  }
  // Rectangle.
  {
    D3D12_ROOT_PARAMETER& guest_output_paint_root_parameter_rect =
        guest_output_paint_root_parameters[UINT(
            GuestOutputPaintRootParameter::kRectangle)];
    guest_output_paint_root_parameter_rect.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    guest_output_paint_root_parameter_rect.Constants.ShaderRegister = 0;
    guest_output_paint_root_parameter_rect.Constants.RegisterSpace = 0;
    guest_output_paint_root_parameter_rect.Constants.Num32BitValues =
        sizeof(GuestOutputPaintRectangleConstants) / sizeof(uint32_t);
    guest_output_paint_root_parameter_rect.ShaderVisibility =
        D3D12_SHADER_VISIBILITY_VERTEX;
  }
  // Pixel shader constants.
  D3D12_ROOT_PARAMETER& guest_output_paint_root_parameter_effect_constants =
      guest_output_paint_root_parameters[UINT(
          GuestOutputPaintRootParameter::kEffectConstants)];
  guest_output_paint_root_parameter_effect_constants.ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  guest_output_paint_root_parameter_effect_constants.Constants.ShaderRegister =
      0;
  guest_output_paint_root_parameter_effect_constants.Constants.RegisterSpace =
      0;
  guest_output_paint_root_parameter_effect_constants.ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL;
  // Bilinear sampler.
  D3D12_STATIC_SAMPLER_DESC guest_output_paint_root_sampler;
  guest_output_paint_root_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  guest_output_paint_root_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  guest_output_paint_root_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  guest_output_paint_root_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  guest_output_paint_root_sampler.MipLODBias = 0.0f;
  guest_output_paint_root_sampler.MaxAnisotropy = 1;
  guest_output_paint_root_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  guest_output_paint_root_sampler.BorderColor =
      D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
  guest_output_paint_root_sampler.MinLOD = 0.0f;
  guest_output_paint_root_sampler.MaxLOD = 0.0f;
  guest_output_paint_root_sampler.ShaderRegister = 0;
  guest_output_paint_root_sampler.RegisterSpace = 0;
  guest_output_paint_root_sampler.ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC guest_output_paint_root_signature_desc;
  guest_output_paint_root_signature_desc.NumParameters =
      UINT(GuestOutputPaintRootParameter::kCount);
  guest_output_paint_root_signature_desc.pParameters =
      guest_output_paint_root_parameters;
  guest_output_paint_root_signature_desc.NumStaticSamplers = 1;
  guest_output_paint_root_signature_desc.pStaticSamplers =
      &guest_output_paint_root_sampler;
  guest_output_paint_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  // Bilinear filtering (needs the sampler).
  guest_output_paint_root_parameter_effect_constants.Constants.Num32BitValues =
      sizeof(BilinearConstants) / sizeof(uint32_t);
  {
    ID3D12RootSignature* guest_output_paint_root_signature =
        util::CreateRootSignature(provider_,
                                  guest_output_paint_root_signature_desc);
    if (!guest_output_paint_root_signature) {
      XELOGE(
          "D3D12Presenter: Failed to create the guest output bilinear "
          "filtering presentation root signature");
      return false;
    }
    *(guest_output_paint_root_signatures_
          [kGuestOutputPaintRootSignatureIndexBilinear]
              .ReleaseAndGetAddressOf()) = guest_output_paint_root_signature;
  }
  // EASU (needs the sampler).
  guest_output_paint_root_parameter_effect_constants.Constants.Num32BitValues =
      sizeof(FsrEasuConstants) / sizeof(uint32_t);
  {
    ID3D12RootSignature* guest_output_paint_root_signature =
        util::CreateRootSignature(provider_,
                                  guest_output_paint_root_signature_desc);
    if (!guest_output_paint_root_signature) {
      XELOGE(
          "D3D12Presenter: Failed to create the guest output AMD FidelityFX "
          "FSR EASU presentation root signature");
      return false;
    }
    *(guest_output_paint_root_signatures_
          [kGuestOutputPaintRootSignatureIndexFsrEasu]
              .ReleaseAndGetAddressOf()) = guest_output_paint_root_signature;
  }
  // RCAS and CAS don't need the sampler.
  guest_output_paint_root_signature_desc.NumStaticSamplers = 0;
  // RCAS.
  guest_output_paint_root_parameter_effect_constants.Constants.Num32BitValues =
      sizeof(FsrRcasConstants) / sizeof(uint32_t);
  {
    ID3D12RootSignature* guest_output_paint_root_signature =
        util::CreateRootSignature(provider_,
                                  guest_output_paint_root_signature_desc);
    if (!guest_output_paint_root_signature) {
      XELOGE(
          "D3D12Presenter: Failed to create the guest output AMD FidelityFX "
          "FSR RCAS presentation root signature");
      return false;
    }
    *(guest_output_paint_root_signatures_
          [kGuestOutputPaintRootSignatureIndexFsrRcas]
              .ReleaseAndGetAddressOf()) = guest_output_paint_root_signature;
  }
  // CAS, sharpening only.
  guest_output_paint_root_parameter_effect_constants.Constants.Num32BitValues =
      sizeof(CasSharpenConstants) / sizeof(uint32_t);
  {
    ID3D12RootSignature* guest_output_paint_root_signature =
        util::CreateRootSignature(provider_,
                                  guest_output_paint_root_signature_desc);
    if (!guest_output_paint_root_signature) {
      XELOGE(
          "D3D12Presenter: Failed to create the guest output AMD FidelityFX "
          "CAS presentation root signature");
      return false;
    }
    *(guest_output_paint_root_signatures_
          [kGuestOutputPaintRootSignatureIndexCasSharpen]
              .ReleaseAndGetAddressOf()) = guest_output_paint_root_signature;
  }
  // CAS, resampling.
  guest_output_paint_root_parameter_effect_constants.Constants.Num32BitValues =
      sizeof(CasResampleConstants) / sizeof(uint32_t);
  {
    ID3D12RootSignature* guest_output_paint_root_signature =
        util::CreateRootSignature(provider_,
                                  guest_output_paint_root_signature_desc);
    if (!guest_output_paint_root_signature) {
      XELOGE(
          "D3D12Presenter: Failed to create the guest output resampling AMD "
          "FidelityFX CAS presentation root signature");
      return false;
    }
    *(guest_output_paint_root_signatures_
          [kGuestOutputPaintRootSignatureIndexCasResample]
              .ReleaseAndGetAddressOf()) = guest_output_paint_root_signature;
  }

  // Guest output painting pipelines.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC guest_output_paint_pipeline_desc = {};
  guest_output_paint_pipeline_desc.VS.pShaderBytecode =
      shaders::guest_output_triangle_strip_rect_vs;
  guest_output_paint_pipeline_desc.VS.BytecodeLength =
      sizeof(shaders::guest_output_triangle_strip_rect_vs);
  guest_output_paint_pipeline_desc.BlendState.RenderTarget[0]
      .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  guest_output_paint_pipeline_desc.SampleMask = UINT_MAX;
  guest_output_paint_pipeline_desc.RasterizerState.FillMode =
      D3D12_FILL_MODE_SOLID;
  guest_output_paint_pipeline_desc.RasterizerState.CullMode =
      D3D12_CULL_MODE_NONE;
  guest_output_paint_pipeline_desc.RasterizerState.DepthClipEnable = true;
  guest_output_paint_pipeline_desc.PrimitiveTopologyType =
      D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  guest_output_paint_pipeline_desc.NumRenderTargets = 1;
  guest_output_paint_pipeline_desc.SampleDesc.Count = 1;
  for (size_t i = 0; i < size_t(GuestOutputPaintEffect::kCount); ++i) {
    GuestOutputPaintEffect guest_output_paint_effect =
        GuestOutputPaintEffect(i);
    switch (guest_output_paint_effect) {
      case GuestOutputPaintEffect::kBilinear:
        guest_output_paint_pipeline_desc.PS.pShaderBytecode =
            shaders::guest_output_bilinear_ps;
        guest_output_paint_pipeline_desc.PS.BytecodeLength =
            sizeof(shaders::guest_output_bilinear_ps);
        break;
      case GuestOutputPaintEffect::kBilinearDither:
        guest_output_paint_pipeline_desc.PS.pShaderBytecode =
            shaders::guest_output_bilinear_dither_ps;
        guest_output_paint_pipeline_desc.PS.BytecodeLength =
            sizeof(shaders::guest_output_bilinear_dither_ps);
        break;
      case GuestOutputPaintEffect::kCasSharpen:
        guest_output_paint_pipeline_desc.PS.pShaderBytecode =
            shaders::guest_output_ffx_cas_sharpen_ps;
        guest_output_paint_pipeline_desc.PS.BytecodeLength =
            sizeof(shaders::guest_output_ffx_cas_sharpen_ps);
        break;
      case GuestOutputPaintEffect::kCasSharpenDither:
        guest_output_paint_pipeline_desc.PS.pShaderBytecode =
            shaders::guest_output_ffx_cas_sharpen_dither_ps;
        guest_output_paint_pipeline_desc.PS.BytecodeLength =
            sizeof(shaders::guest_output_ffx_cas_sharpen_dither_ps);
        break;
      case GuestOutputPaintEffect::kCasResample:
        guest_output_paint_pipeline_desc.PS.pShaderBytecode =
            shaders::guest_output_ffx_cas_resample_ps;
        guest_output_paint_pipeline_desc.PS.BytecodeLength =
            sizeof(shaders::guest_output_ffx_cas_resample_ps);
        break;
      case GuestOutputPaintEffect::kCasResampleDither:
        guest_output_paint_pipeline_desc.PS.pShaderBytecode =
            shaders::guest_output_ffx_cas_resample_dither_ps;
        guest_output_paint_pipeline_desc.PS.BytecodeLength =
            sizeof(shaders::guest_output_ffx_cas_resample_dither_ps);
        break;
      case GuestOutputPaintEffect::kFsrEasu:
        guest_output_paint_pipeline_desc.PS.pShaderBytecode =
            shaders::guest_output_ffx_fsr_easu_ps;
        guest_output_paint_pipeline_desc.PS.BytecodeLength =
            sizeof(shaders::guest_output_ffx_fsr_easu_ps);
        break;
      case GuestOutputPaintEffect::kFsrRcas:
        guest_output_paint_pipeline_desc.PS.pShaderBytecode =
            shaders::guest_output_ffx_fsr_rcas_ps;
        guest_output_paint_pipeline_desc.PS.BytecodeLength =
            sizeof(shaders::guest_output_ffx_fsr_rcas_ps);
        break;
      case GuestOutputPaintEffect::kFsrRcasDither:
        guest_output_paint_pipeline_desc.PS.pShaderBytecode =
            shaders::guest_output_ffx_fsr_rcas_dither_ps;
        guest_output_paint_pipeline_desc.PS.BytecodeLength =
            sizeof(shaders::guest_output_ffx_fsr_rcas_dither_ps);
        break;
      default:
        // Not supported by this implementation.
        continue;
    }
    guest_output_paint_pipeline_desc.pRootSignature =
        guest_output_paint_root_signatures_
            [GetGuestOutputPaintRootSignatureIndex(guest_output_paint_effect)]
                .Get();
    if (CanGuestOutputPaintEffectBeIntermediate(guest_output_paint_effect)) {
      guest_output_paint_pipeline_desc.RTVFormats[0] =
          kGuestOutputIntermediateFormat;
      if (FAILED(device->CreateGraphicsPipelineState(
              &guest_output_paint_pipeline_desc,
              IID_PPV_ARGS(&guest_output_paint_intermediate_pipelines_[i])))) {
        XELOGE(
            "D3D12Presenter: Failed to create the guest output painting "
            "pipeline for effect {} writing to an intermediate texture",
            i);
        return false;
      }
    }
    if (CanGuestOutputPaintEffectBeFinal(guest_output_paint_effect)) {
      guest_output_paint_pipeline_desc.RTVFormats[0] = kSwapChainFormat;
      if (FAILED(device->CreateGraphicsPipelineState(
              &guest_output_paint_pipeline_desc,
              IID_PPV_ARGS(&guest_output_paint_final_pipelines_[i])))) {
        XELOGE(
            "D3D12Presenter: Failed to create the guest output painting "
            "pipeline for effect {} writing to a swap chain buffer",
            i);
        return false;
      }
    }
  }

  // Initialize connection-independent parts of the painting context.

  ID3D12CommandQueue* direct_queue = provider_.GetDirectQueue();

  // Paint submission trackers.
  if (!paint_context_.paint_submission_tracker.Initialize(device,
                                                          direct_queue) ||
      !paint_context_.present_submission_tracker.Initialize(device,
                                                            direct_queue)) {
    return false;
  }

  // Paint command allocators and command list.
  for (Microsoft::WRL::ComPtr<ID3D12CommandAllocator>&
           paint_command_allocator_ref : paint_context_.command_allocators) {
    if (FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&paint_command_allocator_ref)))) {
      XELOGE(
          "D3D12Presenter: Failed to create a command allocator for drawing to "
          "a swap chain");
      return false;
    }
  }
  if (FAILED(device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT,
          paint_context_.command_allocators[0].Get(), nullptr,
          IID_PPV_ARGS(&paint_context_.command_list)))) {
    XELOGE(
        "D3D12Presenter: Failed to create the command list for drawing to a "
        "swap chain");
    return false;
  }
  // Command lists are created in an open state.
  paint_context_.command_list->Close();

  // RTV descriptor heap.
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = PaintContext::kRTVCount;
  rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  rtv_heap_desc.NodeMask = 0;
  if (FAILED(device->CreateDescriptorHeap(
          &rtv_heap_desc, IID_PPV_ARGS(&paint_context_.rtv_heap)))) {
    XELOGE(
        "D3D12Presenter: Failed to create an RTV descriptor heap with {} "
        "descriptors",
        rtv_heap_desc.NumDescriptors);
    return false;
  }

  // CBV/SRV/UAV descriptor heap.
  D3D12_DESCRIPTOR_HEAP_DESC view_heap_desc;
  view_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  view_heap_desc.NumDescriptors = PaintContext::kViewCount;
  view_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  view_heap_desc.NodeMask = 0;
  if (FAILED(device->CreateDescriptorHeap(
          &view_heap_desc, IID_PPV_ARGS(&paint_context_.view_heap)))) {
    XELOGE(
        "D3D12Presenter: Failed to create a shader-visible CBV/SRV/UAV "
        "descriptor heap with {} descriptors",
        view_heap_desc.NumDescriptors);
    return false;
  }

  if (!guest_output_resource_refresher_submission_tracker_.Initialize(
          device, direct_queue)) {
    return false;
  }

  if (!ui_submission_tracker_.Initialize(device, direct_queue)) {
    return false;
  }

  return InitializeCommonSurfaceIndependent();
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe
