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
#include <vector>

#include "xenia/ui/d3d12/command_list.h"
#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/d3d12/d3d12_context.h"
#include "xenia/ui/d3d12/pools.h"
#include "xenia/ui/immediate_drawer.h"

namespace xe {
namespace ui {
namespace d3d12 {

class D3D12ImmediateDrawer : public ImmediateDrawer {
 public:
  D3D12ImmediateDrawer(D3D12Context* graphics_context);
  ~D3D12ImmediateDrawer() override;

  bool Initialize();
  void Shutdown();

  std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t width,
                                                  uint32_t height,
                                                  ImmediateTextureFilter filter,
                                                  bool repeat,
                                                  const uint8_t* data) override;
  void UpdateTexture(ImmediateTexture* texture, const uint8_t* data) override;

  void Begin(int render_target_width, int render_target_height) override;
  void BeginDrawBatch(const ImmediateDrawBatch& batch) override;
  void Draw(const ImmediateDraw& draw) override;
  void EndDrawBatch() override;
  void End() override;

 private:
  D3D12Context* context_ = nullptr;

  ID3D12RootSignature* root_signature_ = nullptr;
  enum class RootParameter {
    kRestrictTextureSamples,
    kTexture,
    kSampler,
    kViewportInvSize,

    kCount
  };

  ID3D12PipelineState* pipeline_triangle_ = nullptr;
  ID3D12PipelineState* pipeline_line_ = nullptr;

  enum class SamplerIndex {
    kNearestClamp,
    kLinearClamp,
    kNearestRepeat,
    kLinearRepeat,

    kCount,
    kInvalid = kCount
  };
  ID3D12DescriptorHeap* sampler_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_heap_cpu_start_;
  D3D12_GPU_DESCRIPTOR_HANDLE sampler_heap_gpu_start_;

  std::unique_ptr<UploadBufferPool> vertex_buffer_pool_ = nullptr;
  std::unique_ptr<DescriptorHeapPool> texture_descriptor_pool_ = nullptr;

  struct PendingTextureUpload {
    ImmediateTexture* texture;
    ID3D12Resource* buffer;
  };
  std::vector<PendingTextureUpload> texture_uploads_pending_;

  struct SubmittedTextureUpload {
    ID3D12Resource* buffer;
    uint64_t frame;
  };
  std::deque<SubmittedTextureUpload> texture_uploads_submitted_;

  ID3D12GraphicsCommandList* current_command_list_ = nullptr;
  int current_render_target_width_, current_render_target_height_;
  bool batch_open_ = false;
  bool batch_has_index_buffer_;
  uint64_t texture_descriptor_pool_full_update_;
  D3D_PRIMITIVE_TOPOLOGY current_primitive_topology_;
  ImmediateTexture* current_texture_;
  SamplerIndex current_sampler_index_;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_IMMEDIATE_DRAWER_H_
