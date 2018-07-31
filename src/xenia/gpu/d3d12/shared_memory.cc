/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/shared_memory.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"

namespace xe {
namespace gpu {
namespace d3d12 {

SharedMemory::SharedMemory(Memory* memory, ui::d3d12::D3D12Context* context)
    : memory_(memory), context_(context) {
  page_size_log2_ = xe::log2_ceil(uint32_t(xe::memory::page_size()));
  page_count_ = kBufferSize >> page_size_log2_;
  uint32_t page_bitmap_length = page_count_ >> 6;
  uint32_t page_bitmap_l2_length = page_bitmap_length >> 6;
  assert_true(page_bitmap_l2_length > 0);

  pages_in_sync_.resize(page_bitmap_length);

  watched_pages_.resize(page_bitmap_length);
  watches_triggered_l1_.resize(page_bitmap_length);
  watches_triggered_l2_.resize(page_bitmap_l2_length);

  upload_pages_.resize(page_bitmap_length);
}

SharedMemory::~SharedMemory() { Shutdown(); }

bool SharedMemory::Initialize() {
  auto device = context_->GetD3D12Provider()->GetDevice();

  buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  D3D12_RESOURCE_DESC buffer_desc;
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Alignment = 0;
  buffer_desc.Width = kBufferSize;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.SampleDesc.Quality = 0;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (FLAGS_d3d12_tiled_resources) {
    if (FAILED(device->CreateReservedResource(
            &buffer_desc, buffer_state_, nullptr, IID_PPV_ARGS(&buffer_)))) {
      XELOGE("Shared memory: Failed to create the 512 MB tiled buffer");
      Shutdown();
      return false;
    }
  } else {
    D3D12_HEAP_PROPERTIES heap_properties = {};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(device->CreateCommittedResource(
            &heap_properties, D3D12_HEAP_FLAG_NONE, &buffer_desc, buffer_state_,
            nullptr, IID_PPV_ARGS(&buffer_)))) {
      XELOGE("Shared memory: Failed to create the 512 MB buffer");
      Shutdown();
      return false;
    }
  }
  buffer_gpu_address_ = buffer_->GetGPUVirtualAddress();

  std::memset(heaps_, 0, sizeof(heaps_));
  heap_creation_failed_ = false;

  std::memset(pages_in_sync_.data(), 0,
              pages_in_sync_.size() * sizeof(uint64_t));

  std::memset(watched_pages_.data(), 0,
              watched_pages_.size() * sizeof(uint64_t));
  std::memset(watches_triggered_l2_.data(), 0,
              watches_triggered_l2_.size() * sizeof(uint64_t));

  std::memset(upload_pages_.data(), 0, upload_pages_.size() * sizeof(uint64_t));
  upload_buffer_pool_ =
      std::make_unique<ui::d3d12::UploadBufferPool>(context_, 4 * 1024 * 1024);

  memory_->SetGlobalPhysicalAccessWatch(WatchCallbackThunk, this);

  return true;
}

void SharedMemory::Shutdown() {
  memory_->SetGlobalPhysicalAccessWatch(nullptr, nullptr);

  upload_buffer_pool_.reset();

  // First free the buffer to detach it from the heaps.
  if (buffer_ != nullptr) {
    buffer_->Release();
    buffer_ = nullptr;
  }

  if (FLAGS_d3d12_tiled_resources) {
    for (uint32_t i = 0; i < xe::countof(heaps_); ++i) {
      if (heaps_[i] != nullptr) {
        heaps_[i]->Release();
        heaps_[i] = nullptr;
      }
    }
  }
}

void SharedMemory::BeginFrame() {
  // XELOGGPU("SharedMemory: BeginFrame start");

  // Check triggered watches, clear them and mark modified pages as out of date.
  watch_mutex_.lock();
  for (uint32_t i = 0; i < watches_triggered_l2_.size(); ++i) {
    uint64_t bits_l2 = watches_triggered_l2_[i];
    uint32_t index_l1_local;
    while (xe::bit_scan_forward(bits_l2, &index_l1_local)) {
      bits_l2 &= ~(1ull << index_l1_local);
      uint32_t index_l1_global = (i << 6) + index_l1_local;
      pages_in_sync_[index_l1_global] &=
          ~(watches_triggered_l1_[index_l1_global]);
    }
    watches_triggered_l2_[i] = 0;
  }
  watch_mutex_.unlock();

  upload_buffer_pool_->BeginFrame();

  heap_creation_failed_ = false;

  // XELOGGPU("SharedMemory: BeginFrame end");
}

bool SharedMemory::EndFrame(ID3D12GraphicsCommandList* command_list_setup,
                            ID3D12GraphicsCommandList* command_list_draw) {
  // XELOGGPU("SharedMemory: EndFrame start");

  // Before drawing starts, it's assumed that the buffer is a copy destination.
  // This transition is for the next frame, not for the current one.
  TransitionBuffer(D3D12_RESOURCE_STATE_COPY_DEST, command_list_draw);

  auto current_frame = context_->GetCurrentFrame();
  auto device = context_->GetD3D12Provider()->GetDevice();

  // Write ranges to upload buffers and submit them.
  uint32_t upload_end = 0, upload_range_start = 0, upload_range_length;
  while ((upload_range_start =
              NextUploadRange(upload_end, upload_range_length)) != UINT_MAX) {
    /* XELOGGPU(
        "Shared memory: Uploading %.8X-%.8X range",
        upload_range_start << page_size_log2_,
        ((upload_range_start + upload_range_length) << page_size_log2_) - 1); */
    while (upload_range_length > 0) {
      ID3D12Resource* upload_buffer;
      uint32_t upload_buffer_offset, upload_buffer_size;
      uint8_t* upload_buffer_mapping = upload_buffer_pool_->RequestPartial(
          upload_range_length << page_size_log2_, &upload_buffer,
          &upload_buffer_offset, &upload_buffer_size, nullptr);
      if (upload_buffer_mapping == nullptr) {
        XELOGE("Shared memory: Failed to get an upload buffer");
        break;
      }
      std::memcpy(
          upload_buffer_mapping,
          memory_->TranslatePhysical(upload_range_start << page_size_log2_),
          upload_buffer_size);
      command_list_setup->CopyBufferRegion(
          buffer_, upload_range_start << page_size_log2_, upload_buffer,
          upload_buffer_offset, upload_buffer_size);
      upload_range_start += upload_buffer_size >> page_size_log2_;
      upload_range_length -= upload_buffer_size >> page_size_log2_;
      upload_end = upload_range_start;
    }
    if (upload_range_length > 0) {
      // Buffer creation or mapping failed.
      break;
    }
  }
  upload_buffer_pool_->EndFrame();

  // Protect the uploaded ranges.
  // TODO(Triang3l): Add L2 or store ranges in a list - this may hold the mutex
  // for pretty long.
  if (upload_end != 0) {
    watch_mutex_.lock();
    uint32_t protect_end = 0, protect_start, protect_length;
    while ((protect_start = NextUploadRange(protect_end, protect_length)) !=
           UINT_MAX) {
      if (protect_start >= upload_end) {
        break;
      }
      protect_length = std::min(protect_length, upload_end - protect_start);
      uint32_t protect_last = protect_start + protect_length - 1;
      uint32_t protect_block_first = protect_start >> 6;
      uint32_t protect_block_last = protect_last >> 6;
      for (uint32_t i = protect_block_first; i <= protect_block_last; ++i) {
        uint64_t protect_bits = ~0ull;
        if (i == protect_block_first) {
          protect_bits &= ~((1ull << (protect_start & 63)) - 1);
        }
        if (i == protect_block_last && (protect_last & 63) != 63) {
          protect_bits &= (1ull << ((protect_last & 63) + 1)) - 1;
        }
        watched_pages_[i] |= protect_bits;
      }
      memory_->ProtectPhysicalMemory(protect_start << page_size_log2_,
                                     protect_length << page_size_log2_,
                                     cpu::MMIOHandler::WatchType::kWatchWrite);
      protect_end = protect_last + 1;
      if (protect_end >= upload_end) {
        break;
      }
    }
    watch_mutex_.unlock();
  }

  // Mark the newly uploaded ranges as uploaded.
  std::memset(upload_pages_.data(), 0, (upload_end >> 6) * sizeof(uint64_t));
  if (upload_end < page_count_) {
    upload_pages_[upload_end >> 6] &= ~((1ull << (upload_end & 63)) - 1);
  }

  // If some upload failed, mark the pages not uploaded as out-of-date again
  // because they were marked as up-to-date when used as textures/buffers.
  if (upload_range_start != UINT_MAX) {
    for (uint32_t i = upload_end >> 6; i < upload_pages_.size(); ++i) {
      pages_in_sync_[i] &= ~(upload_pages_[i]);
    }
  }

  // XELOGGPU("SharedMemory: EndFrame end");

  return upload_end != 0;
}

uint32_t SharedMemory::NextUploadRange(uint32_t search_start,
                                       uint32_t& length) const {
  uint32_t search_start_block_index = search_start >> 6;
  for (uint32_t i = search_start_block_index; i < upload_pages_.size(); ++i) {
    uint64_t start_block = upload_pages_[i];
    if (i == search_start_block_index) {
      // Exclude already visited pages in the first checked 64-page block.
      start_block &= ~((1ull << (search_start & 63)) - 1);
    }
    uint32_t start_page_local;
    if (!xe::bit_scan_forward(start_block, &start_page_local)) {
      continue;
    }
    // Found the beginning of a range - find the end.
    uint32_t start_page = (i << 6) + start_page_local;
    for (uint32_t j = i; j < upload_pages_.size(); ++j) {
      uint64_t end_block = upload_pages_[i];
      if (j == i) {
        end_block |= (1ull << start_page_local) - 1;
      }
      uint32_t end_page_local;
      if (xe::bit_scan_forward(~end_block, &end_page_local)) {
        length = ((j << 6) + end_page_local) - start_page;
        return start_page;
      }
    }
    length = page_count_ - start_page;
    return start_page;
  }
  return UINT_MAX;
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
  uint32_t last = start + length - 1;
  // XELOGGPU("Shared memory: Range %.8X-%.8X is being used", start, last);

  // Ensure all tile heaps are present.
  if (FLAGS_d3d12_tiled_resources) {
    uint32_t heap_first = start >> kHeapSizeLog2;
    uint32_t heap_last = last >> kHeapSizeLog2;
    for (uint32_t i = heap_first; i <= heap_last; ++i) {
      if (heaps_[i] != nullptr) {
        continue;
      }
      if (heap_creation_failed_) {
        // Don't try to create a heap for every vertex buffer or texture in the
        // current frame anymore if have failed at least once.
        return false;
      }
      /* XELOGGPU("Shared memory: Creating %.8X-%.8X tile heap",
               heap_first << kHeapSizeLog2,
               (heap_last << kHeapSizeLog2) + (kHeapSize - 1)); */
      auto provider = context_->GetD3D12Provider();
      auto device = provider->GetDevice();
      auto direct_queue = provider->GetDirectQueue();
      D3D12_HEAP_DESC heap_desc = {};
      heap_desc.SizeInBytes = kHeapSize;
      heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
      heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
      if (FAILED(device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heaps_[i])))) {
        XELOGE("Shared memory: Failed to create a tile heap");
        heap_creation_failed_ = true;
        return false;
      }
      D3D12_TILED_RESOURCE_COORDINATE region_start_coordinates;
      region_start_coordinates.X = i << (kHeapSizeLog2 - kTileSizeLog2);
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
  }

  // Mark the outdated tiles in this range as requiring upload, and also make
  // them up-to-date so textures aren't invalidated every use.
  // TODO(Triang3l): Invalidate textures referencing outdated pages.
  // Safe invalidate textures here because only actually used ranges will be
  // uploaded and marked as in-sync at the end of the frame.
  uint32_t page_first_index = start >> page_size_log2_;
  uint32_t page_last_index = last >> page_size_log2_;
  uint32_t block_first_index = page_first_index >> 6;
  uint32_t block_last_index = page_last_index >> 6;
  for (uint32_t i = block_first_index; i <= block_last_index; ++i) {
    uint64_t block_outdated = ~pages_in_sync_[i];
    if (i == block_first_index) {
      block_outdated &= ~((1ull << (page_first_index & 63)) - 1);
    }
    if (i == block_last_index && (page_last_index & 63) != 63) {
      block_outdated &= (1ull << ((page_last_index & 63) + 1)) - 1;
    }
    pages_in_sync_[i] |= block_outdated;
    upload_pages_[i] |= block_outdated;
  }

  return true;
}

bool SharedMemory::WatchCallbackThunk(void* context_ptr, uint32_t address) {
  return reinterpret_cast<SharedMemory*>(context_ptr)->WatchCallback(address);
}

bool SharedMemory::WatchCallback(uint32_t address) {
  address &= 0x1FFFFFFF;
  uint32_t page_index_l1_global = address >> page_size_log2_;
  uint32_t block_index_l1 = page_index_l1_global >> 6;
  uint64_t page_bit_l1 = 1ull << (page_index_l1_global & 63);

  std::lock_guard<std::mutex> lock(watch_mutex_);
  if (!(watched_pages_[block_index_l1] & page_bit_l1)) {
    return false;
  }
  // XELOGGPU("Shared memory: Watch triggered for %.8X", address);

  // Mark the page as modified.
  uint32_t block_index_l2 = block_index_l1 >> 6;
  uint64_t page_bit_l2 = 1ull << (block_index_l1 & 63);
  if (!(watches_triggered_l2_[block_index_l2] & page_bit_l2)) {
    watches_triggered_l2_[block_index_l2] |= page_bit_l2;
    // L1 is not cleared in BeginFrame, so clear it now.
    watches_triggered_l1_[block_index_l1] = 0;
  }
  watches_triggered_l1_[block_index_l1] |= page_bit_l1;

  // Unprotect the page.
  memory_->UnprotectPhysicalMemory(page_index_l1_global << page_size_log2_,
                                   1 << page_size_log2_);
  watched_pages_[block_index_l1] &= ~page_bit_l1;
  return true;
}

void SharedMemory::TransitionBuffer(D3D12_RESOURCE_STATES new_state,
                                    ID3D12GraphicsCommandList* command_list) {
  if (buffer_state_ == new_state) {
    return;
  }
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = buffer_;
  barrier.Transition.Subresource = 0;
  barrier.Transition.StateBefore = buffer_state_;
  barrier.Transition.StateAfter = new_state;
  command_list->ResourceBarrier(1, &barrier);
  buffer_state_ = new_state;
}

void SharedMemory::UseForReading(ID3D12GraphicsCommandList* command_list) {
  TransitionBuffer(D3D12_RESOURCE_STATE_INDEX_BUFFER |
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   command_list);
}

void SharedMemory::UseForWriting(ID3D12GraphicsCommandList* command_list) {
  TransitionBuffer(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, command_list);
}

void SharedMemory::CreateSRV(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  desc.Buffer.FirstElement = 0;
  desc.Buffer.NumElements = kBufferSize >> 2;
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  context_->GetD3D12Provider()->GetDevice()->CreateShaderResourceView(
      buffer_, &desc, handle);
}

void SharedMemory::CreateUAV(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = 0;
  desc.Buffer.NumElements = kBufferSize >> 2;
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.CounterOffsetInBytes = 0;
  desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_->GetD3D12Provider()->GetDevice()->CreateUnorderedAccessView(
      buffer_, nullptr, &desc, handle);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
