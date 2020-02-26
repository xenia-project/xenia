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
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/ui/d3d12/d3d12_util.h"

DEFINE_bool(d3d12_tiled_shared_memory, true,
            "Enable tiled resources for shared memory emulation. Disabling "
            "them greatly increases video memory usage - a 512 MB buffer is "
            "created - but allows graphics debuggers that don't support tiled "
            "resources to work.",
            "D3D12");

namespace xe {
namespace gpu {
namespace d3d12 {

constexpr uint32_t SharedMemory::kBufferSizeLog2;
constexpr uint32_t SharedMemory::kBufferSize;
constexpr uint32_t SharedMemory::kHeapSizeLog2;
constexpr uint32_t SharedMemory::kHeapSize;
constexpr uint32_t SharedMemory::kWatchBucketSizeLog2;
constexpr uint32_t SharedMemory::kWatchBucketCount;
constexpr uint32_t SharedMemory::kWatchRangePoolSize;
constexpr uint32_t SharedMemory::kWatchNodePoolSize;

SharedMemory::SharedMemory(D3D12CommandProcessor* command_processor,
                           Memory* memory, TraceWriter* trace_writer)
    : command_processor_(command_processor),
      memory_(memory),
      trace_writer_(trace_writer) {
  page_size_log2_ = xe::log2_ceil(uint32_t(xe::memory::page_size()));
  page_count_ = kBufferSize >> page_size_log2_;
}

SharedMemory::~SharedMemory() { Shutdown(); }

bool SharedMemory::Initialize() {
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();

  D3D12_RESOURCE_DESC buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(
      buffer_desc, kBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  if (AreTiledResourcesUsed()) {
    if (FAILED(device->CreateReservedResource(
            &buffer_desc, buffer_state_, nullptr, IID_PPV_ARGS(&buffer_)))) {
      XELOGE("Shared memory: Failed to create the 512 MB tiled buffer");
      Shutdown();
      return false;
    }
  } else {
    XELOGGPU(
        "Direct3D 12 tiled resources are not used for shared memory "
        "emulation - video memory usage may increase significantly "
        "because a full 512 MB buffer will be created!");
    if (provider->GetGraphicsAnalysis() != nullptr) {
      // As of October 8th, 2018, PIX doesn't support tiled buffers.
      // FIXME(Triang3l): Re-enable tiled resources with PIX once fixed.
      XELOGGPU(
          "This is caused by PIX being attached, which doesn't support tiled "
          "resources yet.");
    }
    if (FAILED(device->CreateCommittedResource(
            &ui::d3d12::util::kHeapPropertiesDefault, D3D12_HEAP_FLAG_NONE,
            &buffer_desc, buffer_state_, nullptr, IID_PPV_ARGS(&buffer_)))) {
      XELOGE("Shared memory: Failed to create the 512 MB buffer");
      Shutdown();
      return false;
    }
  }
  buffer_gpu_address_ = buffer_->GetGPUVirtualAddress();

  std::memset(heaps_, 0, sizeof(heaps_));
  heap_count_ = 0;

  D3D12_DESCRIPTOR_HEAP_DESC buffer_descriptor_heap_desc;
  buffer_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  buffer_descriptor_heap_desc.NumDescriptors =
      uint32_t(BufferDescriptorIndex::kCount);
  buffer_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  buffer_descriptor_heap_desc.NodeMask = 0;
  if (FAILED(device->CreateDescriptorHeap(
          &buffer_descriptor_heap_desc,
          IID_PPV_ARGS(&buffer_descriptor_heap_)))) {
    XELOGE(
        "Failed to create the descriptor heap for shared memory buffer views");
    Shutdown();
    return false;
  }
  buffer_descriptor_heap_start_ =
      buffer_descriptor_heap_->GetCPUDescriptorHandleForHeapStart();
  ui::d3d12::util::CreateRawBufferSRV(
      device,
      provider->OffsetViewDescriptor(buffer_descriptor_heap_start_,
                                     uint32_t(BufferDescriptorIndex::kRawSRV)),
      buffer_, kBufferSize);
  ui::d3d12::util::CreateRawBufferUAV(
      device,
      provider->OffsetViewDescriptor(buffer_descriptor_heap_start_,
                                     uint32_t(BufferDescriptorIndex::kRawUAV)),
      buffer_, kBufferSize);

  system_page_flags_.clear();
  system_page_flags_.resize((page_count_ + 63) / 64);

  upload_buffer_pool_ = std::make_unique<ui::d3d12::UploadBufferPool>(
      device,
      xe::align(uint32_t(4 * 1024 * 1024), uint32_t(1) << page_size_log2_));

  memory_invalidation_callback_handle_ =
      memory_->RegisterPhysicalMemoryInvalidationCallback(
          MemoryInvalidationCallbackThunk, this);

  ResetTraceGPUWrittenBuffer();

  return true;
}

void SharedMemory::Shutdown() {
  ResetTraceGPUWrittenBuffer();

  FireWatches(0, (kBufferSize - 1) >> page_size_log2_, false);
  assert_true(global_watches_.empty());
  // No watches now, so no references to the pools accessible by guest threads -
  // safe not to enter the global critical region.
  watch_node_first_free_ = nullptr;
  watch_node_current_pool_allocated_ = 0;
  for (WatchNode* pool : watch_node_pools_) {
    delete[] pool;
  }
  watch_node_pools_.clear();
  watch_range_first_free_ = nullptr;
  watch_range_current_pool_allocated_ = 0;
  for (WatchRange* pool : watch_range_pools_) {
    delete[] pool;
  }
  watch_range_pools_.clear();

  if (memory_invalidation_callback_handle_ != nullptr) {
    memory_->UnregisterPhysicalMemoryInvalidationCallback(
        memory_invalidation_callback_handle_);
    memory_invalidation_callback_handle_ = nullptr;
  }

  upload_buffer_pool_.reset();

  ui::d3d12::util::ReleaseAndNull(buffer_descriptor_heap_);

  // First free the buffer to detach it from the heaps.
  ui::d3d12::util::ReleaseAndNull(buffer_);

  if (AreTiledResourcesUsed()) {
    for (uint32_t i = 0; i < xe::countof(heaps_); ++i) {
      ui::d3d12::util::ReleaseAndNull(heaps_[i]);
    }
    heap_count_ = 0;
    COUNT_profile_set("gpu/shared_memory/used_mb", 0);
  }
}

void SharedMemory::ClearCache() {
  upload_buffer_pool_->ClearCache();

  // Keeping GPU-written data, so "invalidated by GPU".
  FireWatches(0, (kBufferSize - 1) >> page_size_log2_, true);
  // No watches now, so no references to the pools accessible by guest threads -
  // safe not to enter the global critical region.
  watch_node_first_free_ = nullptr;
  watch_node_current_pool_allocated_ = 0;
  for (WatchNode* pool : watch_node_pools_) {
    delete[] pool;
  }
  watch_node_pools_.clear();
  watch_range_first_free_ = nullptr;
  watch_range_current_pool_allocated_ = 0;
  for (WatchRange* pool : watch_range_pools_) {
    delete[] pool;
  }
  watch_range_pools_.clear();

  {
    auto global_lock = global_critical_region_.Acquire();
    for (SystemPageFlagsBlock& block : system_page_flags_) {
      block.valid = block.valid_and_gpu_written;
    }
  }

  // TODO(Triang3l): Unmap and destroy heaps.
}

void SharedMemory::CompletedSubmissionUpdated() {
  upload_buffer_pool_->Reclaim(command_processor_->GetCompletedSubmission());
}

SharedMemory::GlobalWatchHandle SharedMemory::RegisterGlobalWatch(
    GlobalWatchCallback callback, void* callback_context) {
  GlobalWatch* watch = new GlobalWatch;
  watch->callback = callback;
  watch->callback_context = callback_context;

  auto global_lock = global_critical_region_.Acquire();
  global_watches_.push_back(watch);

  return reinterpret_cast<GlobalWatchHandle>(watch);
}

void SharedMemory::UnregisterGlobalWatch(GlobalWatchHandle handle) {
  auto watch = reinterpret_cast<GlobalWatch*>(handle);

  {
    auto global_lock = global_critical_region_.Acquire();
    auto it = std::find(global_watches_.begin(), global_watches_.end(), watch);
    assert_false(it == global_watches_.end());
    if (it != global_watches_.end()) {
      global_watches_.erase(it);
    }
  }

  delete watch;
}

SharedMemory::WatchHandle SharedMemory::WatchMemoryRange(
    uint32_t start, uint32_t length, WatchCallback callback,
    void* callback_context, void* callback_data, uint64_t callback_argument) {
  if (length == 0 || start >= kBufferSize) {
    return nullptr;
  }
  length = std::min(length, kBufferSize - start);
  uint32_t watch_page_first = start >> page_size_log2_;
  uint32_t watch_page_last = (start + length - 1) >> page_size_log2_;
  uint32_t bucket_first =
      watch_page_first << page_size_log2_ >> kWatchBucketSizeLog2;
  uint32_t bucket_last =
      watch_page_last << page_size_log2_ >> kWatchBucketSizeLog2;

  auto global_lock = global_critical_region_.Acquire();

  // Allocate the range.
  WatchRange* range = watch_range_first_free_;
  if (range != nullptr) {
    watch_range_first_free_ = range->next_free;
  } else {
    if (watch_range_pools_.empty() ||
        watch_range_current_pool_allocated_ >= kWatchRangePoolSize) {
      watch_range_pools_.push_back(new WatchRange[kWatchRangePoolSize]);
      watch_range_current_pool_allocated_ = 0;
    }
    range = &(watch_range_pools_.back()[watch_range_current_pool_allocated_++]);
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
      }
      node = &(watch_node_pools_.back()[watch_node_current_pool_allocated_++]);
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

void SharedMemory::UnwatchMemoryRange(WatchHandle handle) {
  if (handle == nullptr) {
    // Could be a zero length range.
    return;
  }
  auto global_lock = global_critical_region_.Acquire();
  UnlinkWatchRange(reinterpret_cast<WatchRange*>(handle));
}

bool SharedMemory::EnsureTilesResident(uint32_t start, uint32_t length) {
  if (length == 0) {
    // Some texture is empty, for example - safe to draw in this case.
    return true;
  }
  if (start > kBufferSize || (kBufferSize - start) < length) {
    return false;
  }

  if (!AreTiledResourcesUsed()) {
    return true;
  }

  uint32_t heap_first = start >> kHeapSizeLog2;
  uint32_t heap_last = (start + length - 1) >> kHeapSizeLog2;
  for (uint32_t i = heap_first; i <= heap_last; ++i) {
    if (heaps_[i] != nullptr) {
      continue;
    }
    auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
    auto device = provider->GetDevice();
    auto direct_queue = provider->GetDirectQueue();
    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = kHeapSize;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    if (FAILED(device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heaps_[i])))) {
      XELOGE("Shared memory: Failed to create a tile heap");
      return false;
    }
    ++heap_count_;
    COUNT_profile_set("gpu/shared_memory/used_mb",
                      heap_count_ << kHeapSizeLog2 >> 20);
    D3D12_TILED_RESOURCE_COORDINATE region_start_coordinates;
    region_start_coordinates.X =
        (i << kHeapSizeLog2) / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    region_start_coordinates.Y = 0;
    region_start_coordinates.Z = 0;
    region_start_coordinates.Subresource = 0;
    D3D12_TILE_REGION_SIZE region_size;
    region_size.NumTiles = kHeapSize / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    region_size.UseBox = FALSE;
    D3D12_TILE_RANGE_FLAGS range_flags = D3D12_TILE_RANGE_FLAG_NONE;
    UINT heap_range_start_offset = 0;
    UINT range_tile_count = kHeapSize / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    // FIXME(Triang3l): This may cause issues if the emulator is shut down
    // mid-frame and the heaps are destroyed before tile mappings are updated
    // (awaiting the fence won't catch this then). Defer this until the actual
    // command list submission at the end of the frame.
    direct_queue->UpdateTileMappings(
        buffer_, 1, &region_start_coordinates, &region_size, heaps_[i], 1,
        &range_flags, &heap_range_start_offset, &range_tile_count,
        D3D12_TILE_MAPPING_FLAG_NONE);
  }
  return true;
}

bool SharedMemory::RequestRange(uint32_t start, uint32_t length) {
  if (length == 0) {
    // Some texture is empty, for example - safe to draw in this case.
    return true;
  }
  if (start > kBufferSize || (kBufferSize - start) < length) {
    return false;
  }
  uint32_t last = start + length - 1;

  auto command_list = command_processor_->GetDeferredCommandList();

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  // Ensure all tile heaps are present.
  if (!EnsureTilesResident(start, length)) {
    return false;
  }

  // Upload and protect used ranges.
  GetRangesToUpload(start >> page_size_log2_, last >> page_size_log2_);
  if (upload_ranges_.size() == 0) {
    return true;
  }
  TransitionBuffer(D3D12_RESOURCE_STATE_COPY_DEST);
  command_processor_->SubmitBarriers();
  for (auto upload_range : upload_ranges_) {
    uint32_t upload_range_start = upload_range.first;
    uint32_t upload_range_length = upload_range.second;
    trace_writer_->WriteMemoryRead(upload_range_start << page_size_log2_,
                                   upload_range_length << page_size_log2_);
    while (upload_range_length != 0) {
      ID3D12Resource* upload_buffer;
      uint32_t upload_buffer_offset, upload_buffer_size;
      uint8_t* upload_buffer_mapping = upload_buffer_pool_->RequestPartial(
          command_processor_->GetCurrentSubmission(),
          upload_range_length << page_size_log2_, &upload_buffer,
          &upload_buffer_offset, &upload_buffer_size, nullptr);
      if (upload_buffer_mapping == nullptr) {
        XELOGE("Shared memory: Failed to get an upload buffer");
        return false;
      }
      uint32_t upload_buffer_pages = upload_buffer_size >> page_size_log2_;
      MakeRangeValid(upload_range_start << page_size_log2_,
                     upload_buffer_pages << page_size_log2_, false);
      std::memcpy(
          upload_buffer_mapping,
          memory_->TranslatePhysical(upload_range_start << page_size_log2_),
          upload_buffer_size);
      command_list->D3DCopyBufferRegion(
          buffer_, upload_range_start << page_size_log2_, upload_buffer,
          upload_buffer_offset, upload_buffer_size);
      upload_range_start += upload_buffer_pages;
      upload_range_length -= upload_buffer_pages;
    }
  }

  return true;
}

void SharedMemory::FireWatches(uint32_t page_first, uint32_t page_last,
                               bool invalidated_by_gpu) {
  uint32_t address_first = page_first << page_size_log2_;
  uint32_t address_last =
      (page_last << page_size_log2_) + ((1 << page_size_log2_) - 1);
  uint32_t bucket_first = address_first >> kWatchBucketSizeLog2;
  uint32_t bucket_last = address_last >> kWatchBucketSizeLog2;

  auto global_lock = global_critical_region_.Acquire();

  // Fire global watches.
  for (const auto global_watch : global_watches_) {
    global_watch->callback(global_watch->callback_context, address_first,
                           address_last, invalidated_by_gpu);
  }

  // Fire per-range watches.
  for (uint32_t i = bucket_first; i <= bucket_last; ++i) {
    WatchNode* node = watch_buckets_[i];
    while (node != nullptr) {
      WatchRange* range = node->range;
      // Store the next node now since when the callback is triggered, the links
      // will be broken.
      node = node->bucket_node_next;
      if (page_first <= range->page_last && page_last >= range->page_first) {
        range->callback(range->callback_context, range->callback_data,
                        range->callback_argument, invalidated_by_gpu);
        UnlinkWatchRange(range);
      }
    }
  }
}

void SharedMemory::RangeWrittenByGPU(uint32_t start, uint32_t length) {
  if (length == 0 || start >= kBufferSize) {
    return;
  }
  length = std::min(length, kBufferSize - start);
  uint32_t end = start + length - 1;
  uint32_t page_first = start >> page_size_log2_;
  uint32_t page_last = end >> page_size_log2_;

  // Trigger modification callbacks so, for instance, resolved data is loaded to
  // the texture.
  FireWatches(page_first, page_last, true);

  // Mark the range as valid (so pages are not reuploaded until modified by the
  // CPU) and watch it so the CPU can reuse it and this will be caught.
  MakeRangeValid(start, length, true);
}

bool SharedMemory::AreTiledResourcesUsed() const {
  if (!cvars::d3d12_tiled_shared_memory) {
    return false;
  }
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  // As of October 8th, 2018, PIX doesn't support tiled buffers.
  // FIXME(Triang3l): Re-enable tiled resources with PIX once fixed.
  return provider->GetTiledResourcesTier() >= 1 &&
         provider->GetGraphicsAnalysis() == nullptr;
}

void SharedMemory::MakeRangeValid(uint32_t start, uint32_t length,
                                  bool written_by_gpu) {
  if (length == 0 || start >= kBufferSize) {
    return;
  }
  length = std::min(length, kBufferSize - start);
  uint32_t last = start + length - 1;
  uint32_t valid_page_first = start >> page_size_log2_;
  uint32_t valid_page_last = last >> page_size_log2_;
  uint32_t valid_block_first = valid_page_first >> 6;
  uint32_t valid_block_last = valid_page_last >> 6;

  {
    auto global_lock = global_critical_region_.Acquire();

    for (uint32_t i = valid_block_first; i <= valid_block_last; ++i) {
      uint64_t valid_bits = UINT64_MAX;
      if (i == valid_block_first) {
        valid_bits &= ~((1ull << (valid_page_first & 63)) - 1);
      }
      if (i == valid_block_last && (valid_page_last & 63) != 63) {
        valid_bits &= (1ull << ((valid_page_last & 63) + 1)) - 1;
      }
      SystemPageFlagsBlock& block = system_page_flags_[i];
      block.valid |= valid_bits;
      if (written_by_gpu) {
        block.valid_and_gpu_written |= valid_bits;
      } else {
        block.valid_and_gpu_written &= ~valid_bits;
      }
    }
  }

  if (memory_invalidation_callback_handle_) {
    memory_->EnablePhysicalMemoryAccessCallbacks(
        valid_page_first << page_size_log2_,
        (valid_page_last - valid_page_first + 1) << page_size_log2_, true,
        false);
  }
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
                                     uint32_t request_page_last) {
  upload_ranges_.clear();
  request_page_last = std::min(request_page_last, page_count_ - 1u);
  if (request_page_first > request_page_last) {
    return;
  }
  uint32_t request_block_first = request_page_first >> 6;
  uint32_t request_block_last = request_page_last >> 6;

  auto global_lock = global_critical_region_.Acquire();

  uint32_t range_start = UINT32_MAX;
  for (uint32_t i = request_block_first; i <= request_block_last; ++i) {
    uint64_t block_valid = system_page_flags_[i].valid;
    // Consider pages in the block outside the requested range valid.
    if (i == request_block_first) {
      block_valid |= (1ull << (request_page_first & 63)) - 1;
    }
    if (i == request_block_last && (request_page_last & 63) != 63) {
      block_valid |= ~((1ull << ((request_page_last & 63) + 1)) - 1);
    }

    while (true) {
      uint32_t block_page;
      if (range_start == UINT32_MAX) {
        // Check if need to open a new range.
        if (!xe::bit_scan_forward(~block_valid, &block_page)) {
          break;
        }
        range_start = (i << 6) + block_page;
      } else {
        // Check if need to close the range.
        // Ignore the valid pages before the beginning of the range.
        uint64_t block_valid_from_start = block_valid;
        if (i == (range_start >> 6)) {
          block_valid_from_start &= ~((1ull << (range_start & 63)) - 1);
        }
        if (!xe::bit_scan_forward(block_valid_from_start, &block_page)) {
          break;
        }
        upload_ranges_.push_back(
            std::make_pair(range_start, (i << 6) + block_page - range_start));
        // In the next interation within this block, consider this range valid
        // since it has been queued for upload.
        block_valid |= (1ull << block_page) - 1;
        range_start = UINT32_MAX;
      }
    }
  }
  if (range_start != UINT32_MAX) {
    upload_ranges_.push_back(
        std::make_pair(range_start, request_page_last + 1 - range_start));
  }
}

std::pair<uint32_t, uint32_t> SharedMemory::MemoryInvalidationCallbackThunk(
    void* context_ptr, uint32_t physical_address_start, uint32_t length,
    bool exact_range) {
  return reinterpret_cast<SharedMemory*>(context_ptr)
      ->MemoryInvalidationCallback(physical_address_start, length, exact_range);
}

std::pair<uint32_t, uint32_t> SharedMemory::MemoryInvalidationCallback(
    uint32_t physical_address_start, uint32_t length, bool exact_range) {
  if (length == 0 || physical_address_start >= kBufferSize) {
    return std::make_pair(uint32_t(0), UINT32_MAX);
  }
  length = std::min(length, kBufferSize - physical_address_start);
  uint32_t physical_address_last = physical_address_start + (length - 1);

  uint32_t page_first = physical_address_start >> page_size_log2_;
  uint32_t page_last = physical_address_last >> page_size_log2_;
  assert_true(page_first < page_count_ && page_last < page_count_);
  uint32_t block_first = page_first >> 6;
  uint32_t block_last = page_last >> 6;

  auto global_lock = global_critical_region_.Acquire();

  if (!exact_range) {
    // Check if a somewhat wider range (up to 256 KB with 4 KB pages) can be
    // invalidated - if no GPU-written data nearby that was not intended to be
    // invalidated since it's not in sync with CPU memory and can't be
    // reuploaded. It's a lot cheaper to upload some excess data than to catch
    // access violations - with 4 KB callbacks, the original Doom runs at 4 FPS
    // on Intel Core i7-3770, with 64 KB the CPU game code takes 3 ms to run per
    // frame, but with 256 KB it's 0.7 ms.
    if (page_first & 63) {
      uint64_t gpu_written_start =
          system_page_flags_[block_first].valid_and_gpu_written;
      gpu_written_start &= (1ull << (page_first & 63)) - 1;
      page_first =
          (page_first & ~uint32_t(63)) + (64 - xe::lzcnt(gpu_written_start));
    }
    if ((page_last & 63) != 63) {
      uint64_t gpu_written_end =
          system_page_flags_[block_last].valid_and_gpu_written;
      gpu_written_end &= ~((1ull << ((page_last & 63) + 1)) - 1);
      page_last = (page_last & ~uint32_t(63)) +
                  (std::max(xe::tzcnt(gpu_written_end), uint8_t(1)) - 1);
    }
  }

  for (uint32_t i = block_first; i <= block_last; ++i) {
    uint64_t invalidate_bits = UINT64_MAX;
    if (i == block_first) {
      invalidate_bits &= ~((1ull << (page_first & 63)) - 1);
    }
    if (i == block_last && (page_last & 63) != 63) {
      invalidate_bits &= (1ull << ((page_last & 63) + 1)) - 1;
    }
    SystemPageFlagsBlock& block = system_page_flags_[i];
    block.valid &= ~invalidate_bits;
    block.valid_and_gpu_written &= ~invalidate_bits;
  }

  FireWatches(page_first, page_last, false);

  return std::make_pair(page_first << page_size_log2_,
                        (page_last - page_first + 1) << page_size_log2_);
}

void SharedMemory::TransitionBuffer(D3D12_RESOURCE_STATES new_state) {
  command_processor_->PushTransitionBarrier(buffer_, buffer_state_, new_state);
  buffer_state_ = new_state;
}

void SharedMemory::WriteRawSRVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  device->CopyDescriptorsSimple(
      1, handle,
      provider->OffsetViewDescriptor(buffer_descriptor_heap_start_,
                                     uint32_t(BufferDescriptorIndex::kRawSRV)),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void SharedMemory::WriteRawUAVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  device->CopyDescriptorsSimple(
      1, handle,
      provider->OffsetViewDescriptor(buffer_descriptor_heap_start_,
                                     uint32_t(BufferDescriptorIndex::kRawUAV)),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

bool SharedMemory::InitializeTraceSubmitDownloads() {
  // Invalidate the entire memory CPU->GPU memory copy so all the history
  // doesn't have to be written into every frame trace, and collect the list of
  // ranges with data modified on the GPU.
  ResetTraceGPUWrittenBuffer();
  uint32_t gpu_written_page_count = 0;

  {
    auto global_lock = global_critical_region_.Acquire();
    uint32_t fire_watches_range_start = UINT32_MAX;
    uint32_t gpu_written_range_start = UINT32_MAX;
    for (uint32_t i = 0; i < system_page_flags_.size(); ++i) {
      SystemPageFlagsBlock& page_flags_block = system_page_flags_[i];
      uint64_t previously_valid_block = page_flags_block.valid;
      uint64_t gpu_written_block = page_flags_block.valid_and_gpu_written;
      page_flags_block.valid = gpu_written_block;

      // Fire watches on the invalidated pages.
      uint64_t fire_watches_block = previously_valid_block & ~gpu_written_block;
      uint64_t fire_watches_break_block = ~fire_watches_block;
      while (true) {
        uint32_t fire_watches_block_page;
        if (!xe::bit_scan_forward(fire_watches_range_start == UINT32_MAX
                                      ? fire_watches_block
                                      : fire_watches_break_block,
                                  &fire_watches_block_page)) {
          break;
        }
        uint32_t fire_watches_page = (i << 6) + fire_watches_block_page;
        if (fire_watches_range_start == UINT32_MAX) {
          fire_watches_range_start = fire_watches_page;
        } else {
          FireWatches(fire_watches_range_start, fire_watches_page - 1, false);
          fire_watches_range_start = UINT32_MAX;
        }
        uint64_t fire_watches_block_mask =
            ~((1ull << fire_watches_block_page) - 1);
        fire_watches_block &= fire_watches_block_mask;
        fire_watches_break_block &= fire_watches_block_mask;
      }

      // Add to the GPU-written ranges.
      uint64_t gpu_written_break_block = ~gpu_written_block;
      while (true) {
        uint32_t gpu_written_block_page;
        if (!xe::bit_scan_forward(gpu_written_range_start == UINT32_MAX
                                      ? gpu_written_block
                                      : gpu_written_break_block,
                                  &gpu_written_block_page)) {
          break;
        }
        uint32_t gpu_written_page = (i << 6) + gpu_written_block_page;
        if (gpu_written_range_start == UINT32_MAX) {
          gpu_written_range_start = gpu_written_page;
        } else {
          uint32_t gpu_written_range_length =
              gpu_written_page - gpu_written_range_start;
          trace_gpu_written_ranges_.push_back(
              std::make_pair(gpu_written_range_start << page_size_log2_,
                             gpu_written_range_length << page_size_log2_));
          gpu_written_page_count += gpu_written_range_length;
          gpu_written_range_start = UINT32_MAX;
        }
        uint64_t gpu_written_block_mask =
            ~((1ull << gpu_written_block_page) - 1);
        gpu_written_block &= gpu_written_block_mask;
        gpu_written_break_block &= gpu_written_block_mask;
      }
    }
    if (fire_watches_range_start != UINT32_MAX) {
      FireWatches(fire_watches_range_start, page_count_ - 1, false);
    }
    if (gpu_written_range_start != UINT32_MAX) {
      uint32_t gpu_written_range_length = page_count_ - gpu_written_range_start;
      trace_gpu_written_ranges_.push_back(
          std::make_pair(gpu_written_range_start << page_size_log2_,
                         gpu_written_range_length << page_size_log2_));
      gpu_written_page_count += gpu_written_range_length;
    }
  }

  // Request downloading of GPU-written memory.
  if (!gpu_written_page_count) {
    return false;
  }
  D3D12_RESOURCE_DESC gpu_written_buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(
      gpu_written_buffer_desc, gpu_written_page_count << page_size_log2_,
      D3D12_RESOURCE_FLAG_NONE);
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesReadback, D3D12_HEAP_FLAG_NONE,
          &gpu_written_buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&trace_gpu_written_buffer_)))) {
    XELOGE(
        "Shared memory: Failed to create a %u KB GPU-written memory download "
        "buffer for frame tracing",
        gpu_written_page_count << page_size_log2_ >> 10);
    ResetTraceGPUWrittenBuffer();
    return false;
  }
  auto command_list = command_processor_->GetDeferredCommandList();
  UseAsCopySource();
  command_processor_->SubmitBarriers();
  uint32_t gpu_written_buffer_offset = 0;
  for (auto& gpu_written_submit_range : trace_gpu_written_ranges_) {
    // For cases like resolution scale, when the data may not be actually
    // written, just marked as valid.
    if (!EnsureTilesResident(gpu_written_submit_range.first,
                             gpu_written_submit_range.second)) {
      gpu_written_submit_range.second = 0;
      continue;
    }
    command_list->D3DCopyBufferRegion(
        trace_gpu_written_buffer_, gpu_written_buffer_offset, buffer_,
        gpu_written_submit_range.first, gpu_written_submit_range.second);
    gpu_written_buffer_offset += gpu_written_submit_range.second;
  }
  return true;
}

void SharedMemory::InitializeTraceCompleteDownloads() {
  if (!trace_gpu_written_buffer_) {
    return;
  }
  void* download_mapping;
  if (SUCCEEDED(
          trace_gpu_written_buffer_->Map(0, nullptr, &download_mapping))) {
    uint32_t gpu_written_buffer_offset = 0;
    for (auto gpu_written_submit_range : trace_gpu_written_ranges_) {
      trace_writer_->WriteMemoryRead(
          gpu_written_submit_range.first, gpu_written_submit_range.second,
          reinterpret_cast<const uint8_t*>(download_mapping) +
              gpu_written_buffer_offset);
    }
    D3D12_RANGE download_write_range = {};
    trace_gpu_written_buffer_->Unmap(0, &download_write_range);
  } else {
    XELOGE(
        "Failed to map the GPU-written memory download buffer for frame "
        "tracing");
  }
  ResetTraceGPUWrittenBuffer();
}

void SharedMemory::ResetTraceGPUWrittenBuffer() {
  trace_gpu_written_ranges_.clear();
  trace_gpu_written_ranges_.shrink_to_fit();
  ui::d3d12::util::ReleaseAndNull(trace_gpu_written_buffer_);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
