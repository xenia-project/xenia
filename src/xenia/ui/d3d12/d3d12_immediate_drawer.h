/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
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
#include "xenia/ui/d3d12/d3d12_upload_buffer_pool.h"
#include "xenia/ui/immediate_drawer.h"

namespace xe {
namespace ui {
namespace d3d12 {

class D3D12Context;

class D3D12ImmediateDrawer : public ImmediateDrawer {
 public:
  D3D12ImmediateDrawer(D3D12Context& graphics_context);
  ~D3D12ImmediateDrawer() override;

  bool Initialize();
  void Shutdown();

  std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t width,
                                                  uint32_t height,
                                                  ImmediateTextureFilter filter,
                                                  bool is_repeated,
                                                  const uint8_t* data) override;

  void Begin(int render_target_width, int render_target_height) override;
  void BeginDrawBatch(const ImmediateDrawBatch& batch) override;
  void Draw(const ImmediateDraw& draw) override;
  void EndDrawBatch() override;
  void End() override;

 private:
  enum class SamplerIndex {
    kNearestClamp,
    kLinearClamp,
    kNearestRepeat,
    kLinearRepeat,

    kCount,
    kInvalid = kCount
  };

  class D3D12ImmediateTexture : public ImmediateTexture {
   public:
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12ImmediateTexture(uint32_t width, uint32_t height,
                          ID3D12Resource* resource, SamplerIndex sampler_index,
                          D3D12ImmediateDrawer* immediate_drawer,
                          size_t immediate_drawer_index);
    ~D3D12ImmediateTexture() override;

    ID3D12Resource* resource() const { return resource_; }
    SamplerIndex sampler_index() const { return sampler_index_; }

    size_t immediate_drawer_index() const { return immediate_drawer_index_; }
    void SetImmediateDrawerIndex(size_t index) {
      immediate_drawer_index_ = index;
    }
    void OnImmediateDrawerShutdown();

    uint64_t last_usage_fence_value() const { return last_usage_fence_value_; }
    void SetLastUsageFenceValue(uint64_t fence_value) {
      last_usage_fence_value_ = fence_value;
    }

   private:
    ID3D12Resource* resource_;
    SamplerIndex sampler_index_;

    D3D12ImmediateDrawer* immediate_drawer_;
    size_t immediate_drawer_index_;

    uint64_t last_usage_fence_value_ = 0;
  };

  void OnImmediateTextureDestroyed(D3D12ImmediateTexture& texture);

  void UploadTextures();

  D3D12Context& context_;

  ID3D12RootSignature* root_signature_ = nullptr;
  enum class RootParameter {
    kTexture,
    kSampler,
    kViewportSizeInv,

    kCount
  };

  ID3D12PipelineState* pipeline_triangle_ = nullptr;
  ID3D12PipelineState* pipeline_line_ = nullptr;

  ID3D12DescriptorHeap* sampler_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_heap_cpu_start_;
  D3D12_GPU_DESCRIPTOR_HANDLE sampler_heap_gpu_start_;

  std::unique_ptr<D3D12UploadBufferPool> vertex_buffer_pool_;
  std::unique_ptr<D3D12DescriptorHeapPool> texture_descriptor_pool_;

  // Only with non-null resources.
  std::vector<D3D12ImmediateTexture*> textures_;

  struct PendingTextureUpload {
    ID3D12Resource* texture;
    ID3D12Resource* buffer;
  };
  std::vector<PendingTextureUpload> texture_uploads_pending_;

  struct SubmittedTextureUpload {
    ID3D12Resource* texture;
    ID3D12Resource* buffer;
    uint64_t fence_value;
  };
  std::deque<SubmittedTextureUpload> texture_uploads_submitted_;

  std::vector<std::pair<ID3D12Resource*, uint64_t>> textures_deleted_;

  ID3D12GraphicsCommandList* current_command_list_ = nullptr;
  int current_render_target_width_, current_render_target_height_;
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
