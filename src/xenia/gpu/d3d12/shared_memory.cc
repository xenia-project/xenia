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

SharedMemory::SharedMemory(Memory* memory, ui::d3d12::D3D12Context* context)
    : memory_(memory), context_(context) {
  page_size_log2_ = xe::math::log2_ceil(xe::memory::page_size());
  page_count_ = kBufferSize >> page_size_log2_;
  uint32_t page_bitmap_length = page_count_ >> 6;

  pages_in_sync_.resize(page_bitmap_length);

  watched_pages_.resize(page_bitmap_length);
  watches_triggered_l1_.resize(page_bitmap_length);
  watches_triggered_l2_.resize(page_bitmap_length >> 6);
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
  heap_creation_failed_ = false;

  std::memset(pages_in_sync_.data(), 0,
              page_in_sync_.size() * sizeof(uint64_t));

  std::memset(watched_pages_.data(), 0,
              watched_pages_.size() * sizeof(uint64_t));
  std::memset(watches_triggered_l2_.data(), 0,
              watches_triggered_l2_.size() * sizeof(uint64_t));

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

void SharedMemory::BeginFrame() {
  // Check triggered watches, clear them and mark modified pages as out of date.
  watch_mutex_.lock();
  for (uint32_t i = 0; i < watches_triggered_l2_.size(); ++i) {
    uint64_t bits_l2 = watches_triggered_l2_[i];
    uint32_t index_l2;
    while (xe::bit_scan_forward(bits_l2, &index_l2)) {
      bits_l2 &= ~(1ull << index_l2);
      uint32_t index_l1 = (i << 6) + index_l2;
      pages_in_sync_[index_l1] &= ~(watches_triggered_l1[index_l1]);
    }
    watches_triggered_l2_[i] = 0;
  }
  watch_mutex_.unlock();

  heap_creation_failed_ = false;
}

bool SharedMemory::UseRange(uint32_t start, uint32_t length) {
  if (length == 0) {
    // Some texture is empty, for example - safe to draw in this case.
    return true;
  }
  start &= kAddressMask;
  if ((kBufferSize - start) < length) {
    // Exceeds the physical address space.
    return false;
  }

  // Ensure all tile heaps are present.
  uint32_t heap_first = start >> kHeapSizeLog2;
  uint32_t heap_last = (start + length - 1) >> kHeapSizeLog2;
  for (uint32_t i = heap_first; i <= heap_last; ++i) {
    if (heaps_[i] != nullptr) {
      continue;
    }
    if (heap_creation_failed_) {
      // Don't try to create a heap for every vertex buffer or texture in the
      // current frame anymore if have failed at least once.
      return false;
    }
    auto provider = context_->GetD3D12Provider();
    auto device = provider->GetDevice();
    auto direct_queue = provider->GetDirectQueue();
    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = kHeapSize;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heaps_[i])))) {
      heap_creation_failed_ = true;
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
    // FIXME(Triang3l): This may cause issues if the emulator is shut down
    // mid-frame and the heaps are destroyed before tile mappings are updated
    // (AwaitAllFramesCompletion won't catch this then). Defer this until the
    // actual command list submission at the end of the frame.
    direct_queue->UpdateTileMappings(
        buffer_, 1, &region_start_coordinates, &region_size, heaps_[i], 1,
        &range_flags, &heap_range_start_offset, &range_tile_count,
        D3D12_TILE_MAPPING_FLAG_NONE);
  }
  // TODO(Triang3l): Mark the range for upload.
  return true;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
