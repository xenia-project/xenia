/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_SHARED_MEMORY_H_
#define XENIA_GPU_D3D12_SHARED_MEMORY_H_

#include <memory>
#include <mutex>
#include <vector>

#include "xenia/memory.h"
#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/d3d12/pools.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor;

// Manages memory for unconverted textures, resolve targets, vertex and index
// buffers that can be accessed from shaders with Xenon physical addresses, with
// system page size granularity.
class SharedMemory {
 public:
  SharedMemory(D3D12CommandProcessor* command_processor, Memory* memory);
  ~SharedMemory();

  bool Initialize();
  void Shutdown();

  ID3D12Resource* GetBuffer() const { return buffer_; }
  D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const {
    return buffer_gpu_address_;
  }

  void BeginFrame();
  void EndFrame();

  typedef void (*WatchCallback)(void* context, void* data, uint64_t argument);
  typedef void* WatchHandle;
  // Registers a callback invoked when something is written to the specified
  // memory range by the CPU or (if triggered explicitly - such as by a resolve)
  // the GPU.
  //
  // Generally the context is the subsystem pointer (for example, the
  // texture cache), the data is the object (such as a texture), and the
  // argument is additional subsystem/object-specific data (such as whether the
  // range belongs to the base mip level or to the rest of the mips).
  //
  // The callback is called with the mutex locked. Do NOT watch or unwatch
  // ranges from within it! The watch for the callback is cancelled after the
  // callback - the handle becomes invalid.
  WatchHandle WatchMemoryRange(uint32_t start, uint32_t length,
                               WatchCallback callback, void* callback_context,
                               void* callback_data, uint64_t callback_argument);
  // Unregisters previously registered watched memory range.
  void UnwatchMemoryRange(WatchHandle handle);
  // Locks the mutex that gets locked when watch callbacks are invoked - must be
  // done when checking variables that may be changed by a watch callback.
  inline void LockWatchMutex() { validity_mutex_.lock(); }
  inline void UnlockWatchMutex() { validity_mutex_.unlock(); }

  // Ensures the buffer tiles backing the range are resident, but doesn't upload
  // anything.
  bool MakeTilesResident(uint32_t start, uint32_t length);

  // Checks if the range has been updated, uploads new data if needed and
  // ensures the buffer tiles backing the range are resident. May transition the
  // tiled buffer to copy destination - call this before UseForReading or
  // UseForWriting. Returns true if the range has been fully updated and is
  // usable.
  bool RequestRange(uint32_t start, uint32_t length);

  // Marks the range as containing GPU-generated data (such as resolves),
  // triggering modification callbacks, making it valid (so pages are not
  // copied from the main memory until they're modified by the CPU) and
  // protecting it.
  void RangeWrittenByGPU(uint32_t start, uint32_t length);

  // Makes the buffer usable for vertices, indices and texture untiling.
  void UseForReading();
  // Makes the buffer usable for texture tiling after a resolve.
  void UseForWriting();

  void CreateSRV(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  void CreateRawUAV(D3D12_CPU_DESCRIPTOR_HANDLE handle);

 private:
  D3D12CommandProcessor* command_processor_;

  Memory* memory_;

  // The 512 MB tiled buffer.
  static constexpr uint32_t kBufferSizeLog2 = 29;
  static constexpr uint32_t kBufferSize = 1 << kBufferSizeLog2;
  static constexpr uint32_t kAddressMask = kBufferSize - 1;
  ID3D12Resource* buffer_ = nullptr;
  D3D12_GPU_VIRTUAL_ADDRESS buffer_gpu_address_ = 0;
  D3D12_RESOURCE_STATES buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;

  // D3D resource tiles are 64 KB in size.
  static constexpr uint32_t kTileSizeLog2 = 16;
  static constexpr uint32_t kTileSize = 1 << kTileSizeLog2;
  // Heaps are 16 MB, so not too many of them are allocated.
  static constexpr uint32_t kHeapSizeLog2 = 24;
  static constexpr uint32_t kHeapSize = 1 << kHeapSizeLog2;
  // Resident portions of the tiled buffer.
  ID3D12Heap* heaps_[kBufferSize >> kHeapSizeLog2] = {};
  // Whether creation of a heap has failed in the current frame.
  bool heap_creation_failed_ = false;

  // Log2 of system page size.
  uint32_t page_size_log2_;
  // Total physical page count.
  uint32_t page_count_;

  // Mutex between the exception handler and the command processor, to be locked
  // when checking or updating validity of pages/ranges.
  std::recursive_mutex validity_mutex_;

  // ***************************************************************************
  // Things below should be protected by validity_mutex_.
  // ***************************************************************************

  // Bit vector containing whether physical memory system pages are up to date.
  std::vector<uint64_t> valid_pages_;
  // Mark the memory range as updated and protect it.
  void MakeRangeValid(uint32_t valid_page_first, uint32_t valid_page_count);

  // Whether each physical page is protected by the GPU code (after uploading).
  std::vector<uint64_t> protected_pages_;
  // Memory access callback.
  static bool MemoryWriteCallbackThunk(void* context_ptr, uint32_t address);
  bool MemoryWriteCallback(uint32_t address);

  struct WatchNode;
  // Watched range placed by other GPU subsystems.
  struct WatchRange {
    union {
      struct {
        WatchCallback callback;
        void* callback_context;
        void* callback_data;
        uint64_t callback_argument;
        WatchNode* node_first;
        uint32_t page_first;
        uint32_t page_last;
      };
      WatchRange* next_free;
    };
  };
  // Node for faster checking of watches when pages have been written to - all
  // 512 MB are split into smaller equally sized buckets, and then ranges are
  // linearly checked.
  struct WatchNode {
    union {
      struct {
        WatchRange* range;
        // Link to another node of this watched range in the next bucket.
        WatchNode* range_node_next;
        // Links to nodes belonging to other watched ranges in the bucket.
        WatchNode* bucket_node_previous;
        WatchNode* bucket_node_next;
      };
      WatchNode* next_free;
    };
  };
  static constexpr uint32_t kWatchBucketSizeLog2 = 22;
  static constexpr uint32_t kWatchBucketCount =
      1 << (kBufferSizeLog2 - kWatchBucketSizeLog2);
  WatchNode* watch_buckets_[kWatchBucketCount] = {};
  // Allocation from pools - taking new WatchRanges and WatchNodes from the free
  // list, and if there are none, creating a pool if the current one is fully
  // used, and linearly allocating from the current pool.
  static constexpr uint32_t kWatchRangePoolSize = 8192;
  static constexpr uint32_t kWatchNodePoolSize = 8192;
  std::vector<WatchRange*> watch_range_pools_;
  std::vector<WatchNode*> watch_node_pools_;
  uint32_t watch_range_current_pool_allocated_ = 0;
  uint32_t watch_node_current_pool_allocated_ = 0;
  WatchRange* watch_range_first_free_ = nullptr;
  WatchNode* watch_node_first_free_ = nullptr;
  // Unlinks and frees the range and its nodes. Call this with the mutex locked.
  void UnlinkWatchRange(WatchRange* range);

  // ***************************************************************************
  // Things above should be protected by validity_mutex_.
  // ***************************************************************************

  // First page and length in pages.
  typedef std::pair<uint32_t, uint32_t> UploadRange;
  // Ranges that need to be uploaded, generated by GetRangesToUpload (a
  // persistently allocated vector).
  std::vector<UploadRange> upload_ranges_;
  void GetRangesToUpload(uint32_t request_page_first,
                         uint32_t request_page_count);
  std::unique_ptr<ui::d3d12::UploadBufferPool> upload_buffer_pool_ = nullptr;

  void TransitionBuffer(D3D12_RESOURCE_STATES new_state);
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_SHARED_MEMORY_H_
