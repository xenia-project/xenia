/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/shared_memory.h"

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"

namespace xe {
namespace gpu {
namespace d3d12 {

SharedMemory::SharedMemory(ui::d3d12::D3D12Context* context)
    : context_(context) {
  page_size_log2_ = xe::math::log2_ceil(xe::memory::page_size());
  pages_in_sync_.resize(kBufferSize >> page_size_log2_ >> 6);
}

SharedMemory::~SharedMemory() { Shutdown(); }

bool SharedMemory::Initialize() {
  auto device = context_->GetD3D12Provider()->GetDevice();

  buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  D3D12_RESOURCE_DESC buffer_desc;
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  buffer_desc.Width = kBufferSize;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.SampleDesc.Quality = 0;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (FAILED(device->CreateReservedResource(&buffer_desc, buffer_state_,
                                            nullptr, IID_PPV_ARGS(&buffer_)))) {
    XELOGE("Failed to create the 512 MB shared memory tiled buffer");
    Shutdown();
    return false;
  }

  std::memset(heaps_, 0, sizeof(heaps_));

  std::memset(pages_in_sync_.data(), 0,
              page_in_sync_.size() * sizeof(uint64_t));

  return true;
}

void SharedMemory::Shutdown() {
  // First free the buffer to detach it from the heaps.
  if (buffer_ != nullptr) {
    buffer_->Release();
    buffer_ = nullptr;
  }

  for (uint32_t i = 0; i < xe::countof(heaps_); ++i) {
    if (heaps_[i] != nullptr) {
      heaps_[i]->Release();
      heaps_[i] = nullptr;
    }
  }
}

bool SharedMemory::EnsureRangeAllocated(uint32_t start, uint32_t length) {
  if (length == 0) {
    // Some texture is empty, for example - safe to draw in this case.
    return true;
  }
  start &= kAddressMask;
  if ((kBufferSize - start) < length) {
    // Exceeds the physical address space.
    return false;
  }
  uint32_t heap_first = start >> kHeapSizeLog2;
  uint32_t heap_last = (start + length - 1) >> kHeapSizeLog2;
  for (uint32_t i = heap_first; i <= heap_last; ++i) {
    if (heaps_[i] != nullptr) {
      continue;
    }
    // TODO(Triang3l): If heap creation has failed at least once in this frame,
    // don't try to allocate heaps until the next frame.
    auto provider = context_->GetD3D12Provider();
    auto device = provider->GetDevice();
    auto direct_queue = provider->GetDirectQueue();
    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = kHeapSize;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heaps_[i])))) {
      return false;
    }
    D3D12_TILED_RESOURCE_COORDINATE region_start_coordinates;
    region_start_coordinates.X = i << kHeapSize;
    region_start_coordinates.Y = 0;
    region_start_coordinates.Z = 0;
    region_start_coordinates.Subresource = 0;
    D3D12_TILE_REGION_SIZE region_size;
    region_size.NumTiles = kHeapSize >> kTileSizeLog2;
    region_size.UseBox = FALSE;
    D3D12_TILE_RANGE_FLAGS range_flags = D3D12_TILE_RANGE_FLAG_NONE;
    UINT heap_range_start_offset = 0;
    UINT range_tile_count = kHeapSize >> kTileSizeLog2;
    direct_queue->UpdateTileMappings(
        buffer_, 1, &region_start_coordinates, &region_size, heaps_[i], 1,
        &range_flags, &heap_range_start_offset, &range_tile_count,
        D3D12_TILE_MAPPING_FLAG_NONE);
  }
  return true;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
