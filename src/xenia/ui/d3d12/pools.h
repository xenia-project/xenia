/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_POOLS_H_
#define XENIA_UI_D3D12_POOLS_H_

#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/d3d12/d3d12_context.h"

namespace xe {
namespace ui {
namespace d3d12 {

class UploadBufferPool {
 public:
  UploadBufferPool(D3D12Context* context, uint32_t page_size);
  ~UploadBufferPool();

  void BeginFrame();
  void EndFrame();
  void ClearCache();

  // Request to write data in a single piece, creating a new page if the current
  // one doesn't have enough free space.
  uint8_t* RequestFull(uint32_t size, ID3D12Resource*& buffer_out,
                       uint32_t& offset_out);
  // Request to write data in multiple parts, filling the buffer entirely.
  uint8_t* RequestPartial(uint32_t size, ID3D12Resource*& buffer_out,
                          uint32_t& offset_out, uint32_t& size_out);

 private:
  D3D12Context* context_;
  uint32_t page_size_;

  void EndPage();
  bool BeginNextPage();

  struct UploadBuffer {
    ID3D12Resource* buffer;
    UploadBuffer* next;
    uint64_t frame_sent;
  };

  // A list of unsent buffers, with the first one being the current.
  UploadBuffer* unsent_ = nullptr;
  // A list of sent buffers, moved to unsent in the beginning of a frame.
  UploadBuffer* sent_first_ = nullptr;
  UploadBuffer* sent_last_ = nullptr;

  uint32_t current_size_ = 0;
  uint8_t* current_mapping_ = nullptr;

  // Reset in the beginning of a frame - don't try and fail to create a new
  // buffer if failed to create one in the current frame.
  bool creation_failed_ = false;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_POOLS_H_
