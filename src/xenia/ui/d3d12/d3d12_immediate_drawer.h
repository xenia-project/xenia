/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_IMMEDIATE_DRAWER_H_
#define XENIA_UI_D3D12_D3D12_IMMEDIATE_DRAWER_H_

#include <deque>
#include <memory>
#include <utility>
#include <vector>

#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/d3d12/d3d12_descriptor_heap_pool.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/d3d12/d3d12_upload_buffer_pool.h"
#include "xenia/ui/immediate_drawer.h"

namespace xe {
namespace ui {
namespace d3d12 {

class D3D12ImmediateDrawer final : public ImmediateDrawer {
 public:
  static std::unique_ptr<D3D12ImmediateDrawer> Create(
      const D3D12Provider& provider) {
    auto immediate_drawer = std::unique_ptr<D3D12ImmediateDrawer>(
        new D3D12ImmediateDrawer(provider));
    if (!immediate_drawer->Initialize()) {
      return nullptr;
    }
    return std::move(immediate_drawer);
  }

  ~D3D12ImmediateDrawer();

  std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t width,
                                                  uint32_t height,
                                                  ImmediateTextureFilter filter,
                                                  bool is_repeated,
                                                  const uint8_t* data) override;

  void Begin(UIDrawContext& ui_draw_context, float coordinate_space_width,
             float coordinate_space_height) override;
  void BeginDrawBatch(const ImmediateDrawBatch& batch) override;
  void Draw(const ImmediateDraw& draw) override;
  void EndDrawBatch() override;
  void End() override;

 protected:
  void OnLeavePresenter() override;

 private:
  enum class SamplerIndex {
    kNearestClamp,
    kLinearClamp,
    kNearestRepeat,
    kLinearRepeat,

    kCount,
    kInvalid = kCount
  };

  class D3D12ImmediateTexture final : public ImmediateTexture {
   public:
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12ImmediateTexture(uint32_t width, uint32_t height,
                          ID3D12Resource* resource, SamplerIndex sampler_index,
                          D3D12ImmediateDrawer* immediate_drawer,
                          size_t immediate_drawer_index);
    ~D3D12ImmediateTexture() override;

    ID3D12Resource* resource() const { return resource_.Get(); }
    SamplerIndex sampler_index() const { return sampler_index_; }

    size_t immediate_drawer_index() const { return immediate_drawer_index_; }
    void SetImmediateDrawerIndex(size_t index) {
      immediate_drawer_index_ = index;
    }
    void OnImmediateDrawerDestroyed();

    UINT64 last_usage_submission_index() const {
      return last_usage_submission_index_;
    }
    void SetLastUsageSubmissionIndex(UINT64 submission_index) {
      last_usage_submission_index_ = submission_index;
    }

   private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    SamplerIndex sampler_index_;

    D3D12ImmediateDrawer* immediate_drawer_;
    size_t immediate_drawer_index_;

    UINT64 last_usage_submission_index_ = 0;
  };

  D3D12ImmediateDrawer(const D3D12Provider& provider) : provider_(provider) {}
  bool Initialize();

  void OnImmediateTextureDestroyed(D3D12ImmediateTexture& texture);

  void UploadTextures();

  const D3D12Provider& provider_;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  enum class RootParameter {
    kTexture,
    kSampler,
    kCoordinateSpaceSizeInv,

    kCount
  };

  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_triangle_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_line_;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sampler_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_heap_cpu_start_;
  D3D12_GPU_DESCRIPTOR_HANDLE sampler_heap_gpu_start_;

  // Only with non-null resources.
  std::vector<D3D12ImmediateTexture*> textures_;

  struct PendingTextureUpload {
    PendingTextureUpload(ID3D12Resource* texture, ID3D12Resource* buffer)
        : texture(texture), buffer(buffer) {}
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
  };
  std::vector<PendingTextureUpload> texture_uploads_pending_;

  struct SubmittedTextureUpload {
    SubmittedTextureUpload(ID3D12Resource* texture, ID3D12Resource* buffer,
                           UINT64 submission_index)
        : texture(texture),
          buffer(buffer),
          submission_index(submission_index) {}
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    UINT64 submission_index;
  };
  std::deque<SubmittedTextureUpload> texture_uploads_submitted_;

  std::deque<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, UINT64>>
      textures_deleted_;

  std::unique_ptr<D3D12UploadBufferPool> vertex_buffer_pool_;
  std::unique_ptr<D3D12DescriptorHeapPool> texture_descriptor_pool_;

  // The submission index within the current Begin (or the last, if outside
  // one).
  UINT64 last_paint_submission_index_ = 0;
  // Completed submission index as of the latest Begin, to coarsely skip delayed
  // texture deletion.
  UINT64 last_completed_submission_index_ = 0;

  bool batch_open_ = false;
  bool batch_has_index_buffer_;
  D3D12_RECT current_scissor_;
  D3D_PRIMITIVE_TOPOLOGY current_primitive_topology_;
  ID3D12Resource* current_texture_;
  uint64_t current_texture_descriptor_heap_index_;
  SamplerIndex current_sampler_index_;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_IMMEDIATE_DRAWER_H_
