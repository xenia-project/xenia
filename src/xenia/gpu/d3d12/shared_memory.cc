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
#include "xenia/base/profiling.h"

namespace xe {
namespace gpu {
namespace d3d12 {

SharedMemory::SharedMemory(Memory* memory, ui::d3d12::D3D12Context* context)
    : memory_(memory), context_(context) {
  page_size_log2_ = xe::log2_ceil(uint32_t(xe::memory::page_size()));
  page_count_ = kBufferSize >> page_size_log2_;
  uint32_t page_bitmap_length = page_count_ >> 6;
  assert_true(page_bitmap_length != 0);

  valid_pages_.resize(page_bitmap_length);
  protected_pages_.resize(page_bitmap_length);
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

  std::memset(valid_pages_.data(), 0, valid_pages_.size() * sizeof(uint64_t));

  std::memset(protected_pages_.data(), 0,
              protected_pages_.size() * sizeof(uint64_t));

  upload_buffer_pool_ =
      std::make_unique<ui::d3d12::UploadBufferPool>(context_, 4 * 1024 * 1024);

  memory_->SetGlobalPhysicalAccessWatch(MemoryWriteCallbackThunk, this);

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
  upload_buffer_pool_->BeginFrame();
  heap_creation_failed_ = false;
}

void SharedMemory::EndFrame() { upload_buffer_pool_->EndFrame(); }

SharedMemory::WatchHandle SharedMemory::WatchMemoryRange(
    uint32_t start, uint32_t length, WatchCallback callback,
    void* callback_context, void* callback_data, uint64_t callback_argument) {
  start &= kAddressMask;
  if (start >= kBufferSize || length == 0) {
    return nullptr;
  }
  length = std::min(length, kBufferSize - start);
  uint32_t watch_page_first = start >> page_size_log2_;
  uint32_t watch_page_last = (start + length - 1) >> page_size_log2_;
  uint32_t bucket_first =
      watch_page_first << page_size_log2_ >> kWatchBucketSizeLog2;
  uint32_t bucket_last =
      watch_page_last << page_size_log2_ >> kWatchBucketSizeLog2;

  std::lock_guard<std::mutex> lock(validity_mutex_);

  // Allocate the range.
  WatchRange* range = watch_range_first_free_;
  if (range != nullptr) {
    watch_range_first_free_ = range->next_free;
  } else {
    if (watch_range_pools_.empty() ||
        watch_range_current_pool_allocated_ >= kWatchRangePoolSize) {
      watch_range_pools_.push_back(new WatchRange[kWatchRangePoolSize]);
      watch_range_current_pool_allocated_ = 0;
      range =
          &(watch_range_pools_.back()[watch_range_current_pool_allocated_++]);
    }
  }
  range->callback = callback;
  range->callback_context = callback_context;
  range->callback_data = callback_data;
  range->callback_argument = callback_argument;
  range->page_first = watch_page_first;
  range->page_last = watch_page_last;

  // Allocate and link the nodes.
  WatchNode* node_previous = nullptr;
  for (uint32_t i = bucket_first; i <= bucket_last; ++i) {
    WatchNode* node = watch_node_first_free_;
    if (node != nullptr) {
      watch_node_first_free_ = node->next_free;
    } else {
      if (watch_node_pools_.empty() ||
          watch_node_current_pool_allocated_ >= kWatchNodePoolSize) {
        watch_node_pools_.push_back(new WatchNode[kWatchNodePoolSize]);
        watch_node_current_pool_allocated_ = 0;
        node =
            &(watch_node_pools_.back()[watch_node_current_pool_allocated_++]);
      }
    }
    node->range = range;
    node->range_node_next = nullptr;
    if (node_previous != nullptr) {
      node_previous->range_node_next = node;
    } else {
      range->node_first = node;
    }
    node_previous = node;
    node->bucket_node_previous = nullptr;
    node->bucket_node_next = watch_buckets_[i];
    if (watch_buckets_[i] != nullptr) {
      watch_buckets_[i]->bucket_node_previous = node;
    }
    watch_buckets_[i] = node;
  }

  return reinterpret_cast<WatchHandle>(range);
}

bool SharedMemory::RequestRange(uint32_t start, uint32_t length,
                                ID3D12GraphicsCommandList* command_list) {
  if (length == 0) {
    // Some texture is empty, for example - safe to draw in this case.
    return true;
  }
  start &= kAddressMask;
  if (start >= kBufferSize || (kBufferSize - start) < length) {
    // Exceeds the physical address space.
    return false;
  }
  uint32_t last = start + length - 1;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

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

  // Upload and protect used ranges.
  GetRangesToUpload(start >> page_size_log2_,
                    ((start & ((1 << page_size_log2_) - 1)) + length +
                     ((1 << page_size_log2_) - 1)) >>
                        page_size_log2_);
  if (upload_ranges_.size() == 0) {
    return true;
  }
  TransitionBuffer(D3D12_RESOURCE_STATE_COPY_DEST, command_list);
  for (auto upload_range : upload_ranges_) {
    uint32_t upload_range_start = upload_range.first;
    uint32_t upload_range_length = upload_range.second;
    while (upload_range_length != 0) {
      ID3D12Resource* upload_buffer;
      uint32_t upload_buffer_offset, upload_buffer_size;
      uint8_t* upload_buffer_mapping = upload_buffer_pool_->RequestPartial(
          upload_range_length << page_size_log2_, &upload_buffer,
          &upload_buffer_offset, &upload_buffer_size, nullptr);
      if (upload_buffer_mapping == nullptr) {
        XELOGE("Shared memory: Failed to get an upload buffer");
        return false;
      }
      uint32_t upload_buffer_pages = upload_buffer_size >> page_size_log2_;
      MakeRangeValid(upload_range_start, upload_buffer_pages);
      std::memcpy(
          upload_buffer_mapping,
          memory_->TranslatePhysical(upload_range_start << page_size_log2_),
          upload_buffer_size);
      command_list->CopyBufferRegion(
          buffer_, upload_range_start << page_size_log2_, upload_buffer,
          upload_buffer_offset, upload_buffer_size);
      upload_range_start += upload_buffer_pages;
      upload_range_length -= upload_buffer_pages;
    }
  }

  return true;
}

void SharedMemory::MakeRangeValid(uint32_t valid_page_first,
                                  uint32_t valid_page_count) {
  if (valid_page_first >= page_count_ || valid_page_count == 0) {
    return;
  }
  valid_page_count = std::min(valid_page_count, page_count_ - valid_page_first);
  uint32_t valid_page_last = valid_page_first + valid_page_count - 1;
  uint32_t valid_block_first = valid_page_first >> 6;
  uint32_t valid_block_last = valid_page_last >> 6;

  std::lock_guard<std::mutex> lock(validity_mutex_);

  for (uint32_t i = valid_block_first; i <= valid_block_last; ++i) {
    uint64_t valid_bits = UINT64_MAX;
    if (i == valid_block_first) {
      valid_bits &= ~((1ull << (valid_page_first & 63)) - 1);
    }
    if (i == valid_block_last && (valid_page_last & 63) != 63) {
      valid_bits &= (1ull << ((valid_page_last & 63) + 1)) - 1;
    }
    valid_pages_[i] |= valid_bits;
    protected_pages_[i] |= valid_bits;
  }

  memory_->ProtectPhysicalMemory(
      valid_page_first << page_size_log2_, valid_page_count << page_size_log2_,
      cpu::MMIOHandler::WatchType::kWatchWrite, false);
}

void SharedMemory::UnlinkWatchRange(WatchRange* range) {
  uint32_t bucket =
      range->page_first << page_size_log2_ >> kWatchBucketSizeLog2;
  WatchNode* node = range->node_first;
  while (node != nullptr) {
    WatchNode* node_next = node->range_node_next;
    if (node->bucket_node_previous != nullptr) {
      node->bucket_node_previous->bucket_node_next = node->bucket_node_next;
    } else {
      watch_buckets_[bucket] = node->bucket_node_next;
    }
    if (node->bucket_node_next != nullptr) {
      node->bucket_node_next->bucket_node_previous = node->bucket_node_previous;
    }
    node->next_free = watch_node_first_free_;
    watch_node_first_free_ = node;
    node = node_next;
    ++bucket;
  }
  range->next_free = watch_range_first_free_;
  watch_range_first_free_ = range;
}

void SharedMemory::GetRangesToUpload(uint32_t request_page_first,
                                     uint32_t request_page_count) {
  upload_ranges_.clear();
  if (request_page_first >= page_count_ || request_page_count == 0) {
    return;
  }
  request_page_count =
      std::min(request_page_count, page_count_ - request_page_first);
  uint32_t request_page_last = request_page_first + request_page_count - 1;
  uint32_t request_block_first = request_page_first >> 6;
  uint32_t request_block_last = request_page_last >> 6;

  std::lock_guard<std::mutex> lock(validity_mutex_);

  uint32_t range_start = UINT32_MAX;
  for (uint32_t i = request_block_first; i <= request_block_last; ++i) {
    uint64_t block_valid = valid_pages_[i];
    uint64_t block_invalid = ~block_valid;

    // Ignore pages outside the requested range in bits scans completely.
    uint64_t bits_to_keep;
    if (i == request_block_first) {
      bits_to_keep = ~((1ull << (request_page_first & 63)) - 1);
      block_valid &= bits_to_keep;
      block_invalid &= bits_to_keep;
    }
    if (i == request_block_last && (request_page_last & 63) != 63) {
      bits_to_keep = (1ull << ((request_page_last & 63) + 1)) - 1;
      block_valid &= bits_to_keep;
      block_invalid &= bits_to_keep;
    }

    while (true) {
      uint32_t block_page;
      if (range_start == UINT32_MAX) {
        // Check if need to open a new range.
        if (!xe::bit_scan_forward(block_invalid, &block_page)) {
          break;
        }
        range_start = (i << 6) + block_page;
      } else {
        // Check if need to close the range.
        if (!xe::bit_scan_forward(block_valid, &block_page)) {
          break;
        }
        upload_ranges_.push_back(
            std::make_pair(range_start, (i << 6) + block_page - range_start));
        range_start = UINT32_MAX;
      }
      // There may be multiple ranges within a single block, so ignore the bits
      // that have already been processed.
      bits_to_keep = ~((1ull << block_page) - 1);
      block_valid &= bits_to_keep;
      block_invalid &= bits_to_keep;
    }
  }
  if (range_start != UINT32_MAX) {
    upload_ranges_.push_back(
        std::make_pair(range_start, request_page_last + 1 - range_start));
  }
}

bool SharedMemory::MemoryWriteCallbackThunk(void* context_ptr,
                                            uint32_t address) {
  SharedMemory* shared_memory = reinterpret_cast<SharedMemory*>(context_ptr);
  return shared_memory->MemoryWriteCallback(address);
}

bool SharedMemory::MemoryWriteCallback(uint32_t address) {
  uint32_t page_index = (address & kAddressMask) >> page_size_log2_;
  uint32_t block_index = page_index >> 6;
  uint64_t page_bit = 1ull << (page_index & 63);

  std::lock_guard<std::mutex> lock(validity_mutex_);

  if (!(protected_pages_[block_index] & page_bit)) {
    return false;
  }

  valid_pages_[block_index] &= ~page_bit;
  // TODO(Triang3l): Invoke watch callbacks.

  memory_->UnprotectPhysicalMemory(page_index << page_size_log2_,
                                   1 << page_size_log2_, false);
  protected_pages_[block_index] &= ~page_bit;
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
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
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
