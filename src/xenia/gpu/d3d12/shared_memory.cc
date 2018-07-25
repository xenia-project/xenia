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
              pages_in_sync_.size() * sizeof(uint64_t));

  std::memset(watched_pages_.data(), 0,
              watched_pages_.size() * sizeof(uint64_t));
  std::memset(watches_triggered_l2_.data(), 0,
              watches_triggered_l2_.size() * sizeof(uint64_t));

  std::memset(upload_pages_.data(), 0, upload_pages_.size() * sizeof(uint64_t));
  upload_buffer_available_first_ = nullptr;
  upload_buffer_submitted_first_ = nullptr;
  upload_buffer_submitted_last_ = nullptr;

  return true;
}

void SharedMemory::Shutdown() {
  while (upload_buffer_available_first_ != nullptr) {
    auto upload_buffer_next = upload_buffer_available_first_->next;
    upload_buffer_available_first_->buffer->Release();
    delete upload_buffer_available_first_;
    upload_buffer_available_first_ = upload_buffer_next;
  }
  while (upload_buffer_submitted_first_ != nullptr) {
    auto upload_buffer_next = upload_buffer_submitted_first_->next;
    upload_buffer_submitted_first_->buffer->Release();
    delete upload_buffer_submitted_first_;
    upload_buffer_submitted_first_ = upload_buffer_next;
  }

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

  // Make processed upload buffers available.
  uint64_t last_completed_frame = context_->GetLastCompletedFrame();
  while (upload_buffer_submitted_first_ != nullptr) {
    auto upload_buffer = upload_buffer_submitted_first_;
    if (upload_buffer->submit_frame > last_completed_frame) {
      break;
    }
    upload_buffer_submitted_first_ = upload_buffer->next;
    upload_buffer->next = upload_buffer_available_first_;
    upload_buffer_available_first_ = upload_buffer;
  }
  if (upload_buffer_submitted_first_ == nullptr) {
    upload_buffer_submitted_last_ = nullptr;
  }

  heap_creation_failed_ = false;
}

bool SharedMemory::EndFrame(ID3D12GraphicsCommandList* command_list_setup,
                            ID3D12GraphicsCommandList* command_list_draw) {
  // Before drawing starts, it's assumed that the buffer is a copy destination.
  // This transition is for the next frame, not for the current one.
  TransitionBuffer(D3D12_RESOURCE_STATE_COPY_DEST, command_list_draw);

  auto current_frame = context_->GetCurrentFrame();
  auto device = context_->GetD3D12Provider()->GetDevice();

  // Write ranges to upload buffers and submit them.
  const uint32_t upload_buffer_capacity = kUploadBufferSize >> page_size_log2_;
  assert_true(upload_buffer_capacity > 0);
  uint32_t upload_end = 0;
  void* upload_buffer_mapping = nullptr;
  uint32_t upload_buffer_written = 0;
  uint32_t upload_range_start = 0, upload_range_length;
  while ((upload_range_start =
              NextUploadRange(upload_end, upload_range_length)) != UINT_MAX) {
    while (upload_range_length > 0) {
      if (upload_buffer_mapping == nullptr) {
        // Create a completely new upload buffer if the available pool is empty.
        if (upload_buffer_available_first_ == nullptr) {
          D3D12_HEAP_PROPERTIES upload_buffer_heap_properties = {};
          upload_buffer_heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
          D3D12_RESOURCE_DESC upload_buffer_desc;
          upload_buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
          upload_buffer_desc.Alignment = 0;
          upload_buffer_desc.Width = kUploadBufferSize;
          upload_buffer_desc.Height = 1;
          upload_buffer_desc.DepthOrArraySize = 1;
          upload_buffer_desc.MipLevels = 1;
          upload_buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
          upload_buffer_desc.SampleDesc.Count = 1;
          upload_buffer_desc.SampleDesc.Quality = 0;
          upload_buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
          upload_buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
          ID3D12Resource* upload_buffer_resource;
          if (FAILED(device->CreateCommittedResource(
                  &upload_buffer_heap_properties, D3D12_HEAP_FLAG_NONE,
                  &upload_buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                  nullptr, IID_PPV_ARGS(&upload_buffer_resource)))) {
            XELOGE("Failed to create a shared memory upload buffer");
            break;
          }
          upload_buffer_available_first_ = new UploadBuffer;
          upload_buffer_available_first_->buffer = upload_buffer_resource;
          upload_buffer_available_first_->next = nullptr;
        }
        // New buffer, need to map it.
        D3D12_RANGE upload_buffer_read_range;
        upload_buffer_read_range.Begin = 0;
        upload_buffer_read_range.End = 0;
        if (FAILED(upload_buffer_available_first_->buffer->Map(
                0, &upload_buffer_read_range, &upload_buffer_mapping))) {
          XELOGE("Failed to map a shared memory upload buffer");
          break;
        }
      }

      // Upload the portion we can upload.
      uint32_t upload_write_length = std::min(
          upload_range_length, upload_buffer_capacity - upload_buffer_written);
      std::memcpy(
          reinterpret_cast<uint8_t*>(upload_buffer_mapping) +
              (upload_buffer_written << page_size_log2_),
          memory_->TranslatePhysical(upload_range_start << page_size_log2_),
          upload_write_length << page_size_log2_);
      command_list_draw->CopyBufferRegion(
          buffer_, upload_range_start << page_size_log2_,
          upload_buffer_available_first_->buffer,
          upload_buffer_written << page_size_log2_,
          upload_write_length << page_size_log2_);
      upload_buffer_written += upload_write_length;
      upload_range_start += upload_write_length;
      upload_range_length -= upload_write_length;
      upload_end = upload_range_start;

      // Check if we are done with this buffer.
      if (upload_buffer_written == upload_buffer_capacity) {
        auto upload_buffer = upload_buffer_available_first_;
        upload_buffer->buffer->Unmap(0, nullptr);
        upload_buffer_mapping = nullptr;
        upload_buffer_available_first_ = upload_buffer->next;
        upload_buffer->next = nullptr;
        upload_buffer->submit_frame = current_frame;
        if (upload_buffer_submitted_last_ != nullptr) {
          upload_buffer_submitted_last_->next = upload_buffer;
        } else {
          upload_buffer_submitted_first_ = upload_buffer;
        }
        upload_buffer_submitted_last_ = upload_buffer;
        upload_buffer_written = 0;
      }
    }
    if (upload_range_length > 0) {
      // Buffer creation or mapping failed.
      break;
    }
  }
  // Mark the last upload buffer as submitted if anything was uploaded from it,
  // also unmap it.
  if (upload_buffer_mapping != nullptr) {
    upload_buffer_available_first_->buffer->Unmap(0, nullptr);
  }
  if (upload_buffer_written > 0) {
    auto upload_buffer = upload_buffer_available_first_;
    upload_buffer_available_first_ = upload_buffer->next;
    upload_buffer->next = nullptr;
    upload_buffer->submit_frame = current_frame;
    if (upload_buffer_submitted_last_ != nullptr) {
      upload_buffer_submitted_last_->next = upload_buffer;
    } else {
      upload_buffer_submitted_first_ = upload_buffer;
    }
    upload_buffer_submitted_last_ = upload_buffer;
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
        end_block |= ~((1ull << start_page_local) - 1);
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
    region_start_coordinates.X = i << kHeapSizeLog2;
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

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
