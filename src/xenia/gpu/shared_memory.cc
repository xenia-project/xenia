/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shared_memory.h"

#include <algorithm>

#include "xenia/base/assert.h"
#include "xenia/base/bit_range.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"

namespace xe {
namespace gpu {

SharedMemory::SharedMemory(Memory& memory) : memory_(memory) {
  page_size_log2_ = xe::log2_ceil(uint32_t(xe::memory::page_size()));
}

SharedMemory::~SharedMemory() { ShutdownCommon(); }

void SharedMemory::InitializeCommon() {
  size_t num_system_page_flags_entries =
      ((kBufferSize >> page_size_log2_) + 63) / 64;
  num_system_page_flags_ = static_cast<uint32_t>(num_system_page_flags_entries);

  // in total on windows the page flags take up 2048 entries per fields, with 3
  // fields and 8 bytes per entry thats 49152 bytes. having page alignment for
  // them is probably beneficial, we do waste 16384 bytes with this alloc though

  uint64_t* system_page_flags_base = (uint64_t*)memory::AllocFixed(
      nullptr, num_system_page_flags_ * 3 * sizeof(uint64_t),
      memory::AllocationType::kReserveCommit, memory::PageAccess::kReadWrite);

  system_page_flags_valid_ = system_page_flags_base,
  system_page_flags_valid_and_gpu_resolved_ =
      system_page_flags_base + (num_system_page_flags_),
  system_page_flags_valid_and_gpu_written_ =
      system_page_flags_base + (num_system_page_flags_ * 2);
  memset(system_page_flags_valid_, 0, 8 * num_system_page_flags_entries);
  memset(system_page_flags_valid_and_gpu_resolved_, 0,
         8 * num_system_page_flags_entries);
  memset(system_page_flags_valid_and_gpu_written_, 0,
         8 * num_system_page_flags_entries);
  memory_invalidation_callback_handle_ =
      memory_.RegisterPhysicalMemoryInvalidationCallback(
          MemoryInvalidationCallbackThunk, this);
}

void SharedMemory::InitializeSparseHostGpuMemory(uint32_t granularity_log2) {
  assert_true(granularity_log2 <= kBufferSizeLog2);
  assert_true(host_gpu_memory_sparse_granularity_log2_ == UINT32_MAX);
  host_gpu_memory_sparse_granularity_log2_ = granularity_log2;
  host_gpu_memory_sparse_allocated_.resize(
      size_t(1) << (std::max(kBufferSizeLog2 - granularity_log2, uint32_t(6)) -
                    6));
}

void SharedMemory::ShutdownCommon() {
  ReleaseTraceDownloadRanges();

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
    memory_.UnregisterPhysicalMemoryInvalidationCallback(
        memory_invalidation_callback_handle_);
    memory_invalidation_callback_handle_ = nullptr;
  }

  if (host_gpu_memory_sparse_used_bytes_) {
    host_gpu_memory_sparse_used_bytes_ = 0;
    COUNT_profile_set("gpu/shared_memory/host_gpu_memory_sparse_used_mb", 0);
  }
  if (host_gpu_memory_sparse_allocations_) {
    host_gpu_memory_sparse_allocations_ = 0;
    COUNT_profile_set("gpu/shared_memory/host_gpu_memory_sparse_allocations",
                      0);
  }
  host_gpu_memory_sparse_allocated_.clear();
  host_gpu_memory_sparse_allocated_.shrink_to_fit();
  host_gpu_memory_sparse_granularity_log2_ = UINT32_MAX;
  memory::DeallocFixed(system_page_flags_valid_, 0,
                       memory::DeallocationType::kRelease);
  system_page_flags_valid_ = nullptr;
  system_page_flags_valid_and_gpu_resolved_ = nullptr;
  system_page_flags_valid_and_gpu_written_ = nullptr;
  num_system_page_flags_ = 0;
}

void SharedMemory::ClearCache() {
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
  SetSystemPageBlocksValidWithGpuDataWritten();
}

void SharedMemory::SetSystemPageBlocksValidWithGpuDataWritten() {
  auto global_lock = global_critical_region_.Acquire();

  for (unsigned i = 0; i < num_system_page_flags_; ++i) {
    system_page_flags_valid_[i] = system_page_flags_valid_and_gpu_written_[i];
  }
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
  // chrispy: Not required the global lock is always held by the caller
  // auto global_lock = global_critical_region_.Acquire();

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
  auto global_lock = global_critical_region_.Acquire();
  UnlinkWatchRange(reinterpret_cast<WatchRange*>(handle));
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
    global_watch->callback(global_lock, global_watch->callback_context,
                           address_first, address_last, invalidated_by_gpu);
  }

  // Fire per-range watches.
  for (uint32_t i = bucket_first; i <= bucket_last; ++i) {
    WatchNode* node = watch_buckets_[i];
    if (i + 1 <= bucket_last) {
      WatchNode* nextnode = watch_buckets_[i + 1];
      if (nextnode) {
        swcache::PrefetchL1(nextnode->range);
      }
    }
    while (node != nullptr) {
      WatchRange* range = node->range;
      // Store the next node now since when the callback is triggered, the links
      // will be broken.
      node = node->bucket_node_next;
      if (node) {
        swcache::PrefetchL1(node);
      }
      if (page_first <= range->page_last && page_last >= range->page_first) {
        range->callback(global_lock, range->callback_context,
                        range->callback_data, range->callback_argument,
                        invalidated_by_gpu);
        if (node && node->range) {
          swcache::PrefetchL1(node->range);
        }
        UnlinkWatchRange(range);
      }
    }
  }
}

void SharedMemory::RangeWrittenByGpu(uint32_t start, uint32_t length,
                                     bool is_resolve) {
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
  MakeRangeValid(start, length, true, is_resolve);
}

bool SharedMemory::AllocateSparseHostGpuMemoryRange(
    uint32_t offset_allocations, uint32_t length_allocations) {
  assert_always(
      "Sparse host GPU memory allocation has been initialized, but the "
      "implementation doesn't provide AllocateSparseHostGpuMemoryRange");
  return false;
}

void SharedMemory::MakeRangeValid(uint32_t start, uint32_t length,
                                  bool written_by_gpu,
                                  bool written_by_gpu_resolve) {
  assert_false(written_by_gpu_resolve && !written_by_gpu);
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
        valid_bits &= ~((uint64_t(1) << (valid_page_first & 63)) - 1);
      }
      if (i == valid_block_last && (valid_page_last & 63) != 63) {
        valid_bits &= (uint64_t(1) << ((valid_page_last & 63) + 1)) - 1;
      }
      // SystemPageFlagsBlock& block = system_page_flags_[i];
      system_page_flags_valid_[i] |= valid_bits;
      if (written_by_gpu) {
        system_page_flags_valid_and_gpu_written_[i] |= valid_bits;
      } else {
        system_page_flags_valid_and_gpu_written_[i] &= ~valid_bits;
      }
      if (written_by_gpu_resolve) {
        system_page_flags_valid_and_gpu_resolved_[i] |= valid_bits;
      } else {
        system_page_flags_valid_and_gpu_resolved_[i] &= ~valid_bits;
      }
    }
  }

  if (memory_invalidation_callback_handle_) {
    memory().EnablePhysicalMemoryAccessCallbacks(
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
// todo: optimize, an enormous amount of cpu time (1.34%) is spent here.
bool SharedMemory::RequestRange(uint32_t start, uint32_t length,
                                bool* any_data_resolved_out) {
  if (!length) {
    // Some texture or buffer is empty, for example - safe to draw in this case.
    if (any_data_resolved_out) {
      *any_data_resolved_out = false;
    }
    return true;
  }
  if (start > kBufferSize || (kBufferSize - start) < length) {
    return false;
  }

  SCOPE_profile_cpu_f("gpu");

  if (!EnsureHostGpuMemoryAllocated(start, length)) {
    return false;
  }

  unsigned int current_upload_range = 0;
  uint32_t page_first = start >> page_size_log2_;
  uint32_t page_last = (start + length - 1) >> page_size_log2_;

  upload_ranges_.clear();

  std::pair<uint32_t, uint32_t>* uploads =
      reinterpret_cast<std::pair<uint32_t, uint32_t>*>(upload_ranges_.data());

  bool any_data_resolved = false;
  uint32_t block_first = page_first >> 6;
  // swcache::PrefetchL1(&system_page_flags_[block_first]);
  uint32_t block_last = page_last >> 6;
  uint32_t range_start = UINT32_MAX;

  {
    auto global_lock = global_critical_region_.Acquire();
    TryFindUploadRange(block_first, block_last, page_first, page_last,
                       any_data_resolved, range_start, current_upload_range,
                       uploads);
  }
  if (range_start != UINT32_MAX) {
    uploads[current_upload_range++] =
        (std::make_pair(range_start, page_last + 1 - range_start));
  }
  if (any_data_resolved_out) {
    *any_data_resolved_out = any_data_resolved;
  }
  if (!current_upload_range) {
    return true;
  }

  return UploadRanges(uploads, current_upload_range);
}

template <typename T>
XE_FORCEINLINE XE_NOALIAS static T mod_shift_left(T value, uint32_t by) {
#if XE_ARCH_AMD64 == 1
  // arch has modular shifts
  return value << by;
#else
  return value << (by % (sizeof(T) * CHAR_BIT));
#endif
}
void SharedMemory::TryFindUploadRange(const uint32_t& block_first,
                                      const uint32_t& block_last,
                                      const uint32_t& page_first,
                                      const uint32_t& page_last,
                                      bool& any_data_resolved,
                                      uint32_t& range_start,
                                      unsigned int& current_upload_range,
                                      std::pair<uint32_t, uint32_t>* uploads) {
  for (uint32_t i = block_first; i <= block_last; ++i) {
    // const SystemPageFlagsBlock& block = system_page_flags_[i];
    uint64_t block_valid = system_page_flags_valid_[i];
    uint64_t block_resolved = 0;

    if (any_data_resolved) {
      block_resolved = 0;
    } else {
      block_resolved = system_page_flags_valid_and_gpu_resolved_[i];
    }
    if (i == block_first) {
      uint64_t block_before = mod_shift_left(uint64_t(1), page_first) - 1;
      block_valid |= block_before;
      block_resolved &= ~block_before;
    }
    if (i == block_last && (page_last & 63) != 63) {
      uint64_t block_inside = mod_shift_left(uint64_t(1), page_last + 1) - 1;
      block_valid |= ~block_inside;
      block_resolved &= block_inside;
    }
    // Consider pages in the block outside the requested range valid.
    if (!block_resolved) {
    } else {
      any_data_resolved = true;
    }
    TryGetNextUploadRange(range_start, block_valid, i, current_upload_range,
                          uploads);
  }
}

static bool UploadRange_DoBestScanForward(uint64_t v, uint32_t* out) {
#if XE_ARCH_AMD64 == 1 && XE_PLATFORM_WIN32
  if (!v) {
    return false;
  }
  if (amd64::GetFeatureFlags() & amd64::kX64EmitBMI1) {
    *out = static_cast<uint32_t>(_tzcnt_u64(v));
  } else {
    unsigned char bsfres = _BitScanForward64((unsigned long*)out, v);

    XE_MSVC_ASSUME(bsfres == 1);
  }
  return true;
#else
  return xe::bit_scan_forward(v, out);
#endif
}

void SharedMemory::TryGetNextUploadRange(
    uint32_t& range_start, uint64_t& block_valid, const uint32_t& i,
    unsigned int& current_upload_range,
    std::pair<uint32_t, uint32_t>* uploads) {
  while (true) {
    uint32_t block_page = 0;
    if (range_start == UINT32_MAX) {
      // Check if need to open a new range.
      if (!UploadRange_DoBestScanForward(~block_valid, &block_page)) {
        break;
      }
      range_start = (i << 6) + block_page;
    } else {
      // Check if need to close the range.
      // Ignore the valid pages before the beginning of the range.
      uint64_t block_valid_from_start = block_valid;
      if (i == (range_start >> 6)) {
        block_valid_from_start &=
            ~(mod_shift_left(uint64_t(1), range_start) - 1);
      }
      if (!UploadRange_DoBestScanForward(block_valid_from_start, &block_page)) {
        break;
      }
      if (current_upload_range + 1 < MAX_UPLOAD_RANGES) {
        uploads[current_upload_range++] =
            std::make_pair(range_start, (i << 6) + block_page - range_start);
        // In the next iteration within this block, consider this range valid
        // since it has been queued for upload.
        block_valid |= (uint64_t(1) << block_page) - 1;
        range_start = UINT32_MAX;
      } else {
        xe::FatalError(
            "Hit max upload ranges in shared_memory.cc, tell a dev to "
            "raise the limit!");
      }
    }
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
  uint32_t block_first = page_first >> 6;
  uint32_t block_last = page_last >> 6;

  auto global_lock = global_critical_region_.Acquire();

  if (!exact_range) {
    // Check if a somewhat wider range (up to 256 KB with 4 KB pages) can be
    // invalidated - if no GPU-written data nearby that was not intended to be
    // invalidated since it's not in sync with CPU memory and can't be
    // reuploaded. It's a lot cheaper to upload some excess data than to catch
    // access violations - with 4 KB callbacks, 58410824 (being a
    // software-rendered game) runs at 4 FPS on Intel Core i7-3770, with 64 KB,
    // the CPU game code takes 3 ms to run per frame, but with 256 KB, it's
    // 0.7 ms.
    if (page_first & 63) {
      uint64_t gpu_written_start =
          system_page_flags_valid_and_gpu_written_[block_first];
      gpu_written_start &= (uint64_t(1) << (page_first & 63)) - 1;
      page_first =
          (page_first & ~uint32_t(63)) + (64 - xe::lzcnt(gpu_written_start));
    }
    if ((page_last & 63) != 63) {
      uint64_t gpu_written_end =
          system_page_flags_valid_and_gpu_written_[block_last];
      gpu_written_end &= ~((uint64_t(1) << ((page_last & 63) + 1)) - 1);
      page_last = (page_last & ~uint32_t(63)) +
                  (std::max(xe::tzcnt(gpu_written_end), uint8_t(1)) - 1);
    }
  }

  for (uint32_t i = block_first; i <= block_last; ++i) {
    uint64_t invalidate_bits = UINT64_MAX;
    if (i == block_first) {
      invalidate_bits &= ~((uint64_t(1) << (page_first & 63)) - 1);
    }
    if (i == block_last && (page_last & 63) != 63) {
      invalidate_bits &= (uint64_t(1) << ((page_last & 63) + 1)) - 1;
    }
    system_page_flags_valid_[i] &= ~invalidate_bits;
    system_page_flags_valid_and_gpu_resolved_[i] &= ~invalidate_bits;
    system_page_flags_valid_and_gpu_written_[i] &= ~invalidate_bits;
  }

  FireWatches(page_first, page_last, false);

  return std::make_pair(page_first << page_size_log2_,
                        (page_last - page_first + 1) << page_size_log2_);
}

void SharedMemory::PrepareForTraceDownload() {
  ReleaseTraceDownloadRanges();
  assert_true(trace_download_ranges_.empty());
  assert_zero(trace_download_page_count_);

  // Invalidate the entire memory CPU->GPU memory copy so all the history
  // doesn't have to be written into every frame trace, and collect the list of
  // ranges with data modified on the GPU.

  uint32_t fire_watches_range_start = UINT32_MAX;
  uint32_t gpu_written_range_start = UINT32_MAX;
  auto global_lock = global_critical_region_.Acquire();
  for (uint32_t i = 0; i < num_system_page_flags_; ++i) {
    // SystemPageFlagsBlock& page_flags_block = system_page_flags_[i];
    uint64_t previously_valid_block = system_page_flags_valid_[i];
    uint64_t gpu_written_block = system_page_flags_valid_and_gpu_written_[i];
    system_page_flags_valid_[i] = gpu_written_block;

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
          ~((uint64_t(1) << fire_watches_block_page) - 1);
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
        // Call EnsureHostGpuMemoryAllocated in case the page was marked as
        // GPU-written not as a result to an actual write to the shared memory
        // buffer, but, for instance, by resolving with resolution scaling (to a
        // separate buffer).
        if (EnsureHostGpuMemoryAllocated(
                gpu_written_range_start << page_size_log2_,
                gpu_written_range_length << page_size_log2_)) {
          trace_download_ranges_.push_back(
              std::make_pair(gpu_written_range_start << page_size_log2_,
                             gpu_written_range_length << page_size_log2_));
          trace_download_page_count_ += gpu_written_range_length;
        }
        gpu_written_range_start = UINT32_MAX;
      }
      uint64_t gpu_written_block_mask =
          ~((uint64_t(1) << gpu_written_block_page) - 1);
      gpu_written_block &= gpu_written_block_mask;
      gpu_written_break_block &= gpu_written_block_mask;
    }
  }
  uint32_t page_count = kBufferSize >> page_size_log2_;
  if (fire_watches_range_start != UINT32_MAX) {
    FireWatches(fire_watches_range_start, page_count - 1, false);
  }
  if (gpu_written_range_start != UINT32_MAX) {
    uint32_t gpu_written_range_length = page_count - gpu_written_range_start;
    if (EnsureHostGpuMemoryAllocated(
            gpu_written_range_start << page_size_log2_,
            gpu_written_range_length << page_size_log2_)) {
      trace_download_ranges_.push_back(
          std::make_pair(gpu_written_range_start << page_size_log2_,
                         gpu_written_range_length << page_size_log2_));
      trace_download_page_count_ += gpu_written_range_length;
    }
  }
}

void SharedMemory::ReleaseTraceDownloadRanges() {
  trace_download_ranges_.clear();
  trace_download_ranges_.shrink_to_fit();
  trace_download_page_count_ = 0;
}

bool SharedMemory::EnsureHostGpuMemoryAllocated(uint32_t start,
                                                uint32_t length) {
  if (host_gpu_memory_sparse_granularity_log2_ != UINT32_MAX && length) {
    if (start <= kBufferSize && (kBufferSize - start) >= length) {
      uint32_t page_first = start >> page_size_log2_;
      uint32_t page_last = (start + length - 1) >> page_size_log2_;
      uint32_t allocation_first = page_first << page_size_log2_ >>
                                  host_gpu_memory_sparse_granularity_log2_;
      uint32_t allocation_last = page_last << page_size_log2_ >>
                                 host_gpu_memory_sparse_granularity_log2_;
      while (true) {
        std::pair<size_t, size_t> allocation_range =
            xe::bit_range::NextUnsetRange(
                host_gpu_memory_sparse_allocated_.data(), allocation_first,
                allocation_last - allocation_first + 1);
        if (!allocation_range.second) {
          break;
        }
        if (!AllocateSparseHostGpuMemoryRange(
                uint32_t(allocation_range.first),
                uint32_t(allocation_range.second))) {
          return false;
        }
        xe::bit_range::SetRange(host_gpu_memory_sparse_allocated_.data(),
                                allocation_range.first,
                                allocation_range.second);
        ++host_gpu_memory_sparse_allocations_;
        COUNT_profile_set(
            "gpu/shared_memory/host_gpu_memory_sparse_allocations",
            host_gpu_memory_sparse_allocations_);
        host_gpu_memory_sparse_used_bytes_ +=
            uint32_t(allocation_range.second)
            << host_gpu_memory_sparse_granularity_log2_;
        COUNT_profile_set(
            "gpu/shared_memory/host_gpu_memory_sparse_used_mb",
            (host_gpu_memory_sparse_used_bytes_ + ((1 << 20) - 1)) >> 20);
        allocation_first =
            uint32_t(allocation_range.first + allocation_range.second);
      }
    } else {
      return false;
    }
  } else {
    return true;
  }
  return true;
}

}  // namespace gpu
}  // namespace xe
