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

#include "xenia/memory.h"
#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/d3d12/d3d12_context.h"
#include "xenia/ui/d3d12/pools.h"

namespace xe {
namespace gpu {
namespace d3d12 {

// Manages memory for unconverted textures, resolve targets, vertex and index
// buffers that can be accessed from shaders with Xenon physical addresses, with
// system page size granularity.
class SharedMemory {
 public:
  SharedMemory(Memory* memory, ui::d3d12::D3D12Context* context);
  ~SharedMemory();

  bool Initialize();
  void Shutdown();

  void BeginFrame();
  // Returns true if anything has been written to command_list been done.
  // The draw command list is needed for the transition.
  bool EndFrame(ID3D12GraphicsCommandList* command_list_setup,
                ID3D12GraphicsCommandList* command_list_draw);

  // Marks the range as used in this frame, queues it for upload if it was
  // modified. Ensures the backing memory for the address range is present in
  // the tiled buffer, allocating if needed. If couldn't allocate, false is
  // returned - it's unsafe to use this portion (on tiled resources tier 1 at
  // least).
  bool UseRange(uint32_t start, uint32_t length);

  // Makes the buffer usable for vertices, indices and texture untiling.
  void UseForReading(ID3D12GraphicsCommandList* command_list);
  // Makes the buffer usable for texture tiling after a resolve.
  void UseForWriting(ID3D12GraphicsCommandList* command_list);

 private:
  Memory* memory_;

  ui::d3d12::D3D12Context* context_;

  // The 512 MB tiled buffer.
  static constexpr uint32_t kBufferSizeLog2 = 29;
  static constexpr uint32_t kBufferSize = 1 << kBufferSizeLog2;
  static constexpr uint32_t kAddressMask = kBufferSize - 1;
  ID3D12Resource* buffer_ = nullptr;
  D3D12_RESOURCE_STATES buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;

  // D3D resource tiles are 64 KB in size.
  static constexpr uint32_t kTileSizeLog2 = 16;
  static constexpr uint32_t kTileSize = 1 << kTileSizeLog2;
  // Heaps are 4 MB, so not too many of them are allocated.
  static constexpr uint32_t kHeapSizeLog2 = 22;
  static constexpr uint32_t kHeapSize = 1 << kHeapSizeLog2;
  // Resident portions of the tiled buffer.
  ID3D12Heap* heaps_[kBufferSize >> kHeapSizeLog2] = {};
  // Whether creation of a heap has failed in the current frame.
  bool heap_creation_failed_ = false;

  // Log2 of system page size.
  uint32_t page_size_log2_;
  // Total physical page count.
  uint32_t page_count_;

  // Bit vector containing whether physical memory system pages are up to date.
  std::vector<uint64_t> pages_in_sync_;

  // Mutex for the watched pages and the triggered watches.
  std::mutex watch_mutex_;
  // Whether each physical page is watched by the GPU (after uploading).
  // Once a watch is triggered, it's not watched anymore.
  std::vector<uint64_t> watched_pages_;
  // Whether each page was modified while the current frame is being processed.
  // This is checked and cleared in the beginning of a GPU frame.
  // Because this is done with a locked CPU-GPU mutex, it's stored in 2 levels,
  // so unmodified pages can be skipped quickly, and clearing is also fast.
  // On L1, each bit corresponds to a single page, on L2, to 64 pages.
  // Checking if L2 is non-zero before accessing L1 is REQUIRED since L1 is not
  // cleared!
  std::vector<uint64_t> watches_triggered_l1_;
  std::vector<uint64_t> watches_triggered_l2_;
  // Memory access callback.
  static bool WatchCallbackThunk(void* context_ptr, uint32_t address);
  bool WatchCallback(uint32_t address);

  // Pages that need to be uploaded in this frame (that are used but modified).
  std::vector<uint64_t> upload_pages_;
  uint32_t NextUploadRange(uint32_t search_start, uint32_t& length) const;
  std::unique_ptr<ui::d3d12::UploadBufferPool> upload_buffer_pool_ = nullptr;

  void TransitionBuffer(D3D12_RESOURCE_STATES new_state,
                        ID3D12GraphicsCommandList* command_list);
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_SHARED_MEMORY_H_
